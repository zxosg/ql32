#include "ql32/platform/esp32/remote_debug_protocol.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class MemoryStorage final : public ql32::runtime::StorageService {
public:
    std::string readText(const std::string& path) override {
        const auto found = files.find(path);
        if (found == files.end()) throw std::runtime_error("file not found");
        return found->second;
    }
    void writeText(const std::string& path, const std::string& contents) override {
        files[path] = contents;
    }
    std::vector<ql32::runtime::FileEntry> list(const std::string& path) override {
        if (path != "FFS:/") throw std::runtime_error("directory not found");
        return {{"demo.bas", files.at("FFS:/demo.bas").size(), false},
                {"examples", 0, true}};
    }
    bool exists(const std::string& path) override { return files.count(path) != 0; }
    std::uintmax_t size(const std::string& path) override { return files.at(path).size(); }
    void rename(const std::string&, const std::string&) override {}
    void remove(const std::string& path) override { files.erase(path); }

    std::map<std::string, std::string> files{
        {"FFS:/demo.bas", "10 x=2\n20 PRINT x\n"},
        {"FFS:/window.bas", "WINDOW 1,1,8,6\nPAPER 2\nCLS\n"}
    };
};

std::string encode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char byte : value) {
        if (byte == '%' || byte == '\t' || byte == '\r' || byte == '\n') {
            result.push_back('%');
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        } else result.push_back(static_cast<char>(byte));
    }
    return result;
}

std::string request(unsigned id, std::string_view operation,
                    std::initializer_list<std::string_view> args = {}) {
    std::string result = "@QL32DBG/1\t" + std::to_string(id) + "\t" + std::string(operation);
    for (const auto arg : args) result += "\t" + encode(arg);
    return result;
}

std::string invoke(const ql32::platform::esp32::RemoteDebugProtocol& protocol,
                   ql32::vm::SessionScheduler& scheduler, MemoryStorage& storage,
                   std::ostream& output, const std::string& line) {
    std::string response;
    require(protocol.handles(line), "protocol did not recognize request prefix");
    protocol.handle(line, scheduler, storage, output,
                    [&response](std::string_view bytes) { response.append(bytes); });
    return response;
}

void testCapabilitiesStorageAndTaskStart() {
    ql32::platform::esp32::RemoteDebugCapabilities capabilities;
    capabilities.board = "ESP32-2432S028";
    capabilities.storageRoots = {"FFS:/"};
    capabilities.screenCapture = true;
    ql32::platform::esp32::RemoteDebugProtocol protocol(std::move(capabilities));
    ql32::vm::SessionScheduler scheduler({}, 512);
    MemoryStorage storage;
    std::ostringstream output;

    const auto hello = invoke(protocol, scheduler, storage, output, request(1, "hello"));
    require(hello.find("\tOK\t{\"version\":1") != std::string::npos &&
                hello.find("ESP32-2432S028") != std::string::npos &&
                hello.find("FFS:/") != std::string::npos &&
                hello.find("\"screen_capture\":true") != std::string::npos,
            "capability handshake omitted board or service information");

    const auto listing = invoke(protocol, scheduler, storage, output,
                                request(2, "files.list", {"FFS:/"}));
    require(listing.find("demo.bas") != std::string::npos &&
                listing.find("\"directory\":true") != std::string::npos,
            "remote storage listing returned incorrect entries");

    const auto started = invoke(protocol, scheduler, storage, output,
                                request(3, "task.start", {"FFS:/demo.bas"}));
    require(started.find("\"task_id\":1") != std::string::npos &&
                started.find("\"paused\":true") != std::string::npos,
            "GUI task start did not return an attached paused task");
    require(scheduler.tick(), "new GUI task did not enter its initial debug pause");
    const auto tasks = invoke(protocol, scheduler, storage, output, request(4, "tasks.list"));
    require(tasks.find("DEBUG_PAUSED") != std::string::npos,
            "task list did not report debug-paused state");
}

void testDebuggerOperationsAndErrors() {
    ql32::platform::esp32::RemoteDebugCapabilities capabilities;
    capabilities.board = "Olimex ESP32-SBC-FabGL";
    capabilities.storageRoots = {"FFS:/"};
    capabilities.screenCapture = true;
    ql32::platform::esp32::RemoteDebugProtocol protocol(std::move(capabilities));
    ql32::vm::SessionScheduler scheduler({}, 512);
    MemoryStorage storage;
    std::ostringstream output;
    (void)invoke(protocol, scheduler, storage, output,
                 request(1, "task.start", {"FFS:/demo.bas"}));
    require(invoke(protocol, scheduler, storage, output,
                   request(2, "debug.breakpoint", {"1", "20", "true"})).find("\tOK\t") !=
                std::string::npos, "breakpoint setup failed through the protocol");
    require(scheduler.tick(), "new debug task did not execute to its breakpoint");
    const auto snapshot = invoke(protocol, scheduler, storage, output,
                                 request(3, "debug.inspect", {"1"}));
    require(snapshot.find("\"paused\":true") != std::string::npos &&
                snapshot.find("\"basic_line\":20") != std::string::npos &&
                snapshot.find("\"line\":20") != std::string::npos,
            "debug snapshot omitted location or breakpoint state");
    const auto evaluated = invoke(protocol, scheduler, storage, output,
                                  request(4, "debug.eval", {"1", "x + 3"}));
    require(evaluated.find("\"value\":5") != std::string::npos,
            "read-only expression did not round-trip through the protocol");
    const auto unsafe = invoke(protocol, scheduler, storage, output,
                               request(5, "debug.eval", {"1", "ABS(1)"}));
    require(unsafe.find("\tERR\t") != std::string::npos,
            "protocol exposed unsafe EVAL calls");
    const auto malformed = invoke(protocol, scheduler, storage, output,
                                  "@QL32DBG/1\tbad\thello");
    require(malformed.find("\tERR\t") != std::string::npos,
            "malformed request id was not rejected");
    const auto unknown = invoke(protocol, scheduler, storage, output,
                                request(6, "not.a.command"));
    require(unknown.find("unknown operation") != std::string::npos,
            "unknown protocol operation did not return a structured error");
}

void testRemoteTaskViewportControl() {
    ql32::platform::esp32::RemoteDebugCapabilities capabilities;
    capabilities.board = "ESP32-2432S028";
    capabilities.storageRoots = {"FFS:/"};
    ql32::platform::esp32::RemoteDebugProtocol protocol(std::move(capabilities));
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(
        32, 16, nullptr, 5, 0);
    ql32::vm::Services services;
    services.graphics = graphics;
    ql32::vm::SessionScheduler scheduler(std::move(services), 32);
    MemoryStorage storage;
    std::ostringstream output;

    const auto started = invoke(protocol, scheduler, storage, output,
        request(1, "task.start", {"FFS:/window.bas"}));
    require(started.find("\"task_id\":1") != std::string::npos,
            "viewport scenario task failed to start");
    require(scheduler.tick(), "viewport scenario did not enter its initial pause");
    require(invoke(protocol, scheduler, storage, output,
                    request(2, "debug.resume", {"1", "continue"})).find("\tOK\t") !=
                std::string::npos,
            "viewport scenario task did not resume");
    for (int i = 0; i < 20; ++i) {
        const auto info = scheduler.task(1);
        if (info && info->status == ql32::vm::TaskStatus::WaitingInput) break;
        require(scheduler.tick(), "viewport scenario task stopped before its prompt");
    }
    require(scheduler.task(1)->status == ql32::vm::TaskStatus::WaitingInput,
            "viewport scenario did not reach its interactive prompt");
    const auto before = graphics->windowManager().compositionOrder();
    require(before.size() == 1 && before.front().bounds.x == 1 &&
                before.front().bounds.y == 1,
            "viewport scenario did not create its initial window");

    const auto moved = invoke(protocol, scheduler, storage, output,
        request(3, "task.viewport", {"1", "10", "-2", "8", "6"}));
    require(moved.find("\"canvas_origin_x\":10") != std::string::npos &&
                moved.find("\"canvas_origin_y\":-2") != std::string::npos,
            "remote viewport update response omitted its geometry");
    const auto after = graphics->windowManager().compositionOrder();
    require(after.size() == 1 && after.front().bounds.x == 11 &&
                after.front().bounds.y == -1 && after.front().screenClip.y == 0,
            "remote viewport update did not reposition and clip the retained window");
    const auto taskList = invoke(protocol, scheduler, storage, output,
                                 request(4, "tasks.list"));
    require(taskList.find("\"canvas_origin_x\":10") != std::string::npos &&
                taskList.find("\"canvas_origin_y\":-2") != std::string::npos &&
                taskList.find("\"canvas_width\":8") != std::string::npos &&
                taskList.find("\"canvas_height\":6") != std::string::npos,
            "remote task listing did not report current viewport properties");

    const auto rejected = invoke(protocol, scheduler, storage, output,
        request(5, "task.viewport", {"1", "0", "0", "0", "6"}));
    require(rejected.find("\tERR\t") != std::string::npos &&
                scheduler.task(1)->viewport.originX == 10,
            "invalid viewport update was not rejected atomically");
}

void testLilygoCapabilityIsRejectedByClientHandshake() {
    ql32::platform::esp32::RemoteDebugCapabilities capabilities;
    capabilities.board = "LILYGO VGA32 V1.4";
    ql32::platform::esp32::RemoteDebugProtocol protocol(std::move(capabilities));
    ql32::vm::SessionScheduler scheduler;
    MemoryStorage storage;
    std::ostringstream output;
    const auto hello = invoke(protocol, scheduler, storage, output, request(1, "hello"));
    require(hello.find("\"supported\":false") != std::string::npos,
            "LILYGO was not advertised as unsupported by the PC app");
}

void testTenstarCapabilityIsSupportedByClientHandshake() {
    ql32::platform::esp32::RemoteDebugCapabilities capabilities;
    capabilities.board = "TENSTAR-ESP32-S3";
    ql32::platform::esp32::RemoteDebugProtocol protocol(std::move(capabilities));
    ql32::vm::SessionScheduler scheduler;
    MemoryStorage storage;
    std::ostringstream output;
    const auto hello = invoke(protocol, scheduler, storage, output, request(1, "hello"));
    require(hello.find("\"supported\":true") != std::string::npos,
            "Tenstar was not advertised as supported by the PC app");
}

} // namespace

int main() {
    testCapabilitiesStorageAndTaskStart();
    testDebuggerOperationsAndErrors();
    testRemoteTaskViewportControl();
    testLilygoCapabilityIsRejectedByClientHandshake();
    testTenstarCapabilityIsSupportedByClientHandshake();
}
