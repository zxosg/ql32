#pragma once

#include <cstdint>
#include <chrono>
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

struct DialogOptions {
    bool buttonsRight = false;
    bool noTitle = false;
    bool fullScreen = false;
    bool noDrag = false;
    bool densePack = false;
    bool hideButtons = false;
    bool keyInput = false;
    int positionX = 0;
    int positionY = 0;
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
    DialogOptions dialogOptions;
};

struct Result {
    ResultKind kind;
    std::uint32_t commandId = 0;
    std::vector<FieldValue> fields;
};

struct InfoToast {
    RequestId id;
    SessionId session;
    std::string text;
    std::uint32_t durationMs = 3000;
    int positionX = -1;
    int positionY = 1;
    bool busy = false;
    std::uint32_t delayMs = 0;
    std::chrono::steady_clock::time_point requestedAt{};
    std::uint8_t ink = 0;
    std::uint8_t paper = 7;
    std::uint8_t displayMode = 0;
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
                                  std::string cancelLabel = "Cancel",
                                  DialogOptions options = {}) = 0;
    virtual RequestId beginMenu(SessionId session, std::string title,
                                std::vector<MenuItem> items) = 0;
    virtual RequestId beginInput(SessionId session, std::string prompt,
                                 DialogOptions options = {}) = 0;
    virtual void showInfo(SessionId session, std::string text,
                          std::uint32_t durationMs, int positionX,
                          int positionY, std::uint8_t ink = 0,
                          std::uint8_t paper = 7,
                          std::uint8_t displayMode = 0) = 0;
    virtual void showBusy(SessionId session, std::string text,
                          int positionX, int positionY,
                          std::uint8_t ink = 0, std::uint8_t paper = 7,
                          std::uint8_t displayMode = 0) = 0;
    virtual void clearBusy(SessionId session) = 0;
    virtual std::optional<InfoToast> currentInfo() = 0;
    virtual void dismissInfo(RequestId id) = 0;
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
                          std::string cancelLabel = "Cancel",
                          DialogOptions options = {}) override;
    RequestId beginMenu(SessionId session, std::string title,
                        std::vector<MenuItem> items) override;
    RequestId beginInput(SessionId session, std::string prompt,
                         DialogOptions options = {}) override;
    void showInfo(SessionId session, std::string text, std::uint32_t durationMs,
                  int positionX, int positionY, std::uint8_t ink = 0,
                  std::uint8_t paper = 7,
                  std::uint8_t displayMode = 0) override;
    void showBusy(SessionId session, std::string text, int positionX,
                  int positionY, std::uint8_t ink = 0,
                  std::uint8_t paper = 7,
                  std::uint8_t displayMode = 0) override;
    void clearBusy(SessionId session) override;
    std::optional<InfoToast> currentInfo() override;
    void dismissInfo(RequestId id) override;
    std::optional<Result> poll(RequestId request) override;
    void cancel(RequestId request) override;
    void cancelSession(SessionId session) override;

    std::optional<Request> takeNextRequest();
    bool resolve(RequestId request, ResultKind result);
    bool submitDialog(RequestId request, const std::vector<FieldValue>& fields);
    bool submitDialog(RequestId request, std::vector<FieldValue>&& fields);
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
    std::unordered_map<SessionId, InfoToast> infoToasts_;
    RequestId nextInfoId_ = 1;
    std::mutex mutex_;

    RequestId begin(SessionId session, RequestKind kind, std::string title,
                    std::string message, std::vector<DialogField> fields = {},
                    std::vector<MenuItem> items = {},
                    std::string acceptLabel = "OK",
                    std::string cancelLabel = "Cancel",
                    DialogOptions dialogOptions = {});
};

} // namespace ql32::ui
