#include "ql32/ui/graphic_presenter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ql32::ui {
namespace {
// Match the retained CYD and VGA consoles: five glyph columns, one spacing
// column, seven glyph rows, and one spacing row.
constexpr int glyphWidth = 6;
constexpr int normalRowPitch = 12;
constexpr int denseRowPitch = 9;
constexpr int normalHeaderHeight = 16;
constexpr int normalFooterHeight = 18;
constexpr int hiddenButtonErrorHeight = 10;
constexpr int rightButtonHeight = 14;
constexpr int rightButtonGap = 4;
constexpr int rightButtonPadding = 4;

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
    return request_.has_value();
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
    inputEdit_.clear();
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
    if ((graphicActive() || infoToast_) &&
        (screenWidth_ != graphics_->windowManager().screenWidth() ||
         screenHeight_ != graphics_->windowManager().screenHeight()))
        screenChanged();
    refreshInfo();
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

int GraphicPresenter::titleHeight() const noexcept {
    return request_ && (request_->kind == RequestKind::Dialog ||
                       request_->kind == RequestKind::Input) &&
           request_->dialogOptions.noTitle ? 0 : normalHeaderHeight;
}

int GraphicPresenter::footerHeight() const noexcept {
    if (request_ && (request_->kind == RequestKind::Dialog ||
                     request_->kind == RequestKind::Input)) {
        if (request_->dialogOptions.hideButtons)
            return request_->kind == RequestKind::Dialog ? hiddenButtonErrorHeight : 0;
        if (request_->dialogOptions.buttonsRight) return 0;
    }
    return normalFooterHeight;
}

int GraphicPresenter::rowPitch() const noexcept {
    return request_ && request_->kind == RequestKind::Dialog &&
           request_->dialogOptions.densePack ? denseRowPitch : normalRowPitch;
}

int GraphicPresenter::rightButtonWidth() const noexcept {
    if (!request_ || request_->kind != RequestKind::Dialog ||
        !request_->dialogOptions.buttonsRight || request_->dialogOptions.hideButtons)
        return 0;
    const auto longest = std::max(request_->acceptLabel.size(),
                                  request_->cancelLabel.size());
    return std::clamp(static_cast<int>(longest * glyphWidth + 12), 56, 112);
}

void GraphicPresenter::screenChanged() {
    if (graphicActive()) {
        const auto old = bounds_;
        layout();
        if (old.width && old.height) graphics_->invalidateSystemRegion(old);
    }
    if (infoToast_) layoutInfo();
}

void GraphicPresenter::rebuildRows() {
    rows_.clear();
    if (!graphicActive()) return;
    const auto columns = std::max<std::size_t>(1,
        (bounds_.width > static_cast<std::uint32_t>(20 + buttonColumnWidth_) ?
         bounds_.width - 20 - buttonColumnWidth_ : 1) / glyphWidth);
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
    } else if (request_->kind == RequestKind::Input) {
        if (request_->dialogOptions.noTitle && !request_->title.empty())
            for (const auto& line : wrap(request_->title, columns)) rows_.push_back({line});
        if (!request_->dialogOptions.keyInput) rows_.push_back({inputEdit_});
    }
}

void GraphicPresenter::ensureFocusVisible() {
    if (!graphicActive() || rows_.empty()) return;
    const auto top = titleHeight();
    const auto bottom = footerHeight();
    const auto pitch = rowPitch();
    const std::size_t visible = std::max<std::size_t>(1,
        (bounds_.height > static_cast<std::uint32_t>(top + bottom + 8) ?
         (bounds_.height - top - bottom - 8) / pitch : 1));
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
    const bool fullScreen = request_->kind == RequestKind::Dialog &&
                            request_->dialogOptions.fullScreen;
    const std::uint32_t margin = !fullScreen && sw >= 176 && sh >= 96 ? 8 : 0;
    std::size_t longest = request_->kind == RequestKind::Dialog &&
                          request_->dialogOptions.noTitle ? 0 : request_->title.size();
    longest = std::max(longest, request_->message.size());
    for (const auto& field : request_->fields)
        longest = std::max(longest, field.label.size());
    for (const auto& item : request_->items)
        longest = std::max(longest, item.label.size() + item.shortcut.size() + 4);
    buttonColumnWidth_ = rightButtonWidth();
    const auto desiredWidth = static_cast<std::uint32_t>(
        std::min<std::size_t>(420, std::max<std::size_t>(200,
            longest * glyphWidth + 20 + static_cast<std::size_t>(buttonColumnWidth_))));
    const auto availableWidth = sw > 2 * margin ? sw - 2 * margin : sw;
    const auto width = fullScreen ? sw : std::min(desiredWidth, availableWidth);
    const bool hasDialogPosition = request_->kind == RequestKind::Dialog ||
                                   request_->kind == RequestKind::Input;
    const int positionX = hasDialogPosition ?
        request_->dialogOptions.positionX : 0;
    const int positionY = hasDialogPosition ?
        request_->dialogOptions.positionY : 0;
    const auto x = fullScreen || positionX < 0 ? 0u :
        positionX > 0 ? sw - width : (sw - width) / 2;
    bounds_ = {static_cast<std::int32_t>(x), 0, width, sh};
    rebuildRows();
    const auto top = titleHeight();
    const auto bottom = footerHeight();
    const int chromePadding = request_->kind == RequestKind::Input &&
                              request_->dialogOptions.keyInput ? 4 : 8;
    const auto desired = static_cast<std::uint32_t>(top + bottom + chromePadding +
        std::max<std::size_t>(1, rows_.size()) * rowPitch());
    const std::size_t rightButtonCount = buttonColumnWidth_ > 0 ? 2 : 0;
    const auto buttonStackHeight = rightButtonCount == 0 ? 0u :
        static_cast<std::uint32_t>(rightButtonPadding * 2 +
            rightButtonCount * rightButtonHeight +
            (rightButtonCount - 1) * rightButtonGap);
    const auto availableHeight = sh > 2 * margin ? sh - 2 * margin : sh;
    const auto requiredHeight = std::max(desired,
        static_cast<std::uint32_t>(top) + buttonStackHeight);
    const auto height = fullScreen ? sh : std::min(availableHeight, requiredHeight);
    bounds_.height = height;
    const auto y = fullScreen || positionY < 0 ? 0u :
        positionY > 0 ? sh - height : (sh - height) / 2;
    bounds_.y = static_cast<std::int32_t>(y);
    panel_ = std::make_unique<graphics::IndexedCanvas>(width, height,
                                                        graphics::PixelDepth::One);
    ensureFocusVisible();
    paint();
    graphics_->invalidateSystemRegion(bounds_);
}

void GraphicPresenter::refreshInfo() {
    const auto now = std::chrono::steady_clock::now();
    const bool flashPhase =
        (std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch()).count() / 500) % 2 != 0;
    if (flashPhase != infoFlashPhase_) {
        infoFlashPhase_ = flashPhase;
        if (infoToast_ && infoToast_->displayMode != 0 &&
            infoBounds_.width && infoBounds_.height)
            graphics_->invalidateSystemRegion(infoBounds_);
    }
    if (infoToast_ && now >= infoExpiresAt_) {
        service_->dismissInfo(infoToast_->id);
        const auto old = infoBounds_;
        infoToast_.reset();
        infoPanel_.reset();
        infoBounds_ = {};
        if (old.width && old.height) graphics_->invalidateSystemRegion(old);
    }

    const auto latest = service_->currentInfo();
    if (!latest) {
        if (infoToast_) {
            const auto old = infoBounds_;
            infoToast_.reset();
            infoPanel_.reset();
            infoBounds_ = {};
            if (old.width && old.height) graphics_->invalidateSystemRegion(old);
        }
        return;
    }
    if (infoToast_ && infoToast_->id == latest->id) return;

    const auto old = infoBounds_;
    infoToast_ = *latest;
    infoExpiresAt_ = latest->busy ?
        std::chrono::steady_clock::time_point::max() :
        now + std::chrono::milliseconds(latest->durationMs);
    layoutInfo();
    if (old.width && old.height) graphics_->invalidateSystemRegion(old);
}

void GraphicPresenter::layoutInfo() {
    if (!infoToast_) return;
    const auto sw = graphics_->windowManager().screenWidth();
    const auto sh = graphics_->windowManager().screenHeight();
    if (!sw || !sh) return;
    screenWidth_ = sw;
    screenHeight_ = sh;

    const std::uint32_t margin = sw >= 176 && sh >= 96 ? 8 : 0;
    const auto availableWidth = sw > 2 * margin ? sw - 2 * margin : sw;
    std::string line = infoToast_->text;
    std::replace(line.begin(), line.end(), '\n', ' ');
    std::replace(line.begin(), line.end(), '\r', ' ');
    const auto longest = std::min<std::size_t>(
        line.size(), 420 / glyphWidth);
    const auto desiredWidth = static_cast<std::uint32_t>(std::max<std::size_t>(
        1, longest * glyphWidth + 16));
    const auto width = std::min(availableWidth, desiredWidth);
    const auto columns = std::max<std::size_t>(1,
        width > 16 ? (width - 16) / glyphWidth : 1);
    if (line.size() > columns) {
        if (columns > 3) {
            line.resize(columns - 3);
            line += "...";
        } else {
            line.resize(columns);
        }
    }
    const auto availableHeight = sh > 2 * margin ? sh - 2 * margin : sh;
    const auto height = std::min<std::uint32_t>(availableHeight, 16);
    const auto x = infoToast_->positionX < 0 ? 0u :
        infoToast_->positionX > 0 ? sw - width : (sw - width) / 2;
    const auto y = infoToast_->positionY < 0 ? 0u :
        infoToast_->positionY > 0 ? sh - height : (sh - height) / 2;
    const auto old = infoBounds_;
    infoBounds_ = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                   width, height};
    infoPanel_ = std::make_unique<graphics::IndexedCanvas>(
        width, height, graphics::PixelDepth::Four);
    auto& canvas = *infoPanel_;
    canvas.clear();
    // Semantic colors: 0 text, 1 paper, 2 frame. composeRow applies the
    // caller's ink/paper colors and the current blink phase.
    fill(canvas, 0, 0, static_cast<int>(width), static_cast<int>(height), 1);
    fill(canvas, 0, 0, width, 1, 2);
    fill(canvas, 0, static_cast<int>(height) - 1, width, 1, 2);
    fill(canvas, 0, 0, 1, height, 2);
    fill(canvas, static_cast<int>(width) - 1, 0, 1, height, 2);
    drawText(canvas, 8, 4, line, static_cast<int>(width) - 16, 0);
    if (old.width && old.height) graphics_->invalidateSystemRegion(old);
    graphics_->invalidateSystemRegion(infoBounds_);
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
    const int top = titleHeight();
    const int bottom = footerHeight();
    const int pitch = rowPitch();
    const int rightWidth = buttonColumnWidth_;
    if (top > 0) {
        fill(canvas, 2, 2, w - 4, top - 2, 0);
        drawText(canvas, 7, 4, request_->title, w - 14 - rightWidth, 1, false, true);
    }
    const int bodyBottom = std::max(top, h - bottom);
    const std::size_t visible = bodyBottom > top ?
        static_cast<std::size_t>((bodyBottom - top - 2) / pitch) : 0;
    for (std::size_t i = 0; i < visible && scroll_ + i < rows_.size(); ++i) {
        const Row& row = rows_[scroll_ + i];
        const int y = top + 2 + static_cast<int>(i * pitch);
        const bool selected = row.field >= 0 &&
                              static_cast<std::size_t>(row.field) == focus_ &&
                              row.enabled;
        if (selected) fill(canvas, 4, y - 1, w - 8 - rightWidth, pitch - 1, 0);
        drawText(canvas, 8, y + 1, row.text, w - 20 - rightWidth,
                 selected ? 1 : 0, !row.enabled);
    }
    if (rows_.size() > visible && visible > 0) {
        const int trackTop = top + 2;
        const int trackHeight = std::max(1, bodyBottom - trackTop - 2);
        fill(canvas, w - 6 - rightWidth, trackTop, 1, trackHeight, 0);
        const int thumbHeight = std::max(4, static_cast<int>(
            static_cast<std::size_t>(trackHeight) * visible / rows_.size()));
        const int travel = std::max(0, trackHeight - thumbHeight);
        const auto maxScroll = rows_.size() - visible;
        const int thumbTop = trackTop + static_cast<int>(
            static_cast<std::size_t>(travel) * scroll_ / maxScroll);
        fill(canvas, w - 7 - rightWidth, thumbTop, 3, thumbHeight, 0);
    }
    if (rightWidth > 0) {
        const int columnLeft = w - rightWidth - 3;
        fill(canvas, columnLeft, top + 2, 1,
             std::max(0, bodyBottom - top - 4), 0);
        const int buttonWidth = rightWidth - 10;
        const std::array<const std::string*, 2> buttons{
            &request_->cancelLabel, &request_->acceptLabel};
        const int stackHeight = static_cast<int>(buttons.size()) * rightButtonHeight +
            (static_cast<int>(buttons.size()) - 1) * rightButtonGap;
        const int firstY = std::max(top + rightButtonPadding,
            (std::max(top, bodyBottom) + top - stackHeight) / 2);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const int buttonY = firstY + static_cast<int>(i) *
                (rightButtonHeight + rightButtonGap);
            fill(canvas, columnLeft + 4, buttonY, buttonWidth, 1, 0);
            fill(canvas, columnLeft + 4, buttonY + rightButtonHeight - 1,
                 buttonWidth, 1, 0);
            fill(canvas, columnLeft + 4, buttonY, 1, rightButtonHeight, 0);
            fill(canvas, columnLeft + 3 + buttonWidth, buttonY, 1,
                 rightButtonHeight, 0);
            drawText(canvas, columnLeft + 7, buttonY + 4, *buttons[i],
                     buttonWidth - 6, 0);
        }
    }
    if (bottom > 0) fill(canvas, 1, h - bottom, w - 2, 1, 0);
    std::string actions = request_->kind == RequestKind::Alert ? "Enter  OK   Esc  Close" :
        request_->kind == RequestKind::Confirm ?
            (confirmYes_ ? "N  No   [Y  Yes]" : "[N  No]   Y  Yes") :
        request_->kind == RequestKind::Menu ? "Enter:Select Esc:Back" :
        request_->kind == RequestKind::Input ? "Enter:Submit  Esc:Cancel" :
        request_->cancelLabel + "   " + request_->acceptLabel;
    if ((request_->kind == RequestKind::Dialog ||
         request_->kind == RequestKind::Input) &&
        request_->dialogOptions.hideButtons) {
        if (!error_.empty())
            drawText(canvas, 7, h - hiddenButtonErrorHeight + 1, error_, w - 14, 0);
    } else {
        if (!error_.empty()) actions = error_;
        drawText(canvas, 7, h - bottom + 5, actions, w - 14, 0);
    }
}

void GraphicPresenter::composeRow(std::uint32_t y, std::uint32_t x,
                                  std::vector<graphics::ColorIndex>& row) const {
    const auto compose = [y, x, &row](const auto& canvas,
                                      const graphics::Rect& bounds) {
        if (!canvas || y < static_cast<std::uint32_t>(bounds.y) ||
            y >= static_cast<std::uint32_t>(bounds.y) + bounds.height) return;
        for (std::uint32_t i = 0; i < row.size(); ++i) {
            const auto screenX = x + i;
            if (screenX < static_cast<std::uint32_t>(bounds.x) ||
                screenX >= static_cast<std::uint32_t>(bounds.x) + bounds.width) continue;
            row[i] = canvas->pixel(screenX - bounds.x, y - bounds.y) ? 7 : 0;
        }
    };
    compose(panel_, bounds_);
    if (infoPanel_ && y >= static_cast<std::uint32_t>(infoBounds_.y) &&
        y < static_cast<std::uint32_t>(infoBounds_.y) + infoBounds_.height) {
        for (std::uint32_t i = 0; i < row.size(); ++i) {
            const auto screenX = x + i;
            if (screenX < static_cast<std::uint32_t>(infoBounds_.x) ||
                screenX >= static_cast<std::uint32_t>(infoBounds_.x) + infoBounds_.width)
                continue;
            const auto semantic = infoPanel_->pixel(
                screenX - static_cast<std::uint32_t>(infoBounds_.x),
                y - static_cast<std::uint32_t>(infoBounds_.y));
            const bool phase = infoFlashPhase_;
            const bool swap = infoToast_ && infoToast_->displayMode == 2 && phase;
            if (semantic == 1) {
                row[i] = swap ? infoToast_->ink : infoToast_->paper;
            } else if (semantic == 0) {
                row[i] = (infoToast_ && infoToast_->displayMode == 1 && phase) || swap ?
                    infoToast_->paper : infoToast_->ink;
            } else {
                row[i] = swap ? infoToast_->paper : infoToast_->ink;
            }
        }
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
    if (service_->submitDialog(id, std::move(values_))) { finish(); return true; }
    error_ = "Check values"; paint(); graphics_->invalidateSystemRegion(bounds_);
    return false;
}

bool GraphicPresenter::handleKey(const input::KeyEvent& event) {
    refresh();
    if (!graphicActive() || !event.pressed) return false;
    if (request_->kind == RequestKind::Input) {
        if (event.key == input::Key::Escape) {
            service_->resolve(request_->id, ResultKind::Cancelled); finish(); return true;
        }
        if (request_->dialogOptions.keyInput) {
            if (event.key == input::Key::Enter) {
                service_->submitInput(request_->id, {}); finish(); return true;
            }
            if (event.key == input::Key::Character) {
                const char key = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(event.character)));
                if (key == 'L' || key == 'Q') {
                    service_->submitInput(request_->id, std::string(1, key));
                    finish();
                    return true;
                }
            }
            return true;
        }
        if (event.key == input::Key::Backspace) {
            if (!inputEdit_.empty()) inputEdit_.pop_back();
        } else if (event.key == input::Key::Enter) {
            service_->submitInput(request_->id, inputEdit_); finish(); return true;
        } else if (event.key == input::Key::Character && event.character >= 32 &&
                   event.character <= 126 && inputEdit_.size() < 256) {
            inputEdit_.push_back(static_cast<char>(event.character));
        } else {
            return false;
        }
        rebuildRows(); paint(); graphics_->invalidateSystemRegion(bounds_);
        return true;
    }
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
            bounds_.height > normalHeaderHeight + normalFooterHeight + 8 ?
            (bounds_.height - normalHeaderHeight - normalFooterHeight - 8) /
                normalRowPitch : 1);
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
