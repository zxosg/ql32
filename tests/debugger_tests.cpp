#include "ql32/vm/scheduler.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testBreakStepEvalAndContinue() {
    ql32::vm::SessionScheduler scheduler({}, 1024);
    std::ostringstream output;
    const auto task = scheduler.launch(
        "5 value=4\n"
        "10 value=1: value=value+1\n"
        "20 IF value=2 THEN\n"
        "30 PRINT value\n"
        "40 END IF\n"
        "50 value=value+0\n", output);
    require(scheduler.debugAttach(task), "debugger failed to attach");
    require(scheduler.debugSetBreakpoint(task, 10), "breakpoint was not accepted before compile");
    require(scheduler.debugSetDiagnostics(task, true, true), "diagnostics could not be enabled");
    require(scheduler.tick(), "scheduler did not execute debug task");
    auto info = scheduler.task(task);
    require(info && info->status == ql32::vm::TaskStatus::DebugPaused,
            "BASIC line breakpoint did not pause the task");
    auto snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->paused && snapshot->location.basicLine == 10,
            "debug snapshot did not report the BASIC line");
    auto evaluated = scheduler.debugEvaluate(task, "value + 3 * 2");
    require(evaluated && std::get<double>(*evaluated) == 10,
            "read-only EVAL returned the wrong value");
    require(scheduler.debugAddWatch(task, "value + 1"),
            "safe watch expression was rejected");
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->watches.size() == 1 && snapshot->watches.front().value &&
                std::get<double>(*snapshot->watches.front().value) == 5,
            "watch expression did not evaluate in the stopped context");
    bool unsafeRejected = false;
    try { (void)scheduler.debugEvaluate(task, "ABS(value)"); }
    catch (const std::exception&) { unsafeRejected = true; }
    require(unsafeRejected, "debug EVAL allowed a function call");
    unsafeRejected = false;
    try { (void)scheduler.debugEvaluate(task, "value=9: value=10"); }
    catch (const std::exception&) { unsafeRejected = true; }
    require(unsafeRejected && std::get<double>(scheduler.debugInspect(task)->globals.at("VALUE")) == 4,
            "debug EVAL accepted a write or changed a variable");

    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepInto),
            "STEP failed to resume a paused task");
    require(scheduler.tick(), "scheduler did not run one statement step");
    info = scheduler.task(task);
    snapshot = scheduler.debugInspect(task);
    require(info && info->status == ql32::vm::TaskStatus::DebugPaused && snapshot &&
                snapshot->location.basicLine == 10,
            "STEP did not stop at the next colon-separated statement");
    require(std::get<double>(snapshot->globals.at("VALUE")) == 1,
            "STEP executed more than one statement on a shared line");

    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepInto),
            "second STEP failed");
    require(scheduler.tick(), "scheduler did not execute second step");
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->location.basicLine == 20 &&
                std::get<double>(snapshot->globals.at("VALUE")) == 2,
            (std::string("second STEP did not advance: line=") +
             (snapshot ? std::to_string(snapshot->location.basicLine) : "missing") +
             ", value=" + (snapshot && snapshot->globals.count("VALUE") ?
                 std::to_string(std::get<double>(snapshot->globals.at("VALUE"))) : "missing")).c_str());
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepInto),
            "IF condition STEP failed");
    require(scheduler.tick(), "scheduler did not step through IF condition");
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->location.basicLine == 30,
            "STEP did not pause at the selected IF branch");
    require(snapshot->bytecodes > 0 && snapshot->ifTrue > 0 && !snapshot->trace.empty(),
            "execution diagnostics were not recorded");
    scheduler.debugResetDiagnostics(task);
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->bytecodes == 0 && snapshot->userCalls == 0 &&
                snapshot->ifTrue == 0 && snapshot->ifFalse == 0 && snapshot->trace.empty(),
            "diagnostics reset did not clear counters and trace");
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepInto),
            "STEP failed after diagnostics reset");
    require(scheduler.tick(), "scheduler did not step after diagnostics reset");
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->bytecodes > 0 && !snapshot->trace.empty(),
            "diagnostics reset disabled future counter collection");

    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::Continue),
            "CONTINUE failed");
    for (int i = 0; i < 5 && scheduler.task(task)->status != ql32::vm::TaskStatus::Completed; ++i)
        require(scheduler.tick(), "scheduler stopped before debug task completed");
    require(scheduler.task(task)->status == ql32::vm::TaskStatus::Completed &&
                output.str() == "2\n", "continued program produced the wrong result");
}

void testSchedulerRunsOtherTasksWhileDebugPaused() {
    ql32::vm::SessionScheduler scheduler({}, 256);
    std::ostringstream debugOutput, workerOutput;
    const auto debugTask = scheduler.launch("10 PRINT \"break\"\n", debugOutput);
    const auto worker = scheduler.launch("FOR i=1 TO 20: PRINT i: NEXT i\n", workerOutput);
    require(scheduler.debugAttach(debugTask), "debugger attach failed");
    require(scheduler.debugSetBreakpoint(debugTask, 10), "breakpoint setup failed");
    require(scheduler.tick(), "first scheduler tick failed");
    require(scheduler.task(debugTask)->status == ql32::vm::TaskStatus::DebugPaused,
            "debug task did not pause");
    const std::string before = workerOutput.str();
    require(scheduler.tick(), "scheduler failed to run another task");
    require(workerOutput.str().size() > before.size(),
            "another task did not run while debugger target was paused");
    require(scheduler.task(debugTask)->status == ql32::vm::TaskStatus::DebugPaused,
            "debug-paused task was scheduled again");
    require(scheduler.debugResume(debugTask, ql32::runtime::DebugResumeMode::Continue),
            "debug-paused task could not be resumed");
    (void)worker;
}

void testStepIntoNextAndArrayReads() {
    ql32::vm::SessionScheduler scheduler({}, 1024);
    std::ostringstream output;
    const std::string procedureSource =
        "10 Work\n"
        "20 DIM items(2)\n"
        "30 items(0)=7\n"
        "40 PRINT items(0)\n"
        "100 DEFINE PROCEDURE Work\n"
        "110 LOCAL localValue\n"
        "120 localValue=5\n"
        "130 PRINT localValue\n"
        "140 END DEFINE\n";
    const auto task = scheduler.launch(procedureSource, output);
    require(scheduler.debugAttach(task), "debugger attach failed");
    require(scheduler.debugSetBreakpoint(task, 10), "procedure breakpoint setup failed");
    require(scheduler.tick(), "scheduler did not enter procedure call");
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepInto),
            "STEP into procedure setup failed");
    require(scheduler.tick(), "scheduler did not step into procedure");
    auto snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->location.basicLine == 120 && snapshot->frames.size() == 1,
            (std::string("STEP target mismatch: line=") +
             (snapshot ? std::to_string(snapshot->location.basicLine) : "missing") +
             ", frames=" + (snapshot ? std::to_string(snapshot->frames.size()) : "missing")).c_str());
    require(snapshot->frames.back().name == "WORK", "call frame name is missing");
    require(snapshot->frames.back().returnLocation.basicLine == 10,
            "call frame did not report its caller source position");
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::StepOver),
            "NEXT within procedure setup failed");
    require(scheduler.tick(), "scheduler did not execute NEXT within procedure");
    snapshot = scheduler.debugInspect(task);
    require(snapshot && snapshot->location.basicLine == 130 && snapshot->frames.size() == 1,
            "NEXT did not advance one statement at the current call depth");
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::Continue),
            "CONTINUE setup failed");
    for (int i = 0; i < 5 && scheduler.task(task)->status != ql32::vm::TaskStatus::Completed; ++i)
        require(scheduler.tick(), "scheduler stopped before program completion");
    require(output.str() == "5\n7\n", "procedure continuation changed program output");

    ql32::vm::SessionScheduler nextScheduler({}, 1024);
    std::ostringstream nextOutput;
    const auto nextTask = nextScheduler.launch(procedureSource, nextOutput);
    require(nextScheduler.debugAttach(nextTask), "NEXT debugger attach failed");
    require(nextScheduler.debugSetBreakpoint(nextTask, 10), "NEXT breakpoint setup failed");
    require(nextScheduler.tick(), "NEXT program did not hit its breakpoint");
    require(nextScheduler.debugResume(nextTask, ql32::runtime::DebugResumeMode::StepOver),
            "NEXT over procedure setup failed");
    require(nextScheduler.tick(), "NEXT over procedure did not run");
    snapshot = nextScheduler.debugInspect(nextTask);
    require(snapshot && snapshot->location.basicLine == 20 && snapshot->frames.empty(),
            "NEXT did not run through the procedure call");

    ql32::vm::SessionScheduler arrayScheduler({}, 1024);
    std::ostringstream arrayOutput;
    const auto arrayTask = arrayScheduler.launch(
        "DIM items(2)\n"
        "items(0)=7\n"
        "PRINT items(0)\n", arrayOutput);
    require(arrayScheduler.debugAttach(arrayTask), "array debugger attach failed");
    require(arrayScheduler.debugSetBreakpoint(arrayTask, 3), "physical source breakpoint failed");
    require(arrayScheduler.tick(), "array program did not reach its breakpoint");
    const auto arraySnapshot = arrayScheduler.debugInspect(arrayTask);
    require(arraySnapshot && arraySnapshot->arrays.size() == 1 &&
                arraySnapshot->arrays.front().first == "ITEMS" &&
                arraySnapshot->arrays.front().second == std::vector<std::size_t>{3},
            "global array dimensions were not included in inspection");
    const auto arrayValue = arrayScheduler.debugEvaluate(arrayTask, "items(0)+1");
    require(arrayValue && std::get<double>(*arrayValue) == 8,
            "read-only EVAL did not support array reads");
    const auto breakpoints = arrayScheduler.debugBreakpoints(arrayTask);
    require(breakpoints.size() == 1 && breakpoints.front().requestedLine == 3 &&
                breakpoints.front().location && breakpoints.front().location->sourceLine == 3,
            "breakpoint listing did not include the resolved source position");
}

void testSupervisorDebuggerCommands() {
    ql32::vm::SessionScheduler scheduler({}, 512);
    std::ostringstream supervisorOutput, targetOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    const auto target = scheduler.launch("10 PRINT 42\n", targetOutput);
    require(scheduler.setTaskPriority(target, 0), "could not hold target before debugger attach");
    require(scheduler.submit(supervisor, "DEBUG " + std::to_string(target) + "\n"),
            "supervisor rejected DEBUG command");
    require(scheduler.tick(), "DEBUG command did not execute");
    require(scheduler.submit(supervisor, "BREAK 10\n"),
            "supervisor rejected BREAK command");
    require(scheduler.tick(), "BREAK command did not execute");
    require(scheduler.setTaskPriority(target, 4), "could not enable debug target");
    for (int i = 0; i < 4 && scheduler.task(target)->status != ql32::vm::TaskStatus::DebugPaused; ++i)
        require(scheduler.tick(), "scheduler stopped before supervisor breakpoint");
    require(scheduler.task(target)->status == ql32::vm::TaskStatus::DebugPaused,
            "supervisor BREAK command did not pause the target");
    require(scheduler.submit(supervisor, "WHERE\n"), "supervisor rejected WHERE command");
    require(scheduler.tick(), "WHERE command did not execute");
    require(supervisorOutput.str().find("BASIC line 10") != std::string::npos,
            "supervisor WHERE command did not display the stop location");
    require(scheduler.submit(supervisor, "BREAKPOINTS\n"),
            "supervisor rejected BREAKPOINTS command");
    require(scheduler.tick(), "BREAKPOINTS command did not execute");
    require(supervisorOutput.str().find("enabled line 10") != std::string::npos,
            "supervisor did not list the active breakpoint");
    require(scheduler.submit(supervisor, "DEBUG CONTINUE\n"),
            "supervisor rejected DEBUG CONTINUE");
    require(scheduler.tick(), "DEBUG CONTINUE did not execute");
    for (int i = 0; i < 4 && scheduler.task(target)->status !=
            ql32::vm::TaskStatus::Completed; ++i)
        require(scheduler.tick(), "scheduler stopped before debug target completed");
    require(scheduler.task(target)->status == ql32::vm::TaskStatus::Completed &&
                targetOutput.str() == "42\n",
            "DEBUG CONTINUE did not resume the attached debugger target");
}

void testDebuggerSupervisorDiagnosticsResetCommand() {
    ql32::vm::SessionScheduler scheduler({}, 512);
    std::ostringstream supervisorOutput, targetOutput;
    const auto supervisor = scheduler.launchInteractive(supervisorOutput);
    const auto target = scheduler.launch("10 value=41\n20 PRINT value\n", targetOutput);
    require(scheduler.setTaskPriority(target, 0), "could not hold target before reset test");
    require(scheduler.submit(supervisor, "DEBUG " + std::to_string(target) + "\n"),
            "supervisor rejected DEBUG command");
    require(scheduler.tick(), "DEBUG command did not execute in reset test");
    require(scheduler.submit(supervisor, "BREAK 20\n"), "supervisor rejected BREAK");
    require(scheduler.tick(), "BREAK command did not execute in reset test");
    require(scheduler.submit(supervisor, "DIAGNOSTICS ON\n"),
            "supervisor rejected DIAGNOSTICS ON");
    require(scheduler.tick(), "DIAGNOSTICS ON did not execute");
    require(scheduler.setTaskPriority(target, 4), "could not run target in reset test");
    for (int i = 0; i < 4 && scheduler.task(target)->status != ql32::vm::TaskStatus::DebugPaused; ++i)
        require(scheduler.tick(), "target did not reach reset-test breakpoint");
    require(scheduler.debugInspect(target)->bytecodes > 0,
            "reset test did not accumulate diagnostic counters");
    require(scheduler.submit(supervisor, "DIAGNOSTICS RESET\n"),
            "supervisor rejected DIAGNOSTICS RESET");
    require(scheduler.tick(), "DIAGNOSTICS RESET did not execute");
    const auto snapshot = scheduler.debugInspect(target);
    require(snapshot && snapshot->bytecodes == 0 && snapshot->userCalls == 0,
            "supervisor diagnostics reset did not clear counters");
    require(supervisorOutput.str().find("Diagnostics reset") != std::string::npos,
            "supervisor did not acknowledge diagnostics reset");
}

void testPendingUiRequestSurvivesDebugPause() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::vm::Services services;
    services.ui = ui;
    ql32::vm::SessionScheduler scheduler(std::move(services), 512);
    std::ostringstream output;
    const auto task = scheduler.launch("ALERT \"wait\",\"acknowledge\"\nPRINT \"resumed\"\n", output);
    require(scheduler.tick() && scheduler.task(task)->status == ql32::vm::TaskStatus::WaitingUi,
            "program did not suspend for its UI request");
    const auto request = ui->takeNextRequest();
    require(request.has_value(), "UI request was not queued");
    require(scheduler.debugAttach(task) &&
                scheduler.task(task)->status == ql32::vm::TaskStatus::DebugPaused,
            "attaching to a UI-waiting task did not preserve a debug pause");
    require(ui->resolve(request->id, ql32::ui::ResultKind::Acknowledged),
            "test could not resolve the pending UI request");
    require(scheduler.debugResume(task, ql32::runtime::DebugResumeMode::Continue),
            "UI-waiting debug task did not resume");
    require(scheduler.tick(), "scheduler did not resume the UI task");
    require(scheduler.task(task)->status == ql32::vm::TaskStatus::Completed &&
                output.str() == "resumed\n",
            "pending UI request was lost across the debug pause");
}

void testRepeatedBreakpointsAndUnreachedIfConditions() {
    ql32::vm::SessionScheduler loopScheduler({}, 1024);
    std::ostringstream loopOutput;
    const auto loopTask = loopScheduler.launch(
        "10 value=0\n"
        "20 value=value+1\n"
        "30 GOTO 20\n", loopOutput);
    require(loopScheduler.debugAttach(loopTask), "loop debugger attach failed");
    require(loopScheduler.debugSetBreakpoint(loopTask, 20), "loop breakpoint setup failed");
    require(loopScheduler.tick(), "loop did not reach its first breakpoint");
    require(loopScheduler.debugResume(loopTask, ql32::runtime::DebugResumeMode::Continue),
            "loop continue failed");
    require(loopScheduler.tick(), "loop did not run back to its breakpoint");
    auto snapshot = loopScheduler.debugInspect(loopTask);
    require(snapshot && snapshot->paused && snapshot->location.basicLine == 20 &&
                std::get<double>(snapshot->globals.at("VALUE")) == 1,
            "loop breakpoint did not re-trigger after a back edge");

    ql32::vm::SessionScheduler ifScheduler({}, 1024);
    std::ostringstream ifOutput;
    const auto ifTask = ifScheduler.launch(
        "10 IF 0 THEN\n"
        "20 IF 1 THEN\n"
        "30 value=1\n"
        "40 END IF\n"
        "50 END IF\n"
        "60 PRINT 0\n", ifOutput);
    require(ifScheduler.debugAttach(ifTask), "IF debugger attach failed");
    require(ifScheduler.debugSetBreakpoint(ifTask, 60), "IF breakpoint setup failed");
    require(ifScheduler.debugSetDiagnostics(ifTask, true), "IF diagnostics setup failed");
    require(ifScheduler.tick(), "nested IF program did not run");
    snapshot = ifScheduler.debugInspect(ifTask);
    require(snapshot && snapshot->location.basicLine == 60 && snapshot->ifFalse == 1 &&
                snapshot->ifTrue == 0,
            "IF counters counted a nested condition that was not executed");
}

} // namespace

int main() {
    try {
        testBreakStepEvalAndContinue();
        testSchedulerRunsOtherTasksWhileDebugPaused();
        testStepIntoNextAndArrayReads();
        testSupervisorDebuggerCommands();
        testDebuggerSupervisorDiagnosticsResetCommand();
        testPendingUiRequestSurvivesDebugPause();
        testRepeatedBreakpointsAndUnreachedIfConditions();
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("debugger test failed: ") + error.what());
    }
}
