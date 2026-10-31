#pragma once

#include <string>

namespace ql32::runtime {

enum class WifiState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

struct WifiStatus {
    WifiState state = WifiState::Disconnected;
    std::string ipv4Address;
    int signalDbm = 0;
};

// A connection request starts asynchronously. Callers poll status() rather
// than blocking the VM or firmware loop while association is in progress.
class NetworkService {
public:
    virtual ~NetworkService() = default;
    virtual void connectWifi(const std::string& ssid,
                             const std::string& password) = 0;
    virtual void disconnectWifi() = 0;
    virtual WifiStatus wifiStatus() const = 0;
};

} // namespace ql32::runtime
