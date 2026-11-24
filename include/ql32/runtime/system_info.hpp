#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ql32::runtime {

enum class SystemInfoSelector : std::uint32_t {
    Version = 0,
    Build = 1,
    Platform = 2,
    Display = 3,
    Input = 4,
    Storage = 5,
    Features = 6
};

struct SystemInfoProfile {
    std::array<std::string, 7> values;
    std::vector<std::pair<std::string, bool>> capabilities;
};

class SystemInfoService {
public:
    virtual ~SystemInfoService() = default;
    virtual std::string value(SystemInfoSelector selector) const = 0;
    // nullopt means the query name is unknown; otherwise true/false describe
    // support compiled into the selected profile, not current device state.
    virtual std::optional<bool> capability(std::string_view query) const = 0;
};

class StaticSystemInfoService final : public SystemInfoService {
public:
    explicit StaticSystemInfoService(SystemInfoProfile profile)
        : profile_(std::move(profile)) {}

    std::string value(SystemInfoSelector selector) const override {
        const auto index = static_cast<std::size_t>(selector);
        if (index >= profile_.values.size()) return {};
        return profile_.values[index];
    }

    std::optional<bool> capability(std::string_view query) const override {
        const auto normalized = normalize(query);
        for (const auto& entry : profile_.capabilities)
            if (normalize(entry.first) == normalized) return entry.second;
        return std::nullopt;
    }

private:
    static std::string normalize(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        bool pendingSpace = false;
        for (unsigned char character : value) {
            if (character == ' ' || character == '\t' || character == '\r' ||
                character == '\n') {
                pendingSpace = !result.empty();
                continue;
            }
            if (pendingSpace) result.push_back(' ');
            pendingSpace = false;
            result.push_back(character >= 'A' && character <= 'Z' ?
                static_cast<char>(character - 'A' + 'a') : static_cast<char>(character));
        }
        return result;
    }

    SystemInfoProfile profile_;
};

inline std::shared_ptr<SystemInfoService> makeHostSystemInfoService() {
    SystemInfoProfile profile;
    profile.values = {"0.1.0", "unknown", "host", "host", "terminal",
                      "hostfs", ""};
    profile.capabilities = {{"display", true}, {"input", true}, {"storage", true},
                            {"network", false}, {"wifi", false}, {"ps2", false},
                            {"serial", true}, {"sd", false}, {"ffs", false},
                            {"psram", false}, {"spi", false}, {"i2c", false},
                            {"gpio", false}, {"adc", false}, {"timer", false},
                            {"touch", false}, {"buttons", false}, {"usb-hid", false}};
    return std::make_shared<StaticSystemInfoService>(std::move(profile));
}

} // namespace ql32::runtime
