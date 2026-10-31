#pragma once

#include <string>
#include <utility>

enum WiFiMode { WIFI_OFF, WIFI_STA };
enum wl_status_t {
    WL_IDLE_STATUS,
    WL_NO_SSID_AVAIL,
    WL_SCAN_COMPLETED,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED,
    WL_NO_SHIELD
};

class IPAddress {
public:
    explicit IPAddress(std::string value = "0.0.0.0") : value_(std::move(value)) {}
    std::string toString() const { return value_; }
private:
    std::string value_;
};

class FakeWiFiClass {
public:
    void mode(WiFiMode value) { currentMode = value; }
    void begin(const char* network, const char* key) {
        requestedSsid = network;
        requestedPassword = key;
        ++beginCalls;
    }
    void disconnect(bool turnOff, bool eraseCredentials) {
        erasedCredentials = eraseCredentials;
        turnedOff = turnOff;
        ++disconnectCalls;
    }
    wl_status_t status() const { return currentStatus; }
    IPAddress localIP() const { return address; }
    int RSSI() const { return signal; }

    WiFiMode currentMode = WIFI_OFF;
    wl_status_t currentStatus = WL_DISCONNECTED;
    IPAddress address{"192.168.1.25"};
    int signal = -55;
    std::string requestedSsid;
    std::string requestedPassword;
    int beginCalls = 0;
    int disconnectCalls = 0;
    bool erasedCredentials = false;
    bool turnedOff = false;
};

extern FakeWiFiClass WiFi;
