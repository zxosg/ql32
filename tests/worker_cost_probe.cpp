// Host probe (not a pass/fail test): live heap bytes of one scheduler worker.
// Counts every global operator new/delete with malloc_size so std::vector,
// std::string and std::make_unique are all included.
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/vm/scheduler.hpp"

#include <cstdio>
#include <cstdlib>
#include <malloc/malloc.h>
#include <new>
#include <sstream>

namespace {
std::size_t liveBytes = 0;
std::size_t peakBytes = 0;
std::size_t allocations = 0;

void* track(void* p) {
    if (!p) throw std::bad_alloc();
    liveBytes += malloc_size(p);
    if (liveBytes > peakBytes) peakBytes = liveBytes;
    ++allocations;
    return p;
}
} // namespace

void* operator new(std::size_t n) { return track(std::malloc(n ? n : 1)); }
void* operator new[](std::size_t n) { return track(std::malloc(n ? n : 1)); }
void operator delete(void* p) noexcept {
    if (p) liveBytes -= malloc_size(p);
    std::free(p);
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

namespace {
class NullPresenter final : public ql32::graphics::FramePresenter {
public:
    void present(const ql32::graphics::Frame&) override {}
};

struct Program { const char* name; const char* source; };

const Program programs[] = {
    {"mandel",
     "PAPER 0\nINK 7\nGUPDATE 1\nCLS\n"
     "FOR py = 0 TO 55\n cy = (28 - py) / 21\n FOR px = 0 TO 79\n"
     "  cx = (px - 40) / 22 - 0.5\n  zx = 0\n  zy = 0\n  iter = 0\n"
     "  FOR it = 1 TO 24\n   IF zx * zx + zy * zy <= 4 THEN\n"
     "    temp = zx * zx - zy * zy + cx\n    zy = 2 * zx * zy + cy\n"
     "    zx = temp\n    iter = iter + 1\n   END IF\n  END FOR it\n"
     "  INK iter MOD 7 + 1\n  RECT px * 3, py * 3, 3, 3\n END FOR px\n"
     " GUPDATE\nEND FOR py\nPAUSE 600000\n"},
    {"sincos",
     "PAPER 0\nINK 7\nGUPDATE 1\nCLS\n"
     "FOR k = 0 TO 400\n FOR gy = 0 TO 15\n  FOR gx = 0 TO 15\n"
     "   INK 1 + (gx + gy + k) MOD 7\n"
     "   RECT gx * 18 + 8 * SIN((gx + k) / 5), gy * 14 + 6 * COS((gy + k) / 5), 8, 8\n"
     "  END FOR gx\n END FOR gy\n GUPDATE\nEND FOR k\n"},
    {"churn",
     "DIM vals(255)\nn = 0\nREPeat churnLoop\n n = n + 1\n s$ = \"\"\n"
     " FOR i = 1 TO 20\n  s$ = s$ + \"x\" + STR$(i)\n END FOR i\n"
     " FOR i = 0 TO 255\n  vals(i) = i * n\n END FOR i\nEND REPeat churnLoop\n"},
};
} // namespace

int main() {
    std::printf("program,graphics,baseline,after_launch,first_slice,steady_state,peak,"
                "launch_delta,first_slice_delta,steady_delta\n");
    for (const bool withGraphics : {false, true}) {
        for (const auto& program : programs) {
            ql32::vm::Services services;
            std::shared_ptr<ql32::graphics::WindowedService> graphics;
            if (withGraphics) {
                graphics = std::make_shared<ql32::graphics::WindowedService>(
                    320, 240, std::make_shared<NullPresenter>());
                services.graphics = graphics;
            }
            ql32::vm::SessionScheduler scheduler(std::move(services), 16);
            std::ostringstream output;
            const auto baseline = liveBytes;
            peakBytes = liveBytes;
            const auto id = scheduler.launchWorker(program.source, output, program.name);
            const auto afterLaunch = liveBytes;
            scheduler.tick();
            const auto firstSlice = liveBytes;
            for (int i = 0; i < 200; ++i) scheduler.tick();
            const auto steady = liveBytes;
            std::printf("%s,%d,%zu,%zu,%zu,%zu,%zu,%td,%td,%td\n", program.name,
                        withGraphics, baseline, afterLaunch, firstSlice, steady,
                        peakBytes, afterLaunch - baseline, firstSlice - baseline,
                        steady - baseline);
            scheduler.cancel(id);
            scheduler.reap(id);
        }
    }
}
