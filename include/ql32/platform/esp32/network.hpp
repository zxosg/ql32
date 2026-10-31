#pragma once

#include "ql32/runtime/network.hpp"

namespace ql32::platform::esp32 {

class WifiNetworkService final : public runtime::NetworkService {
public:
    void connectWifi(const std::string& ssid,
                     const std::string& password) override;
    void disconnectWifi() override;
    runtime::WifiStatus wifiStatus() const override;

private:
    bool connectionRequested_ = false;
};

} // namespace ql32::platform::esp32
