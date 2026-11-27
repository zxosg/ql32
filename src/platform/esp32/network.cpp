#include "ql32/platform/esp32/network.hpp"

#include <WiFi.h>
#if defined(ARDUINO)
#include <esp_sntp.h>
#endif
#include <time.h>

namespace ql32::platform::esp32 {

void WifiNetworkService::connectWifi(const std::string& ssid,
                                     const std::string& password) {
    connectionRequested_ = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
}

void WifiNetworkService::disconnectWifi() {
    connectionRequested_ = false;
#if defined(ARDUINO)
    if (sntpStarted_) esp_sntp_stop();
#endif
    sntpStarted_ = false;
    // Power the radio down so the driver's heap is returned to BASIC programs.
    WiFi.disconnect(true, true);
}

std::string WifiNetworkService::networkTime() {
    if (!sntpStarted_) {
        if (!connectionRequested_ || WiFi.status() != WL_CONNECTED) return {};
#if defined(ARDUINO)
        configTime(0, 0, "pool.ntp.org", "time.google.com");
#endif
        sntpStarted_ = true;
        return {};
    }
#if defined(ARDUINO)
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) return {};
#else
    return {};
#endif
    time_t now = time(nullptr);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char buf[24];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
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
