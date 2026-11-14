#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/olimex_vga/canvas_raster_ops.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireSame(const ql32::graphics::IndexedCanvas& expected,
                 ql32::graphics::IndexedCanvas& actual,
                 const char* message) {
    auto expectedView = const_cast<ql32::graphics::IndexedCanvas&>(expected).bufferView();
    auto actualView = actual.bufferView();
    require(expected.width() == actual.width() &&
            expected.height() == actual.height() &&
            expected.storageBytes() == actual.storageBytes(), message);
    require(std::memcmp(expectedView.pixels, actualView.pixels,
                        expected.storageBytes()) == 0, message);
}

void testCanvasOperationsMatchPortableFallback() {
    using namespace ql32::graphics;
    ql32::platform::olimex_vga::CanvasRasterOps ops;
    IndexedCanvas oddWidthPortable(31, 23, PixelDepth::Four);
    IndexedCanvas oddWidthFallback(31, 23, PixelDepth::Four, &ops);
    require(oddWidthFallback.bufferView().stride == 0,
            "odd-width packed canvas exposed an invalid row stride");
    oddWidthPortable.clear();
    oddWidthFallback.clear();
    oddWidthPortable.rectangle({-2, 3}, 19, 11, true);
    oddWidthFallback.rectangle({-2, 3}, 19, 11, true);
    oddWidthPortable.line({1, 2}, {28, 19});
    oddWidthFallback.line({1, 2}, {28, 19});
    oddWidthPortable.circle({15, 11}, 7, false);
    oddWidthFallback.circle({15, 11}, 7, false);
    oddWidthPortable.scrollUp(2);
    oddWidthFallback.scrollUp(2);
    requireSame(oddWidthPortable, oddWidthFallback,
                "odd-width fallback changed packed-canvas pixels");

    IndexedCanvas portable(32, 24, PixelDepth::Four);
    IndexedCanvas accelerated(32, 24, PixelDepth::Four, &ops);

    portable.setInk(9);
    accelerated.setInk(9);
    portable.setPaper(2);
    accelerated.setPaper(2);
    portable.clear();
    accelerated.clear();
    requireSame(portable, accelerated, "accelerated clear changed indexed pixels");

    portable.rectangle({-3, 2}, 19, 11, true);
    accelerated.rectangle({-3, 2}, 19, 11, true);
    requireSame(portable, accelerated, "accelerated clipped fill changed indexed pixels");

    portable.line({1, 1}, {28, 20});
    accelerated.line({1, 1}, {28, 20});
    requireSame(portable, accelerated, "accelerated line changed indexed pixels");

    portable.line({-5, 4}, {29, 18});
    accelerated.line({-5, 4}, {29, 18});
    requireSame(portable, accelerated, "clipped line fallback changed indexed pixels");

    portable.circle({15, 11}, 8, false);
    accelerated.circle({15, 11}, 8, false);
    requireSame(portable, accelerated, "accelerated circle changed indexed pixels");

    for (const auto circle : {
             std::tuple<ql32::graphics::Point, std::int32_t, bool>{{15, 11}, 7, true},
             {{-2, 11}, 8, true},
             {{30, 22}, 9, true},
             {{0, 0}, 0, true}}) {
        portable.circle(std::get<0>(circle), std::get<1>(circle), std::get<2>(circle));
        accelerated.circle(std::get<0>(circle), std::get<1>(circle), std::get<2>(circle));
        requireSame(portable, accelerated,
                    "accelerated filled circle changed packed canvas pixels");
    }

    portable.setOverMode(OverMode::Clear);
    accelerated.setOverMode(OverMode::Clear);
    portable.rectangle({4, 6}, 17, 9, true);
    accelerated.rectangle({4, 6}, 17, 9, true);
    portable.line({3, 3}, {27, 17});
    accelerated.line({3, 3}, {27, 17});
    portable.circle({15, 11}, 6, false);
    accelerated.circle({15, 11}, 6, false);
    portable.circle({15, 11}, 5, true);
    accelerated.circle({15, 11}, 5, true);
    requireSame(portable, accelerated, "accelerated clear-over operations differ");

    portable.setOverMode(OverMode::Invert);
    accelerated.setOverMode(OverMode::Invert);
    portable.line({2, 2}, {26, 18});
    accelerated.line({2, 2}, {26, 18});
    portable.circle({15, 11}, 5, false);
    accelerated.circle({15, 11}, 5, false);
    requireSame(portable, accelerated, "invert-mode fallback changed indexed pixels");

    portable.setOverMode(OverMode::Set);
    accelerated.setOverMode(OverMode::Set);
    portable.scrollUp(3);
    accelerated.scrollUp(3);
    requireSame(portable, accelerated, "accelerated scroll changed indexed pixels");
}

void testWindowedServiceUsesInjectedRasterOps() {
    using namespace ql32::graphics;
    auto ops = std::make_shared<ql32::platform::olimex_vga::CanvasRasterOps>();
    WindowedService portable(64, 40, nullptr);
    WindowedService accelerated(64, 40, nullptr, ops);

    for (auto* service : {&portable, &accelerated}) {
        service->setInk(1, 11);
        service->setPaper(1, 3);
        service->clear(1, 1);
        service->fillRectangle(1, 1, {5, 4}, 43, 27);
        service->line(1, 1, {2, 1}, {57, 35});
        service->circle(1, 1, {32, 20}, 11);
    }
    const auto portableWindow = portable.windowForSession(1);
    const auto acceleratedWindow = accelerated.windowForSession(1);
    auto& expected = portable.windowManager().canvas(portableWindow);
    auto& actual = accelerated.windowManager().canvas(acceleratedWindow);
    requireSame(expected, actual,
                "WindowedService raster operations differ from portable output");
}

} // namespace

int main() {
    testCanvasOperationsMatchPortableFallback();
    testWindowedServiceUsesInjectedRasterOps();
}
