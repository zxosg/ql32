#include "ql32/platform/olimex_vga/present_budget.hpp"
#include "ql32/platform/olimex_vga/present_stats.hpp"

#include <cassert>

int main() {
    using ql32::platform::olimex_vga::PresentStats;
    PresentStats s;
    s.recordAccepted(640, 480, 640, 480, 100, 5000, 2);
    s.recordAccepted(640, 480, 640, 16, 10, 200, 1);
    s.recordRejected();
    assert(s.calls == 3 && s.rejectedCalls == 1);
    assert(s.fullWindowCalls == 1 && s.partialCalls == 1);
    assert(s.rows == 496);
    assert(s.pixels == 640ull * 480 + 640ull * 16);
    assert(s.totalUs == 5102 + 211);
    assert(s.beginUs == 110 && s.rowsUs == 5200 && s.publishUs == 3);
    assert(s.maxUs == 5102 && s.maxRows == 480);
    ql32::platform::olimex_vga::PresentBudget b(1000);
    assert(!b.charge(5, 600));
    assert(!b.exhausted(5));
    assert(b.charge(5, 400));
    assert(b.exhausted(5));
    assert(!b.exhausted(6));
    assert(!b.charge(6, 999));
    b.setLimit(0);
    assert(!b.charge(6, 100000) && !b.exhausted(6));
}
