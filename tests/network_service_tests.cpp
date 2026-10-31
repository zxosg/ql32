#include "ql32/interpreter.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

class FakeNetwork final : public ql32::runtime::NetworkService {
public:
    void connectWifi(const std::string& ssid, const std::string& password) override {
        requestedSsid = ssid;
        requestedPassword = password;
        status.state = ql32::runtime::WifiState::Connecting;
        ++connectCalls;
    }

    void disconnectWifi() override {
        status = {};
        ++disconnectCalls;
    }

    ql32::runtime::WifiStatus wifiStatus() const override { return status; }

    std::string requestedSsid;
    std::string requestedPassword;
    ql32::runtime::WifiStatus status;
    int connectCalls = 0;
    int disconnectCalls = 0;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testBasicNetworkCalls() {
    auto network = std::make_shared<FakeNetwork>();
    ql32::Interpreter interpreter({}, {}, {}, {}, network);
    std::ostringstream output;
    interpreter.executeInteractive(
        "GETNET \"demo-net\",\"demo-password\"\nPRINT NETSTAT$()\n", output);
    require(network->connectCalls == 1 && network->requestedSsid == "demo-net" &&
                network->requestedPassword == "demo-password",
            "BASIC GETNET did not start an asynchronous station connection");
    require(output.str() == "CONNECTING\n",
            "NETSTAT$ did not report the pending association");

    network->status = {ql32::runtime::WifiState::Connected, "192.0.2.4", -47};
    output.str("");
    output.clear();
    interpreter.executeInteractive(
        "PRINT NETSTAT$();\" \";NETADDR$();\" \";NETSIGNAL()\n", output);
    require(output.str() == "CONNECTED 192.0.2.4 -47\n",
            "BASIC network status calls returned incorrect connection details");

    interpreter.executeInteractive("DISNET\nPRINT NETSTAT$()\n", output);
    require(network->disconnectCalls == 1 &&
                output.str().find("DISCONNECTED\n") != std::string::npos,
            "BASIC DISNET did not stop the connection");
}

void testUnavailableNetworkService() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    bool failed = false;
    try {
        interpreter.executeInteractive("PRINT NETSTAT$()\n", output);
    } catch (const ql32::runtime::ServiceError& error) {
        failed = error.code() == ql32::runtime::ServiceErrorCode::Unavailable;
    }
    require(failed, "missing network service did not report Unavailable");
}

} // namespace

int main() {
    try {
        testBasicNetworkCalls();
        testUnavailableNetworkService();
    } catch (const std::exception& error) {
        std::cerr << "network service test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
