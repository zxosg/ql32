#include "ql32/platform/esp32/line_ui_presenter.hpp"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ql32::platform::esp32 {
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

std::string initialText(const ui::FieldValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream output;
        output << std::setprecision(8) << *number;
        return output.str();
    }
    if (const auto* checked = std::get_if<bool>(&value)) return *checked ? "Y" : "N";
    return {};
}

} // namespace

LineUiPresenter::LineUiPresenter(std::shared_ptr<ui::QueuedService> service,
                                 std::ostream& output)
    : service_(std::move(service)), output_(output) {
    if (!service_) throw std::invalid_argument("line UI presenter requires a service");
}

bool LineUiPresenter::beginNext() {
    if (request_) return true;
    request_ = service_->takeNextRequest();
    if (!request_) return false;
    fieldIndex_ = 0;
    fieldValues_.clear();
    dialogStage_ = DialogStage::Field;
    present();
    return true;
}

void LineUiPresenter::present() {
    if (request_->kind == ui::RequestKind::Input) {
        if (!request_->title.empty()) output_ << request_->title;
        output_.flush();
        return;
    }
    output_ << '\n' << request_->title << '\n';
    if (!request_->message.empty()) output_ << request_->message << '\n';
    switch (request_->kind) {
    case ui::RequestKind::Alert:
        output_ << "Press Enter to continue.\n";
        break;
    case ui::RequestKind::Confirm:
        output_ << "Continue? [y/N]\n";
        break;
    case ui::RequestKind::Dialog:
        fieldValues_.reserve(request_->fields.size());
        presentNextDialogField();
        break;
    case ui::RequestKind::Menu:
        for (std::size_t i = 0; i < request_->items.size(); ++i) {
            const auto& item = request_->items[i];
            output_ << "  " << (i + 1) << ") " << item.label;
            if (!item.shortcut.empty()) output_ << " [" << item.shortcut << ']';
            if (!item.enabled) output_ << " (disabled)";
            output_ << '\n';
        }
        output_ << "Select item or shortcut; Enter cancels.\n";
        break;
    case ui::RequestKind::Input:
        break;
    }
    output_.flush();
}

void LineUiPresenter::presentNextDialogField() {
    while (fieldIndex_ < request_->fields.size() &&
           request_->fields[fieldIndex_].kind == ui::FieldKind::Text) {
        output_ << request_->fields[fieldIndex_].label << '\n';
        fieldValues_.emplace_back(std::monostate{});
        ++fieldIndex_;
    }
    if (fieldIndex_ >= request_->fields.size()) {
        dialogStage_ = DialogStage::Submit;
        output_ << request_->acceptLabel << " / " << request_->cancelLabel
                << " [" << request_->acceptLabel << "]: " << std::flush;
        return;
    }

    const auto& field = request_->fields[fieldIndex_];
    output_ << field.label;
    if (field.kind == ui::FieldKind::Choice) {
        const auto initial = static_cast<std::size_t>(std::get<double>(field.initialValue));
        output_ << '\n';
        for (std::size_t i = 0; i < field.choices.size(); ++i)
            output_ << "  " << (i + 1) << ") " << field.choices[i]
                    << (i == initial ? " [current]" : "") << '\n';
        output_ << "Select [" << (initial + 1) << "]: ";
    } else if (field.kind == ui::FieldKind::Check) {
        output_ << " [" << (std::get<bool>(field.initialValue) ? "Y/n" : "y/N")
                << "]: ";
    } else {
        output_ << " [" << initialText(field.initialValue) << "]: ";
    }
    output_.flush();
}

void LineUiPresenter::finish() {
    request_.reset();
    fieldValues_.clear();
    fieldIndex_ = 0;
}

bool LineUiPresenter::consumeLine(const std::string& line) {
    if (!request_) return false;
    switch (request_->kind) {
    case ui::RequestKind::Alert:
        service_->resolve(request_->id, ui::ResultKind::Acknowledged);
        finish();
        return true;
    case ui::RequestKind::Confirm: {
        const std::string answer = lowercase(line);
        if (answer.empty() || answer == "n" || answer == "no" || answer == "cancel") {
            service_->resolve(request_->id, ui::ResultKind::Cancelled);
            finish();
            return true;
        }
        if (answer == "y" || answer == "yes") {
            service_->resolve(request_->id, ui::ResultKind::Confirmed);
            finish();
            return true;
        }
        output_ << "Enter y or n.\nContinue? [y/N]\n" << std::flush;
        return false;
    }
    case ui::RequestKind::Dialog:
        return consumeDialogLine(line);
    case ui::RequestKind::Menu:
        return consumeMenuLine(line);
    case ui::RequestKind::Input:
        service_->submitInput(request_->id, line);
        finish();
        return true;
    }
    return false;
}

bool LineUiPresenter::consumeMenuLine(const std::string& line) {
    if (line.empty()) {
        service_->resolve(request_->id, ui::ResultKind::Cancelled);
        finish();
        return true;
    }

    const std::string key = lowercase(line);
    for (const auto& item : request_->items) {
        if (item.enabled && !item.shortcut.empty() && key == lowercase(item.shortcut)) {
            service_->selectMenu(request_->id, item.commandId);
            finish();
            return true;
        }
    }

    const auto position = parseNumber(line);
    if (position && std::trunc(*position) == *position && *position >= 1 &&
        *position <= static_cast<double>(request_->items.size())) {
        const auto& item = request_->items[static_cast<std::size_t>(*position - 1)];
        if (item.enabled) {
            service_->selectMenu(request_->id, item.commandId);
            finish();
            return true;
        }
    }
    output_ << "Invalid selection. Select item or shortcut; Enter cancels.\n";
    return false;
}

bool LineUiPresenter::consumeDialogLine(const std::string& line) {
    if (dialogStage_ == DialogStage::Submit) {
        const std::string answer = lowercase(line);
        const std::string accept = lowercase(request_->acceptLabel);
        const std::string cancel = lowercase(request_->cancelLabel);
        if (answer.empty() || answer == "y" || answer == "yes" || answer == accept) {
            if (service_->submitDialog(request_->id, std::move(fieldValues_))) {
                finish();
                return true;
            }
            output_ << "Dialog values were rejected.\n";
            service_->resolve(request_->id, ui::ResultKind::Cancelled);
            finish();
            return true;
        }
        if (answer == "n" || answer == "no" || answer == cancel || answer == "cancel") {
            service_->resolve(request_->id, ui::ResultKind::Cancelled);
            finish();
            return true;
        }
        output_ << request_->acceptLabel << " / " << request_->cancelLabel
                << " [" << request_->acceptLabel << "]: " << std::flush;
        return false;
    }

    const auto& field = request_->fields[fieldIndex_];
    ui::FieldValue value;
    bool valid = true;
    switch (field.kind) {
    case ui::FieldKind::Text:
        value = std::monostate{};
        break;
    case ui::FieldKind::Edit:
        value = line.empty() ? field.initialValue : ui::FieldValue(line);
        break;
    case ui::FieldKind::Check: {
        const std::string answer = lowercase(line);
        if (answer.empty()) value = field.initialValue;
        else if (answer == "y" || answer == "yes" || answer == "1") value = true;
        else if (answer == "n" || answer == "no" || answer == "0") value = false;
        else valid = false;
        break;
    }
    case ui::FieldKind::Choice: {
        if (line.empty()) {
            value = field.initialValue;
            break;
        }
        const auto selected = parseNumber(line);
        if (!selected || std::trunc(*selected) != *selected || *selected < 1 ||
            *selected > static_cast<double>(field.choices.size())) {
            valid = false;
            break;
        }
        value = *selected - 1;
        break;
    }
    case ui::FieldKind::Integer:
    case ui::FieldKind::Float: {
        if (line.empty()) {
            value = field.initialValue;
            break;
        }
        const auto number = parseNumber(line);
        const bool integerOutOfRange = number &&
            field.kind == ui::FieldKind::Integer &&
            (*number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
             *number > static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        if (!number || (field.kind == ui::FieldKind::Integer &&
                        (std::trunc(*number) != *number || integerOutOfRange)) ||
            (field.minimum && *number < *field.minimum) ||
            (field.maximum && *number > *field.maximum)) {
            valid = false;
            break;
        }
        value = *number;
        break;
    }
    }

    if (!valid) {
        output_ << "Invalid value.\n";
        presentNextDialogField();
        return false;
    }
    fieldValues_.push_back(std::move(value));
    ++fieldIndex_;
    presentNextDialogField();
    return false;
}

} // namespace ql32::platform::esp32
