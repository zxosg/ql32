#include "ql32/vm/scheduler.hpp"
#include "ql32/ui/service.hpp"

#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class ProgramStorage final : public ql32::runtime::StorageService {
public:
    std::string readText(const std::string& path) override {
        const auto found = files.find(path);
        if (found == files.end())
            throw ql32::runtime::ServiceError(ql32::runtime::ServiceErrorCode::NotFound,
                                              "test file not found");
        return found->second;
    }
    void writeText(const std::string& path, const std::string& contents) override {
        files[path] = contents;
    }
    std::vector<ql32::runtime::FileEntry> list(const std::string&) override {
        return {};
    }

    std::unordered_map<std::string, std::string> files;
};

class CapturingChannelService final : public ql32::runtime::ChannelService {
public:
    ql32::runtime::ChannelId open(ql32::runtime::SessionId,
                                  const std::string&,
                                  ql32::runtime::ChannelMode) override {
        return 2;
    }
    ql32::runtime::ChannelId openDevice(
        ql32::runtime::SessionId, ql32::runtime::ChannelDevice,
        const std::vector<std::string>&) override {
        return 2;
    }
    std::optional<std::string> readLine(ql32::runtime::SessionId,
                                        ql32::runtime::ChannelId) override {
        return std::nullopt;
    }
    std::string readBytes(ql32::runtime::SessionId, ql32::runtime::ChannelId,
                          std::size_t) override {
        return {};
    }
    void writeBytes(ql32::runtime::SessionId, ql32::runtime::ChannelId,
                    std::string_view) override {}
    void seek(ql32::runtime::SessionId, ql32::runtime::ChannelId,
              std::uint64_t) override {}
    bool atEnd(ql32::runtime::SessionId, ql32::runtime::ChannelId) override {
        return true;
    }
    void write(ql32::runtime::SessionId, ql32::runtime::ChannelId channel,
               const std::string& text) override {
        lastChannel = channel;
        lastText = text;
    }
    void close(ql32::runtime::SessionId, ql32::runtime::ChannelId) override {}
    void closeSession(ql32::runtime::SessionId) override {}
    std::vector<ql32::runtime::ChannelInfo> listOpen(
        ql32::runtime::SessionId) override {
        return {{2, ql32::runtime::ChannelType::Null}};
    }

    ql32::runtime::ChannelId lastChannel = 0;
    std::string lastText;
};

class CapturingGraphicsService final : public ql32::graphics::Service {
public:
    void setInk(ql32::graphics::SessionId, ql32::graphics::ColorIndex) override {}
    void setPaper(ql32::graphics::SessionId, ql32::graphics::ColorIndex) override {}
    void line(ql32::graphics::SessionId, ql32::graphics::Point,
              ql32::graphics::Point) override {}
    void rectangle(ql32::graphics::SessionId, ql32::graphics::Point,
                   std::int32_t, std::int32_t) override {}
    void circle(ql32::graphics::SessionId, ql32::graphics::Point,
                std::int32_t) override {}
    void text(ql32::graphics::SessionId, ql32::graphics::Point,
              const std::string& value) override {
        writtenText += value;
    }
    void writeText(ql32::graphics::SessionId,
                   const std::string& value) override {
        writtenText += value;
    }
    void clear(ql32::graphics::SessionId) override {}
    void closeSession(ql32::graphics::SessionId) override {}
    ql32::graphics::WindowInfo windowInfo(ql32::graphics::SessionId) override {
        return {};
    }
    ql32::graphics::RgbColor paletteColor(ql32::graphics::SessionId,
                                          std::uint16_t) override {
        return {};
    }

    std::string writtenText;
};

class CountingSystemControl final : public ql32::runtime::SystemControlService {
public:
    void reboot() override { ++rebootCount; }
    int rebootCount = 0;
};

void testRoundRobinSlicesKeepSessionsIndependent() {
    ql32::vm::SessionScheduler scheduler({}, 1);
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;
    const auto first = scheduler.launch(
        "FOR i=1 TO 3: PRINT \"first\";i: NEXT i\n", firstOutput);
    const auto second = scheduler.launch(
        "FOR i=1 TO 3: PRINT \"second\";i: NEXT i\n", secondOutput);

    for (int tick = 0; tick < 100 && scheduler.tick(); ++tick) {}
    const auto firstState = scheduler.task(first);
    const auto secondState = scheduler.task(second);
    require(firstState && firstState->status == ql32::vm::TaskStatus::Completed,
            "first scheduled program did not complete");
    require(secondState && secondState->status == ql32::vm::TaskStatus::Completed,
            "second scheduled program did not complete");
    require(firstOutput.str() == "first1\nfirst2\nfirst3\n",
            "first program state or output was corrupted across slices");
    require(secondOutput.str() == "second1\nsecond2\nsecond3\n",
            "second program state or output was corrupted across slices");
}

void testUiWaitDoesNotBlockOtherSessions() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream waitingOutput;
    std::ostringstream readyOutput;
    const auto waiting = scheduler.launch(
        "ALERT \"wait\",\"acknowledge\"\nPRINT \"resumed\"\n", waitingOutput);
    const auto ready = scheduler.launch("PRINT \"other session ran\"\n", readyOutput);

    require(scheduler.tick(), "scheduler did not run waiting UI task");
    require(scheduler.task(waiting)->status == ql32::vm::TaskStatus::WaitingUi,
            "alert did not suspend only its session");
    require(scheduler.tick(), "scheduler did not run second task while UI was pending");
    require(scheduler.task(ready)->status == ql32::vm::TaskStatus::Completed &&
                readyOutput.str() == "other session ran\n",
            "pending UI blocked another session");

    const auto request = ui->takeNextRequest();
    require(request && ui->resolve(request->id, ql32::ui::ResultKind::Acknowledged),
            "test could not acknowledge queued alert");
    require(scheduler.tick(), "scheduler did not resume acknowledged UI task");
    require(scheduler.task(waiting)->status == ql32::vm::TaskStatus::Completed &&
                waitingOutput.str() == "resumed\n",
            "suspended session did not resume after UI acknowledgement");
}

void testInteractiveSessionUsesSlicesAndKeepsReplState() {
    ql32::vm::SessionScheduler scheduler({}, 1);
    std::ostringstream replOutput;
    std::ostringstream otherOutput;
    const auto repl = scheduler.launchInteractive(replOutput);
    const auto other = scheduler.launch("PRINT \"other task\"\n", otherOutput);

    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "interactive session did not start waiting for input");
    require(scheduler.submit(repl,
                "10 FOR i=1 TO 3: PRINT \"repl\";i: NEXT i\n"),
            "interactive session rejected input while ready");
    require(scheduler.tick(), "scheduler did not run interactive input");
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "program line entry did not return to the REPL");
    require(scheduler.submit(repl, "RUN\n"), "interactive session rejected RUN");

    for (int tick = 0; tick < 200 && scheduler.tick(); ++tick) {}
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "sliced RUN did not return to the interactive prompt");
    require(scheduler.task(other)->status == ql32::vm::TaskStatus::Completed,
            "finite scheduled task did not run alongside the interactive session");
    require(replOutput.str() == "repl1\nrepl2\nrepl3\n",
            "interactive RUN output was corrupted across scheduler slices");
    require(otherOutput.str() == "other task\n",
            "other scheduled task output was corrupted");

    require(scheduler.submit(repl, "answer=42\n"),
            "interactive session did not accept another REPL statement");
    for (int tick = 0; tick < 20 && scheduler.tick(); ++tick) {}
    require(scheduler.submit(repl, "PRINT answer\n"),
            "interactive session did not retain variables between submissions");
    for (int tick = 0; tick < 20 && scheduler.tick(); ++tick) {}
    require(replOutput.str() == "repl1\nrepl2\nrepl3\n42\n",
            "interactive session lost state between REPL submissions");
}

void testInteractiveStartupProgramSharesReplSession() {
    ql32::vm::SessionScheduler scheduler({}, 1);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output,
        "startupValue=40\nPRINT \"startup complete\"\n");
    for (int tick = 0; tick < 50 && scheduler.tick(); ++tick) {}
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "interactive startup did not yield the prompt after completion");
    require(scheduler.submit(repl, "PRINT startupValue+2\n"),
            "REPL rejected input after startup");
    for (int tick = 0; tick < 20 && scheduler.tick(); ++tick) {}
    require(output.str() == "startup complete\n42\n",
            "startup and REPL did not share one interpreter session");
}

void testInteractiveSessionRecoversFromCommandErrors() {
    ql32::vm::SessionScheduler scheduler({}, 8);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output);
    require(scheduler.submit(repl, "PRINT )\n"),
            "interactive session rejected malformed input");
    require(scheduler.tick(), "scheduler did not report malformed input");
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "interactive command error terminated the REPL session");
    require(!scheduler.takeError(repl).empty(),
            "interactive command error was not made available to the host");
    require(scheduler.submit(repl, "PRINT \"recovered\"\n"),
            "interactive session did not recover after an error");
    require(scheduler.tick(), "scheduler did not execute the recovered command");
    require(output.str() == "recovered\n",
            "interactive session output was incorrect after error recovery");
}

void testInteractiveUiWaitResumesThroughScheduler() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output);
    require(scheduler.submit(repl,
                "ALERT \"wait\",\"acknowledge\"\nPRINT \"resumed\"\n"),
            "interactive session rejected UI command");
    require(scheduler.tick() &&
                scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingUi,
            "interactive UI request did not suspend its session");
    const auto request = ui->takeNextRequest();
    require(request && ui->resolve(request->id, ql32::ui::ResultKind::Acknowledged),
            "test could not acknowledge interactive alert");
    require(scheduler.tick() &&
                scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "interactive session did not return to input after UI completion");
    require(output.str() == "resumed\n",
            "interactive session did not continue after UI completion");
}

void testSmoothWeightedRoundRobinMatchesReferenceSequence() {
    ql32::vm::SmoothWeightedRoundRobin policy;
    const std::vector<ql32::vm::SmoothWeightedRoundRobin::Task> tasks = {
        {1, 4}, {2, 5}, {3, 1}, {4, 2}, {5, 16}};
    const std::vector<std::uint64_t> expectedPrefix = {
        5, 2, 5, 1, 5, 4, 5, 5, 2, 5, 1};
    std::vector<std::uint64_t> counts(5, 0);

    for (int tick = 0; tick < 71; ++tick) {
        const auto selected = policy.select(tasks);
        require(selected.has_value(), "positive weights produced an idle tick");
        const auto taskId = tasks[*selected].id;
        if (tick < static_cast<int>(expectedPrefix.size()))
            require(taskId == expectedPrefix[static_cast<std::size_t>(tick)],
                    "scheduler sequence differs from the reference sequence");
        ++counts[static_cast<std::size_t>(taskId - 1)];
    }

    require(counts == std::vector<std::uint64_t>({10, 13, 3, 5, 40}),
            "scheduler totals differ from the reference 71-tick totals");
}

void testZeroPriorityTaskIsNeverSelected() {
    ql32::vm::SmoothWeightedRoundRobin policy;
    const std::vector<ql32::vm::SmoothWeightedRoundRobin::Task> tasks = {
        {1, 4}, {2, 0}};
    for (int tick = 0; tick < 40; ++tick) {
        const auto selected = policy.select(tasks);
        require(selected && *selected == 0,
                "zero-priority task was selected");
    }
}

void testWeightedPolicyResetsForRemovalAndPriorityChanges() {
    using Policy = ql32::vm::SmoothWeightedRoundRobin;
    Policy removalPolicy;
    std::vector<Policy::Task> tasks = {{1, 1}, {2, 1}, {3, 1}};
    require(removalPolicy.select(tasks) == 0,
            "initial tie did not select the first task");
    tasks.pop_back();
    require(removalPolicy.select(tasks) == 0,
            "removing a task did not reset the remaining task credits");

    Policy priorityPolicy;
    tasks = {{1, 1}, {2, 1}};
    require(priorityPolicy.select(tasks) == 0,
            "initial priority-change setup selected the wrong task");
    tasks[0].priority = 2;
    require(priorityPolicy.select(tasks) == 0,
            "changing a priority did not reset all task credits");

    Policy additionPolicy;
    tasks = {{1, 1}, {2, 1}};
    require(additionPolicy.select(tasks) == 0,
            "initial task-addition setup selected the wrong task");
    tasks.push_back({3, 1});
    require(additionPolicy.select(tasks) == 1,
            "adding a task did not preserve existing credits and start new credit at zero");
}

void testPriorityWeightsChangeFrequency() {
    ql32::vm::SessionScheduler scheduler({}, 16);
    std::ostringstream highOutput;
    std::ostringstream normalOutput;
    std::ostringstream backgroundOutput;
    const auto high = scheduler.launch(
        "FOR i=1 TO 100000: PRINT \"high\": NEXT i\n", highOutput);
    const auto normal = scheduler.launch(
        "FOR i=1 TO 100000: PRINT \"normal\": NEXT i\n", normalOutput);
    const auto background = scheduler.launch(
        "FOR i=1 TO 100000: PRINT \"background\": NEXT i\n", backgroundOutput);

    require(scheduler.setTaskPriority(high, 16), "could not set high task priority");
    require(scheduler.setTaskPriority(normal, 4), "could not set normal task priority");
    require(scheduler.setTaskPriority(background, 1),
            "could not set low task priority");
    for (int tick = 0; tick < 260; ++tick)
        require(scheduler.tick(), "weighted scheduler stopped with runnable tasks");

    const auto countLines = [](const std::string& output) {
        std::size_t lines = 0;
        for (char ch : output)
            if (ch == '\n') ++lines;
        return lines;
    };
    const auto highLines = countLines(highOutput.str());
    const auto normalLines = countLines(normalOutput.str());
    const auto backgroundLines = countLines(backgroundOutput.str());
    require(highLines > normalLines && normalLines > backgroundLines,
            "higher priority tasks did not run more often");
    require(backgroundLines > 0,
            "low-priority runnable task was starved");
    require(scheduler.task(high)->priority == 16 &&
                scheduler.task(normal)->priority == 4 &&
                scheduler.task(background)->priority == 1,
            "task priority was not retained in task information");
}

void testSupervisorCanSetSliceAndWorkerPriority() {
    ql32::vm::SessionScheduler scheduler({}, 128);
    std::ostringstream supervisorOutput;
    std::ostringstream workerOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    const auto worker = scheduler.launchWorker("PRINT \"worker ready\"\n", workerOutput);

    require(scheduler.submit(supervisor, "SLICE 16\n"),
            "supervisor did not accept SLICE command");
    for (int tick = 0; tick < 10 && scheduler.instructionBudget() != 16; ++tick)
        require(scheduler.tick(), "scheduler did not execute SLICE command");
    require(scheduler.instructionBudget() == 16,
            "SLICE command did not change scheduler instruction budget");

    require(scheduler.submit(supervisor, "PRIORITY " + std::to_string(worker) + ",12\n"),
            "supervisor did not accept PRIORITY command");
    for (int tick = 0; tick < 10 && scheduler.task(worker)->priority != 12; ++tick)
        require(scheduler.tick(), "scheduler did not execute PRIORITY command");
    require(scheduler.task(worker)->priority == 12,
            "PRIORITY command did not update the selected task");

    require(scheduler.submit(supervisor, "SUSPEND " + std::to_string(worker) + "\n"),
            "supervisor did not accept SUSPEND command");
    for (int tick = 0; tick < 10 && scheduler.task(worker)->priority != 0; ++tick)
        require(scheduler.tick(), "scheduler did not execute SUSPEND command");
    require(scheduler.task(worker)->priority == 0 &&
                scheduler.task(worker)->resumePriority == 12,
            "SUSPEND did not preserve the previous task priority");

    require(scheduler.submit(supervisor, "TASKS\n"),
            "supervisor did not accept TASKS after suspension");
    for (int tick = 0; tick < 10 &&
         supervisorOutput.str().find("SUSPENDED (resume P=12)") == std::string::npos;
         ++tick)
        require(scheduler.tick(), "scheduler did not list the suspended task");

    require(scheduler.submit(supervisor, "RESUME " + std::to_string(worker) + "\n"),
            "supervisor did not accept RESUME command");
    for (int tick = 0; tick < 10 && scheduler.task(worker)->priority == 0; ++tick)
        require(scheduler.tick(), "scheduler did not execute RESUME command");
    require(scheduler.task(worker)->priority == 12,
            "RESUME did not restore the previous task priority");

    require(scheduler.submit(supervisor,
                "PRIORITY " + std::to_string(worker) + ",0\n"),
            "supervisor did not accept zero-priority suspension");
    for (int tick = 0; tick < 10 && scheduler.task(worker)->priority != 0; ++tick)
        require(scheduler.tick(), "scheduler did not execute zero-priority command");
    require(scheduler.task(worker)->resumePriority == 0,
            "PRIORITY 0 did not replace the saved priority baseline");
    require(scheduler.submit(supervisor,
                "PRIORITY " + std::to_string(worker) + ",12\n"),
            "supervisor did not accept a new baseline priority");
    for (int tick = 0; tick < 10 && scheduler.task(worker)->priority != 12; ++tick)
        require(scheduler.tick(), "scheduler did not execute the new priority command");
    require(scheduler.task(worker)->priority == 12,
            "positive PRIORITY did not replace the zero baseline");

    require(supervisorOutput.str().find("Instruction slice set to 16") != std::string::npos &&
                supervisorOutput.str().find("priority set to 12") != std::string::npos &&
                supervisorOutput.str().find("Suspended session") != std::string::npos &&
                supervisorOutput.str().find("Resumed session") != std::string::npos &&
                supervisorOutput.str().find("SUSPENDED (resume P=12)") !=
                    std::string::npos,
            "supervisor did not receive scheduler command confirmations");
}

void testTasksUsesDefaultAndExplicitPrintChannels() {
    auto channels = std::make_shared<CapturingChannelService>();
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.channels = channels;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream consoleOutput;
    const auto supervisor = scheduler.launchInteractive(consoleOutput);

    require(scheduler.submit(supervisor, "sink=OPEN(3)\n"),
            "supervisor could not open a channel for TASKS output");
    require(scheduler.tick(), "scheduler did not open the test channel");

    require(scheduler.submit(supervisor, "TASKS\n"),
            "supervisor did not accept unqualified TASKS");
    require(scheduler.tick(), "scheduler did not execute unqualified TASKS");
    require(graphics->writtenText.find("SLICE 32 instructions") != std::string::npos,
            "TASKS did not default to print channel #1");

    require(scheduler.submit(supervisor, "TASKS #2\n"),
            "supervisor did not accept TASKS channel redirection");
    require(scheduler.tick(), "scheduler did not execute redirected TASKS");
    require(channels->lastChannel == 2 &&
                channels->lastText.find("SLICE 32 instructions") != std::string::npos,
            "TASKS #channel did not redirect output to the selected channel");
}

void testKillConfirmationUsesConsoleChannel() {
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream supervisorOutput;
    std::ostringstream workerOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    const auto worker = scheduler.launchWorker("x=1\n", workerOutput);

    require(scheduler.submit(supervisor, "KILL " + std::to_string(worker) + "\n"),
            "supervisor did not accept KILL command");
    require(scheduler.tick(), "scheduler did not execute KILL command");
    require(supervisorOutput.str() == "Killed task " + std::to_string(worker) + "\n",
            "KILL confirmation was not sent to console channel #0");
    require(graphics->writtenText.empty(),
            "KILL confirmation allocated or wrote to graphics channel #1");
}

void testTaskSupportsCommandAndFunctionSyntax() {
    auto storage = std::make_shared<ProgramStorage>();
    storage->files["worker.bas"] = "PRINT \"worker ready\"\n";
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.storage = storage;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);

    require(scheduler.submit(supervisor, "id=TASK(\"worker.bas\")\n"),
            "supervisor did not accept TASK function syntax");
    require(scheduler.tick(), "scheduler did not execute TASK function syntax");
    require(graphics->writtenText.empty(),
            "TASK function printed an implicit status message");
    const auto functionChild = scheduler.tasks();
    require(functionChild.size() == 2 && functionChild[1].id == 2,
            "TASK function did not create a worker");

    require(scheduler.submit(supervisor, "TASK \"worker.bas\"\n"),
            "supervisor did not accept TASK command syntax");
    require(scheduler.tick(), "scheduler did not execute TASK command syntax");
    const auto commandChild = scheduler.tasks();
    require(commandChild.size() == 3 && commandChild[2].id == 3,
            "TASK command did not create a worker");
    require(output.str() == "Started task 3\n" && graphics->writtenText.empty(),
            "TASK command did not report its task ID to console channel #0");
}

void testTaskWaitResumesCallerAndExposesChildStatus() {
    auto storage = std::make_shared<ProgramStorage>();
    storage->files["worker.bas"] = "x=42\n";
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.storage = storage;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);

    require(scheduler.submit(supervisor,
                "id=TASK(\"worker.bas\",1)\n"
                "state$=TASKRESULT(id,1)\n"
                "PRINT state$\n"),
            "supervisor did not accept TASK wait syntax");
    require(scheduler.tick(), "scheduler did not start the waiting task");
    require(scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingTask,
            "caller did not suspend while its child was running");
    require(scheduler.focusedTask() == 2,
            "waiting task did not focus its child");
    require(scheduler.tick(), "scheduler did not run the waiting task's child");
    require(scheduler.tick(), "scheduler did not resume the waiting caller");
    const auto tasks = scheduler.tasks();
    require(tasks.size() == 2 && tasks[0].status == ql32::vm::TaskStatus::WaitingInput &&
                tasks[1].status == ql32::vm::TaskStatus::Completed,
            "wait form did not finish child and return caller to its prompt");
    require(graphics->writtenText == "COMPLETED\n",
            "TASKRESULT did not expose the completed standard task status");
}

void testTaskWaitReturnsChildErrorContext() {
    auto storage = std::make_shared<ProgramStorage>();
    storage->files["bad.bas"] = "PRINT (\n";
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.storage = storage;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);

    require(scheduler.submit(supervisor, "TASK \"bad.bas\",1\n"),
            "supervisor did not accept command-form TASK wait syntax");
    require(scheduler.tick(), "scheduler did not start the failing child");
    require(scheduler.tick(), "scheduler did not report the child's failure");
    require(scheduler.tick(), "scheduler did not resume after child failure");
    const auto parent = scheduler.task(supervisor);
    const auto child = scheduler.task(2);
    require(parent && child && child->status == ql32::vm::TaskStatus::Failed &&
                parent->errorCode == ql32::runtime::ErrorCode::Syntax &&
                !parent->errorDetail.empty(),
            "waiting caller did not receive the child's error code and detail");
}

void testTaskAndVmPausePreserveRunnableSessions() {
    ql32::vm::SessionScheduler scheduler({}, 16);
    std::ostringstream taskOutput;
    const auto task = scheduler.launch("PRINT \"task resumed\"\n", taskOutput);

    const auto paused = scheduler.toggleTaskPaused(task);
    require(paused && *paused && !scheduler.tick() && taskOutput.str().empty(),
            "paused task was allowed to execute");
    const auto resumed = scheduler.toggleTaskPaused(task);
    require(resumed && !*resumed, "task pause toggle did not resume it");
    for (int tick = 0; tick < 20 && scheduler.task(task)->status !=
            ql32::vm::TaskStatus::Completed; ++tick)
        (void)scheduler.tick();
    require(taskOutput.str() == "task resumed\n",
            "resumed task did not continue from its saved execution state");

    ql32::vm::SessionScheduler vmScheduler({}, 16);
    std::ostringstream vmOutput;
    const auto vmTask = vmScheduler.launch("PRINT \"vm resumed\"\n", vmOutput);
    require(vmScheduler.toggleExecutionPaused() && !vmScheduler.tick() &&
                vmOutput.str().empty(),
            "VM-wide pause did not stop scheduler execution");
    require(!vmScheduler.toggleExecutionPaused(), "VM-wide pause did not toggle off");
    for (int tick = 0; tick < 20 && vmScheduler.task(vmTask)->status !=
            ql32::vm::TaskStatus::Completed; ++tick)
        (void)vmScheduler.tick();
    require(vmOutput.str() == "vm resumed\n",
            "unpaused VM did not resume its task");
}

void testBreakPreservesInteractiveSessionAndStopAllReturnsSupervisor() {
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 8);
    std::ostringstream supervisorOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    require(scheduler.submit(supervisor,
                "x=7\nREPeat forever\nx=x+1\nEND REPeat forever\n"),
            "supervisor did not accept the long-running program");
    for (int tick = 0; tick < 8; ++tick) (void)scheduler.tick();
    require(scheduler.breakTaskExecution(supervisor),
            "break did not interrupt the focused interactive program");
    require(scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingInput,
            "break did not return the interactive session to its prompt");
    require(scheduler.submit(supervisor, "PRINT x\n"),
            "broken session did not accept another command");
    require(scheduler.tick(), "broken session did not execute the next command");
    require(graphics->writtenText.find('7') != std::string::npos,
            "break discarded the interactive session's variables");

    std::ostringstream workerOutput;
    const auto worker = scheduler.launchWorker("REPeat forever\nEND REPeat forever\n",
                                               workerOutput);
    scheduler.toggleExecutionPaused();
    scheduler.stopWorkersAndReturnToSupervisor();
    require(scheduler.focusedTask() == supervisor && !scheduler.executionPaused() &&
                scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingInput &&
                scheduler.task(worker)->status == ql32::vm::TaskStatus::Cancelled,
            "stop-all did not cancel workers and restore the supervisor prompt");
}

void testRebootInteractiveCommandCallsSystemService() {
    auto systemControl = std::make_shared<CountingSystemControl>();
    ql32::vm::Services services;
    services.systemControl = systemControl;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);
    require(scheduler.submit(supervisor, "REBOOT\n"),
            "supervisor did not accept REBOOT");
    require(scheduler.tick(), "scheduler did not dispatch REBOOT");
    require(systemControl->rebootCount == 1 &&
                output.str().find("Rebooting QL/32") != std::string::npos,
            "REBOOT did not notify and invoke the system-control service");
}

} // namespace

int main() {
    testRoundRobinSlicesKeepSessionsIndependent();
    testUiWaitDoesNotBlockOtherSessions();
    testInteractiveSessionUsesSlicesAndKeepsReplState();
    testInteractiveStartupProgramSharesReplSession();
    testInteractiveSessionRecoversFromCommandErrors();
    testInteractiveUiWaitResumesThroughScheduler();
    testSmoothWeightedRoundRobinMatchesReferenceSequence();
    testZeroPriorityTaskIsNeverSelected();
    testWeightedPolicyResetsForRemovalAndPriorityChanges();
    testPriorityWeightsChangeFrequency();
    testSupervisorCanSetSliceAndWorkerPriority();
    testTasksUsesDefaultAndExplicitPrintChannels();
    testKillConfirmationUsesConsoleChannel();
    testTaskSupportsCommandAndFunctionSyntax();
    testTaskWaitResumesCallerAndExposesChildStatus();
    testTaskWaitReturnsChildErrorContext();
    testTaskAndVmPausePreserveRunnableSessions();
    testBreakPreservesInteractiveSessionAndStopAllReturnsSupervisor();
    testRebootInteractiveCommandCallsSystemService();
    return 0;
}
