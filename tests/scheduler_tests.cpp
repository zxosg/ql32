#include "ql32/vm/scheduler.hpp"
#include "ql32/ui/service.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
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
    bool exists(const std::string& path) override { return files.count(path) != 0; }
    std::uintmax_t size(const std::string& path) override { return files.at(path).size(); }
    void rename(const std::string& path, const std::string& newPath) override {
        files[newPath] = std::move(files.at(path));
        files.erase(path);
    }
    void remove(const std::string& path) override { files.erase(path); }

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
        return info;
    }
    ql32::graphics::RgbColor paletteColor(ql32::graphics::SessionId,
                                          std::uint16_t) override {
        return {};
    }

    std::string writtenText;
    ql32::graphics::WindowInfo info{120, 32, 4, 16, 0, 0, 0};
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

void testOptionalPagerSuspendsOnlyReplAndContinuesTail() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    auto graphics = std::make_shared<CapturingGraphicsService>();
    ql32::vm::Services services;
    services.ui = ui;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 64);
    std::ostringstream replOutput;
    std::ostringstream workerOutput;
    const auto repl = scheduler.launchInteractive(replOutput);
    const auto worker = scheduler.launch("PRINT \"other session ran\"\n", workerOutput);

    require(scheduler.submit(repl,
                "PAGER ON\nHELP\nPRINT #0,\"after pager\"\n"),
            "REPL did not accept the pager command sequence");
    for (int tick = 0; tick < 8 && scheduler.task(repl)->status !=
         ql32::vm::TaskStatus::WaitingUi; ++tick)
        (void)scheduler.tick();
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingUi,
            "PAGER ON did not suspend after filling the output window");
    const auto firstPage = ui->takeNextRequest();
    require(firstPage && firstPage->kind == ql32::ui::RequestKind::Input &&
                firstPage->title.find("Enter=page") != std::string::npos,
            "pager did not request its next-page input asynchronously");

    for (int tick = 0; tick < 8 && scheduler.task(worker)->status !=
         ql32::vm::TaskStatus::Completed; ++tick)
        (void)scheduler.tick();
    require(scheduler.task(worker)->status == ql32::vm::TaskStatus::Completed &&
                graphics->writtenText.find("other session ran\n") != std::string::npos,
            "a waiting pager blocked another session");

    auto request = firstPage;
    bool useLineStep = true;
    std::size_t pages = 0;
    while (request) {
        require(ui->submitInput(request->id, useLineStep ? "L" : ""),
                "pager input was not accepted");
        useLineStep = false;
        ++pages;
        request.reset();
        for (int tick = 0; tick < 64; ++tick) {
            (void)scheduler.tick();
            request = ui->takeNextRequest();
            if (request || scheduler.task(repl)->status ==
                               ql32::vm::TaskStatus::WaitingInput)
                break;
        }
    }
    for (int tick = 0; tick < 4 && scheduler.task(repl)->status !=
         ql32::vm::TaskStatus::WaitingInput; ++tick)
        (void)scheduler.tick();
    require(pages > 1 && scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "pager did not finish and return the REPL to input");
    require(graphics->writtenText.find("Shell commands:") != std::string::npos &&
                graphics->writtenText.find("Built-in functions:") != std::string::npos,
            "pager did not emit the complete HELP listing");
    require(replOutput.str() == "after pager\n",
            "commands after the paged listing were not resumed in order");
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

void testSchedulerOffersNonAllocatingTaskStatusAndErrorViews() {
    ql32::vm::SessionScheduler scheduler({}, 8);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output);
    require(scheduler.activeTaskCount() == 1 &&
                scheduler.taskStatus(repl) == ql32::vm::TaskStatus::WaitingInput &&
                !scheduler.hasWaitingUiTask(),
            "non-allocating scheduler status queries returned incorrect state");
    require(scheduler.submit(repl, "PRINT )\n") && scheduler.tick(),
            "scheduler did not produce an error for the status-view test");
    std::string captured;
    scheduler.visitTaskErrors([](ql32::vm::TaskId id, const char* error, void* context) {
        require(id == 1, "task error visitor received the wrong task ID");
        static_cast<std::string*>(context)->assign(error);
    }, &captured);
    require(!captured.empty() && scheduler.takeError(repl).empty(),
            "scheduler did not visit and clear a pending task error");
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

void testInteractiveRunUnwindsReplAndContinuesTrailingCommands() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 8);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output);
    require(scheduler.submit(repl,
                "10 IF 1 THEN\n"
                "20 IF 1 THEN\n"
                "30 ALERT \"nested\",\"acknowledge\"\n"
                "40 PRINT \"program finished\"\n"
                "50 END IF\n"
                "60 END IF\n"),
            "interactive session rejected nested program lines");
    require(scheduler.tick() &&
                scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "nested program entry did not return to the REPL");

    require(scheduler.submit(repl, "RUN\nPRINT \"tail command\"\n"),
            "interactive session rejected RUN with a following command");
    require(scheduler.tick() &&
                scheduler.task(repl)->status == ql32::vm::TaskStatus::Runnable,
            "RUN was not deferred to the scheduler after the REPL slice");
    for (int tick = 0; tick < 40 && scheduler.task(repl)->status !=
            ql32::vm::TaskStatus::WaitingUi; ++tick)
        (void)scheduler.tick();
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingUi,
            "deferred nested program did not reach its alert");
    const auto request = ui->takeNextRequest();
    require(request && ui->resolve(request->id, ql32::ui::ResultKind::Acknowledged),
            "test could not acknowledge deferred RUN alert");
    for (int tick = 0; tick < 40 && scheduler.task(repl)->status !=
            ql32::vm::TaskStatus::WaitingInput; ++tick)
        (void)scheduler.tick();
    require(scheduler.task(repl)->status == ql32::vm::TaskStatus::WaitingInput,
            "deferred RUN or its trailing command did not return to the prompt");
    require(output.str() == "program finished\ntail command\n",
            "deferred RUN lost program output or its trailing command");
}

void testBreakDuringDeferredRunDiscardsTrailingCommands() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 8);
    std::ostringstream output;
    const auto repl = scheduler.launchInteractive(output);
    require(scheduler.submit(repl, "10 ALERT \"wait\",\"acknowledge\"\n"),
            "interactive session rejected the alert program");
    require(scheduler.tick(), "scheduler did not store the alert program");
    require(scheduler.submit(repl, "RUN\nPRINT \"discarded tail\"\n"),
            "interactive session rejected RUN before break");
    require(scheduler.tick(), "scheduler did not defer RUN before break");
    require(scheduler.tick() && scheduler.task(repl)->status ==
                ql32::vm::TaskStatus::WaitingUi,
            "deferred program did not suspend for UI");
    require(scheduler.breakTaskExecution(repl),
            "break did not abort the deferred interactive run");
    require(scheduler.submit(repl, "PRINT \"after break\"\n"),
            "interactive session did not accept input after breaking RUN");
    require(scheduler.tick() && scheduler.task(repl)->status ==
                ql32::vm::TaskStatus::WaitingInput,
            "interactive session did not return to input after break");
    require(output.str() == "after break\n",
            "breaking deferred RUN executed its trailing command");
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

void testSchedulerReleasesTaskBookkeepingAfterWorkerBursts() {
    ql32::vm::SessionScheduler scheduler({}, 8);
    std::ostringstream output;
    for (int cycle = 0; cycle < 4; ++cycle) {
        std::vector<ql32::vm::TaskId> workers;
        for (int index = 0; index < 12; ++index) {
            workers.push_back(scheduler.launchWorker(
                "REPeat forever\nEND REPeat forever\n", output));
        }
        for (int tick = 0; tick < 24; ++tick)
            require(scheduler.tick(), "worker burst unexpectedly became idle");
        for (const auto worker : workers)
            require(scheduler.cancel(worker), "worker cancellation failed");
        for (const auto worker : workers)
            require(scheduler.reap(worker), "cancelled worker was not reaped");
        require(scheduler.activeTaskCount() == 0 && scheduler.tasks().empty(),
                "reaped worker bookkeeping remained in the scheduler");
    }
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

void testIdleWorkerRetainsItsWindowWhenAnotherWorkerStarts() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        32, 8, nullptr);
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream supervisorOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);

    const auto startWorker = [&](const char* source) {
        std::ostringstream output;
        const auto worker = scheduler.launchWorker(source, output);
        for (int tick = 0; tick < 20 && scheduler.task(worker)->status !=
                ql32::vm::TaskStatus::WaitingInput; ++tick)
            require(scheduler.tick(), "worker did not finish its startup source");
        require(scheduler.task(worker)->status == ql32::vm::TaskStatus::WaitingInput,
                "worker did not remain available at its interactive prompt");
        return worker;
    };
    const auto first = startWorker("WINDOW 0,0,16,8\nPAPER 2\nCLS\n");
    require(graphics->windowManager().size() == 1,
            "first worker's window disappeared at its prompt");
    const auto second = startWorker("WINDOW 16,0,16,8\nPAPER 4\nCLS\n");
    require(second != first && graphics->windowManager().size() == 2,
            "starting another worker discarded the idle worker's window");
    require(scheduler.logon(first) && scheduler.focusedTask() == first,
            "LOGON could not focus the first worker after its startup program");
    require(scheduler.logoff() == supervisor,
            "LOGOFF did not return focus to the supervisor");
}

void testCancellingWorkerDiscardsItsGraphicsAllocations() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        32, 8, nullptr);
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream supervisorOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    std::ostringstream workerOutput;
    const auto worker = scheduler.launchWorker(
        "WINDOW 0,0,32,8\nPAPER 4\nCLS\nREPeat forever\nEND REPeat forever\n",
        workerOutput);
    for (int tick = 0; tick < 30 && graphics->windowManager().size() == 0; ++tick)
        (void)scheduler.tick();
    require(graphics->windowManager().size() == 1,
            "worker did not allocate its graphics canvas");
    require(scheduler.cancel(worker), "worker cancellation failed");
    require(scheduler.task(worker)->status == ql32::vm::TaskStatus::Cancelled &&
                graphics->windowManager().size() == 0,
            "cancelling a worker retained its canvas allocation");
    require(scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingInput,
            "cancelling a worker changed the supervisor session");
}

void testSupervisorTaskViewportUsesTaskToSessionMapping() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        32, 16, nullptr);
    // Consume a graphics session ID before task IDs are allocated so the test
    // detects accidental use of TaskId as the window owner SessionId.
    ql32::Interpreter unrelatedSession;
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream supervisorOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;
    const auto first = scheduler.launchWorker(
        "WINDOW 1,1,6,4\nCLS\n", firstOutput);
    const auto second = scheduler.launchWorker(
        "WINDOW 2,0,4,4\nCLS\n", secondOutput);
    for (int tick = 0; tick < 30; ++tick) {
        const auto firstInfo = scheduler.task(first);
        const auto secondInfo = scheduler.task(second);
        if (firstInfo && secondInfo &&
            firstInfo->status == ql32::vm::TaskStatus::WaitingInput &&
            secondInfo->status == ql32::vm::TaskStatus::WaitingInput)
            break;
        require(scheduler.tick(), "scheduler did not run viewport worker startup");
    }

    const auto before = graphics->windowManager().compositionOrder();
    require(before.size() == 2,
            "viewport workers did not create two graphics windows");
    const auto firstBefore = std::find_if(before.begin(), before.end(),
        [](const auto& item) { return item.bounds.x == 1 && item.bounds.y == 1; });
    const auto secondBefore = std::find_if(before.begin(), before.end(),
        [](const auto& item) { return item.bounds.x == 2 && item.bounds.y == 0; });
    require(firstBefore != before.end() && secondBefore != before.end() &&
                firstBefore->owner != first && secondBefore->owner != second,
            "test did not create distinct task IDs and graphics session IDs");
    require(scheduler.setTaskViewport(first, {5, 2, 12, 8}),
            "supervisor viewport operation rejected a valid task");

    const auto after = graphics->windowManager().compositionOrder();
    const auto firstOwner = firstBefore->owner;
    const auto secondOwner = secondBefore->owner;
    const auto moved = std::find_if(after.begin(), after.end(),
        [firstOwner](const auto& item) { return item.owner == firstOwner; });
    const auto unchanged = std::find_if(after.begin(), after.end(),
        [secondOwner](const auto& item) { return item.owner == secondOwner; });
    require(moved != after.end() && moved->bounds.x == 6 && moved->bounds.y == 3,
            "TaskId-to-SessionId mapping did not move the selected task's window");
    require(unchanged != after.end() && unchanged->bounds.x == 2 &&
                unchanged->bounds.y == 0,
            "setting one task viewport moved another task's window");
    const auto reported = scheduler.task(first);
    const auto untouched = scheduler.task(second);
    require(reported && reported->viewport.originX == 5 &&
                reported->viewport.originY == 2 &&
                reported->viewport.canvasWidth == 12 &&
                reported->viewport.canvasHeight == 8 && untouched &&
                untouched->viewport.originX == 0 &&
                untouched->viewport.canvasWidth == 32 &&
                untouched->viewport.canvasHeight == 16,
            "task information did not report viewport properties");
    (void)supervisor;
}

void testBasicTaskViewCommandIsSupervisorOnly() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        32, 16, nullptr);
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream supervisorOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    std::ostringstream workerOutput;
    const auto worker = scheduler.launchWorker("WINDOW 0,0,8,8\nCLS\n", workerOutput);
    for (int tick = 0; tick < 20; ++tick) {
        const auto info = scheduler.task(worker);
        if (info && info->status == ql32::vm::TaskStatus::WaitingInput) break;
        require(scheduler.tick(), "scheduler did not start viewport command worker");
    }

    require(scheduler.submit(supervisor,
                "TASKVIEW " + std::to_string(worker) + ",5,2,12,8\n"),
            "supervisor did not accept TASKVIEW");
    require(scheduler.tick(), "scheduler did not execute TASKVIEW");
    const auto changed = scheduler.task(worker);
    require(changed && changed->viewport.originX == 5 &&
                changed->viewport.originY == 2 &&
                changed->viewport.canvasWidth == 12 &&
                changed->viewport.canvasHeight == 8,
            "TASKVIEW did not update the selected task's viewport");
    require(scheduler.submit(supervisor,
                "PRINT #0,GETTASK(KTaskCount)\n"),
            "supervisor did not accept GETTASK count");
    require(scheduler.tick(), "scheduler did not execute GETTASK count");
    require(scheduler.submit(supervisor,
                "PRINT #0,GETTASK(KTaskHandle,1)\n"),
            "supervisor did not accept GETTASK handle");
    require(scheduler.tick(), "scheduler did not execute GETTASK handle");
    require(supervisorOutput.str().find("2") != std::string::npos &&
                supervisorOutput.str().find(std::to_string(worker)) != std::string::npos,
            "GETTASK did not return task count and ID enumeration");

    std::ostringstream unauthorizedOutput;
    const auto unauthorized = scheduler.launchWorker(
        "TASKVIEW 1,0,0,8,8\n", unauthorizedOutput);
    for (int tick = 0; tick < 20; ++tick) {
        const auto info = scheduler.task(unauthorized);
        if (info && info->status == ql32::vm::TaskStatus::Failed) break;
        if (!scheduler.tick()) break;
    }
    const auto rejected = scheduler.task(unauthorized);
    if (!(rejected &&
          rejected->errorDetail.find("supervisor-only") != std::string::npos)) {
        throw std::runtime_error("TASKVIEW was not restricted to the supervisor (state=" +
            (rejected ? std::to_string(static_cast<int>(rejected->status)) :
                        std::string("missing")) + ", error=" +
            (rejected ? rejected->errorDetail : std::string("missing")) + ")");
    }
}

void testSupervisorCanToggleDebugBar() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        320, 240, nullptr);
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 16);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);
    require(scheduler.submit(supervisor, "DEBUGBAR ON\n"),
            "supervisor did not accept DEBUGBAR ON");
    require(scheduler.tick() && graphics->debugOverlayEnabled(),
            "DEBUGBAR ON did not enable the overlay");
    require(scheduler.submit(supervisor, "DEBUGBAR OFF\n"),
            "supervisor did not accept DEBUGBAR OFF");
    require(scheduler.tick() && !graphics->debugOverlayEnabled(),
            "DEBUGBAR OFF did not disable the overlay");
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

void testKillRemovesCompletedAndFailedTaskRecords() {
    ql32::vm::SessionScheduler scheduler({}, 32);
    std::ostringstream completedOutput;
    std::ostringstream failedOutput;
    std::ostringstream supervisorOutput;
    const auto completed = scheduler.launch("PRINT \"done\"\n", completedOutput);
    const auto failed = scheduler.launch("PRINT (\n", failedOutput);
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);

    for (int tick = 0; tick < 8; ++tick) scheduler.tick();
    require(scheduler.task(completed)->status == ql32::vm::TaskStatus::Completed &&
                scheduler.task(failed)->status == ql32::vm::TaskStatus::Failed,
            "test tasks did not reach completed and failed states");

    require(scheduler.submit(supervisor, "KILL " + std::to_string(completed) + "\n"),
            "supervisor did not accept KILL for a completed task");
    require(scheduler.tick(), "scheduler did not remove the completed task record");
    require(!scheduler.task(completed),
            "KILL left a completed task in the task registry");

    require(scheduler.submit(supervisor, "KILL " + std::to_string(failed) + "\n"),
            "supervisor did not accept KILL for a failed task");
    require(scheduler.tick(), "scheduler did not remove the failed task record");
    require(!scheduler.task(failed),
            "KILL left a failed task in the task registry");
    require(supervisorOutput.str() ==
                "Killed task " + std::to_string(completed) + "\n" +
                "Killed task " + std::to_string(failed) + "\n",
            "KILL did not confirm removal of terminal task records");
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

void testCompletedTaskCanBeReapedExplicitly() {
    ql32::vm::SessionScheduler scheduler;
    std::ostringstream output;
    const auto task = scheduler.launch("PRINT 1\n", output);
    require(scheduler.tick(), "scheduler did not run task before reaping");
    require(scheduler.task(task) &&
                scheduler.task(task)->status == ql32::vm::TaskStatus::Completed,
            "completed task result was not retained before reaping");
    require(scheduler.reap(task), "completed task could not be reaped");
    require(!scheduler.task(task), "reaped task remained queryable");
    require(!scheduler.reap(task), "reaping an unknown task succeeded");
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

void testWaitTaskResumesAfterDialogAndMenu() {
    auto storage = std::make_shared<ProgramStorage>();
    storage->files["ui.bas"] =
        "dialogHandle = DINIT(\"Profile\")\n"
        "DTEXT dialogHandle, \"Edit the profile\"\n"
        "DEDIT dialogHandle, \"Name\", \"Ada\"\n"
        "DINT dialogHandle, \"Age\", 36, 0, 150\n"
        "DFLOAT dialogHandle, \"Score\", 1.5, 0, 10\n"
        "DCHOICE dialogHandle, \"Color\", 2, \"Red|Green|Blue\"\n"
        "DCHECK dialogHandle, \"Active\", 1\n"
        "DBUTTONS dialogHandle, \"Apply\", \"Cancel\"\n"
        "IF DIALOG(dialogHandle) THEN\n"
        "  PRINT DGET$(dialogHandle, 2)\n"
        "END IF\n"
        "menuHandle = MINIT(\"Main menu\")\n"
        "MCARD menuHandle, 10, \"Open\", \"O\"\n"
        "MCARD menuHandle, 90, \"Exit\", \"X\"\n"
        "command = MENU(menuHandle)\n"
        "PRINT command\n";
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.storage = storage;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 128);
    std::ostringstream output;
    const auto supervisor = scheduler.launchInteractive(output);
    require(scheduler.submit(supervisor,
                "id=TASK(\"ui.bas\",1)\nPRINT TASKRESULT(id,1)\n"),
            "supervisor did not accept wait-mode UI task");
    require(scheduler.tick() &&
                scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingTask,
            "supervisor did not wait for its UI task");
    require(scheduler.tick() &&
                scheduler.task(2)->status == ql32::vm::TaskStatus::WaitingUi,
            "UI task did not suspend at its dialog");
    auto request = ui->takeNextRequest();
    require(request && request->kind == ql32::ui::RequestKind::Dialog &&
                ui->submitDialog(request->id, {
                    std::monostate{}, std::string("Grace"), 42.0, 2.75, 1.0, false}),
            "test could not submit the UI task's dialog");
    require(scheduler.tick() &&
                scheduler.task(2)->status == ql32::vm::TaskStatus::WaitingUi,
            "UI task did not resume from dialog and suspend at its menu");
    request = ui->takeNextRequest();
    require(request && request->kind == ql32::ui::RequestKind::Menu &&
                ui->selectMenu(request->id, 90),
            "test could not select the UI task's menu item");
    require(scheduler.tick() &&
                scheduler.task(2)->status == ql32::vm::TaskStatus::Completed,
            "UI task did not complete after its menu selection");
    require(scheduler.tick() &&
                scheduler.task(supervisor)->status == ql32::vm::TaskStatus::WaitingInput,
            "wait-mode caller did not resume after UI task completion");
    require(output.str() == "Grace\n90\nCOMPLETED\n",
            "wait-mode caller did not observe the UI task's output and completion");
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
                "x=7\nREPeat forever\ny=y+1\nEND REPeat forever\n"),
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
    testOptionalPagerSuspendsOnlyReplAndContinuesTail();
    testInteractiveSessionUsesSlicesAndKeepsReplState();
    testInteractiveStartupProgramSharesReplSession();
    testInteractiveSessionRecoversFromCommandErrors();
    testSchedulerOffersNonAllocatingTaskStatusAndErrorViews();
    testInteractiveUiWaitResumesThroughScheduler();
    testInteractiveRunUnwindsReplAndContinuesTrailingCommands();
    testBreakDuringDeferredRunDiscardsTrailingCommands();
    testSmoothWeightedRoundRobinMatchesReferenceSequence();
    testZeroPriorityTaskIsNeverSelected();
    testWeightedPolicyResetsForRemovalAndPriorityChanges();
    testPriorityWeightsChangeFrequency();
    testSchedulerReleasesTaskBookkeepingAfterWorkerBursts();
    testSupervisorCanSetSliceAndWorkerPriority();
    testIdleWorkerRetainsItsWindowWhenAnotherWorkerStarts();
    testCancellingWorkerDiscardsItsGraphicsAllocations();
    testSupervisorTaskViewportUsesTaskToSessionMapping();
    testBasicTaskViewCommandIsSupervisorOnly();
    testSupervisorCanToggleDebugBar();
    testTasksUsesDefaultAndExplicitPrintChannels();
    testKillConfirmationUsesConsoleChannel();
    testKillRemovesCompletedAndFailedTaskRecords();
    testTaskSupportsCommandAndFunctionSyntax();
    testCompletedTaskCanBeReapedExplicitly();
    testTaskWaitResumesCallerAndExposesChildStatus();
    testWaitTaskResumesAfterDialogAndMenu();
    testTaskWaitReturnsChildErrorContext();
    testTaskAndVmPausePreserveRunnableSessions();
    testBreakPreservesInteractiveSessionAndStopAllReturnsSupervisor();
    testRebootInteractiveCommandCallsSystemService();
    return 0;
}
