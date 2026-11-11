#pragma once

#include "ql32/input/service.hpp"

#include <cstddef>
#include <string>

namespace ql32::ui {

enum class LineEditorAction {
    Ignored,
    Changed,
    Submit
};

// Small single-line editor shared by terminal and keyboard input adapters.
class LineEditor {
public:
    static constexpr std::size_t maximumLength = 1024;

    LineEditorAction handleKey(const input::KeyEvent& event);
    bool insertRawCharacter(char character, bool allowTab = false);

    const std::string& text() const noexcept { return text_; }
    std::size_t cursor() const noexcept { return cursor_; }
    const std::string& previousLine() const noexcept { return previousLine_; }

    void setText(std::string text);
    void rememberCurrentLine();
    void clear() noexcept;

private:
    std::string text_;
    std::string previousLine_;
    std::size_t cursor_ = 0;
};

} // namespace ql32::ui
