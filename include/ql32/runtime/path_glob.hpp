#pragma once

#include <cctype>
#include <string>

namespace ql32::runtime {

inline bool hasWildcard(const std::string& text) {
    return text.find_first_of("*?") != std::string::npos;
}

// Case-insensitive match supporting '*' (any run) and '?' (any one character).
inline bool wildcardMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    const auto same = [](char a, char b) {
        return std::toupper(static_cast<unsigned char>(a)) ==
               std::toupper(static_cast<unsigned char>(b));
    };
    while (t < text.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' || (pattern[p] != '*' && same(pattern[p], text[t])))) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace ql32::runtime
