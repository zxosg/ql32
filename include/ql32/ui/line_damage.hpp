#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace ql32::ui {

struct LineDamage {
    std::size_t first = 0;
    std::size_t last = 0; // exclusive
    bool empty() const noexcept { return first == last; }
};

// Return the character cells whose content or caret appearance changed.
inline LineDamage lineDamage(std::string_view before, std::string_view after,
                             std::size_t columns,
                             bool oldCaretVisible, std::size_t oldCaret,
                             bool newCaretVisible, std::size_t newCaret) noexcept {
    std::size_t first = columns, last = 0;
    const auto mark = [&](std::size_t column) {
        if (column >= columns) return;
        first = std::min(first, column);
        last = std::max(last, column + 1);
    };
    const auto length = std::min(columns, std::max(before.size(), after.size()));
    for (std::size_t column = 0; column < length; ++column) {
        const char a = column < before.size() ? before[column] : ' ';
        const char b = column < after.size() ? after[column] : ' ';
        if (a != b) mark(column);
    }
    if (oldCaretVisible != newCaretVisible ||
        (oldCaretVisible && newCaretVisible && oldCaret != newCaret)) {
        if (oldCaretVisible) mark(oldCaret);
        if (newCaretVisible) mark(newCaret);
    }
    return last == 0 ? LineDamage{} : LineDamage{first, last};
}

} // namespace ql32::ui
