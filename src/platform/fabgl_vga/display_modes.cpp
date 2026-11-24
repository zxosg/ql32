#include "ql32/platform/fabgl_vga/display_modes.hpp"
#include "ql32/platform/fabgl_vga/mode_memory.hpp"

#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace ql32::platform::fabgl_vga {
namespace {

constexpr std::uint32_t kDoubleBufferedOption = 1u;
constexpr const char* kSupportedModes =
    "; supported modes (internal DMA memory permitting): "
    "0 (320x200), 1 (320x240), 2 (400x300), 3 (640x400), 4 (640x480)";
static_assert(kVgaModeMemoryReserve >= FABGLIB_MINFREELARGESTBLOCK,
              "viewport probe must preserve FabGL's per-block reserve");

struct ModePreset {
    const char* name;
    const char* modeline;
    std::uint32_t width;
    std::uint32_t height;
    std::uint16_t refreshHz;
};

constexpr std::array<ModePreset, 5> presets{{
    {"320x200", VGA_320x200_70Hz, 320, 200, 70},
    {"320x240", QVGA_320x240_60Hz, 320, 240, 60},
    {"400x300", VGA_400x300_60Hz, 400, 300, 60},
    {"640x400", VGA_640x400_70Hz, 640, 400, 70},
    {"640x480", VGA_640x480_60Hz, 640, 480, 60},
}};

std::string normalized(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value)
        result.push_back(static_cast<char>(std::toupper(ch)));
    return result;
}

std::uint32_t scanoutOverhead(const ModePreset& preset, bool doubleBuffered) {
    fabgl::VGATimings timings{};
    if (!fabgl::VGABaseController::convertModelineToTimings(preset.modeline, &timings))
        throw std::runtime_error("invalid VGA preset modeline");
    const auto frameHeight = timings.VVisibleArea + timings.VFrontPorch +
                             timings.VSyncPulse + timings.VBackPorch;
    auto descriptors = timings.scanCount * (frameHeight + preset.height);
    if (timings.HStartingBlock == fabgl::VGAScanStart::Sync ||
        timings.HStartingBlock == fabgl::VGAScanStart::BackPorch)
        descriptors += timings.scanCount * preset.height;
    const auto lineBytes = timings.HVisibleArea + timings.HFrontPorch +
                           timings.HSyncPulse + timings.HBackPorch;
    const auto buffers = doubleBuffered ? 2u : 1u;
    return (descriptors * buffers + 1u) * sizeof(lldesc_t) +
           2u * lineBytes +
           (preset.height * buffers + FABGLIB_VIEWPORT_MEMORY_POOL_COUNT + 1u) *
               sizeof(std::uint8_t*);
}

bool probeViewportAllocation(const ModePreset& preset, bool doubleBuffered) {
    struct Probe {
        void* overhead = nullptr;
        std::array<void*, FABGLIB_VIEWPORT_MEMORY_POOL_COUNT> pools{};
        ~Probe() {
            for (auto pool = pools.rbegin(); pool != pools.rend(); ++pool)
                if (*pool) heap_caps_free(*pool);
            if (overhead) heap_caps_free(overhead);
        }
    } probe;
    probe.overhead = heap_caps_malloc(scanoutOverhead(preset, doubleBuffered),
                                     MALLOC_CAP_DMA);
    if (!probe.overhead) return false;
    auto remaining = preset.height * (doubleBuffered ? 2u : 1u);
    for (auto& pool : probe.pools) {
        if (remaining == 0) return true;
        const auto rows = vgaAllocatableRows(preset.width, remaining,
            static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
        if (rows == 0) break;
        pool = heap_caps_malloc(rows * preset.width, MALLOC_CAP_DMA);
        if (!pool) break;
        remaining -= rows;
    }
    return remaining == 0;
}

} // namespace

DisplayModes::DisplayModes(fabgl::VGAController& display, fabgl::Canvas& canvas)
    : display_(display), canvas_(canvas) {}

std::vector<runtime::DisplayMode> DisplayModes::availableModes() const {
    std::vector<runtime::DisplayMode> result;
    result.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        result.push_back({preset.name, preset.width, preset.height,
                          preset.refreshHz, static_cast<std::uint32_t>(index), 0});
    }
    return result;
}

std::string DisplayModes::currentMode() const {
    return presets[current_].name;
}

runtime::DisplayMode DisplayModes::describe(std::size_t index) const {
    const auto& preset = presets.at(index);
    return {preset.name,
            static_cast<std::uint32_t>(std::max(0, canvas_.getWidth())),
            static_cast<std::uint32_t>(std::max(0, canvas_.getHeight())),
            preset.refreshHz, static_cast<std::uint32_t>(index), currentOptions_};
}

runtime::DisplayMode DisplayModes::currentDisplayMode() const {
    return describe(current_);
}

void DisplayModes::apply(std::size_t index, bool doubleBuffered) {
    switch (index) {
    case 0: display_.setResolution(VGA_320x200_70Hz, -1, -1, doubleBuffered); break;
    case 1: display_.setResolution(QVGA_320x240_60Hz, -1, -1, doubleBuffered); break;
    case 2: display_.setResolution(VGA_400x300_60Hz, -1, -1, doubleBuffered); break;
    case 3: display_.setResolution(VGA_640x400_70Hz, -1, -1, doubleBuffered); break;
    case 4: display_.setResolution(VGA_640x480_60Hz, -1, -1, doubleBuffered); break;
    default: throw std::out_of_range("unknown VGA display mode");
    }
}

runtime::DisplayMode DisplayModes::setMode(std::string_view name) {
    const auto key = normalized(name);
    const auto selected = std::find_if(presets.begin(), presets.end(), [&key](const auto& preset) {
        return normalized(preset.name) == key;
    });
    if (selected == presets.end())
        throw std::invalid_argument("unsupported VGA mode: " + std::string(name) +
                                    kSupportedModes);
    const auto next = static_cast<std::size_t>(selected - presets.begin());
    return setMode(static_cast<std::uint32_t>(next), currentOptions_);
}

runtime::DisplayMode DisplayModes::setMode(std::uint32_t id,
                                           std::uint32_t options) {
    if (id >= presets.size())
        throw std::invalid_argument("unsupported VGA display mode id: " +
                                    std::to_string(id) + kSupportedModes);
    if ((options & ~kDoubleBufferedOption) != 0)
        throw std::invalid_argument(
            std::string("unsupported VGA display option bits; use 0 (single) or 1 (double)") +
            kSupportedModes);
    // On this Olimex target FabGL's partially allocated 640x480 double buffer
    // corrupts its viewport allocation when the next resolution is selected.
    if (id == 4 && (options & kDoubleBufferedOption) != 0)
        throw std::invalid_argument(
            std::string("640x480 double buffering is unavailable on this VGA target; use option 0") +
            kSupportedModes);
    // This service is constructed as a global before setup() configures the
    // controller. Reconcile the tracked option with the live controller before
    // deciding that a requested mode is already active.
    currentOptions_ = display_.isDoubleBufferedEnabled()
        ? kDoubleBufferedOption : 0;
    const auto next = static_cast<std::size_t>(id);
    const auto previous = current_;
    const auto previousOptions = currentOptions_;
    if (next != previous || options != previousOptions) {
        const auto freeDma = static_cast<std::uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_DMA));
        const auto currentBytes = vgaModeBytes(
            static_cast<std::uint32_t>(std::max(0, canvas_.getWidth())),
            static_cast<std::uint32_t>(std::max(0, canvas_.getHeight()))) *
            ((previousOptions & kDoubleBufferedOption) != 0 ? 2u : 1u);
        const bool doubleBuffered = (options & kDoubleBufferedOption) != 0;
        if (!vgaModeFitsMemory(presets[next].width, presets[next].height,
                               currentBytes, freeDma, doubleBuffered))
            throw std::runtime_error(
                std::string("not enough memory for VGA mode ") +
                presets[next].name + ": needs " +
                std::to_string(vgaModeBytes(presets[next].width, presets[next].height) *
                               (doubleBuffered ? 2u : 1u)) +
                " framebuffer bytes plus " + std::to_string(kVgaModeMemoryReserve) +
                " reserve, " + std::to_string(freeDma) + " free DMA bytes and " +
                std::to_string(currentBytes) + " reclaimable; PSRAM cannot supply scanout" +
                kSupportedModes);
        try {
            if (vgaModeBytes(presets[next].width, presets[next].height) *
                    (doubleBuffered ? 2u : 1u) > currentBytes) {
                // Total free DMA RAM includes fragments FabGL cannot use:
                // each pool leaves FABGLIB_MINFREELARGESTBLOCK untouched.
                // Probe complete row-pool allocation after releasing the old
                // viewport. Never let FabGL build pointers for partial buffers.
                canvas_.waitCompletion(false);
                display_.end();
                const auto largest = static_cast<std::uint32_t>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
                const auto overhead = scanoutOverhead(presets[next], doubleBuffered);
                if (!probeViewportAllocation(presets[next], doubleBuffered))
                    throw std::runtime_error(
                        std::string("not enough contiguous DMA memory for VGA mode ") +
                        presets[next].name + ": largest block " + std::to_string(largest) +
                        ", framebuffer " +
                        std::to_string(vgaModeBytes(presets[next].width, presets[next].height) *
                                       (doubleBuffered ? 2u : 1u)) +
                        ", scanout overhead " + std::to_string(overhead) +
                        ", reserve " + std::to_string(kVgaModeMemoryReserve) +
                        "; PSRAM cannot supply scanout" + kSupportedModes);
            }
            apply(next, (options & kDoubleBufferedOption) != 0);
            const auto& preset = presets[next];
            const auto allocatedWidth = canvas_.getWidth();
            const auto allocatedHeight = canvas_.getHeight();
            if (allocatedWidth != static_cast<int>(preset.width) ||
                allocatedHeight != static_cast<int>(preset.height)) {
                throw std::runtime_error(
                    "VGA framebuffer allocation incomplete for " +
                    std::string(preset.name) + ": allocated " +
                    std::to_string(allocatedWidth) + "x" +
                    std::to_string(allocatedHeight) + ", required " +
                    std::to_string(preset.width) + "x" +
                    std::to_string(preset.height) + kSupportedModes);
            }
            if ((options & kDoubleBufferedOption) != 0 &&
                !display_.isDoubleBuffered())
                throw std::runtime_error("VGA double buffering could not be enabled");
            current_ = next;
            currentOptions_ = options;
        } catch (const std::exception& error) {
            try {
                apply(previous, (previousOptions & kDoubleBufferedOption) != 0);
                if (canvas_.getWidth() != static_cast<int>(presets[previous].width) ||
                    canvas_.getHeight() != static_cast<int>(presets[previous].height) ||
                    display_.isDoubleBufferedEnabled() !=
                        ((previousOptions & kDoubleBufferedOption) != 0))
                    throw std::runtime_error("previous VGA viewport was not fully restored");
                current_ = previous;
                currentOptions_ = previousOptions;
            } catch (const std::exception& rollbackError) {
                throw std::runtime_error(std::string(error.what()) +
                    "; VGA rollback failed: " + rollbackError.what());
            }
            throw;
        }
    }
    return describe(current_);
}

} // namespace ql32::platform::fabgl_vga
