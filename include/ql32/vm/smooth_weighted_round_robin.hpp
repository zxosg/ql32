#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ql32::vm {

// Selects one positive-weight task per tick using smooth weighted
// round-robin. Input order defines deterministic tie-breaking. New task IDs
// start at zero credit; removing a task or changing a priority resets credits.
class SmoothWeightedRoundRobin {
public:
    struct Task {
        std::uint64_t id = 0;
        std::uint32_t priority = 0;
    };

    std::optional<std::size_t> select(const std::vector<Task>& tasks) {
        if (++generation_ == 0) {
            for (auto& [id, state] : states_) {
                (void)id;
                state.seenGeneration = 0;
            }
            generation_ = 1;
        }

        bool priorityChanged = false;
        for (const auto& task : tasks) {
            auto [it, inserted] = states_.try_emplace(
                task.id, State{task.priority, 0, generation_});
            if (!inserted && it->second.priority != task.priority)
                priorityChanged = true;
            it->second.priority = task.priority;
            it->second.seenGeneration = generation_;
        }

        bool removed = false;
        for (auto it = states_.begin(); it != states_.end();) {
            if (it->second.seenGeneration != generation_) {
                removed = true;
                it = states_.erase(it);
            } else {
                ++it;
            }
        }

        if (removed || priorityChanged) resetCredits();

        std::int64_t total = 0;
        std::optional<std::size_t> best;
        for (std::size_t index = 0; index < tasks.size(); ++index) {
            const auto& task = tasks[index];
            if (task.priority == 0) continue;
            auto& state = states_.at(task.id);
            state.credit += static_cast<std::int64_t>(task.priority);
            total += static_cast<std::int64_t>(task.priority);
            if (!best || state.credit > states_.at(tasks[*best].id).credit)
                best = index;
        }

        if (!best || total == 0) return std::nullopt;
        states_.at(tasks[*best].id).credit -= total;
        return best;
    }

    void remove(std::uint64_t id) noexcept {
        if (states_.erase(id) != 0) resetCredits();
    }

    void resetCredits() noexcept {
        for (auto& [id, state] : states_) {
            (void)id;
            state.credit = 0;
        }
    }

private:
    struct State {
        std::uint32_t priority = 0;
        std::int64_t credit = 0;
        std::uint64_t seenGeneration = 0;
    };

    std::uint64_t generation_ = 0;
    std::unordered_map<std::uint64_t, State> states_;
};

} // namespace ql32::vm
