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

} // namespace

int main() {
    testRoundRobinSlicesKeepSessionsIndependent();
    testUiWaitDoesNotBlockOtherSessions();
    return 0;
}
