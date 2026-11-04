#include "ql32/ui/service.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace ql32::ui {

RequestId QueuedService::begin(SessionId session, RequestKind kind, std::string title,
                               std::string message, std::vector<DialogField> fields,
                               std::vector<MenuItem> items, std::string acceptLabel,
                               std::string cancelLabel) {
    std::lock_guard lock(mutex_);
    while (nextId_ == 0 || requests_.find(nextId_) != requests_.end()) {
        ++nextId_;
        if (nextId_ == 0) ++nextId_;
    }
    const RequestId id = nextId_++;
    if (nextId_ == 0) nextId_ = 1;
    Request request{id, session, kind, std::move(title), std::move(message),
                    std::move(fields), std::move(items)};
    request.acceptLabel = std::move(acceptLabel);
    request.cancelLabel = std::move(cancelLabel);
    requests_.emplace(id, PendingRequest{std::move(request), false, std::nullopt});
    queue_.push_back(id);
    return id;
}

RequestId QueuedService::beginAlert(SessionId session, std::string title,
                                    std::string message) {
    return begin(session, RequestKind::Alert, std::move(title), std::move(message));
}

RequestId QueuedService::beginConfirm(SessionId session, std::string title,
                                      std::string message) {
    return begin(session, RequestKind::Confirm, std::move(title), std::move(message));
}

RequestId QueuedService::beginDialog(SessionId session, std::string title,
                                     std::vector<DialogField> fields,
                                     std::string acceptLabel,
                                     std::string cancelLabel) {
    for (const DialogField& field : fields) {
        const bool validValue = field.kind == FieldKind::Text ?
            std::holds_alternative<std::monostate>(field.initialValue) :
            ((field.kind == FieldKind::Edit &&
              std::holds_alternative<std::string>(field.initialValue)) ||
             ((field.kind == FieldKind::Integer || field.kind == FieldKind::Float ||
               field.kind == FieldKind::Choice) &&
              std::holds_alternative<double>(field.initialValue)) ||
             (field.kind == FieldKind::Check &&
              std::holds_alternative<bool>(field.initialValue)));
        if (!validValue)
            throw std::invalid_argument("dialog field initial value has the wrong type");
        if (field.minimum && field.maximum && *field.minimum > *field.maximum)
            throw std::invalid_argument("dialog field minimum exceeds maximum");
        if (field.kind == FieldKind::Choice && field.choices.empty())
            throw std::invalid_argument("choice field requires at least one option");
        if (field.kind == FieldKind::Integer || field.kind == FieldKind::Choice) {
            const double value = std::get<double>(field.initialValue);
            if (!std::isfinite(value) || std::trunc(value) != value ||
                (field.kind == FieldKind::Choice &&
                 (value < 0 || value >= static_cast<double>(field.choices.size()))))
                throw std::invalid_argument("dialog field initial value is outside its options");
        }
        if (field.kind == FieldKind::Integer || field.kind == FieldKind::Float) {
            const double value = std::get<double>(field.initialValue);
            if (!std::isfinite(value) || (field.minimum && value < *field.minimum) ||
                (field.maximum && value > *field.maximum))
                throw std::invalid_argument("dialog field initial value is outside its range");
        }
    }
    return begin(session, RequestKind::Dialog, std::move(title), {},
                 std::move(fields), {}, std::move(acceptLabel), std::move(cancelLabel));
}

RequestId QueuedService::beginMenu(SessionId session, std::string title,
                                   std::vector<MenuItem> items) {
    if (items.empty()) throw std::invalid_argument("menu requires at least one item");
    std::unordered_set<std::uint32_t> ids;
    for (const MenuItem& item : items) {
        if (item.commandId == 0 || !ids.insert(item.commandId).second)
            throw std::invalid_argument("menu command IDs must be unique and nonzero");
    }
    return begin(session, RequestKind::Menu, std::move(title), {}, {}, std::move(items));
}

RequestId QueuedService::beginInput(SessionId session, std::string prompt) {
    return begin(session, RequestKind::Input, std::move(prompt), {});
}

std::optional<Result> QueuedService::poll(RequestId request) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    if (found == requests_.end() || !found->second.result) return std::nullopt;
    const Result result = *found->second.result;
    requests_.erase(found);
    return result;
}

void QueuedService::cancel(RequestId request) {
    std::lock_guard lock(mutex_);
    requests_.erase(request);
}

void QueuedService::cancelSession(SessionId session) {
    std::lock_guard lock(mutex_);
    for (auto request = requests_.begin(); request != requests_.end();) {
        if (request->second.request.session == session) request = requests_.erase(request);
        else ++request;
    }
}

std::optional<Request> QueuedService::takeNextRequest() {
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
        const RequestId id = queue_.front();
        queue_.pop_front();
        const auto found = requests_.find(id);
        if (found == requests_.end() || found->second.dispatched || found->second.result)
            continue;
        found->second.dispatched = true;
        return found->second.request;
    }
    return std::nullopt;
}

bool QueuedService::resolve(RequestId request, ResultKind result) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    if (found == requests_.end() || !found->second.dispatched || found->second.result)
        return false;
    if (result == ResultKind::Cancelled ||
        (found->second.request.kind == RequestKind::Alert &&
         result == ResultKind::Acknowledged) ||
        (found->second.request.kind == RequestKind::Confirm &&
         result == ResultKind::Confirmed)) {
        found->second.result = Result{result};
        return true;
    }
    return false;
}

bool QueuedService::submitDialog(RequestId request, std::vector<FieldValue> fields) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    if (found == requests_.end() || !found->second.dispatched || found->second.result ||
        found->second.request.kind != RequestKind::Dialog ||
        fields.size() != found->second.request.fields.size()) return false;
    const auto valid = [](const DialogField& field, const FieldValue& value) {
        if (field.kind == FieldKind::Text) return std::holds_alternative<std::monostate>(value);
        if (field.kind == FieldKind::Edit) return std::holds_alternative<std::string>(value);
        if (field.kind == FieldKind::Check) return std::holds_alternative<bool>(value);
        const auto* number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number)) return false;
        if (field.kind == FieldKind::Integer && std::trunc(*number) != *number) return false;
        if ((field.minimum && *number < *field.minimum) ||
            (field.maximum && *number > *field.maximum)) return false;
        if (field.kind == FieldKind::Choice &&
            (*number < 0 || std::trunc(*number) != *number ||
             *number >= static_cast<double>(field.choices.size()))) return false;
        return true;
    };
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (!valid(found->second.request.fields[i], fields[i])) return false;
    found->second.result = Result{ResultKind::Submitted, 0, std::move(fields)};
    return true;
}

bool QueuedService::submitInput(RequestId request, std::string line) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    if (found == requests_.end() || !found->second.dispatched || found->second.result ||
        found->second.request.kind != RequestKind::Input) return false;
    found->second.result = Result{ResultKind::Submitted, 0, {std::move(line)}};
    return true;
}

bool QueuedService::selectMenu(RequestId request, std::uint32_t commandId) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    if (found == requests_.end() || !found->second.dispatched || found->second.result ||
        found->second.request.kind != RequestKind::Menu) return false;
    for (const MenuItem& item : found->second.request.items) {
        if (item.commandId == commandId && item.enabled) {
            found->second.result = Result{ResultKind::Selected, commandId, {}};
            return true;
        }
    }
    return false;
}

bool QueuedService::isOutstanding(RequestId request) {
    std::lock_guard lock(mutex_);
    const auto found = requests_.find(request);
    return found != requests_.end() && !found->second.result;
}

} // namespace ql32::ui
