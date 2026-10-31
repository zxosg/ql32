#include "ql32/platform/esp32/network.hpp"

#include <WiFi.h>

#include <iostream>
#include <stdexcept>

FakeWiFiClass WiFi;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testConnectionLifecycle() {
    ql32::platform::esp32::WifiNetworkService network;
    require(network.wifiStatus().state == ql32::runtime::WifiState::Disconnected,
            "Wi-Fi should begin disconnected");

    network.connectWifi("test-ap", "test-key");
    require(WiFi.currentMode == WIFI_STA && WiFi.beginCalls == 1,
            "connect did not initiate station mode asynchronously");
    require(WiFi.requestedSsid == "test-ap" && WiFi.requestedPassword == "test-key",
            "connect did not pass the requested credentials");
    require(network.wifiStatus().state == ql32::runtime::WifiState::Connecting,
            "pending association should report Connecting");

    WiFi.currentStatus = WL_CONNECTED;
    const auto connected = network.wifiStatus();
    require(connected.state == ql32::runtime::WifiState::Connected,
            "associated station should report Connected");
    require(connected.ipv4Address == "192.168.1.25" && connected.signalDbm == -55,
            "connected status omitted IP address or signal strength");

    WiFi.currentStatus = WL_NO_SSID_AVAIL;
    require(network.wifiStatus().state == ql32::runtime::WifiState::Failed,
            "unavailable access point should report Failed");

    network.disconnectWifi();
    require(WiFi.disconnectCalls == 1 && WiFi.erasedCredentials && !WiFi.turnedOff,
            "disconnect should erase driver credentials without powering down Wi-Fi");
    require(network.wifiStatus().state == ql32::runtime::WifiState::Disconnected,
            "explicit disconnect should report Disconnected");
}

} // namespace

int main() {
    try {
        testConnectionLifecycle();
    } catch (const std::exception& error) {
        std::cerr << "ESP32 network adapter test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
