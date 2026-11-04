#include "ql32/ui/graphic_presenter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace ql32::ui {
namespace {
// Match the retained CYD and VGA consoles: five glyph columns, one spacing
// column, seven glyph rows, and one spacing row.
constexpr int glyphWidth = 6;
constexpr int rowHeight = 12;
constexpr int headerHeight = 16;
constexpr int footerHeight = 18;

void fill(graphics::IndexedCanvas& canvas, int x, int y, int w, int h,
          graphics::ColorIndex color) {
    for (int py = std::max(0, y); py < std::min<int>(canvas.height(), y + h); ++py)
        for (int px = std::max(0, x); px < std::min<int>(canvas.width(), x + w); ++px)
            canvas.setPixel(px, py, color);
}

void drawText(graphics::IndexedCanvas& canvas, int x, int y,
              const std::string& value, int maxWidth, graphics::ColorIndex color,
              bool dim = false, bool bold = false) {
    const auto columns = static_cast<std::size_t>(std::max(0, maxWidth / glyphWidth));
    if (!columns) return;
    const std::string shown = value.size() <= columns ? value :
        (columns > 3 ? value.substr(0, columns - 3) + "..." :
         value.substr(0, columns));
    int cursor = x;
    for (unsigned char ch : shown) {
        if (cursor + glyphWidth > x + maxWidth) break;
        const auto glyph = graphics::classicGlyphColumns(static_cast<char>(ch));
        for (int column = 0; column < 5; ++column)
            for (int bit = 0; bit < 7; ++bit)
                if (glyph[column] & (1u << bit)) {
                    for (int weight = 0; weight <= static_cast<int>(bold); ++weight) {
                        const int px = cursor + column + weight;
                        const int py = y + bit;
                        if ((!dim || ((px + py) & 1) == 0) &&
                            px >= 0 && py >= 0 &&
                            px < static_cast<int>(canvas.width()) &&
                            py < static_cast<int>(canvas.height()))
                            canvas.setPixel(px, py, color);
                    }
                }
        cursor += glyphWidth;
    }
}

std::vector<std::string> wrap(const std::string& text, std::size_t columns) {
    columns = std::max<std::size_t>(1, columns);
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        while (word.size() > columns) {
            if (!line.empty()) { lines.push_back(line); line.clear(); }
            lines.push_back(word.substr(0, columns));
            word.erase(0, columns);
        }
        if (line.empty()) line = word;
        else if (line.size() + 1 + word.size() <= columns) line += " " + word;
        else { lines.push_back(line); line = word; }
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

std::string numberText(double number) {
    std::ostringstream out;
    out.precision(10);
    out << number;
    return out.str();
}
} // namespace

GraphicPresenter::GraphicPresenter(std::shared_ptr<QueuedService> service,
                                   std::shared_ptr<graphics::WindowedService> graphics)
    : service_(std::move(service)), graphics_(std::move(graphics)) {
    if (!service_ || !graphics_) throw std::invalid_argument("graphic UI needs services");
    graphics_->setUiLayer([this](std::uint32_t y, std::uint32_t x,
                                 std::vector<graphics::ColorIndex>& row) {
        composeRow(y, x, row);
    });
}

GraphicPresenter::~GraphicPresenter() {
    if (graphics_) graphics_->setUiLayer({});
}

bool GraphicPresenter::graphicActive() const noexcept {
    return request_ && request_->kind != RequestKind::Input;
}
bool GraphicPresenter::inputActive() const noexcept {
    return request_ && request_->kind == RequestKind::Input;
}

bool GraphicPresenter::beginNext() {
    refresh();
    if (request_) return true;
    request_ = service_->takeNextRequest();
    if (!request_) return false;
    edits_.clear(); values_.clear(); error_.clear();
    focus_ = scroll_ = 0;
    confirmYes_ = false;
    for (const auto& field : request_->fields) {
        values_.push_back(field.initialValue);
        if (const auto* str = std::get_if<std::string>(&field.initialValue))
            edits_.push_back(*str);
        else if (const auto* number = std::get_if<double>(&field.initialValue))
            edits_.push_back(numberText(*number));
        else edits_.emplace_back();
    }
    if (request_->kind == RequestKind::Dialog) {
        while (focus_ < request_->fields.size() &&
               request_->fields[focus_].kind == FieldKind::Text) ++focus_;
        if (focus_ == request_->fields.size()) focus_ = 0;
    } else if (request_->kind == RequestKind::Menu) {
        while (focus_ < request_->items.size() && !request_->items[focus_].enabled)
            ++focus_;
        if (focus_ == request_->items.size()) focus_ = 0;
    }
    if (graphicActive()) layout();
    return true;
}

void GraphicPresenter::refresh() {
    if (request_ && !service_->isOutstanding(request_->id)) finish();
    if (graphicActive() && (screenWidth_ != graphics_->windowManager().screenWidth() ||
                            screenHeight_ != graphics_->windowManager().screenHeight()))
        screenChanged();
}

void GraphicPresenter::finish() {
    const auto old = bounds_;
    request_.reset(); panel_.reset(); rows_.clear(); bounds_ = {};
    if (old.width && old.height) graphics_->invalidateSystemRegion(old);
}

void GraphicPresenter::cancelActive() {
    if (!request_) return;
    service_->resolve(request_->id, ResultKind::Cancelled);
    finish();
}

void GraphicPresenter::screenChanged() {
    if (graphicActive()) {
        const auto old = bounds_;
        layout();
        if (old.width && old.height) graphics_->invalidateSystemRegion(old);
    }
}

void GraphicPresenter::rebuildRows() {
    rows_.clear();
    if (!graphicActive()) return;
    const auto columns = std::max<std::size_t>(1,
        (bounds_.width > 20 ? bounds_.width - 20 : 1) / glyphWidth);
    if (request_->kind == RequestKind::Alert || request_->kind == RequestKind::Confirm) {
        for (const auto& line : wrap(request_->message, columns)) rows_.push_back({line});
    } else if (request_->kind == RequestKind::Menu) {
        for (std::size_t i = 0; i < request_->items.size(); ++i) {
            const auto& item = request_->items[i];
            std::string label = item.label;
            if (!item.shortcut.empty()) label += "  [" + item.shortcut + "]";
            for (const auto& line : wrap(label, columns))
                rows_.push_back({line, static_cast<int>(i), item.enabled});
        }
    } else if (request_->kind == RequestKind::Dialog) {
        for (std::size_t i = 0; i < request_->fields.size(); ++i) {
            const auto& field = request_->fields[i];
            for (const auto& line : wrap(field.label, columns)) rows_.push_back({line});
            if (field.kind == FieldKind::Text) continue;
            std::string value;
            if (field.kind == FieldKind::Choice) {
                const auto index = static_cast<std::size_t>(std::get<double>(values_[i]));
                value = "< " + field.choices.at(index) + " >";
            } else if (field.kind == FieldKind::Check)
                value = std::get<bool>(values_[i]) ? "[X]" : "[ ]";
            else value = edits_[i];
            if (value.size() > columns) value = value.substr(value.size() - columns);
            rows_.push_back({value, static_cast<int>(i)});
        }
    }
}

void GraphicPresenter::ensureFocusVisible() {
    if (!graphicActive() || rows_.empty()) return;
    const std::size_t visible = std::max<std::size_t>(1,
        (bounds_.height > headerHeight + footerHeight + 8 ?
         bounds_.height - headerHeight - footerHeight - 8 : rowHeight) / rowHeight);
    auto it = std::find_if(rows_.begin(), rows_.end(), [this](const Row& row) {
        return row.field == static_cast<int>(focus_);
    });
    if (it == rows_.end()) return;
    const auto index = static_cast<std::size_t>(it - rows_.begin());
    if (index < scroll_) scroll_ = index;
    if (index >= scroll_ + visible) scroll_ = index - visible + 1;
}

void GraphicPresenter::layout() {
    if (!graphicActive()) return;
    const auto sw = graphics_->windowManager().screenWidth();
    const auto sh = graphics_->windowManager().screenHeight();
    screenWidth_ = sw;
    screenHeight_ = sh;
    if (!sw || !sh) return;
    const std::uint32_t margin = sw >= 176 && sh >= 96 ? 8 : 0;
    std::size_t longest = request_->title.size();
    longest = std::max(longest, request_->message.size());
    for (const auto& field : request_->fields)
        longest = std::max(longest, field.label.size());
    for (const auto& item : request_->items)
        longest = std::max(longest, item.label.size() + item.shortcut.size() + 4);
    const auto desiredWidth = static_cast<std::uint32_t>(
        std::min<std::size_t>(420, std::max<std::size_t>(200, longest * glyphWidth + 20)));
    const auto width = std::min(desiredWidth, sw - 2 * margin);
    bounds_ = {static_cast<std::int32_t>((sw - width) / 2), 0, width, sh};
    rebuildRows();
    const auto desired = static_cast<std::uint32_t>(headerHeight + footerHeight + 8 +
        std::max<std::size_t>(1, rows_.size()) * rowHeight);
    const auto height = std::min(sh - 2 * margin, desired);
    bounds_.height = height;
    bounds_.y = static_cast<std::int32_t>((sh - height) / 2);
    panel_ = std::make_unique<graphics::IndexedCanvas>(width, height,
                                                        graphics::PixelDepth::One);
    ensureFocusVisible();
    paint();
    graphics_->invalidateSystemRegion(bounds_);
}

void GraphicPresenter::paint() {
    if (!panel_ || !graphicActive()) return;
    auto& canvas = *panel_;
    canvas.clear();
    const int w = static_cast<int>(canvas.width());
    const int h = static_cast<int>(canvas.height());
    fill(canvas, 0, 0, w, h, 1);
    // White outside and black inside remain visible over either BASIC paper.
    fill(canvas, 1, 1, w - 2, 1, 0); fill(canvas, 1, h - 2, w - 2, 1, 0);
    fill(canvas, 1, 1, 1, h - 2, 0); fill(canvas, w - 2, 1, 1, h - 2, 0);
    fill(canvas, 2, 2, w - 4, headerHeight - 2, 0);
    drawText(canvas, 7, 4, request_->title, w - 14, 1, false, true);
    const int bodyBottom = std::max(headerHeight, h - footerHeight);
    const std::size_t visible = bodyBottom > headerHeight ?
        static_cast<std::size_t>((bodyBottom - headerHeight - 2) / rowHeight) : 0;
    for (std::size_t i = 0; i < visible && scroll_ + i < rows_.size(); ++i) {
        const Row& row = rows_[scroll_ + i];
        const int y = headerHeight + 2 + static_cast<int>(i * rowHeight);
        const bool selected = row.field >= 0 &&
                              static_cast<std::size_t>(row.field) == focus_ &&
                              row.enabled;
        if (selected) fill(canvas, 4, y - 1, w - 8, rowHeight - 1, 0);
        drawText(canvas, 8, y + 1, row.text, w - 20,
                 selected ? 1 : 0, !row.enabled);
    }
    if (rows_.size() > visible && visible > 0) {
        const int trackTop = headerHeight + 2;
        const int trackHeight = std::max(1, bodyBottom - trackTop - 2);
        fill(canvas, w - 6, trackTop, 1, trackHeight, 0);
        const int thumbHeight = std::max(4, static_cast<int>(
            static_cast<std::size_t>(trackHeight) * visible / rows_.size()));
        const int travel = std::max(0, trackHeight - thumbHeight);
        const auto maxScroll = rows_.size() - visible;
        const int thumbTop = trackTop + static_cast<int>(
            static_cast<std::size_t>(travel) * scroll_ / maxScroll);
        fill(canvas, w - 7, thumbTop, 3, thumbHeight, 0);
    }
    fill(canvas, 1, h - footerHeight, w - 2, 1, 0);
    std::string actions = request_->kind == RequestKind::Alert ? "Enter  OK   Esc  Close" :
        request_->kind == RequestKind::Confirm ?
            (confirmYes_ ? "N  No   [Y  Yes]" : "[N  No]   Y  Yes") :
        request_->kind == RequestKind::Menu ? "Enter:Select Esc:Back" :
        request_->cancelLabel + "   " + request_->acceptLabel;
    if (!error_.empty()) actions = error_;
    drawText(canvas, 7, h - footerHeight + 5, actions, w - 14, 0);
}

void GraphicPresenter::composeRow(std::uint32_t y, std::uint32_t x,
                                  std::vector<graphics::ColorIndex>& row) const {
    if (!panel_ || y < static_cast<std::uint32_t>(bounds_.y) ||
        y >= static_cast<std::uint32_t>(bounds_.y) + bounds_.height) return;
    for (std::uint32_t i = 0; i < row.size(); ++i) {
        const auto screenX = x + i;
        if (screenX < static_cast<std::uint32_t>(bounds_.x) ||
            screenX >= static_cast<std::uint32_t>(bounds_.x) + bounds_.width) continue;
        row[i] = panel_->pixel(screenX - bounds_.x, y - bounds_.y) ? 7 : 0;
    }
}

void GraphicPresenter::moveFocus(int delta) {
    if (!graphicActive()) return;
    const auto count = request_->kind == RequestKind::Menu ? request_->items.size() :
        request_->kind == RequestKind::Dialog ? request_->fields.size() : 0;
    if (!count) return;
    auto index = static_cast<int>(focus_);
    for (std::size_t tries = 0; tries < count; ++tries) {
        index = std::clamp(index + delta, 0, static_cast<int>(count - 1));
        const bool enabled = request_->kind == RequestKind::Menu ?
            request_->items[index].enabled : request_->fields[index].kind != FieldKind::Text;
        if (enabled) { focus_ = static_cast<std::size_t>(index); break; }
        if (index == 0 || index == static_cast<int>(count - 1)) break;
    }
    ensureFocusVisible(); paint(); graphics_->invalidateSystemRegion(bounds_);
}

void GraphicPresenter::changeChoice(int delta) {
    if (!request_ || request_->kind != RequestKind::Dialog ||
        focus_ >= request_->fields.size()) return;
    const auto& field = request_->fields[focus_];
    if (field.kind == FieldKind::Choice) {
        const auto old = static_cast<int>(std::get<double>(values_[focus_]));
        values_[focus_] = static_cast<double>(std::clamp(old + delta, 0,
            static_cast<int>(field.choices.size() - 1)));
    } else if (field.kind == FieldKind::Check) {
        values_[focus_] = !std::get<bool>(values_[focus_]);
    } else return;
    rebuildRows(); ensureFocusVisible(); paint(); graphics_->invalidateSystemRegion(bounds_);
}

bool GraphicPresenter::editCharacter(char character) {
    if (!request_ || request_->kind != RequestKind::Dialog ||
        focus_ >= request_->fields.size() ||
        (request_->fields[focus_].kind != FieldKind::Edit &&
         request_->fields[focus_].kind != FieldKind::Integer &&
         request_->fields[focus_].kind != FieldKind::Float)) return false;
    if (edits_[focus_].size() >= 256) return true;
    edits_[focus_].push_back(character); error_.clear();
    rebuildRows(); ensureFocusVisible(); paint(); graphics_->invalidateSystemRegion(bounds_);
    return true;
}

bool GraphicPresenter::backspace() {
    if (!request_ || request_->kind != RequestKind::Dialog ||
        focus_ >= request_->fields.size()) return false;
    if (!edits_[focus_].empty()) edits_[focus_].pop_back();
    error_.clear(); rebuildRows(); paint(); graphics_->invalidateSystemRegion(bounds_);
    return true;
}

bool GraphicPresenter::submit() {
    if (!request_) return false;
    const auto id = request_->id;
    if (request_->kind == RequestKind::Alert) {
        service_->resolve(id, ResultKind::Acknowledged); finish(); return true;
    }
    if (request_->kind == RequestKind::Confirm) {
        service_->resolve(id, confirmYes_ ? ResultKind::Confirmed :
                                              ResultKind::Cancelled);
        finish(); return true;
    }
    if (request_->kind == RequestKind::Menu) {
        if (focus_ < request_->items.size() && request_->items[focus_].enabled) {
            if (service_->selectMenu(id, request_->items[focus_].commandId)) finish();
        }
        return true;
    }
    for (std::size_t i = 0; i < request_->fields.size(); ++i) {
        const auto& field = request_->fields[i];
        if (field.kind == FieldKind::Edit) values_[i] = edits_[i];
        if (field.kind != FieldKind::Integer && field.kind != FieldKind::Float) continue;
        char* end = nullptr;
        const double number = std::strtod(edits_[i].c_str(), &end);
        const bool valid = end != edits_[i].c_str() && *end == '\0' &&
            std::isfinite(number) &&
            (field.kind != FieldKind::Integer || std::trunc(number) == number) &&
            (!field.minimum || number >= *field.minimum) &&
            (!field.maximum || number <= *field.maximum);
        if (!valid) {
            focus_ = i; error_ = "Check " + field.label;
            rebuildRows(); ensureFocusVisible(); paint();
            graphics_->invalidateSystemRegion(bounds_);
            return false;
        }
        values_[i] = number;
    }
    if (service_->submitDialog(id, values_)) { finish(); return true; }
    error_ = "Check values"; paint(); graphics_->invalidateSystemRegion(bounds_);
    return false;
}

bool GraphicPresenter::handleKey(const input::KeyEvent& event) {
    refresh();
    if (!graphicActive() || !event.pressed) return false;
    if (event.key == input::Key::Escape) {
        service_->resolve(request_->id, ResultKind::Cancelled); finish(); return true;
    }
    if (request_->kind == RequestKind::Confirm && event.key == input::Key::Character) {
        if (event.character == 'y' || event.character == 'Y') {
            service_->resolve(request_->id, ResultKind::Confirmed); finish(); return true;
        }
        if (event.character == 'n' || event.character == 'N') { submit(); return true; }
    }
    if (request_->kind == RequestKind::Confirm &&
        (event.key == input::Key::Left || event.key == input::Key::Right ||
         event.key == input::Key::Tab)) {
        confirmYes_ = event.key == input::Key::Right ||
                      (event.key == input::Key::Tab && !confirmYes_);
        paint(); graphics_->invalidateSystemRegion(bounds_);
        return true;
    }
    if ((request_->kind == RequestKind::Alert ||
         request_->kind == RequestKind::Confirm) &&
        (event.key == input::Key::Up || event.key == input::Key::Down ||
         event.key == input::Key::PageUp || event.key == input::Key::PageDown)) {
        const auto visible = std::max<std::size_t>(1,
            bounds_.height > headerHeight + footerHeight + 8 ?
            (bounds_.height - headerHeight - footerHeight - 8) / rowHeight : 1);
        const auto maxScroll = rows_.size() > visible ? rows_.size() - visible : 0;
        const auto step = event.key == input::Key::PageUp ||
                          event.key == input::Key::PageDown ? visible : 1;
        if (event.key == input::Key::Up || event.key == input::Key::PageUp)
            scroll_ = scroll_ > step ? scroll_ - step : 0;
        else scroll_ = std::min(maxScroll, scroll_ + step);
        paint(); graphics_->invalidateSystemRegion(bounds_);
        return true;
    }
    if (event.key == input::Key::Up || (event.key == input::Key::Tab && event.shift)) {
        moveFocus(-1); return true;
    }
    if (event.key == input::Key::Down || event.key == input::Key::Tab) {
        moveFocus(1); return true;
    }
    if (event.key == input::Key::Left) { changeChoice(-1); return true; }
    if (event.key == input::Key::Right) { changeChoice(1); return true; }
    if (event.key == input::Key::Character && event.character == ' ' &&
        request_->kind == RequestKind::Dialog &&
        focus_ < request_->fields.size() &&
        request_->fields[focus_].kind == FieldKind::Check) {
        changeChoice(1); return true;
    }
    if (event.key == input::Key::Backspace) return backspace();
    if (event.key == input::Key::Enter) { submit(); return true; }
    if (event.key == input::Key::Character && event.character >= 32 &&
        event.character <= 126) {
        if (request_->kind == RequestKind::Menu) {
            for (const auto& item : request_->items)
                if (item.enabled && item.shortcut.size() == 1 &&
                    std::tolower(static_cast<unsigned char>(item.shortcut[0])) ==
                    std::tolower(static_cast<unsigned char>(event.character))) {
                    service_->selectMenu(request_->id, item.commandId); finish(); return true;
                }
        }
        return editCharacter(static_cast<char>(event.character));
    }
    return false;
}

void GraphicPresenter::handleSerialByte(char byte, std::uint32_t nowMs) {
    if (!graphicActive()) return;
    input::KeyEvent event;
    if (serialEscapeState_ == 1) {
        if (byte == '[') { serialEscapeState_ = 2; return; }
        serialEscapeState_ = 0;
        event.key = input::Key::Escape;
        handleKey(event);
        if (!graphicActive()) return;
    } else if (serialEscapeState_ == 2) {
        serialEscapeState_ = 0;
        if (byte == 'A') event.key = input::Key::Up;
        else if (byte == 'B') event.key = input::Key::Down;
        else if (byte == 'C') event.key = input::Key::Right;
        else if (byte == 'D') event.key = input::Key::Left;
        else return;
        handleKey(event);
        return;
    }
    if (byte == 27) { serialEscapeState_ = 1; serialEscapeAt_ = nowMs; return; }
    if (byte == '\r' || byte == '\n') event.key = input::Key::Enter;
    else if (byte == '\b' || byte == 127) event.key = input::Key::Backspace;
    else if (byte == '\t') event.key = input::Key::Tab;
    else if (static_cast<unsigned char>(byte) >= 32 &&
             static_cast<unsigned char>(byte) < 127) {
        event.key = input::Key::Character;
        event.character = static_cast<unsigned char>(byte);
    } else return;
    handleKey(event);
}

void GraphicPresenter::flushSerialEscape(std::uint32_t nowMs) {
    if (serialEscapeState_ == 1 && nowMs - serialEscapeAt_ >= 40) {
        serialEscapeState_ = 0;
        input::KeyEvent event;
        event.key = input::Key::Escape;
        handleKey(event);
    }
    if (serialEscapeState_ == 2 && nowMs - serialEscapeAt_ >= 40)
        serialEscapeState_ = 0;
}

bool GraphicPresenter::consumeLine(const std::string& line) {
    refresh();
    if (!request_) return false;
    if (request_->kind == RequestKind::Input) {
        service_->submitInput(request_->id, line); finish(); return true;
    }
    if (line == "esc" || line == "cancel") {
        service_->resolve(request_->id, ResultKind::Cancelled); finish(); return true;
    }
    if (line == "up" || line == "down" || line == "pageup" || line == "pagedown") {
        input::KeyEvent event;
        event.key = line == "up" ? input::Key::Up :
                    line == "down" ? input::Key::Down :
                    line == "pageup" ? input::Key::PageUp : input::Key::PageDown;
        return handleKey(event);
    }
    if (line == "left" || line == "right") {
        input::KeyEvent event;
        event.key = line == "left" ? input::Key::Left : input::Key::Right;
        return handleKey(event);
    }
    if ((line == "yes" || line == "y") && request_->kind == RequestKind::Confirm) {
        service_->resolve(request_->id, ResultKind::Confirmed); finish(); return true;
    }
    if (request_->kind == RequestKind::Dialog && focus_ < edits_.size()) {
        const auto& field = request_->fields[focus_];
        if (field.kind == FieldKind::Choice && !line.empty()) {
            char* end = nullptr;
            const auto choice = std::strtol(line.c_str(), &end, 10);
            if (*end == '\0' && choice >= 1 &&
                choice <= static_cast<long>(field.choices.size())) {
                values_[focus_] = static_cast<double>(choice - 1);
                rebuildRows(); moveFocus(1); return true;
            }
        }
        if (field.kind == FieldKind::Check &&
            (line == "y" || line == "yes" || line == "n" || line == "no")) {
            values_[focus_] = line == "y" || line == "yes";
            rebuildRows(); moveFocus(1); return true;
        }
    }
    if (request_->kind == RequestKind::Dialog && focus_ < edits_.size() &&
        !line.empty()) {
        const auto kind = request_->fields[focus_].kind;
        if (kind == FieldKind::Edit || kind == FieldKind::Integer || kind == FieldKind::Float) {
            edits_[focus_] = line; rebuildRows(); paint();
            graphics_->invalidateSystemRegion(bounds_);
            moveFocus(1); return true;
        }
    }
    if (line.empty() || line == "enter" || line == "ok") return submit();
    if (request_->kind == RequestKind::Menu) {
        char* end = nullptr;
        const auto itemNumber = std::strtol(line.c_str(), &end, 10);
        if (*end == '\0' && itemNumber >= 1 &&
            itemNumber <= static_cast<long>(request_->items.size()) &&
            request_->items[static_cast<std::size_t>(itemNumber - 1)].enabled) {
            focus_ = static_cast<std::size_t>(itemNumber - 1);
            return submit();
        }
        for (std::size_t i = 0; i < request_->items.size(); ++i)
            if (request_->items[i].enabled && line.size() == 1 &&
                request_->items[i].shortcut.size() == 1 &&
                std::tolower(static_cast<unsigned char>(request_->items[i].shortcut[0])) ==
                std::tolower(static_cast<unsigned char>(line[0]))) {
                focus_ = i; return submit();
            }
    }
    return false;
}
} // namespace ql32::ui
