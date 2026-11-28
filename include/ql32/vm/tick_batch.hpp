#pragma once

#include <cstdint>

namespace ql32::vm {

// Runs scheduler slices back to back until the time budget is spent, nothing is
// runnable, or a task is waiting for the UI (the host must service it first).
// Returns true when at least one slice ran. A zero budget runs exactly one slice.
template <class Scheduler, class NowMicros>
bool tickBatch(Scheduler& scheduler, std::uint64_t budgetMicros, NowMicros&& nowMicros) {
    const auto start = nowMicros();
    bool ran = false;
    do {
        if (!scheduler.tick()) break;
        ran = true;
        if (scheduler.hasWaitingUiTask()) break;
    } while (static_cast<std::uint64_t>(nowMicros() - start) < budgetMicros);
    return ran;
}

} // namespace ql32::vm
