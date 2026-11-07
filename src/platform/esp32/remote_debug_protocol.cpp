#include "ql32/platform/esp32/remote_debug_protocol.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace ql32::platform::esp32 {
namespace {

constexpr std::string_view prefix = "@QL32DBG/1\t";

std::vector<std::string> fields(std::string_view input) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= input.size()) {
        const auto end = input.find('\t', begin);
        result.emplace_back(input.substr(begin,
            end == std::string_view::npos ? input.size() - begin : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

int hexValue(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

std::string decode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            result.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size()) throw std::runtime_error("bad percent escape");
        const int high = hexValue(value[i + 1]);
        const int low = hexValue(value[i + 2]);
        if (high < 0 || low < 0) throw std::runtime_error("bad percent escape");
        result.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return result;
}

std::string json(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                result += "\\u00";
                result.push_back(digits[character >> 4]);
                result.push_back(digits[character & 0x0f]);
            } else result.push_back(static_cast<char>(character));
        }
    }
    result.push_back('"');
    return result;
}

template <typename Number>
Number number(std::string_view value, const char* label) {
    Number result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error(std::string("invalid ") + label);
    return result;
}

bool boolean(std::string_view value) {
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    throw std::runtime_error("expected boolean");
}

const char* statusName(vm::TaskStatus status) {
    switch (status) {
    case vm::TaskStatus::Runnable: return "RUNNABLE";
    case vm::TaskStatus::WaitingUi: return "WAITING_UI";
    case vm::TaskStatus::WaitingInput: return "WAITING_INPUT";
    case vm::TaskStatus::WaitingChannel: return "WAITING_CHANNEL";
    case vm::TaskStatus::WaitingTask: return "WAITING_TASK";
    case vm::TaskStatus::WaitingTime: return "WAITING_TIME";
    case vm::TaskStatus::DebugPaused: return "DEBUG_PAUSED";
    case vm::TaskStatus::Completed: return "COMPLETED";
    case vm::TaskStatus::Failed: return "FAILED";
    case vm::TaskStatus::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

std::string valueJson(const runtime::DebugValue& value) {
    if (const auto* string = std::get_if<std::string>(&value)) return json(*string);
    std::ostringstream output;
    output << std::setprecision(17) << std::get<double>(value);
    return output.str();
}

void appendMap(std::string& output,
               const std::unordered_map<std::string, runtime::DebugValue>& values) {
    output.push_back('{');
    bool first = true;
    for (const auto& [name, value] : values) {
        if (!first) output.push_back(',');
        first = false;
        output += json(name);
        output.push_back(':');
        output += valueJson(value);
    }
    output.push_back('}');
}

void appendArrays(std::string& output,
    const std::vector<std::pair<std::string, std::vector<std::size_t>>>& arrays) {
    output.push_back('[');
    bool first = true;
    for (const auto& [name, bounds] : arrays) {
        if (!first) output.push_back(',');
        first = false;
        output += "{\"name\":" + json(name) + ",\"upper_bounds\":[";
        for (std::size_t i = 0; i < bounds.size(); ++i) {
            if (i) output.push_back(',');
            output += std::to_string(bounds[i] == 0 ? 0 : bounds[i] - 1);
        }
        output += "]}";
    }
    output.push_back(']');
}

std::string locationJson(const runtime::DebugLocation& location) {
    return "{\"instruction\":" + std::to_string(location.instruction) +
        ",\"source_line\":" + std::to_string(location.sourceLine) +
        ",\"column\":" + std::to_string(location.column) +
        ",\"basic_line\":" + std::to_string(location.basicLine) +
        ",\"procedure\":" + json(location.procedure) + "}";
}

std::string snapshotJson(const runtime::DebugSnapshot& snapshot) {
    std::string result = "{\"attached\":" + std::string(snapshot.attached ? "true" : "false") +
        ",\"paused\":" + (snapshot.paused ? "true" : "false") +
        ",\"location\":" + locationJson(snapshot.location) + ",\"globals\":";
    appendMap(result, snapshot.globals);
    result += ",\"arrays\":";
    appendArrays(result, snapshot.arrays);
    result += ",\"frames\":[";
    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        if (i) result.push_back(',');
        const auto& frame = snapshot.frames[i];
        result += "{\"name\":" + json(frame.name) + ",\"function\":" +
            std::string(frame.function ? "true" : "false") + ",\"return_location\":" +
            locationJson(frame.returnLocation) + ",\"locals\":";
        appendMap(result, frame.locals);
        result += ",\"arrays\":";
        appendArrays(result, frame.arrays);
        result.push_back('}');
    }
    result += "],\"breakpoints\":[";
    for (std::size_t i = 0; i < snapshot.breakpoints.size(); ++i) {
        if (i) result.push_back(',');
        const auto& breakpoint = snapshot.breakpoints[i];
        result += "{\"line\":" + std::to_string(breakpoint.requestedLine) +
            ",\"enabled\":" + std::string(breakpoint.enabled ? "true" : "false") +
            ",\"location\":" + (breakpoint.location ? locationJson(*breakpoint.location) : "null") + "}";
    }
    result += "],\"watches\":[";
    for (std::size_t i = 0; i < snapshot.watches.size(); ++i) {
        if (i) result.push_back(',');
        const auto& watch = snapshot.watches[i];
        result += "{\"expression\":" + json(watch.expression) + ",\"value\":" +
            (watch.value ? valueJson(*watch.value) : "null") + ",\"error\":" + json(watch.error) + "}";
    }
    result += "],\"stats\":{\"bytecodes\":" + std::to_string(snapshot.bytecodes) +
        ",\"user_calls\":" + std::to_string(snapshot.userCalls) +
        ",\"if_true\":" + std::to_string(snapshot.ifTrue) +
        ",\"if_false\":" + std::to_string(snapshot.ifFalse) +
        ",\"free_memory\":" +
        (snapshot.freeMemory ? std::to_string(*snapshot.freeMemory) : "null") + "},\"trace\":[";
    for (std::size_t i = 0; i < snapshot.trace.size(); ++i) {
        if (i) result.push_back(',');
        result += json(snapshot.trace[i]);
    }
    result += "]}";
    return result;
}

std::string success(std::string_view id, std::string payload) {
    return std::string(prefix) + std::string(id) + "\tOK\t" + payload + "\n";
}

std::string failure(std::string_view id, std::string_view message) {
    return std::string(prefix) + std::string(id) + "\tERR\t{\"error\":" +
        json(message) + "}\n";
}

} // namespace

bool RemoteDebugProtocol::handles(std::string_view line) const noexcept {
    return line.substr(0, prefix.size()) == prefix;
}

void RemoteDebugProtocol::handle(std::string_view line, vm::SessionScheduler& scheduler,
                                 runtime::StorageService& storage, std::ostream& taskOutput,
                                 const Write& write) const {
    std::string requestId = "0";
    try {
        if (!handles(line)) return;
        const auto request = fields(line.substr(prefix.size()));
        if (request.size() < 2) throw std::runtime_error("request needs id and operation");
        requestId = std::to_string(number<std::uint32_t>(request[0], "request id"));
        const std::string& operation = request[1];
        std::vector<std::string> args;
        for (std::size_t i = 2; i < request.size(); ++i) args.push_back(decode(request[i]));
        const auto requireArgs = [&](std::size_t count) {
            if (args.size() != count) throw std::runtime_error("wrong number of arguments");
        };
        std::string payload;
        if (operation == "hello") {
            requireArgs(0);
            const bool supportedBoard = capabilities_.board == "ESP32-2432S028" ||
                capabilities_.board == "Olimex ESP32-SBC-FabGL" ||
                capabilities_.board == "TENSTAR-ESP32-S3";
            payload = "{\"version\":1,\"board\":" + json(capabilities_.board) +
                ",\"supported\":" +
                std::string(supportedBoard ? "true" : "false") +
                ",\"screen_capture\":" +
                std::string(capabilities_.screenCapture ? "true" : "false") + ",\"storage_roots\":[";
            for (std::size_t i = 0; i < capabilities_.storageRoots.size(); ++i) {
                if (i) payload.push_back(',');
                payload += json(capabilities_.storageRoots[i]);
            }
            payload += "]}";
        } else if (operation == "tasks.list") {
            requireArgs(0);
            payload = "[";
            const auto tasks = scheduler.tasks();
            for (std::size_t i = 0; i < tasks.size(); ++i) {
                if (i) payload.push_back(',');
                const auto& task = tasks[i];
                payload += "{\"id\":" + std::to_string(task.id) +
                    ",\"status\":" + json(statusName(task.status)) +
                    ",\"supervisor\":" + (task.supervisor ? "true" : "false") +
                    ",\"interactive\":" + (task.interactive ? "true" : "false") +
                    ",\"priority\":" + std::to_string(task.priority) +
                    ",\"canvas_origin_x\":" + std::to_string(task.viewport.originX) +
                    ",\"canvas_origin_y\":" + std::to_string(task.viewport.originY) +
                    ",\"canvas_width\":" + std::to_string(task.viewport.canvasWidth) +
                    ",\"canvas_height\":" + std::to_string(task.viewport.canvasHeight) +
                    ",\"canvas_offset_x\":" + std::to_string(task.viewport.offsetX) +
                    ",\"canvas_offset_y\":" + std::to_string(task.viewport.offsetY) +
                    ",\"error\":" + json(task.error) + "}";
            }
            payload.push_back(']');
        } else if (operation == "files.list") {
            requireArgs(1);
            const auto entries = storage.list(args[0]);
            payload = "[";
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (i) payload.push_back(',');
                const auto& entry = entries[i];
                payload += "{\"name\":" + json(entry.name) +
                    ",\"size\":" + std::to_string(entry.size) +
                    ",\"directory\":" + (entry.isDirectory ? "true" : "false") + "}";
            }
            payload.push_back(']');
        } else if (operation == "task.start") {
            requireArgs(1);
            auto task = scheduler.launchWorker(storage.readText(args[0]), taskOutput);
            if (!scheduler.debugAttach(task)) {
                scheduler.kill(task);
                throw std::runtime_error("could not attach debugger to new task");
            }
            payload = "{\"task_id\":" + std::to_string(task) + ",\"paused\":true}";
        } else if (operation == "task.viewport") {
            if (args.size() != 5 && args.size() != 7)
                throw std::runtime_error("task.viewport expects five or seven arguments");
            const auto task = number<vm::TaskId>(args[0], "task id");
            const auto originX = number<std::int32_t>(args[1], "canvas origin x");
            const auto originY = number<std::int32_t>(args[2], "canvas origin y");
            const auto width = number<std::uint32_t>(args[3], "canvas width");
            const auto height = number<std::uint32_t>(args[4], "canvas height");
            const auto offsetX = args.size() == 7 ?
                number<std::int32_t>(args[5], "canvas offset x") : 0;
            const auto offsetY = args.size() == 7 ?
                number<std::int32_t>(args[6], "canvas offset y") : 0;
            if (!scheduler.setTaskViewport(task,
                    {originX, originY, width, height, offsetX, offsetY}))
                throw std::runtime_error("task viewport could not be updated");
            payload = "{\"task_id\":" + std::to_string(task) +
                ",\"canvas_origin_x\":" + std::to_string(originX) +
                ",\"canvas_origin_y\":" + std::to_string(originY) +
                ",\"canvas_width\":" + std::to_string(width) +
                ",\"canvas_height\":" + std::to_string(height) +
                ",\"canvas_offset_x\":" + std::to_string(offsetX) +
                ",\"canvas_offset_y\":" + std::to_string(offsetY) + "}";
        } else if (operation == "debug.attach") {
            requireArgs(1);
            const auto task = number<vm::TaskId>(args[0], "task id");
            if (!scheduler.debugAttach(task)) throw std::runtime_error("task cannot be attached");
            payload = "{\"task_id\":" + std::to_string(task) + ",\"paused\":true}";
        } else if (operation == "debug.breakpoint") {
            requireArgs(3);
            const auto task = number<vm::TaskId>(args[0], "task id");
            const auto line = number<int>(args[1], "source line");
            if (!scheduler.debugSetBreakpoint(task, line, boolean(args[2])))
                throw std::runtime_error("no executable statement at that source line");
            payload = "{\"line\":" + std::to_string(line) + "}";
        } else if (operation == "debug.resume") {
            requireArgs(2);
            const auto task = number<vm::TaskId>(args[0], "task id");
            runtime::DebugResumeMode mode;
            if (args[1] == "continue") mode = runtime::DebugResumeMode::Continue;
            else if (args[1] == "step") mode = runtime::DebugResumeMode::StepInto;
            else if (args[1] == "next") mode = runtime::DebugResumeMode::StepOver;
            else throw std::runtime_error("resume mode must be continue, step, or next");
            if (!scheduler.debugResume(task, mode)) throw std::runtime_error("task is not paused");
            payload = "{\"task_id\":" + std::to_string(task) + "}";
        } else if (operation == "debug.pause") {
            requireArgs(1);
            const auto task = number<vm::TaskId>(args[0], "task id");
            if (!scheduler.debugPause(task)) throw std::runtime_error("task cannot be paused");
            payload = "{\"task_id\":" + std::to_string(task) + "}";
        } else if (operation == "debug.inspect") {
            requireArgs(1);
            const auto task = number<vm::TaskId>(args[0], "task id");
            const auto snapshot = scheduler.debugInspect(task);
            if (!snapshot) throw std::runtime_error("debug task is unavailable");
            payload = snapshotJson(*snapshot);
        } else if (operation == "debug.eval") {
            requireArgs(2);
            const auto task = number<vm::TaskId>(args[0], "task id");
            const auto value = scheduler.debugEvaluate(task, args[1]);
            if (!value) throw std::runtime_error("task is not paused");
            payload = "{\"value\":" + valueJson(*value) + "}";
        } else if (operation == "debug.watch") {
            requireArgs(3);
            const auto task = number<vm::TaskId>(args[0], "task id");
            const bool changed = args[1] == "add" ?
                scheduler.debugAddWatch(task, args[2]) : args[1] == "remove" ?
                scheduler.debugRemoveWatch(task, args[2]) : false;
            if (!changed) throw std::runtime_error("watch operation failed");
            payload = "{\"expression\":" + json(args[2]) + "}";
        } else if (operation == "debug.diagnostics") {
            requireArgs(3);
            const auto task = number<vm::TaskId>(args[0], "task id");
            if (!scheduler.debugSetDiagnostics(task, boolean(args[1]), boolean(args[2])))
                throw std::runtime_error("diagnostics could not be configured");
            payload = "{}";
        } else if (operation == "debug.reset") {
            requireArgs(1);
            const auto task = number<vm::TaskId>(args[0], "task id");
            if (!scheduler.debugResetDiagnostics(task))
                throw std::runtime_error("diagnostics could not be reset");
            payload = "{}";
        } else {
            throw std::runtime_error("unknown operation");
        }
        write(success(requestId, std::move(payload)));
    } catch (const std::exception& error) {
        write(failure(requestId, error.what()));
    } catch (...) {
        write(failure(requestId, "unknown protocol failure"));
    }
}

} // namespace ql32::platform::esp32
