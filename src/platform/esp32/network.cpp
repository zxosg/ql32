#include "ql32/platform/esp32/network.hpp"

#include <WiFi.h>

namespace ql32::platform::esp32 {

void WifiNetworkService::connectWifi(const std::string& ssid,
                                     const std::string& password) {
    connectionRequested_ = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
}

void WifiNetworkService::disconnectWifi() {
    connectionRequested_ = false;
    WiFi.disconnect(false, true);
}

runtime::WifiStatus WifiNetworkService::wifiStatus() const {
    runtime::WifiStatus result;
    if (!connectionRequested_) return result;

    switch (WiFi.status()) {
    case WL_CONNECTED:
        result.state = runtime::WifiState::Connected;
        result.ipv4Address = WiFi.localIP().toString().c_str();
        result.signalDbm = WiFi.RSSI();
        break;
    case WL_CONNECT_FAILED:
    case WL_NO_SSID_AVAIL:
    case WL_CONNECTION_LOST:
        result.state = runtime::WifiState::Failed;
        break;
    case WL_NO_SHIELD:
        result.state = runtime::WifiState::Failed;
        break;
    case WL_IDLE_STATUS:
    case WL_DISCONNECTED:
    default:
        result.state = runtime::WifiState::Connecting;
        break;
    }
    return result;
}

} // namespace ql32::platform::esp32
