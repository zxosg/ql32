#include "ql32/ui/line_editor.hpp"

#include <utility>

namespace ql32::ui {

LineEditorAction LineEditor::handleKey(const input::KeyEvent& event) {
    if (!event.pressed) return LineEditorAction::Ignored;

    switch (event.key) {
    case input::Key::Character:
        return event.character <= 0x7f &&
                insertRawCharacter(static_cast<char>(event.character))
            ? LineEditorAction::Changed : LineEditorAction::Ignored;
    case input::Key::Backspace:
        if (cursor_ == 0) return LineEditorAction::Ignored;
        text_.erase(cursor_ - 1, 1);
        --cursor_;
        return LineEditorAction::Changed;
    case input::Key::Delete:
        if (cursor_ >= text_.size()) return LineEditorAction::Ignored;
        text_.erase(cursor_, 1);
        return LineEditorAction::Changed;
    case input::Key::Left:
        if (cursor_ == 0) return LineEditorAction::Ignored;
        --cursor_;
        return LineEditorAction::Changed;
    case input::Key::Right:
        if (cursor_ >= text_.size()) return LineEditorAction::Ignored;
        ++cursor_;
        return LineEditorAction::Changed;
    case input::Key::Home:
        if (cursor_ == 0) return LineEditorAction::Ignored;
        cursor_ = 0;
        return LineEditorAction::Changed;
    case input::Key::End:
        if (cursor_ == text_.size()) return LineEditorAction::Ignored;
        cursor_ = text_.size();
        return LineEditorAction::Changed;
    case input::Key::Up:
        if (!text_.empty() || previousLine_.empty()) return LineEditorAction::Ignored;
        text_ = previousLine_;
        cursor_ = text_.size();
        return LineEditorAction::Changed;
    case input::Key::Enter:
        return LineEditorAction::Submit;
    default:
        return LineEditorAction::Ignored;
    }
}

bool LineEditor::insertRawCharacter(char character, bool allowTab) {
    if (text_.size() >= maximumLength ||
        (character != '\t' && (character < 32 || character > 126)) ||
        (character == '\t' && !allowTab))
        return false;
    text_.insert(cursor_, 1, character);
    ++cursor_;
    return true;
}

void LineEditor::setText(std::string text) {
    if (text.size() > maximumLength) text.resize(maximumLength);
    text_ = std::move(text);
    cursor_ = text_.size();
}

void LineEditor::rememberCurrentLine() {
    if (!text_.empty()) previousLine_ = text_;
}

void LineEditor::clear() noexcept {
    text_.clear();
    cursor_ = 0;
}

} // namespace ql32::ui
