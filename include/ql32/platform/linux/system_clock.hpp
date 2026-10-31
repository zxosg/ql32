#pragma once

#include "ql32/runtime/clock.hpp"

namespace ql32::platform::linux_host {

class SystemClock final : public runtime::ClockService {
public:
    std::optional<std::int64_t> unixTimeSeconds() const override;
    std::uint64_t monotonicMilliseconds() const override;
};

} // namespace ql32::platform::linux_host
