#pragma once

#include <cstdint>
#include <optional>

namespace ql32::runtime {

// Wall-clock time may be unavailable until a board clock is configured or
// synchronized. Monotonic time is always measured from an unspecified origin.
class ClockService {
public:
    virtual ~ClockService() = default;
    virtual std::optional<std::int64_t> unixTimeSeconds() const = 0;
    virtual std::uint64_t monotonicMilliseconds() const = 0;
};

} // namespace ql32::runtime
