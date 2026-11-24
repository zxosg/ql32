#include "ql32/ui/line_damage.hpp"

#include <cassert>

int main() {
    using ql32::ui::lineDamage;
    auto d = lineDamage("> ABC", "> ABCD", 40, true, 5, true, 6);
    assert(d.first == 5 && d.last == 7); // appended character and moved caret
    d = lineDamage("> ABCD", "> ABXD", 40, true, 4, true, 5);
    assert(d.first == 4 && d.last == 6);
    d = lineDamage("> ABCD", "> ABD", 40, true, 4, true, 3);
    assert(d.first == 3 && d.last == 6); // deletion shifts the suffix
    d = lineDamage("> ABC", "> ABC", 40, true, 5, true, 2);
    assert(d.first == 2 && d.last == 6);
    d = lineDamage("> ABC", "> ABC", 40, true, 5, true, 5);
    assert(d.empty());
    d = lineDamage("> ABC", "> ABC", 40, false, 5, true, 5);
    assert(d.first == 5 && d.last == 6);
    d = lineDamage("ABCDEF", "BCDEFG", 5, true, 4, true, 4);
    assert(d.first == 0 && d.last == 5); // horizontal viewport shift
    d = lineDamage("A", "A ", 5, false, 0, false, 0);
    assert(d.empty()); // an added blank cell does not alter pixels
}
