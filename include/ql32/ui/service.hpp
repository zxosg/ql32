#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <variant>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ql32::ui {

using SessionId = std::uint64_t;
using RequestId = std::uint64_t;

enum class ResultKind { Acknowledged, Confirmed, Cancelled, Submitted, Selected };
enum class RequestKind { Alert, Confirm, Dialog, Menu, Input };
enum class FieldKind { Text, Edit, Integer, Float, Choice, Check };
using FieldValue = std::variant<std::monostate, std::string, double, bool>;

struct DialogField {
    FieldKind kind;
    std::string label;
    FieldValue initialValue;
    std::vector<std::string> choices;
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct MenuItem {
    std::uint32_t commandId;
    std::string label;
    std::string shortcut;
    bool enabled = true;
};

struct Request {
    RequestId id;
    SessionId session;
    RequestKind kind;
    std::string title;
    std::string message;
    std::vector<DialogField> fields;
    std::vector<MenuItem> items;
    std::string acceptLabel = "OK";
    std::string cancelLabel = "Cancel";
};

struct Result {
    ResultKind kind;
    std::uint32_t commandId = 0;
    std::vector<FieldValue> fields;
};

// Requests return immediately. A missing poll result means the owning session
// remains suspended while other sessions and the host event loop keep running.
class Service {
public:
    virtual ~Service() = default;
    virtual RequestId beginAlert(SessionId session, std::string title,
                                 std::string message) = 0;
    virtual RequestId beginConfirm(SessionId session, std::string title,
                                   std::string message) = 0;
    virtual RequestId beginDialog(SessionId session, std::string title,
                                  std::vector<DialogField> fields,
                                  std::string acceptLabel = "OK",
                                  std::string cancelLabel = "Cancel") = 0;
    virtual RequestId beginMenu(SessionId session, std::string title,
                                std::vector<MenuItem> items) = 0;
    virtual RequestId beginInput(SessionId session, std::string prompt) = 0;
    virtual std::optional<Result> poll(RequestId request) = 0;
    virtual void cancel(RequestId request) = 0;
    virtual void cancelSession(SessionId session) = 0;
};

// Thread-safe request broker shared by the VM scheduler and a platform UI
// adapter. The adapter takes queued requests and resolves them after user input.
class QueuedService final : public Service {
public:
    RequestId beginAlert(SessionId session, std::string title,
                         std::string message) override;
    RequestId beginConfirm(SessionId session, std::string title,
                           std::string message) override;
    RequestId beginDialog(SessionId session, std::string title,
                          std::vector<DialogField> fields,
                          std::string acceptLabel = "OK",
                          std::string cancelLabel = "Cancel") override;
    RequestId beginMenu(SessionId session, std::string title,
                        std::vector<MenuItem> items) override;
    RequestId beginInput(SessionId session, std::string prompt) override;
    std::optional<Result> poll(RequestId request) override;
    void cancel(RequestId request) override;
    void cancelSession(SessionId session) override;

    std::optional<Request> takeNextRequest();
    bool resolve(RequestId request, ResultKind result);
    bool submitDialog(RequestId request, std::vector<FieldValue> fields);
    bool submitInput(RequestId request, std::string line);
    bool selectMenu(RequestId request, std::uint32_t commandId);
    bool isOutstanding(RequestId request);

private:
    struct PendingRequest {
        Request request;
        bool dispatched = false;
        std::optional<Result> result;
    };

    RequestId nextId_ = 1;
    std::deque<RequestId> queue_;
    std::unordered_map<RequestId, PendingRequest> requests_;
    std::mutex mutex_;

    RequestId begin(SessionId session, RequestKind kind, std::string title,
                    std::string message, std::vector<DialogField> fields = {},
                    std::vector<MenuItem> items = {},
                    std::string acceptLabel = "OK",
                    std::string cancelLabel = "Cancel");
};

} // namespace ql32::ui
