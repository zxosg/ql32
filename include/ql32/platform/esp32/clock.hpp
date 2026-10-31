#pragma once

#include "ql32/runtime/clock.hpp"

namespace ql32::platform::esp32 {

class Clock final : public runtime::ClockService {
public:
    using WallTimeSource = std::int64_t (*)();

    explicit Clock(WallTimeSource wallTimeSource = nullptr)
        : wallTimeSource_(wallTimeSource) {}

    std::optional<std::int64_t> unixTimeSeconds() const override;
    std::uint64_t monotonicMilliseconds() const override;

private:
    WallTimeSource wallTimeSource_;
};

} // namespace ql32::platform::esp32
