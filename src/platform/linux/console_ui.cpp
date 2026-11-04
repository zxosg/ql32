#include "ql32/platform/linux/console_ui.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace ql32::platform::linux_host {
namespace {

std::string lowercase(std::string value) {
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

std::optional<double> parseNumber(const std::string& value) {
    std::istringstream parser(value);
    double number = 0;
    if (!(parser >> number) || !std::isfinite(number)) return std::nullopt;
    parser >> std::ws;
    if (!parser.eof()) return std::nullopt;
    return number;
}

} // namespace

ConsoleUi::ConsoleUi(std::shared_ptr<ui::QueuedService> service,
                     std::istream& input, std::ostream& output,
                     std::shared_ptr<graphics::WindowedService> graphics)
    : service_(std::move(service)), input_(input), output_(output) {
    if (!service_) throw std::invalid_argument("console UI requires a request service");
    if (graphics) graphic_ = std::make_unique<ui::GraphicPresenter>(service_, graphics);
}

bool ConsoleUi::dispatchNext() {
    if (graphic_) {
        if (!graphic_->beginNext()) return false;
        std::string line;
        while (graphic_->active()) {
            output_ << "UI (enter, up, down, left, right, cancel, or value)> " << std::flush;
            if (!std::getline(input_, line)) { graphic_->cancelActive(); break; }
            (void)graphic_->consumeLine(line);
            graphic_->refresh();
        }
        output_ << '\n';
        return true;
    }
    const auto request = service_->takeNextRequest();
    if (!request) return false;
    std::string response;

    if (request->kind == ui::RequestKind::Input) {
        if (!request->title.empty()) output_ << request->title;
        output_ << std::flush;
        if (std::getline(input_, response)) service_->submitInput(request->id, std::move(response));
        else service_->resolve(request->id, ui::ResultKind::Cancelled);
        output_ << '\n';
        return true;
    }

    output_ << '\n' << request->title << '\n';
    if (request->kind == ui::RequestKind::Alert) {
        if (!request->message.empty()) output_ << request->message << '\n';
        output_ << "Press Enter to continue..." << std::flush;
        const bool received = static_cast<bool>(std::getline(input_, response));
        output_ << '\n';
        service_->resolve(request->id, received ? ui::ResultKind::Acknowledged :
                                                    ui::ResultKind::Cancelled);
        return true;
    }
    if (request->kind == ui::RequestKind::Confirm) {
        if (!request->message.empty()) output_ << request->message << '\n';
        output_ << "Continue? [y/N] " << std::flush;
        if (!std::getline(input_, response)) response.clear();
        response = lowercase(std::move(response));
        output_ << '\n';
        service_->resolve(request->id, response == "y" || response == "yes" ?
            ui::ResultKind::Confirmed : ui::ResultKind::Cancelled);
        return true;
    }
    if (request->kind == ui::RequestKind::Dialog) {
        std::vector<ui::FieldValue> values;
        values.reserve(request->fields.size());
        bool cancelled = false;

        for (const auto& field : request->fields) {
            output_ << field.label;
            if (field.kind == ui::FieldKind::Text) {
                output_ << '\n';
                values.emplace_back(std::monostate{});
                continue;
            }

            if (field.kind == ui::FieldKind::Edit) {
                const auto& initial = std::get<std::string>(field.initialValue);
                output_ << " [" << initial << "]: " << std::flush;
                if (!std::getline(input_, response)) {
                    cancelled = true;
                    break;
                }
                values.emplace_back(response);
            } else if (field.kind == ui::FieldKind::Check) {
                const bool initial = std::get<bool>(field.initialValue);
                output_ << " [" << (initial ? "Y/n" : "y/N") << "]: " << std::flush;
                if (!std::getline(input_, response)) {
                    cancelled = true;
                    break;
                }
                response = lowercase(std::move(response));
                if (response.empty()) values.emplace_back(initial);
                else if (response == "y" || response == "yes") values.emplace_back(true);
                else if (response == "n" || response == "no") values.emplace_back(false);
                else {
                    output_ << "Enter y or n.\n";
                    const bool received = static_cast<bool>(std::getline(input_, response));
                    if (!received) { cancelled = true; break; }
                    response = lowercase(std::move(response));
                    if (response.empty()) values.emplace_back(initial);
                    else if (response == "y" || response == "yes") values.emplace_back(true);
                    else if (response == "n" || response == "no") values.emplace_back(false);
                    else { cancelled = true; break; }
                }
            } else if (field.kind == ui::FieldKind::Choice) {
                const auto initial = static_cast<std::size_t>(
                    std::get<double>(field.initialValue));
                output_ << '\n';
                for (std::size_t i = 0; i < field.choices.size(); ++i)
                    output_ << "  " << (i + 1) << ") " << field.choices[i]
                            << (i == initial ? " [current]" : "") << '\n';
                output_ << "Select [" << (initial + 1) << "]: " << std::flush;
                if (!std::getline(input_, response)) {
                    cancelled = true;
                    break;
                }
                if (response.empty()) values.emplace_back(static_cast<double>(initial));
                else {
                    const auto selected = parseNumber(response);
                    if (!selected || std::trunc(*selected) != *selected ||
                        *selected < 1 || *selected > static_cast<double>(field.choices.size())) {
                        output_ << "Invalid choice; dialog cancelled.\n";
                        cancelled = true;
                        break;
                    }
                    values.emplace_back(*selected - 1);
                }
            } else {
                const double initial = std::get<double>(field.initialValue);
                output_ << " [" << initial << "]: " << std::flush;
                if (!std::getline(input_, response)) {
                    cancelled = true;
                    break;
                }
                if (response.empty()) {
                    values.emplace_back(initial);
                    continue;
                }
                const auto number = parseNumber(response);
                const bool integerOutOfRange = number &&
                    field.kind == ui::FieldKind::Integer &&
                    (*number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                     *number > static_cast<double>(std::numeric_limits<std::int32_t>::max()));
                if (!number || (field.kind == ui::FieldKind::Integer &&
                                (std::trunc(*number) != *number || integerOutOfRange)) ||
                    (field.minimum && *number < *field.minimum) ||
                    (field.maximum && *number > *field.maximum)) {
                    output_ << "Invalid value; dialog cancelled.\n";
                    cancelled = true;
                    break;
                }
                values.emplace_back(*number);
            }
        }

        if (!cancelled) {
            output_ << request->acceptLabel << " / " << request->cancelLabel
                    << " [" << request->acceptLabel << "]: " << std::flush;
            if (!std::getline(input_, response)) cancelled = true;
            else if (!response.empty()) {
                const std::string answer = lowercase(response);
                cancelled = answer != "y" && answer != "yes" &&
                    answer != lowercase(request->acceptLabel);
            }
        }
        if (cancelled) service_->resolve(request->id, ui::ResultKind::Cancelled);
        else if (!service_->submitDialog(request->id, std::move(values)))
            service_->resolve(request->id, ui::ResultKind::Cancelled);
        return true;
    }

    if (request->kind == ui::RequestKind::Menu) {
        for (std::size_t i = 0; i < request->items.size(); ++i) {
            const auto& item = request->items[i];
            output_ << "  " << (i + 1) << ") " << item.label;
            if (!item.shortcut.empty()) output_ << " [" << item.shortcut << ']';
            if (!item.enabled) output_ << " (disabled)";
            output_ << '\n';
        }
        while (true) {
            output_ << "Select item or shortcut (Enter cancels): " << std::flush;
            if (!std::getline(input_, response)) {
                service_->resolve(request->id, ui::ResultKind::Cancelled);
                return true;
            }
            if (response.empty()) {
                service_->resolve(request->id, ui::ResultKind::Cancelled);
                return true;
            }
            const std::string key = lowercase(response);
            bool matched = false;
            for (std::size_t i = 0; i < request->items.size(); ++i) {
                const auto& item = request->items[i];
                if (!item.enabled) continue;
                if (key == lowercase(item.shortcut)) {
                    service_->selectMenu(request->id, item.commandId);
                    matched = true;
                    break;
                }
            }
            if (matched) return true;
            const auto position = parseNumber(response);
            if (position && std::trunc(*position) == *position && *position >= 1 &&
                *position <= static_cast<double>(request->items.size())) {
                const auto& item = request->items[static_cast<std::size_t>(*position - 1)];
                if (item.enabled) {
                    service_->selectMenu(request->id, item.commandId);
                    return true;
                }
            }
            output_ << "Invalid selection.\n";
        }
    }

    return false;
}

} // namespace ql32::platform::linux_host
