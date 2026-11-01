#include "ql32/vm/scheduler.hpp"
#include "ql32/ui/service.hpp"

#include <sstream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

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

} // namespace

int main() {
    testRoundRobinSlicesKeepSessionsIndependent();
    testUiWaitDoesNotBlockOtherSessions();
    testInteractiveSessionUsesSlicesAndKeepsReplState();
    testInteractiveStartupProgramSharesReplSession();
    testInteractiveSessionRecoversFromCommandErrors();
    testInteractiveUiWaitResumesThroughScheduler();
    return 0;
}
