#include "ql32/interpreter.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/ui/service.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic<long> failAllocationAfter{-1};

bool shouldFailAllocation() noexcept {
    auto remaining = failAllocationAfter.load(std::memory_order_relaxed);
    while (remaining >= 0) {
        if (remaining == 0) {
            if (failAllocationAfter.compare_exchange_weak(
                    remaining, -1, std::memory_order_relaxed))
                return true;
        } else if (failAllocationAfter.compare_exchange_weak(
                       remaining, remaining - 1, std::memory_order_relaxed)) {
            return false;
        }
    }
    return false;
}
} // namespace

void* operator new(std::size_t size) {
    if (shouldFailAllocation()) throw std::bad_alloc();
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using namespace ql32::graphics;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Event {
    std::string name;
    ql32::graphics::SessionId session;
    ql32::graphics::Point first{};
    ql32::graphics::Point second{};
    int a = 0;
    int b = 0;
    std::string text;
    ql32::graphics::ChannelId channel = 1;
    bool filled = false;
};

class CapturingPresenter final : public ql32::graphics::FramePresenter {
public:
    void present(const ql32::graphics::Frame& frame) override {
        lastFrame = frame;
        ++presentCount;
    }
    ql32::graphics::Frame lastFrame;
    std::size_t presentCount = 0;
};

class RecordingGraphics final : public ql32::graphics::Service {
public:
    void setInk(SessionId session, ColorIndex color) override {
        events.push_back({"INK", session, {}, {}, color});
    }
    void setInk(SessionId session, ChannelId channel, ColorIndex color) override {
        events.push_back({"INK", session, {}, {}, color, 0, {}, channel});
    }
    void setPaper(SessionId session, ColorIndex color) override {
        events.push_back({"PAPER", session, {}, {}, color});
    }
    void setPaper(SessionId session, ChannelId channel, ColorIndex color) override {
        events.push_back({"PAPER", session, {}, {}, color, 0, {}, channel});
    }
    void line(SessionId session, Point from, Point to) override {
        events.push_back({"LINE", session, from, to});
    }
    void line(SessionId session, ChannelId channel, Point from, Point to) override {
        line(session, from, to);
        events.back().channel = channel;
    }
    void pixel(SessionId session, ChannelId channel, Point position) override {
        events.push_back({"PLOT", session, position, {}, 0, 0, {}, channel});
    }
    void rectangle(SessionId session, Point topLeft, std::int32_t width,
                   std::int32_t height) override {
        events.push_back({"RECT", session, topLeft, {}, width, height});
    }
    void circle(SessionId session, Point center, std::int32_t radius) override {
        events.push_back({"CIRCLE", session, center, {}, radius});
    }
    void circle(SessionId session, ChannelId channel, Point center,
                std::int32_t radius) override {
        circle(session, center, radius);
        events.back().channel = channel;
    }
    void fillRectangle(SessionId session, ChannelId channel, Point topLeft,
                       std::int32_t width, std::int32_t height) override {
        events.push_back({"RECT", session, topLeft, {}, width, height, {}, channel, true});
    }
    void fillCircle(SessionId session, ChannelId channel, Point center,
                    std::int32_t radius) override {
        events.push_back({"CIRCLE", session, center, {}, radius, 0, {}, channel, true});
    }
    void text(SessionId session, Point topLeft, const std::string& value) override {
        events.push_back({"TEXT", session, topLeft, {}, 0, 0, value});
    }
    void writeText(SessionId session, ChannelId channel,
                   const std::string& value) override {
        events.push_back({"PRINT_TEXT", session, {}, {}, 0, 0, value, channel});
    }
    void beginTextOutputBatch(SessionId session, ChannelId channel) override {
        events.push_back({"PRINT_BATCH_BEGIN", session, {}, {}, 0, 0, {}, channel});
    }
    void endTextOutputBatch(SessionId session, ChannelId channel) override {
        events.push_back({"PRINT_BATCH_END", session, {}, {}, 0, 0, {}, channel});
    }
    bool setTextPosition(SessionId session, ChannelId channel,
                         std::uint32_t row, std::uint32_t column) override {
        events.push_back({"AT", session,
            {static_cast<std::int32_t>(row), static_cast<std::int32_t>(column)},
            {}, 0, 0, {}, channel});
        return true;
    }
    void clear(SessionId session) override {
        events.push_back({"CLS", session});
    }
    void clear(SessionId session, ChannelId channel) override {
        events.push_back({"CLS", session, {}, {}, 0, 0, {}, channel});
    }
    void closeWindow(SessionId session) override {
        events.push_back({"CLOSEWINDOW", session});
    }
    void closeWindow(SessionId session, ChannelId channel) override {
        events.push_back({"CLOSEWINDOW", session, {}, {}, 0, 0, {}, channel});
    }
    void closeSession(SessionId session) override {
        events.push_back({"CLOSE_SESSION", session});
    }
    void setUpdateMode(SessionId session, UpdateMode mode) override {
        const char* name = mode == UpdateMode::Off ? "GUPDATE_OFF" :
                           mode == UpdateMode::On ? "GUPDATE_ON" : "GUPDATE_AUTO";
        events.push_back({name, session});
    }
    void setFrameSkip(SessionId session, std::uint32_t frames) override {
        events.push_back({"GUPDATE_SKIP", session, {}, {},
                          static_cast<int>(frames)});
    }
    void update(SessionId session) override { events.push_back({"GUPDATE", session}); }
    WindowInfo windowInfo(SessionId) override { return info; }
    RgbColor paletteColor(SessionId, std::uint16_t index) override {
        if (index == 2) return {255, 0, 0};
        if (index == 7) return {255, 255, 255};
        throw std::out_of_range("palette index");
    }
    void setRotation(SessionId, std::uint16_t degrees) override {
        info.rotationDegrees = degrees;
        events.push_back({"ROTATE", 1, {}, {}, static_cast<int>(degrees)});
    }
    void setWindowBounds(SessionId session, Rect bounds) override {
        info.originX = bounds.x;
        info.originY = bounds.y;
        events.push_back({"WINDOW", session, {bounds.x, bounds.y}, {},
                          static_cast<int>(bounds.width),
                          static_cast<int>(bounds.height)});
    }
    void setWindowBounds(SessionId session, ChannelId channel, Rect bounds) override {
        if (channel == 1) setWindowBounds(session, bounds);
        else events.push_back({"WINDOW", session, {bounds.x, bounds.y}, {},
                              static_cast<int>(bounds.width),
                              static_cast<int>(bounds.height)});
        events.back().channel = channel;
    }

    std::vector<Event> events;
    WindowInfo info{320, 240, 16, 8, 90};
};

class ListingStorage final : public ql32::runtime::StorageService {
public:
    std::string readText(const std::string&) override { return {}; }
    void writeText(const std::string&, const std::string&) override {}
    std::vector<ql32::runtime::FileEntry> list(const std::string&) override {
        return {{"demo.bas", 0, false}};
    }
    bool exists(const std::string&) override { return false; }
    std::uintmax_t size(const std::string&) override { return 0; }
    void rename(const std::string&, const std::string&) override {}
    void remove(const std::string&) override {}
};

class BitmapTestStorage final : public ql32::runtime::StorageService {
public:
    explicit BitmapTestStorage(std::string image) : image_(std::move(image)) {}
    std::string readText(const std::string&) override { return {}; }
    std::unique_ptr<ql32::runtime::StorageReadHandle> openRead(
            const std::string& path) override {
        if (path != "sprite.qimg") throw std::runtime_error("file not found");
        return std::make_unique<ql32::runtime::MemoryStorageReadHandle>(image_);
    }
    void writeText(const std::string&, const std::string&) override {}
    std::vector<ql32::runtime::FileEntry> list(const std::string&) override { return {}; }
    bool exists(const std::string& path) override { return path == "sprite.qimg"; }
    std::uintmax_t size(const std::string&) override { return image_.size(); }
    void rename(const std::string&, const std::string&) override {}
    void remove(const std::string&) override {}
private:
    std::string image_;
};

std::string makeTestQimg(std::uint16_t width, std::uint16_t height,
                         const std::vector<std::uint8_t>& packedRows) {
    const auto stride = static_cast<std::uint16_t>((width + 1u) / 2u);
    std::string bytes(64, '\0');
    bytes[0] = 'Q'; bytes[1] = 'I'; bytes[2] = 'M'; bytes[3] = 'G';
    bytes[4] = 2; bytes[5] = 4;
    bytes[6] = static_cast<char>(width & 0xff);
    bytes[7] = static_cast<char>(width >> 8);
    bytes[8] = static_cast<char>(height & 0xff);
    bytes[9] = static_cast<char>(height >> 8);
    bytes[10] = static_cast<char>(stride & 0xff);
    bytes[11] = static_cast<char>(stride >> 8);
    const auto payloadSize = static_cast<std::uint32_t>(packedRows.size());
    for (std::size_t i = 0; i < 4; ++i)
        bytes[12 + i] = static_cast<char>(payloadSize >> (8 * i));
    constexpr std::array<std::array<std::uint8_t, 3>, 16> palette{{
        {{0, 0, 0}}, {{0, 0, 255}}, {{255, 0, 0}}, {{255, 0, 255}},
        {{0, 255, 0}}, {{0, 255, 255}}, {{255, 255, 0}}, {{255, 255, 255}},
        {{85, 85, 85}}, {{85, 85, 255}}, {{255, 85, 85}}, {{255, 85, 255}},
        {{85, 255, 85}}, {{85, 255, 255}}, {{255, 255, 85}}, {{192, 192, 192}}
    }};
    for (std::size_t index = 0; index < palette.size(); ++index) {
        for (std::size_t component = 0; component < palette[index].size(); ++component)
            bytes[16 + index * 3 + component] = static_cast<char>(palette[index][component]);
    }
    for (const auto byte : packedRows) bytes.push_back(static_cast<char>(byte));
    return bytes;
}

std::string makeTestQfnt() {
    constexpr std::size_t headerSize = 26;
    constexpr std::size_t indexEntrySize = 13;
    constexpr std::uint32_t indexOffset = headerSize;
    constexpr std::uint32_t bitmapOffset = headerSize + 95 * indexEntrySize;
    std::vector<std::uint8_t> bytes(headerSize, 0);
    bytes[0] = 'Q'; bytes[1] = 'F'; bytes[2] = 'N'; bytes[3] = 'T';
    bytes[4] = 2; bytes[5] = headerSize; bytes[6] = 5; bytes[7] = 8;
    bytes[8] = 7; bytes[9] = 32; bytes[10] = 95;
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    };
    put32(14, indexOffset);
    put32(18, bitmapOffset);
    constexpr std::size_t glyphBytes = 7;
    put32(22, bitmapOffset + 95 * glyphBytes);
    for (std::uint8_t character = 32; character <= 126; ++character) {
        const auto record = bytes.size();
        bytes.resize(record + indexEntrySize, 0);
        bytes[record] = character;
        bytes[record + 1] = 5;
        bytes[record + 2] = 7;
        bytes[record + 3] = 5;
        put32(record + 7, bitmapOffset + (character - 32) * glyphBytes);
        bytes[record + 11] = glyphBytes;
    }
    for (std::uint8_t character = 32; character <= 126; ++character) {
        for (std::size_t index = 0; index < glyphBytes; ++index)
            bytes.push_back(index == 0 ? 0x80 : 0);
    }
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct FontReadStats {
    std::string contents;
    std::size_t bytesRead = 0;
};

class CountingFontReadHandle final : public ql32::runtime::StorageReadHandle {
public:
    explicit CountingFontReadHandle(std::shared_ptr<FontReadStats> stats)
        : stats_(std::move(stats)) {}
    std::uint64_t size() const noexcept override { return stats_->contents.size(); }
    std::size_t readAt(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t capacity) override {
        if (offset >= stats_->contents.size() || capacity == 0) return 0;
        const auto count = std::min<std::size_t>(capacity,
            stats_->contents.size() - static_cast<std::size_t>(offset));
        std::memcpy(destination, stats_->contents.data() + offset, count);
        stats_->bytesRead += count;
        return count;
    }
private:
    std::shared_ptr<FontReadStats> stats_;
};

class BitmapFontStorage final : public ql32::runtime::StorageService {
public:
    explicit BitmapFontStorage(std::string contents)
        : stats_(std::make_shared<FontReadStats>()) {
        stats_->contents = std::move(contents);
    }
    std::string readText(const std::string&) override {
        ++readTextCalls;
        throw std::runtime_error("font loading must not use whole-file readText");
    }
    std::unique_ptr<ql32::runtime::StorageReadHandle> openRead(
            const std::string&) override {
        ++openReadCalls;
        return std::make_unique<CountingFontReadHandle>(stats_);
    }
    void writeText(const std::string&, const std::string&) override {}
    std::vector<ql32::runtime::FileEntry> list(const std::string&) override { return {}; }
    bool exists(const std::string&) override { return true; }
    std::uintmax_t size(const std::string&) override { return stats_->contents.size(); }
    void rename(const std::string&, const std::string&) override {}
    void remove(const std::string&) override {}
private:
    std::shared_ptr<FontReadStats> stats_;
public:
    std::size_t readTextCalls = 0;
    std::size_t openReadCalls = 0;
    std::size_t bytesRead() const noexcept { return stats_->bytesRead; }
    std::size_t fileSize() const noexcept { return stats_->contents.size(); }
};

void testDrawingCallsAndSessionOwnership() {
    auto graphics = std::make_shared<RecordingGraphics>();
    std::ostringstream output;
    ql32::graphics::SessionId session = 0;
    {
        ql32::Interpreter interpreter({}, {}, {}, graphics);
        interpreter.execute(
            "INK 7\n"
            "PAPER 1\n"
            "LINE 1,2,30,40\n"
            "PLOT #1,3,4\n"
            "RECT 5,6,20,10\n"
            "RECT #1,7,8,6,5,1\n"
            "CIRCLE 50,60,8\n"
            "CIRCLE 60,70,9,1\n"
            "TEXT 2,3,\"hello\"\n"
            "CLS\n", output);
        require(graphics->events.size() == 10, "drawing calls were not dispatched");
        session = graphics->events.front().session;
        require(session != 0, "graphics calls used an invalid session");
        require(graphics->events[0].name == "INK" && graphics->events[0].a == 7,
                "INK arguments were not preserved");
        require(graphics->events[1].name == "PAPER" && graphics->events[1].a == 1,
                "PAPER arguments were not preserved");
        require(graphics->events[2].first.x == 1 && graphics->events[2].first.y == 2 &&
                    graphics->events[2].second.x == 30 && graphics->events[2].second.y == 40,
                "LINE coordinates were not preserved");
        require(graphics->events[3].name == "PLOT" && graphics->events[3].channel == 1 &&
                    graphics->events[3].first.x == 3 && graphics->events[3].first.y == 4,
                "PLOT coordinates were not preserved");
        require(graphics->events[4].a == 20 && graphics->events[4].b == 10 &&
                    !graphics->events[4].filled,
                "RECT dimensions were not preserved");
        require(graphics->events[5].name == "RECT" && graphics->events[5].channel == 1 &&
                    graphics->events[5].filled && graphics->events[5].a == 6 &&
                    graphics->events[5].b == 5,
                "channel-qualified filled RECT arguments were not preserved");
        require(graphics->events[6].a == 8 && !graphics->events[6].filled,
                "CIRCLE outline radius was not preserved");
        require(graphics->events[7].a == 9 && graphics->events[7].filled,
                "filled CIRCLE was not dispatched");
        require(graphics->events[8].text == "hello", "TEXT value was not preserved");
        require(graphics->events[9].name == "CLS", "CLS was not dispatched");
    }
    require(graphics->events.back().name == "CLOSE_SESSION" &&
                graphics->events.back().session == session,
            "interpreter destruction did not release its graphics session");
}

void testGraphicsUpdateCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    std::ostringstream output;
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    interpreter.execute(
        "CONST KGupdateOff = 0\n"
        "CONST KGupdateOn = 1\n"
        "CONST KGupdateAuto = 1\n"
        "GUPDATE KGupdateOff\n"
        "CIRCLE 2,3,1\n"
        "GUPDATE\n"
        "GUPDATE KGupdateOn\n"
        "GUPDATE KGupdateAuto\n", output);
    require(graphics->events.size() == 5,
            "GUPDATE or drawing command dispatch count was incorrect");
    require(graphics->events[0].name == "GUPDATE_OFF" &&
                graphics->events[1].name == "CIRCLE" &&
                graphics->events[2].name == "GUPDATE" &&
                graphics->events[3].name == "GUPDATE_AUTO" &&
                graphics->events[4].name == "GUPDATE_AUTO",
            "GUPDATE modes or explicit flush were dispatched in the wrong order");
}

void testGupdateOffSuppressesVmDrawingUntilExplicitUpdate() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(32, 24, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "GUPDATE 0\nPLOT 2,2\nPRINT \"pending\"\n", output);
    require(presenter->presentCount == 0,
            "VM GUPDATE 0 presented later drawing commands automatically");
    interpreter.execute("GUPDATE\n", output);
    require(presenter->presentCount == 1,
            "bare GUPDATE did not present the retained frame after GUPDATE 0");
    interpreter.execute("GUPDATE 1\nPLOT 4,4\n", output);
    require(presenter->presentCount == 3,
            "GUPDATE 1 did not restore automatic presentation");
}

void testProgrammableGlobalPalette() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(16, 8, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "PRINT #0,RGB(18,52,86)\n"
        "GUPDATE 0\nPALETTE 1,RGB(18,52,86),RGB(171,205,239)\n"
        "PRINT #0,GPALETTE$(1)\nPRINT #0,GPALETTE$(2)\n", output);
    require(output.str() == "1193046\n#123456\n#ABCDEF\n",
            "RGB packing or PALETTE readback was incorrect");
    require(presenter->presentCount == 0,
            "PALETTE bypassed GUPDATE 0 presentation suppression");
    interpreter.execute("GUPDATE\n", output);
    require(presenter->presentCount == 1 &&
                presenter->lastFrame.palette[1].red == 0x12 &&
                presenter->lastFrame.palette[1].green == 0x34 &&
                presenter->lastFrame.palette[1].blue == 0x56 &&
                presenter->lastFrame.palette[2].red == 0xAB &&
                presenter->lastFrame.palette[2].green == 0xCD &&
                presenter->lastFrame.palette[2].blue == 0xEF,
            "palette range was not reflected in the composed frame");

    require(graphics->paletteColor(2, 1).red == 0x12 &&
                graphics->paletteColor(2, 2).blue == 0xEF,
            "palette changes were not shared across graphics sessions");

    bool outOfRangeRejected = false;
    try {
        interpreter.execute("PALETTE 15,16777215,0\n", output);
    } catch (const std::runtime_error& error) {
        outOfRangeRejected = std::string(error.what()).find("palette range") !=
                             std::string::npos;
    }
    require(outOfRangeRejected, "PALETTE accepted a range past the active palette");

    bool invalidColorRejected = false;
    try {
        interpreter.execute("PALETTE 1,16777216\n", output);
    } catch (const std::runtime_error& error) {
        invalidColorRejected = std::string(error.what()).find("RGB color") !=
                               std::string::npos;
    }
    require(invalidColorRejected, "PALETTE accepted an RGB value above 24 bits");

    bool invalidComponentRejected = false;
    try {
        interpreter.execute("PRINT RGB(256,0,0)\n", output);
    } catch (const std::runtime_error& error) {
        invalidComponentRejected = std::string(error.what()).find("RGB components") !=
                                   std::string::npos;
    }
    require(invalidComponentRejected, "RGB accepted a component above 255");
}

void testProgramControlledPaletteTransforms() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(16, 8, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "GUPDATE 0\n"
        "PALETTE 1,RGB(16,32,48),RGB(64,80,96),RGB(112,128,144)\n"
        "PALROT 1,3,1\n"
        "PRINT #0,GPALETTE$(1)\nPRINT #0,GPALETTE$(2)\nPRINT #0,GPALETTE$(3)\n"
        "PALSHIFT 1,3,1,RGB(170,187,204)\n"
        "PRINT #0,GPALETTE$(1)\nPRINT #0,GPALETTE$(2)\nPRINT #0,GPALETTE$(3)\n"
        "PALSHIFT 1,3,-1,RGB(1,2,3)\n"
        "PALFADE 1,2,16777215,0.5\n"
        "PRINT #0,GPALETTE$(1)\nPRINT #0,GPALETTE$(2)\nPRINT #0,GPALETTE$(3)\n", output);
    require(output.str() ==
                "#708090\n#102030\n#405060\n"
                "#AABBCC\n#708090\n#102030\n"
                "#B8C0C8\n#889098\n#010203\n",
            "palette rotate, shift, or fade produced unexpected colors");
    require(presenter->presentCount == 0,
            "palette transforms bypassed GUPDATE 0 suppression");
    interpreter.execute("GUPDATE\n", output);
    require(presenter->presentCount == 1 &&
                presenter->lastFrame.palette[1].red == 0xB8 &&
                presenter->lastFrame.palette[2].blue == 0x98,
            "palette transform result was not presented by GUPDATE");

    const auto rejects = [&](const std::string& command, const std::string& expected) {
        try {
            interpreter.execute(command, output);
        } catch (const std::runtime_error& error) {
            return std::string(error.what()).find(expected) != std::string::npos;
        }
        return false;
    };
    require(rejects("PALROT 15,2,1\n", "palette range"),
            "PALROT accepted a range beyond the palette");
    require(rejects("PALSHIFT 1,2,0,0\n", "direction must be -1 or 1"),
            "PALSHIFT accepted a zero direction");
    require(rejects("PALFADE 1,2,0,1.1\n", "fade amount"),
            "PALFADE accepted an amount above one");
}

void testIndexedBitmapLoadBlitAndRelease() {
    auto storage = std::make_shared<BitmapTestStorage>(
        makeTestQimg(3, 2, {0x12, 0x30, 0x40, 0x60}));
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(24, 16, presenter);
    ql32::Interpreter interpreter(storage, {}, {}, graphics);
    std::ostringstream output;

    interpreter.execute(
        "image = LOADIMAGE(\"sprite.qimg\")\n"
        "INK 9\n"
        "BLIT image,4,5,0\n", output);

    const auto windows = graphics->windowManager().compositionOrder();
    require(windows.size() == 1, "bitmap draw created unexpected graphics windows");
    const auto& canvas = graphics->windowManager().canvas(windows.front().id);
    require(canvas.pixel(4, 5) == 1 && canvas.pixel(5, 5) == 2 &&
                canvas.pixel(6, 5) == 3,
            "QIMG high-nibble-first pixels were not blitted in row order");
    require(canvas.pixel(4, 6) == 4 && canvas.pixel(5, 6) == 0 &&
                canvas.pixel(6, 6) == 6,
            "odd-width QIMG row or transparent color was rendered incorrectly");
    require(presenter->presentCount == 1,
            "BLIT did not coalesce row updates into one presentation");
    interpreter.execute("PLOT 0,0\n", output);
    require(canvas.pixel(0, 0) == 9,
            "BLIT failed to restore the caller's graphics ink");

    interpreter.execute("UNLOADIMAGE image\n", output);
    bool unloadedRejected = false;
    try {
        interpreter.execute("BLIT image,0,0\n", output);
    } catch (const std::exception& error) {
        unloadedRejected = std::string(error.what()).find("unknown bitmap handle") !=
                           std::string::npos;
    }
    require(unloadedRejected, "BLIT accepted a released image handle");

    auto malformedStorage = std::make_shared<BitmapTestStorage>(
        makeTestQimg(3, 2, {0x12, 0x30, 0x40}));
    ql32::Interpreter malformed(malformedStorage);
    bool malformedRejected = false;
    try {
        malformed.execute("image = LOADIMAGE(\"sprite.qimg\")\n", output);
    } catch (const std::runtime_error& error) {
        malformedRejected = std::string(error.what()).find("QIMG stride or payload") !=
                            std::string::npos;
    }
    require(malformedRejected, "LOADIMAGE accepted a truncated QIMG payload");

    auto remappedQimg = makeTestQimg(3, 2, {0x12, 0x30, 0x40, 0x60});
    remappedQimg[16 + 3] = static_cast<char>(255);
    remappedQimg[16 + 4] = 0;
    remappedQimg[16 + 5] = 0;
    auto remappedStorage = std::make_shared<BitmapTestStorage>(remappedQimg);
    auto remappedGraphics = std::make_shared<WindowedService>(
        24, 16, std::make_shared<CapturingPresenter>());
    ql32::Interpreter remapped(remappedStorage, {}, {}, remappedGraphics);
    remapped.execute("image = LOADIMAGE(\"sprite.qimg\")\nBLIT image,0,0\n", output);
    const auto remappedWindows = remappedGraphics->windowManager().compositionOrder();
    const auto& remappedCanvas = remappedGraphics->windowManager().canvas(
        remappedWindows.front().id);
    require(remappedCanvas.pixel(0, 0) == 2,
            "QIMG embedded palette color was not mapped to the closest display color");

    auto legacyQimg = makeTestQimg(3, 2, {0x12, 0x30, 0x40, 0x60});
    legacyQimg[4] = 1;
    auto legacyStorage = std::make_shared<BitmapTestStorage>(legacyQimg);
    ql32::Interpreter legacy(legacyStorage);
    bool legacyRejected = false;
    try {
        legacy.execute("image = LOADIMAGE(\"sprite.qimg\")\n", output);
    } catch (const std::runtime_error& error) {
        legacyRejected = std::string(error.what()).find("invalid QIMG header") !=
                         std::string::npos;
    }
    require(legacyRejected, "QIMG v1 was accepted after the format moved to v2");
}

void testSuperBasicLineCursorForms() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(24, 16, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "INK 2\n"
        "LINE 2,3\n"
        "LINE TO 5,6\n"
        "LINE 8,9 TO 11,12\n"
        "LINE 1,1,3,1\n"
        "PRINT #0,GX()\nPRINT #0,GY()\n", output);

    const auto windows = graphics->windowManager().compositionOrder();
    require(windows.size() == 1, "LINE forms created unexpected graphics windows");
    const auto session = windows.front().owner;
    auto& defaultCanvas = graphics->windowManager().canvas(windows.front().id);
    require(defaultCanvas.pixel(2, 3) == 2 && defaultCanvas.pixel(5, 6) == 2 &&
                defaultCanvas.pixel(8, 9) == 2 && defaultCanvas.pixel(11, 12) == 2 &&
                defaultCanvas.pixel(1, 1) == 2 && defaultCanvas.pixel(3, 1) == 2,
            "cursor-based LINE forms or the original coordinate form drew incorrectly");
    const auto cursor = graphics->graphicsPosition(session, 1);
    require(cursor.x == 3 && cursor.y == 1,
            "LINE did not retain the endpoint as the graphics cursor");
    require(output.str() == "3\n1\n",
            "GX or GY did not report the current graphics cursor");
    graphics->setGraphicsPosition(session, 2, {4, 4});
    const auto channelCursor = graphics->graphicsPosition(session, 2);
    require(channelCursor.x == 4 && channelCursor.y == 4,
            "graphics cursors were not stored independently by channel");
}

void testGraphicsUpdateSkipCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("GUPDATE SKIP,1\nGUPDATE SKIP,0\n", output);
    require(graphics->events.size() == 2 &&
                graphics->events[0].name == "GUPDATE_SKIP" &&
                graphics->events[0].a == 1 &&
                graphics->events[1].name == "GUPDATE_SKIP" &&
                graphics->events[1].a == 0,
            "GUPDATE SKIP did not dispatch its frame counts");

    bool rejected = false;
    try {
        interpreter.execute("GUPDATE SKIP,1.5\n", output);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("nonnegative 32-bit integer") !=
                   std::string::npos;
    }
    require(rejected, "GUPDATE SKIP accepted a fractional frame count");
}

void testUiInteractionResetsGraphicsUpdateStateAndKeepsIt() {
    auto graphics = std::make_shared<RecordingGraphics>();
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, ui, graphics);
    std::ostringstream output;

    const auto verifyReset = [&](const std::string& source) {
        graphics->events.clear();
        const auto status = interpreter.executeResumable(
            "GUPDATE 0\nGUPDATE SKIP,2\n" + source, output);
        require(status == ql32::ExecutionStatus::Suspended,
                "UI request did not suspend its BASIC session");
        require(graphics->events.size() >= 5 &&
                    graphics->events[graphics->events.size() - 3].name == "GUPDATE_SKIP" &&
                    graphics->events[graphics->events.size() - 3].a == 0 &&
                    graphics->events[graphics->events.size() - 2].name == "GUPDATE_AUTO" &&
                    graphics->events.back().name == "GUPDATE",
                "UI interaction did not reset and present the normal update state");
        const auto request = ui->takeNextRequest();
        require(request.has_value(), "UI interaction did not enqueue its request");
        const bool resolved = request->kind == ql32::ui::RequestKind::Input ?
            ui->submitInput(request->id, "ok") :
            ui->resolve(request->id, ql32::ui::ResultKind::Cancelled);
        if (!resolved)
            throw std::runtime_error("UI request kind " +
                std::to_string(static_cast<unsigned>(request->kind)) +
                " could not be resolved in the test");
        const auto beforeResume = graphics->events.size();
        (void)interpreter.resume(output);
        require(graphics->events.size() == beforeResume,
                "graphics update state was reverted after the UI interaction");
    };

    verifyReset("ALERT \"Notice\",\"Message\"\n");
    verifyReset("INPUT value$\n");
    verifyReset("dialogHandle = DINIT(\"Dialog\")\n"
                "DTEXT dialogHandle,\"Fields\"\n"
                "DEDIT dialogHandle,\"Name\",\"Initial\"\n"
                "accepted = DIALOG(dialogHandle)\n");
    verifyReset("menuHandle = MINIT(\"Menu\")\n"
                "MCARD menuHandle,1,\"One\",\"1\"\n"
                "selected = MENU(menuHandle)\n");
}

void testWindowedGraphicsUpdateBatching() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.setInk(1, 2);
    graphics.circle(1, {4, 5}, 0);
    require(presenter->presentCount == 0,
            "drawing was presented while GUPDATE was OFF");

    graphics.update(1);
    require(presenter->presentCount == 1,
            "GUPDATE did not force one composed-screen presentation");
    require(presenter->lastFrame.pixels[5 * 10 + 4] == 2,
            "forced GUPDATE did not present retained drawing changes");

    graphics.circle(1, {6, 5}, 0);
    require(presenter->presentCount == 1,
            "manual GUPDATE unexpectedly enabled automatic updates");
    graphics.setUpdateMode(1, UpdateMode::On);
    require(presenter->presentCount == 2 && presenter->lastFrame.pixels[5 * 10 + 6] == 2,
            "GUPDATE ON did not restore updates and present pending drawing");

    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.circle(1, {8, 5}, 0);
    graphics.setUpdateMode(1, UpdateMode::Auto);
    require(presenter->presentCount == 3 && presenter->lastFrame.pixels[5 * 10 + 8] == 2,
            "GUPDATE AUTO did not restore default automatic presentation");
}

void testWindowedGraphicsCoalescesPresentationsByTimeAndCost() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    std::uint64_t now = 1000;
    graphics.setPresentCoalescing(500, 0, [&now] { return now; });
    graphics.circle(1, {1, 1}, 0);
    require(presenter->presentCount == 1, "first request must present at once");
    now += 100;
    graphics.circle(1, {3, 3}, 0);
    graphics.circle(1, {5, 5}, 0);
    require(presenter->presentCount == 1 && graphics.hasPendingPresentation(),
            "requests inside the interval must be merged, not presented");
    graphics.pollPendingPresentation();
    require(presenter->presentCount == 1, "poll must respect the interval");
    now += 500;
    graphics.pollPendingPresentation();
    require(presenter->presentCount == 2 && !graphics.hasPendingPresentation() &&
                presenter->lastFrame.pixels[5 * 10 + 5] == 7,
            "poll after the interval must publish the latest merged state");
    require(graphics.coalesceStats().requests == 3 &&
                graphics.coalesceStats().deferred == 2 &&
                graphics.coalesceStats().presented == 2,
            "coalescing counters");
    now += 600;
    graphics.circle(1, {7, 7}, 0);
    graphics.circle(1, {8, 8}, 0);
    graphics.flushPendingPresentation();
    require(presenter->presentCount == 4,
            "flush must publish pending output immediately");
    require(!graphics.hasPendingPresentation(), "flush must clear pending output");
    now += 10000;
    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.circle(1, {4, 4}, 0);
    const auto before = presenter->presentCount;
    graphics.update(1);
    graphics.update(1);
    require(presenter->presentCount <= before + 1 &&
                graphics.coalesceStats().deferred >= 3,
            "explicit GUPDATE must obey the rate limit");
    graphics.flushPendingPresentation();
    graphics.setPresentCoalescing(0, 0, {});
    graphics.setUpdateMode(1, UpdateMode::Auto);
    graphics.circle(1, {2, 2}, 0);
    require(!graphics.hasPendingPresentation(), "disabled coalescing must not defer");
}

void testWindowedGraphicsSkipsAutomaticPresentations() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setFrameSkip(1, 1);
    graphics.circle(1, {1, 1}, 0);
    graphics.circle(1, {3, 3}, 0);
    require(presenter->presentCount == 1,
            "GUPDATE SKIP 1 did not skip one automatic presentation");
    graphics.circle(1, {5, 5}, 0);
    require(presenter->presentCount == 2 &&
                presenter->lastFrame.pixels[3 * 10 + 3] == 7,
            "GUPDATE SKIP did not present the latest retained canvas state");

    graphics.update(1);
    require(presenter->presentCount == 2,
            "bare GUPDATE did not honor the active frame-skip cadence");
    graphics.update(1);
    require(presenter->presentCount == 3,
            "bare GUPDATE did not resume presentation after the skipped request");
    graphics.setFrameSkip(1, 0);
    graphics.setUpdateMode(1, UpdateMode::Auto);
    graphics.circle(1, {7, 7}, 0);
    require(presenter->presentCount == 5,
            "GUPDATE SKIP 0 did not restore presentation on every request");
}

void testWindowedGraphicsResetUpdateStatePresentsAndDisablesSkip() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.setFrameSkip(1, 2);
    graphics.circle(1, {3, 3}, 0);
    require(presenter->presentCount == 0,
            "test setup unexpectedly presented while updates were off");

    graphics.resetUpdateState(1);
    require(presenter->presentCount == 1,
            "resetting graphics update state did not present the current frame");
    graphics.circle(1, {4, 4}, 0);
    require(presenter->presentCount == 2,
            "reset graphics update state did not restore automatic presentation");
    graphics.update(1);
    require(presenter->presentCount == 3,
            "reset graphics update state did not disable frame skipping");
}

void testPresentationBatchCoalescesResetAndPageUpdate() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.circle(1, {3, 3}, 0);
    require(presenter->presentCount == 0,
            "presentation batch test setup unexpectedly presented");

    graphics.beginPresentationBatch(1);
    graphics.resetUpdateState(1);
    graphics.circle(1, {4, 4}, 0);
    require(presenter->presentCount == 0,
            "presentation batch published an intermediate frame");
    graphics.endPresentationBatch(1);
    require(presenter->presentCount == 1 &&
                presenter->lastFrame.pixels[3 * 10 + 3] == 7 &&
                presenter->lastFrame.pixels[4 * 10 + 4] == 7,
            "presentation batch did not publish the composed screen once");

    graphics.circle(1, {5, 5}, 0);
    require(presenter->presentCount == 2,
            "presentation batch did not restore automatic updates");
}

void testPresentationBatchPreservesConcurrentDrawingAcrossResize() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setWindowBounds(1, {0, 0, 4, 4});
    graphics.setWindowBounds(2, {5, 0, 4, 4});
    graphics.setInk(1, 2);
    graphics.setInk(2, 4);
    const auto baseline = presenter->presentCount;

    graphics.beginPresentationBatch(1);
    graphics.pixel(1, 1, {1, 1});
    graphics.beginPresentationBatch(2);
    graphics.pixel(2, 1, {1, 1});
    graphics.endPresentationBatch(2);
    require(graphics.setDisplaySize(12, 12), "batched resize failed");
    require(presenter->presentCount == baseline,
            "resize or another session presented while batched");
    graphics.endPresentationBatch(1);
    require(presenter->presentCount == baseline + 1 &&
                presenter->lastFrame.width == 12 &&
                presenter->lastFrame.height == 12 &&
                presenter->lastFrame.pixels[1 * 12 + 1] == 2 &&
                presenter->lastFrame.pixels[1 * 12 + 6] == 4,
            "batched resize lost a session's retained drawing");
}

void testDrawingArgumentValidation() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    bool invalidColor = false;
    try {
        interpreter.execute("INK 8\n", output);
    } catch (const std::runtime_error& error) {
        invalidColor = std::string(error.what()).find("color index") != std::string::npos;
    }
    require(invalidColor, "invalid palette index was accepted");
    bool invalidCoordinates = false;
    try {
        interpreter.execute("LINE 1,2,2147483648,4\n", output);
    } catch (const std::runtime_error& error) {
        invalidCoordinates = std::string(error.what()).find("pixel coordinate") !=
                             std::string::npos;
    }
    require(invalidCoordinates, "out-of-range pixel coordinate was accepted");
    bool invalidFill = false;
    try {
        interpreter.execute("RECT 1,2,3,4,2\n", output);
    } catch (const std::runtime_error& error) {
        invalidFill = std::string(error.what()).find("fill flag") != std::string::npos;
    }
    require(invalidFill, "RECT accepted a fill flag other than 0 or 1");
    bool missingArguments = false;
    try {
        interpreter.execute("CIRCLE 1,2,3,1,0\n", output);
    } catch (const std::runtime_error& error) {
        missingArguments = std::string(error.what()).find("CIRCLE expects") != std::string::npos;
    }
    require(missingArguments, "CIRCLE accepted too many arguments");
    require(graphics->events.empty(), "invalid drawing call reached the service");
}

void testFractionalDrawingGeometryFloorsToPixels() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "WINDOW -0.2,1.9,20.9,12.4\n"
        "PLOT 0.9,-0.2\n"
        "LINE 1.9,2.1,3.8,4.9\n"
        "RECT 5.9,6.1,7.9,8.9\n"
        "CIRCLE 9.9,10.1,2.9\n"
        "TEXT 11.9,12.9,\"fractional\"\n", output);

    require(graphics->events.size() == 6,
            "fractional drawing geometry was not dispatched");
    require(graphics->events[0].name == "WINDOW" &&
                graphics->events[0].first.x == -1 && graphics->events[0].first.y == 1 &&
                graphics->events[0].a == 20 && graphics->events[0].b == 12,
            "WINDOW geometry did not floor to integer pixels");
    require(graphics->events[1].name == "PLOT" &&
                graphics->events[1].first.x == 0 && graphics->events[1].first.y == -1,
            "PLOT coordinates did not floor to integer pixels");
    require(graphics->events[2].first.x == 1 && graphics->events[2].first.y == 2 &&
                graphics->events[2].second.x == 3 && graphics->events[2].second.y == 4,
            "LINE coordinates did not floor to integer pixels");
    require(graphics->events[3].name == "RECT" &&
                graphics->events[3].first.x == 5 && graphics->events[3].first.y == 6 &&
                graphics->events[3].a == 7 && graphics->events[3].b == 8,
            "RECT geometry did not floor to integer pixels");
    require(graphics->events[4].name == "CIRCLE" &&
                graphics->events[4].first.x == 9 && graphics->events[4].first.y == 10 &&
                graphics->events[4].a == 2,
            "CIRCLE geometry did not floor to integer pixels");
    require(graphics->events[5].name == "TEXT" &&
                graphics->events[5].first.x == 11 && graphics->events[5].first.y == 12,
            "TEXT coordinates did not floor to integer pixels");

    bool zeroSizeRejected = false;
    try {
        interpreter.execute("RECT 0,0,0.9,2\n", output);
    } catch (const std::runtime_error& error) {
        zeroSizeRejected = std::string(error.what()).find("size must be positive") !=
                           std::string::npos;
    }
    require(zeroSizeRejected && graphics->events.size() == 6,
            "fractional RECT size flooring to zero was not rejected");
}

void testClsChannelAndColorSyntax() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "CLS\n"
        "CLS #0\n"
        "CLS #1,2,3\n"
        "CLS 4,5\n"
        "CHANNEL #0\n"
        "CLS 6,7\n"
        "CLOSEWINDOW #0\n", output);
    require(graphics->events.size() == 12 &&
                graphics->events[0].name == "CLS" && graphics->events[0].channel == 1 &&
                graphics->events[1].name == "CLS" && graphics->events[1].channel == 0 &&
                graphics->events[2].name == "INK" && graphics->events[2].channel == 1 &&
                graphics->events[2].a == 2 &&
                graphics->events[3].name == "PAPER" && graphics->events[3].channel == 1 &&
                graphics->events[3].a == 3 &&
                graphics->events[4].name == "CLS" && graphics->events[4].channel == 1 &&
                graphics->events[5].name == "INK" && graphics->events[5].channel == 1 &&
                graphics->events[5].a == 4 &&
                graphics->events[6].name == "PAPER" && graphics->events[6].channel == 1 &&
                graphics->events[6].a == 5 &&
                graphics->events[7].name == "CLS" && graphics->events[7].channel == 1 &&
                graphics->events[8].name == "INK" && graphics->events[8].channel == 0 &&
                graphics->events[8].a == 6 &&
                graphics->events[9].name == "PAPER" && graphics->events[9].channel == 0 &&
                graphics->events[9].a == 7 &&
                graphics->events[10].name == "CLS" && graphics->events[10].channel == 0 &&
                graphics->events[11].name == "CLOSEWINDOW" &&
                graphics->events[11].channel == 0,
            "CLS channel selection or optional ink/paper colors were incorrect");
}

void testClsColorArgumentsClearWithNewPaper() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(12, 10, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("INK 1\nPLOT 3,4\nCLS 2,3\n", output);
    const auto states = graphics->windowManager().compositionOrder();
    require(states.size() == 1, "CLS did not create the default graphics window");
    const auto& canvas = graphics->windowManager().canvas(states.front().id);
    require(canvas.ink() == 2 && canvas.paper() == 3 &&
                canvas.pixel(3, 4) == 3 && canvas.pixel(0, 0) == 3,
            "CLS ink,paper did not set drawing colors and clear to the paper color");
}

void testPixelAndFilledShapesRenderOnRetainedCanvas() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(16, 16, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "INK 2\n"
        "PLOT 1,1\n"
        "RECT 2,2,5,5,1\n"
        "RECT 2,8,5,5,0\n"
        "CIRCLE 10,5,2,1\n"
        "CIRCLE 10,12,2\n", output);
    const auto& frame = presenter->lastFrame;
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
    };
    require(pixel(1, 1) == 2, "PLOT did not set one pixel to the current ink");
    require(pixel(4, 4) == 2, "filled RECT did not draw its interior");
    require(pixel(4, 8) == 2 && pixel(4, 9) == 0,
            "RECT outline and fill=0 behavior were incorrect");
    require(pixel(10, 5) == 2 && pixel(10, 12) == 0,
            "filled and outline CIRCLE behavior was incorrect");
}

void testFontSelectionChangesNativeTextCellSize() {
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 4> dimensions{{
        {5, 8}, {6, 12}, {8, 16}, {12, 24}}};
    for (std::size_t index = 0; index < dimensions.size(); ++index) {
        const auto& font = BitmapFont::builtIn(static_cast<FontHandle>(index + 1));
        require(font.metrics().maxAdvance == dimensions[index].first &&
                    font.metrics().lineHeight == dimensions[index].second,
                "bundled Spleen face has incorrect native dimensions");
        for (std::uint8_t character = BitmapFont::FirstCharacter;
             character < BitmapFont::FirstCharacter + BitmapFont::CharacterCount;
             ++character) {
            FontGlyph glyph{};
            require(font.readGlyph(character, glyph) &&
                        glyph.character == character &&
                        glyph.advance == dimensions[index].first,
                    "built-in QFNT glyph index or advance is incorrect");
        }
    }

    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(32, 64, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("GFONT #1,4,0\nTEXT 0,0,\"A\"\n", output);

    bool inkBelowLegacyCell = false;
    for (std::uint32_t y = 8; y < 24; ++y)
        for (std::uint32_t x = 0; x < 12; ++x)
            inkBelowLegacyCell |= presenter->lastFrame.pixels[
                static_cast<std::size_t>(y) * presenter->lastFrame.width + x] != 0;
    require(inkBelowLegacyCell,
            "GFONT did not render text at the selected native height");

    bool invalidHandle = false;
    try {
        interpreter.execute("FONT #1,9999,0\n", output);
    } catch (const std::runtime_error& error) {
        invalidHandle = std::string(error.what()).find("font handle") != std::string::npos;
    }
    require(invalidHandle, "unknown font handle was accepted");
}

void testTextAndGraphicsFontStateAndStyles() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(32, 32, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "INK 7\nPAPER 3\n"
        "FONT #1,4,0\n"
        "GFONT #1,4,3\n"
        "TEXT #1,0,0,\"A\"\n", output);

    const auto states = graphics->windowManager().compositionOrder();
    require(states.size() == 1, "font styles created an unexpected graphics window");
    const auto& canvas = graphics->windowManager().canvas(states.front().id);
    const auto& title = BitmapFont::builtIn(4).metrics();
    const auto underlineY = std::min<std::uint32_t>(title.lineHeight - 1,
                                                    title.ascent + 1);
    require(canvas.pixel(0, underlineY) == 7 && canvas.pixel(11, 0) != 3,
            "GFONT bold+underline or glyph-only paper behavior was incorrect");

    interpreter.execute("FONT #1,4,3\nCLS\nPRINT #1,\" \";\n", output);
        require(canvas.pixel(11, 0) == 3,
            "normal PRINT did not fill the complete font cell with paper");
        require(canvas.pixel(0, underlineY) == 7,
            "normal PRINT did not rasterize the underline style");
}

void testDefaultFontHandleResetsSelections() {
    FontRegistry fonts;
    fonts.selectText(1, 4, 0);
    fonts.selectGraphics(1, 4, 0);
    fonts.resetText(1);
    fonts.resetGraphics(1);
    require(fonts.textSelection(1).handle == 0 &&
                fonts.graphicsSelection(1).handle == 0,
            "font handle zero did not clear the registry selection");

    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(32, 32, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "FONT #1,4,0\nGFONT #1,4,0\n"
        "FONT #1,0,0\nGFONT #1,0,0\n"
        "PRINT #1,\"default\"\nTEXT #1,0,16,\"default\"\n", output);
}

void testLoadFontIsBoundedAndChannelScoped() {
    auto storage = std::make_shared<BitmapFontStorage>(makeTestQfnt());
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(24, 24, presenter);
    ql32::Interpreter interpreter(storage, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("fontId = LOADFONT(\"fonts/test.qfnt\")\n", output);
    require(storage->bytesRead() == BitmapFont::HeaderSize +
                BitmapFont::CharacterCount * BitmapFont::GlyphIndexEntrySize &&
                storage->bytesRead() < storage->fileSize(),
            "LOADFONT read bitmap payloads instead of only bounded metadata and index");
    interpreter.execute(
        "FONT #1,fontId,3\n"
        "GFONT #0,fontId,1\n"
        "PRINT #1,\"?\"\n"
        "TEXT #0,0,10,\"?\"\n"
        "PRINT #0,GETFONT(0,fontId)\n"
        "PRINT #0,GETFONT(2,fontId)\n", output);

    const auto windows = graphics->windowManager().compositionOrder();
    require(windows.size() == 2, "shared font handle did not render on two channels");
    const auto& printSurface = graphics->windowManager().canvas(
        graphics->windowForSession(interpreter.graphicsSessionId()));
    require(printSurface.pixel(0, 0) == 7 && output.str() == "5\n7\n",
            "LOADFONT, GETFONT, or selected-font PRINT failed");
    bool graphicsChannelRendered = false;
    for (const auto& state : windows) {
        if (state.id != graphics->windowForSession(interpreter.graphicsSessionId()))
            graphicsChannelRendered = graphics->windowManager().canvas(state.id).pixel(0, 10) == 7;
    }
    require(graphicsChannelRendered,
            "one loaded handle was not shared with graphical TEXT on a second channel");
    require(storage->openReadCalls == 1 && storage->readTextCalls == 0,
            "font loading copied data through whole-file readText");

    bool unloadInUseRejected = false;
    try {
        interpreter.execute("UNLOADFONT fontId\n", output);
    } catch (const std::runtime_error& error) {
        unloadInUseRejected = std::string(error.what()).find("selected") !=
                              std::string::npos;
    }
    require(unloadInUseRejected, "UNLOADFONT accepted a font used by a channel");
    interpreter.execute("FONT #1,4,0\nGFONT #0,4,0\nUNLOADFONT fontId\n", output);

    const std::string unsupported("\xc3\xa9", 2);
    std::size_t offset = 0;
    require(ql32::graphics::nextAsciiCharacter(unsupported, offset) == '?' &&
                offset == unsupported.size(),
            "unsupported UTF-8 text was not mapped to one replacement character");

    bool builtInUnloadRejected = false;
    try {
        interpreter.execute("UNLOADFONT 1\n", output);
    } catch (const std::runtime_error& error) {
        builtInUnloadRejected = std::string(error.what()).find("built-in") !=
                                std::string::npos;
    }
    require(builtInUnloadRejected, "UNLOADFONT accepted a built-in font handle");

    for (std::string invalidFont : {std::string("QFNT"), makeTestQfnt()}) {
        if (invalidFont.size() > BitmapFont::HeaderSize)
            invalidFont[BitmapFont::HeaderSize] = 0;
        auto invalidStorage = std::make_shared<BitmapFontStorage>(std::move(invalidFont));
        FontRegistry registry(invalidStorage);
        bool invalidRejected = false;
        try {
            (void)registry.load("invalid.qfnt");
        } catch (const std::invalid_argument&) {
            invalidRejected = true;
        }
        require(invalidRejected && invalidStorage->readTextCalls == 0,
                "malformed QFNT header or index was accepted");
    }
}

void testDisplayCommands() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "PRINT #0,GWIDTH()\nPRINT #0,GHEIGHT()\nPRINT #0,GDEPTH()\n"
        "PRINT #0,GROTATION()\nPRINT #0,GPALETTESIZE()\nPRINT #0,GPALETTE$(2)\n"
        "ROTATE 270\nPRINT #0,GROTATION()\n", output);
    require(output.str() == "320\n240\n16\n90\n8\n#FF0000\n270\n",
            "display queries or rotation command returned incorrect values");
    bool invalidPalette = false;
    try {
        interpreter.execute("PRINT #0,GPALETTE$(8)\n", output);
    } catch (const std::runtime_error& error) {
        invalidPalette = std::string(error.what()).find("palette index") != std::string::npos;
    }
    require(invalidPalette, "out-of-range palette index was accepted");

    bool parenthesizedCommandRejected = false;
    try {
        interpreter.execute("ROTATE(90)\n", output);
    } catch (const std::runtime_error&) {
        parenthesizedCommandRejected = true;
    }
    require(parenthesizedCommandRejected,
            "built-in command accepted function-call parentheses");
    bool functionParenthesesRequired = false;
    try {
        interpreter.execute("PRINT GWIDTH\n", output);
    } catch (const std::runtime_error& error) {
        functionParenthesesRequired = std::string(error.what()).find("requires parentheses") !=
                                      std::string::npos;
    }
    require(functionParenthesesRequired,
            "built-in function was accepted without parentheses");
}

void testWindowPositionCommand() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("WINDOW 21,34,123,95\n", output);
    require(graphics->events.size() == 1 && graphics->events[0].name == "WINDOW",
            "WINDOW command was not dispatched");
    const auto& event = graphics->events[0];
    require(event.first.x == 21 && event.first.y == 34 &&
                event.a == 123 && event.b == 95,
            "WINDOW bounds were not preserved");
    interpreter.execute("WINDOW #0,7,8,80,60\n", output);
    require(graphics->events.back().name == "WINDOW" &&
                graphics->events.back().channel == 0 &&
                graphics->events.back().first.x == 7 &&
                graphics->events.back().first.y == 8 &&
                graphics->events.back().a == 80 && graphics->events.back().b == 60,
            "channel-qualified WINDOW did not target channel #0");
    interpreter.execute("PRINT #0,GORIGINX()\nPRINT #0,GORIGINY()\n", output);
    require(output.str() == "21\n34\n",
            "window screen-origin queries returned incorrect coordinates");

    bool invalidSize = false;
    try {
        interpreter.execute("WINDOW 0,0,0,100\n", output);
    } catch (const std::runtime_error& error) {
        invalidSize = std::string(error.what()).find("window size") != std::string::npos;
    }
    require(invalidSize, "zero-width window was accepted");

    bool parenthesizedCommandRejected = false;
    try {
        interpreter.execute("WINDOW(1,2,30,40)\n", output);
    } catch (const std::runtime_error&) {
        parenthesizedCommandRejected = true;
    }
    require(parenthesizedCommandRejected,
            "WINDOW accepted function-call parentheses");
}

void testPrintChannelRouting() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.execute("PRINT #1,\"hello\"\nPRINT #0,\"console\"\n", console);
    require(console.str() == "console\n", "PRINT #0 did not target the console");
    const auto windowText = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "hello"; });
    require(windowText != graphics->events.end(), "PRINT #1 did not target the user window");
    interpreter.execute("PRINT #0;12\n", console);
    require(console.str() == "console\n12\n",
            "semicolon after a PRINT channel selector was not accepted");

    graphics->events.clear();
    interpreter.execute("PRINT \"default\"\n", console);
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "default"; }) !=
        graphics->events.end(), "unqualified PRINT did not target channel #1");

    graphics->events.clear();
    interpreter.execute("PRINT #1,INK 3,PAPER 6,\"styled\"\n", console);
    const auto appliedInk = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "INK" && event.a == 3; });
    const auto appliedPaper = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "PAPER" && event.a == 6; });
    const auto styledText = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "styled"; });
    require(appliedInk != graphics->events.end() &&
            appliedPaper != graphics->events.end() &&
            styledText != graphics->events.end() && appliedInk < appliedPaper &&
            appliedPaper < styledText,
            "PRINT leading INK/PAPER options were not applied before its text");

    auto presenter = std::make_shared<CapturingPresenter>();
    auto window = std::make_shared<WindowedService>(60, 24, presenter);
    ql32::Interpreter windowInterpreter({}, {}, {}, window);
    std::ostringstream windowConsole;
    windowInterpreter.execute("PRINT #1,\"A\"\nPRINT #1,\"B\"\n", windowConsole);
    const auto hasInk = [&](std::uint32_t top, std::uint32_t bottom) {
        for (std::uint32_t y = top; y < bottom; ++y)
            for (std::uint32_t x = 0; x < presenter->lastFrame.width; ++x)
                if (presenter->lastFrame.pixels[static_cast<std::size_t>(y) *
                        presenter->lastFrame.width + x] != 0) return true;
        return false;
    };
    require(windowConsole.str().empty() && hasInk(0, 8) && hasInk(8, 16),
            "window PRINT did not draw consecutive lines to the retained canvas");
}

void testPrintStatementsBracketGraphicsOutputBatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("PRINT \"x\";1\nPRINT \"tail\";\n", output);

    const auto count = [&](const char* name) {
        return std::count_if(graphics->events.begin(), graphics->events.end(),
            [&](const Event& event) { return event.name == name; });
    };
    require(count("PRINT_BATCH_BEGIN") == 2 && count("PRINT_BATCH_END") == 2,
            "PRINT did not close its graphics batch for both newline and trailing separator");
    require(count("TEXT") >= 4,
            "PRINT test did not exercise multiple text fragments");

    auto consoleGraphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter consoleInterpreter({}, {}, {}, consoleGraphics);
    std::ostringstream consoleOutput;
    consoleInterpreter.execute(
        "CHANNEL #0\nPRINT \"x\";1\nPRINT \"tail\";\n", consoleOutput);
    const auto consoleBatchCount = [&](const char* name) {
        return std::count_if(consoleGraphics->events.begin(), consoleGraphics->events.end(),
            [&](const Event& event) {
                return event.name == name && event.channel == 0;
            });
    };
    require(consoleOutput.str() == "x1\ntail" &&
            consoleBatchCount("PRINT_BATCH_BEGIN") == 2 &&
            consoleBatchCount("PRINT_BATCH_END") == 2,
            "console PRINT fragments were not bracketed by channel-0 batches");
}

void testChannelDefaultRoutingAndGraphicsSelectors() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.execute(
        "CHANNEL #0\n"
        "PRINT 7\n"
        "PRINT #1,8\n"
        "CHANNEL #1\n"
        "CIRCLE #1,10,20,5\n", console);
    require(console.str() == "7\n", "CHANNEL did not change the default output to #0");
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "8"; }) !=
        graphics->events.end(), "explicit PRINT #1 did not override the default channel");
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "CIRCLE" && event.first.x == 10 && event.first.y == 20;
        }) != graphics->events.end(), "CIRCLE #1 was not dispatched to the graphics window");

    interpreter.execute("CIRCLE #0,1,2,3\n", console);
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "CIRCLE" && event.channel == 0 &&
                   event.first.x == 1 && event.first.y == 2;
        }) != graphics->events.end(), "CIRCLE #0 was not dispatched to the console window");

    auto presenter = std::make_shared<CapturingPresenter>();
    auto windowed = std::make_shared<WindowedService>(100, 80, presenter);
    ql32::Interpreter windowInterpreter({}, {}, {}, windowed);
    std::ostringstream windowConsole;
    windowInterpreter.execute(
        "WINDOW #0,5,6,30,20\nRECT #0,0,0,12,12\n"
        "WINDOW #1,50,6,30,20\nRECT #1,0,0,12,12\n",
        windowConsole);
    const auto windowStates = windowed->windowManager().compositionOrder();
    const auto owner = windowStates.empty() ? 0 : windowStates.front().owner;
    const auto zeroWindow = windowed->windowInfo(owner, 0);
    const auto oneWindow = windowed->windowInfo(owner, 1);
    require(windowed->windowManager().size() == 2 &&
                zeroWindow.originX == 5 && zeroWindow.originY == 6 &&
                oneWindow.originX == 50 && oneWindow.originY == 6,
            "channel windows did not retain independent bounds");

    ql32::Interpreter routed({}, {}, {}, graphics);
    std::ostringstream routedConsole;
    routed.execute("CHANNEL #1 TO #0\nPRINT \"default route\"\n"
                   "PRINT #1,\"explicit route\"\n", routedConsole);
    require(routedConsole.str() == "default route\nexplicit route\n",
            "CHANNEL route did not redirect default and explicit output");
    routed.execute("CHANNEL #0 TO #1\n"
                   "PRINT #0,GETCHANNEL(2,#1)\n"
                   "PRINT #1,\"same endpoint\"\n", routedConsole);
    require(routedConsole.str() ==
                "default route\nexplicit route\nCONSOLE\nsame endpoint\n",
            "direct endpoint binding or active endpoint inspection failed");
}

void testListChannelRouting() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.executeInteractive("10 PRINT \"stored\"\n", console);
    interpreter.executeInteractive("LIST #1\n", console);
    require(console.str().empty(), "LIST #1 wrote the listing to the console stream");
    const auto windowListing = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "TEXT" && event.text == "10 PRINT \"stored\"\n";
        });
    require(windowListing != graphics->events.end(), "LIST #1 did not write to the user window");

    interpreter.executeInteractive("LIST #0\n", console);
    require(console.str() == "10 PRINT \"stored\"\n",
            "LIST #0 did not write to the console stream");

    graphics->events.clear();
    console.str({});
    console.clear();
    interpreter.executeInteractive("LIST\n", console);
    require(console.str().empty() && std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "TEXT" && event.text == "10 PRINT \"stored\"\n";
        }) != graphics->events.end(), "bare LIST did not use default output channel #1");

    auto storage = std::make_shared<ListingStorage>();
    auto directoryInterpreter = std::make_unique<ql32::Interpreter>(storage, nullptr, nullptr,
                                                                    graphics);
    graphics->events.clear();
    directoryInterpreter->executeInteractive("DIR\n", console);
    require(console.str().empty() && std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "demo.bas\n"; }) !=
        graphics->events.end(), "DIR did not use default output channel #1");
}

void testWindowMovementRestoresCoveredContent() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(100, 80, presenter);
    constexpr SessionId background = 101;
    constexpr SessionId foreground = 202;

    graphics.setPaper(background, 1);
    graphics.clear(background);
    graphics.setPaper(foreground, 2);
    graphics.clear(foreground);
    graphics.setWindowBounds(foreground, {20, 15, 30, 25});

    auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        return presenter->lastFrame.pixels[static_cast<std::size_t>(y) * 100 + x];
    };
    require(pixel(25, 20) == 2, "foreground window did not cover background pixels");
    require(pixel(10, 10) == 1, "background window was lost outside overlap");

    graphics.setWindowBounds(foreground, {60, 35, 30, 25});
    require(pixel(25, 20) == 1,
            "moving the covering window did not restore retained background pixels");
    require(pixel(65, 40) == 2,
            "moved window did not retain its own canvas contents");
    const auto info = graphics.windowInfo(foreground);
    require(info.originX == 60 && info.originY == 35,
            "window info did not expose absolute screen coordinates");
}

void testCanvasReshapeReusesFullScreenStorage() {
    using namespace ql32::graphics;
    WindowManager manager(240, 320);
    const auto id = manager.create(1, {0, 0, 240, 320}, PixelDepth::Four);
    auto& canvas = manager.canvas(id);
    canvas.setPaper(5);
    canvas.clear();
    canvas.setPixel(0, 0, 1);
    canvas.setPixel(239, 239, 2);
    canvas.setPixel(0, 319, 3);

    manager.setBounds(id, {0, 0, 320, 240});
    require(canvas.width() == 320 && canvas.height() == 240,
            "canvas did not reshape to landscape dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(239, 239) == 2,
            "landscape reshape lost pixels in the retained upper-left area");
    require(canvas.pixel(319, 10) == 5,
            "landscape reshape did not fill newly exposed pixels with paper");

    manager.setBounds(id, {0, 0, 240, 320});
    require(canvas.width() == 240 && canvas.height() == 320,
            "canvas did not reshape back to portrait dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(239, 239) == 2,
            "portrait reshape lost retained pixels");
    require(canvas.pixel(0, 319) == 5,
            "portrait reshape did not fill newly exposed pixels with paper");
}

void testCloseWindowReleasesCanvasAndAllowsReuse() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(20, 10, presenter);
    graphics.setPaper(11, 2);
    graphics.clear(11);
    require(graphics.windowManager().size() == 1,
            "graphics command did not create a retained window");

    graphics.closeWindow(11);
    require(graphics.windowManager().size() == 0,
            "CLOSEWINDOW did not release the window");
    require(!presenter->lastFrame.pixels.empty() && presenter->lastFrame.pixels[0] == 0,
            "closing the final window did not clear its displayed area");

    graphics.setPaper(22, 1);
    graphics.clear(22);
    require(graphics.windowManager().size() == 1,
            "released canvas budget could not be reused");
}

void testCompletedSessionRemainsAvailableForCapture() {
    using namespace ql32::graphics;
    WindowedService graphics(8, 8, nullptr);
    graphics.setInk(31, 2);
    graphics.pixel(31, {2, 2});
    graphics.setInk(31, 4);
    graphics.pixel(31, {3, 3});

    graphics.closeSession(31);
    std::vector<ColorIndex> row(8);
    graphics.composeDisplayRow(2, row);
    require(row[2] == 2,
            "completed session graphics disappeared before screen capture");
    graphics.composeDisplayRow(3, row);
    require(row[3] == 4,
            "completed session did not retain all drawn colors for capture");

    graphics.setInk(32, 6);
    graphics.pixel(32, {4, 4});
    graphics.composeDisplayRow(2, row);
    require(row[2] == 0,
            "starting a new drawing session did not discard the frozen image");
    graphics.composeDisplayRow(4, row);
    require(row[4] == 6,
            "new graphics session did not draw after frozen image cleanup");
}

void testNewWindowCommandReclaimsFrozenCanvasStorage() {
    using namespace ql32::graphics;
    WindowedService graphics(16, 8, nullptr);

    graphics.setInk(41, 3);
    graphics.pixel(41, {2, 2});
    graphics.closeSession(41);
    require(graphics.windowManager().size() == 1,
            "completed session canvas was not retained for capture");

    // BASIC WINDOW creates a new session's first canvas directly, rather than
    // going through ensureWindow(). It must release the frozen canvas first.
    graphics.setWindowBounds(42, {0, 0, 16, 8});
    require(graphics.windowManager().size() == 1,
            "WINDOW did not reclaim the previous session's frozen canvas");
    const auto info = graphics.windowInfo(42);
    require(info.width == 16 && info.height == 8,
            "new WINDOW command did not receive the reclaimed canvas budget");
}

void testDiscardSessionReleasesActiveAndFrozenWindowsByOwner() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(24, 8, presenter);
    graphics.setInk(51, 3);
    graphics.pixel(51, {1, 1});
    graphics.setInk(52, 6);
    graphics.pixel(52, {2, 2});
    graphics.closeSession(51); // Frozen completed output is still discardable.
    require(graphics.windowManager().size() == 2,
            "setup did not retain the completed session canvas");

    graphics.discardSession(51);
    require(graphics.windowManager().size() == 1,
            "discard did not release the selected frozen canvas");
    std::vector<ColorIndex> row(24);
    graphics.composeDisplayRow(1, row);
    require(row[1] == 0,
            "discarded frozen canvas remained available to screen capture");
    graphics.composeDisplayRow(2, row);
    require(row[2] == 6,
            "discarding one session removed another session's canvas");
    require(presenter->lastFrame.pixels[2 * 24 + 2] == 6,
            "discard did not refresh the display with the remaining window");

    graphics.discardSession(52);
    require(graphics.windowManager().size() == 0,
            "discard did not release an active session canvas");
    require(presenter->lastFrame.pixels[2 * 24 + 2] == 0,
            "discard did not clear the released window from the display");
}

void testRepeatedWindowSessionBurstsReleaseAndRecreate() {
    WindowedService graphics(64, 32, nullptr);
    SessionId nextSession = 100;
    for (int cycle = 0; cycle < 4; ++cycle) {
        std::array<SessionId, 12> sessions{};
        for (auto& session : sessions) {
            session = nextSession++;
            graphics.setTaskViewport(session, {0, 0, 64, 32});
            graphics.setInk(session, static_cast<ColorIndex>(session % 16));
            graphics.pixel(session, {cycle, cycle});
        }
        require(graphics.windowManager().size() == sessions.size(),
                "window burst did not create every session window");
        for (const auto session : sessions) graphics.discardSession(session);
        require(graphics.windowManager().size() == 0,
                "discard left a window after a session burst");

        const auto survivor = nextSession++;
        graphics.setTaskViewport(survivor, {0, 0, 64, 32});
        graphics.setInk(survivor, 7);
        graphics.pixel(survivor, {1, 1});
        const auto id = graphics.windowForSession(survivor);
        require(graphics.windowManager().size() == 1 &&
                    graphics.windowManager().canvas(id).pixel(1, 1) == 7,
                "window manager failed to create a fresh window after cleanup");
        graphics.discardSession(survivor);
    }
}

void testDebugOverlayComposesAboveGraphics() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(24, 16, presenter);
    graphics.setInk(51, 7);
    graphics.fillRectangle(51, {0, 0}, 24, 16);
    graphics.setDebugOverlayText("A");
    graphics.setDebugOverlayEnabled(true);

    require(presenter->lastFrame.pixels.size() == 24 * 16,
            "debug overlay did not present the composed screen");
    require(presenter->lastFrame.pixels[8] == 0,
            "debug overlay did not cover application graphics across the status row");
    require(presenter->lastFrame.pixels[8 * 24] == 7,
            "debug overlay covered application graphics below its single text row");
    bool glyphVisible = false;
    for (std::size_t y = 0; y < 7; ++y)
        for (std::size_t x = 0; x < 5; ++x)
            glyphVisible = glyphVisible || presenter->lastFrame.pixels[y * 24 + x] == 15;
    require(glyphVisible, "debug overlay text glyph was not composed");
}

void testCanvasStorageReleasesAndReusesAllocations() {
    using namespace ql32::graphics;
    WindowManager manager(16, 8);
    const auto first = manager.create(1, {0, 0, 4, 4}, PixelDepth::Four);
    const auto middle = manager.create(2, {4, 0, 4, 4}, PixelDepth::Four);
    const auto last = manager.create(3, {8, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(first).setPixel(0, 0, 3);
    manager.canvas(middle).setPixel(1, 1, 6);
    manager.canvas(last).setPixel(2, 2, 9);

    manager.close(middle);
    require(manager.canvas(first).pixel(0, 0) == 3 &&
                manager.canvas(last).pixel(2, 2) == 9,
            "canvas arena compaction changed a live canvas");

    const auto reused = manager.create(4, {12, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(reused).setPixel(3, 3, 12);
    require(manager.canvas(reused).pixel(3, 3) == 12,
            "canvas arena did not reuse released canvas space");
    std::vector<WindowId> additional;
    for (SessionId owner = 5; owner < 15; ++owner)
        additional.push_back(manager.create(owner, {0, 4, 1, 1}, PixelDepth::Four));
    require(manager.size() == 13,
            "window manager retained a fixed window-count or canvas-byte cap");
    for (const auto window : additional) manager.close(window);

    manager.close(reused);
    manager.setPixelDepth(last, PixelDepth::Eight);
    require(manager.canvas(last).pixel(2, 2) == 9,
            "canvas depth change lost pixels in the arena");

    manager.setBounds(last, {8, 0, 6, 4});
    manager.canvas(last).setPixel(5, 3, 211);
    require(manager.canvas(last).pixel(5, 3) == 211,
            "canvas resize did not refresh its cached arena storage pointer");
}

void testPackedCanvasScrollCopiesAlignedStorageAndHandlesOddRows() {
    using namespace ql32::graphics;

    IndexedCanvas packed(4, 4, PixelDepth::Four);
    packed.setPaper(15);
    for (std::uint32_t y = 0; y < packed.height(); ++y)
        for (std::uint32_t x = 0; x < packed.width(); ++x)
            packed.setPixel(static_cast<std::int32_t>(x),
                            static_cast<std::int32_t>(y),
                            static_cast<ColorIndex>(y + 1));
    packed.scrollUp(1);
    for (std::uint32_t y = 0; y < 3; ++y)
        for (std::uint32_t x = 0; x < packed.width(); ++x)
            require(packed.pixel(x, y) == y + 2,
                    "aligned packed scroll did not preserve source rows");
    for (std::uint32_t x = 0; x < packed.width(); ++x)
        require(packed.pixel(x, 3) == 15,
                "aligned packed scroll did not clear the exposed row with paper");

    IndexedCanvas oddWidth(3, 3, PixelDepth::Four);
    oddWidth.setPaper(14);
    for (std::uint32_t y = 0; y < oddWidth.height(); ++y)
        for (std::uint32_t x = 0; x < oddWidth.width(); ++x)
            oddWidth.setPixel(static_cast<std::int32_t>(x),
                              static_cast<std::int32_t>(y),
                              static_cast<ColorIndex>(y * 3 + x));
    oddWidth.scrollUp(1);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < oddWidth.width(); ++x)
            require(oddWidth.pixel(x, y) == (y + 1) * 3 + x,
                    "odd-width packed scroll changed nibble order");
    for (std::uint32_t x = 0; x < oddWidth.width(); ++x)
        require(oddWidth.pixel(x, 2) == 14,
                "odd-width packed scroll did not clear its exposed row");

    IndexedCanvas byteCanvas(3, 3, PixelDepth::Eight);
    byteCanvas.setPaper(255);
    for (std::uint32_t y = 0; y < byteCanvas.height(); ++y)
        for (std::uint32_t x = 0; x < byteCanvas.width(); ++x)
            byteCanvas.setPixel(static_cast<std::int32_t>(x),
                                static_cast<std::int32_t>(y),
                                static_cast<ColorIndex>(20 + y));
    byteCanvas.scrollUp(1);
    require(byteCanvas.pixel(0, 0) == 21 && byteCanvas.pixel(2, 1) == 22 &&
                byteCanvas.pixel(1, 2) == 255,
            "byte-canvas scroll did not move and clear complete rows");

    IndexedCanvas bitCanvas(8, 2, PixelDepth::One);
    bitCanvas.setPaper(0);
    for (std::int32_t x = 0; x < 8; ++x) bitCanvas.setPixel(x, 1, 1);
    bitCanvas.scrollUp(1);
    require(bitCanvas.pixel(7, 0) == 1 && bitCanvas.pixel(0, 1) == 0,
            "byte-aligned one-bit scroll did not move and clear rows");
}

void testTaskViewportTranslatesAndClipsOwnedWindows() {
    using namespace ql32::graphics;
    WindowManager manager(8, 6);
    const auto background = manager.create(9, {0, 0, 8, 6});
    manager.canvas(background).setPaper(3);
    manager.canvas(background).clear();

    const auto first = manager.create(41, {1, 1, 6, 4});
    auto& firstCanvas = manager.canvas(first);
    firstCanvas.setPaper(2);
    firstCanvas.clear();
    firstCanvas.setPixel(1, 0, 8);
    firstCanvas.setPixel(5, 3, 12);

    const auto second = manager.create(41, {6, 0, 3, 3});
    manager.canvas(second).setPaper(5);
    manager.canvas(second).clear();
    manager.canvas(second).setPixel(0, 0, 14);

    require(manager.taskViewport(41).canvasWidth == 8 &&
                manager.taskViewport(41).canvasHeight == 6,
            "new task viewport did not default to the physical display");
    manager.setTaskViewport(41, {2, 1, 3, 2});
    const auto firstState = manager.windowState(first);
    require(firstState.bounds.x == 3 && firstState.bounds.y == 2 &&
                firstState.screenClip.x == 3 && firstState.screenClip.y == 2 &&
                firstState.screenClip.width == 2 && firstState.screenClip.height == 1,
            "task origin did not translate and clip its window bounds");
    require(manager.windowState(second).screenClip.width == 0,
            "window extending outside the task viewport was not clipped");

    manager.setTaskViewport(41, {2, 1, 3, 2, 1, 0});
    const auto offsetState = manager.windowState(first);
    require(offsetState.bounds.x == 4 && offsetState.bounds.y == 2 &&
                offsetState.screenClip.x == 4 && offsetState.screenClip.y == 2 &&
                offsetState.screenClip.width == 1 &&
                offsetState.screenClip.height == 1,
            "task display offsets did not move content inside the fixed viewport");

    auto frame = manager.compose();
    require(frame[2 * 8 + 4] == 2 &&
                frame[1 * 8 + 2] == 3,
            "task clipping changed window-local pixels or covered unrelated pixels");

    manager.setTaskViewport(41, {-1, 0, 8, 6});
    frame = manager.compose();
    require(frame[1 * 8 + 1] == 8 && frame[4 * 8 + 5] == 12 &&
                frame[5] == 14 && frame[5 * 8 + 7] == 3,
            "expanding/moving the task viewport did not reveal retained pixels or preserve other tasks");
}

void testRowCompositionMatchesFullFrameForPackedCanvasWidths() {
    using namespace ql32::graphics;
    constexpr std::uint32_t width = 11;
    constexpr std::uint32_t height = 7;
    WindowManager manager(width, height);
    const auto background = manager.create(1, {0, 0, width, height});
    auto& backgroundCanvas = manager.canvas(background);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            backgroundCanvas.setPixel(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y),
                static_cast<ColorIndex>((x + 3 * y) & 0x0Fu));

    const auto foreground = manager.create(2, {3, 1, 7, 5});
    auto& foregroundCanvas = manager.canvas(foreground);
    foregroundCanvas.setDepth(PixelDepth::Eight);
    for (std::uint32_t y = 0; y < foregroundCanvas.height(); ++y)
        for (std::uint32_t x = 0; x < foregroundCanvas.width(); ++x)
            foregroundCanvas.setPixel(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y),
                static_cast<ColorIndex>(32 + x + y * 7));

    const auto top = manager.create(3, {5, 2, 3, 3});
    auto& topCanvas = manager.canvas(top);
    topCanvas.setDepth(PixelDepth::One);
    for (std::uint32_t y = 0; y < topCanvas.height(); ++y)
        for (std::uint32_t x = 0; x < topCanvas.width(); ++x)
            topCanvas.setPixel(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y), static_cast<ColorIndex>((x + y) & 1u));

    std::vector<ColorIndex> reference(static_cast<std::size_t>(width) * height, 0);
    for (const auto& state : manager.compositionOrder()) {
        const auto& source = manager.canvas(state.id);
        const auto sourceX = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.screenClip.x) - state.bounds.x);
        const auto sourceY = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.screenClip.y) - state.bounds.y);
        for (std::uint32_t y = 0; y < state.screenClip.height; ++y) {
            const auto destinationOffset =
                static_cast<std::size_t>(state.screenClip.y + static_cast<std::int32_t>(y)) *
                    width + static_cast<std::uint32_t>(state.screenClip.x);
            for (std::uint32_t x = 0; x < state.screenClip.width; ++x)
                reference[destinationOffset + x] = source.pixel(sourceX + x, sourceY + y);
        }
    }
    require(manager.compose() == reference,
            "optimized full-frame composition differs from pixel-by-pixel reference");
    std::vector<ColorIndex> row;
    for (std::uint32_t y = 0; y < height; ++y) {
        manager.composeRowInto(y, row);
        require(std::equal(row.begin(), row.end(),
                    reference.begin() + static_cast<std::size_t>(y) * width),
                "optimized full-row composition differs from full-frame output");
        for (const auto region : {std::pair<std::uint32_t, std::uint32_t>{1, 9},
                                  {4, 3}, {7, 4}}) {
            manager.composeRegionRowInto(y, region.first, region.second, row);
            const auto expected = reference.begin() +
                static_cast<std::size_t>(y) * width + region.first;
            require(std::equal(row.begin(), row.end(), expected),
                    "optimized partial-row composition differs from full-frame output");
        }
    }
}

void testTouchHitTestingUsesTopmostVisibleClippedWindow() {
    WindowManager manager(100, 80);
    const auto background = manager.create(11, {0, 0, 60, 60});
    const auto foreground = manager.create(22, {0, 0, 30, 30});
    manager.setTaskViewport(22, {20, 20, 5, 5});

    const auto inside = manager.hitTest({24, 24});
    require(inside && inside->window == foreground && inside->owner == 22 &&
            inside->local.x == 4 && inside->local.y == 4,
            "touch hit test did not select the top clipped window and local coordinates");
    const auto outsideViewport = manager.hitTest({27, 24});
    require(outsideViewport && outsideViewport->window == background &&
            outsideViewport->owner == 11,
            "touch hit test selected content outside the task viewport");
}

void testTouchWindowResolvesToItsBASICChannel() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(40, 30, presenter);
    graphics.line(19, 27, {0, 0}, {1, 1});
    const auto state = graphics.windowManager().compositionOrder();
    require(state.size() == 1, "graphics channel did not create a retained window");
    const auto channel = graphics.channelForWindow(19, state[0].id);
    require(channel && *channel == 27,
            "touch target window did not resolve to its BASIC channel handle");
}

void testDefaultCanvasIsSizedToTaskViewport() {
    using namespace ql32::graphics;
    WindowedService graphics(8, 6, std::make_shared<CapturingPresenter>());
    graphics.setTaskViewport(55, {0, 0, 4, 3});
    const auto window = graphics.windowForSession(55);
    const auto& canvas = graphics.windowManager().canvas(window);
    require(canvas.width() == 4 && canvas.height() == 3,
            "default canvas should match the task viewport, not the full screen");
    const auto full = graphics.windowForSession(56);
    require(graphics.windowManager().canvas(full).width() == 8,
            "a task without a viewport should keep a full-screen canvas");
}

void testWindowedServiceTaskViewportRedrawAndAbsoluteOrigin() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(8, 6, presenter);
    const auto background = graphics.windowManager().create(99, {0, 0, 8, 6});
    graphics.windowManager().canvas(background).setPaper(3);
    graphics.windowManager().canvas(background).clear();

    graphics.setWindowBounds(77, 1, {1, 1, 6, 4});
    const auto window = graphics.windowForSession(77);
    auto& canvas = graphics.windowManager().canvas(window);
    canvas.setPaper(2);
    canvas.clear();
    canvas.setPixel(1, 0, 8);
    graphics.present();

    graphics.setTaskViewport(77, {2, 1, 3, 2});
    const auto info = graphics.windowInfo(77);
    require(info.originX == 3 && info.originY == 2,
            "GORIGINX/Y did not return the translated absolute window position");
    require(presenter->lastFrame.pixels[1 * 8 + 1] == 3 &&
                presenter->lastFrame.pixels[2 * 8 + 4] == 8 &&
                presenter->lastFrame.pixels[2 * 8 + 5] == 3,
            "moving the task viewport did not redraw old and clipped new regions");

    graphics.closeSession(77);
    require(graphics.taskViewport(77).canvasWidth == 3,
            "completed task lost its viewport while its frozen canvas remained visible");
    graphics.clear(88);
    require(graphics.taskViewport(77).canvasWidth == 8 &&
                graphics.taskViewport(77).canvasHeight == 6,
            "reclaiming a frozen canvas did not release its task viewport");
}

void testCanvasResizePreservesPixelsAcrossOddWidthGrowAndShrink() {
    using namespace ql32::graphics;
    WindowManager manager(16, 10);
    const auto first = manager.create(1, {0, 0, 3, 3}, PixelDepth::Four);
    const auto target = manager.create(2, {3, 0, 3, 3}, PixelDepth::Four);
    const auto third = manager.create(3, {6, 0, 3, 3}, PixelDepth::Four);

    manager.canvas(first).setPixel(1, 1, 2);
    manager.canvas(third).setPixel(2, 2, 12);
    auto& canvas = manager.canvas(target);
    canvas.setPaper(6);
    canvas.clear();
    canvas.setPixel(0, 0, 1);
    canvas.setPixel(2, 1, 11);

    std::array<CanvasResizePhase, 3> phases{};
    std::size_t observed = 0;
    bool byteCountsMatch = true;
    manager.setCanvasResizeObserver(
        [&](CanvasResizePhase phase, std::size_t oldBytes, std::size_t newBytes) {
            if (observed < phases.size()) phases[observed] = phase;
            ++observed;
            byteCountsMatch = byteCountsMatch && oldBytes == 5 && newBytes == 13;
        });
    manager.setBounds(target, {3, 0, 5, 5});
    manager.setCanvasResizeObserver({});
    require(observed == 3 && byteCountsMatch &&
                phases[0] == CanvasResizePhase::BeforeAllocation &&
                phases[1] == CanvasResizePhase::BothBuffersLive &&
                phases[2] == CanvasResizePhase::AfterSwap,
            "resize telemetry did not report the three allocation phases");
    require(canvas.width() == 5 && canvas.height() == 5,
            "odd-width grow did not update canvas dimensions");
    for (std::uint32_t y = 0; y < 5; ++y) {
        for (std::uint32_t x = 0; x < 5; ++x) {
            ColorIndex expected = 6;
            if (x == 0 && y == 0) expected = 1;
            if (x == 2 && y == 1) expected = 11;
            require(canvas.pixel(x, y) == expected,
                    "odd-width grow lost pixels or paper-filled new area");
        }
    }

    manager.setBounds(target, {3, 0, 3, 3});
    require(canvas.width() == 3 && canvas.height() == 3,
            "odd-width shrink did not update canvas dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(2, 1) == 11 &&
                canvas.pixel(2, 2) == 6,
            "odd-width shrink did not preserve overlapping pixels");
    require(manager.canvas(first).pixel(1, 1) == 2 &&
                manager.canvas(third).pixel(2, 2) == 12,
            "resizing one window changed another window's pixels");
}

void testFailedCanvasResizePreservesAllWindows() {
    using namespace ql32::graphics;
    WindowManager manager(16, 8);
    const auto first = manager.create(1, {0, 0, 4, 4}, PixelDepth::Four);
    const auto target = manager.create(2, {4, 0, 4, 4}, PixelDepth::Four);
    const auto last = manager.create(3, {8, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(first).setPixel(0, 0, 2);
    manager.canvas(target).setPixel(1, 1, 5);
    manager.canvas(last).setPixel(2, 2, 9);

    // Fail the replacement canvas allocation while the target is in the
    // middle of the window list. No live block or window metadata may change.
    failAllocationAfter.store(0, std::memory_order_relaxed);
    bool failed = false;
    try {
        manager.setBounds(target, {4, 0, 8, 8});
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    failAllocationAfter.store(-1, std::memory_order_relaxed);

    require(failed, "injected replacement canvas allocation did not fail");
    require(manager.size() == 3, "failed resize changed the window count");
    const auto firstState = manager.windowState(first);
    const auto targetState = manager.windowState(target);
    const auto lastState = manager.windowState(last);
    require(firstState.id == first && firstState.bounds.x == 0 &&
                firstState.bounds.y == 0 && firstState.bounds.width == 4 &&
                firstState.bounds.height == 4 && targetState.id == target &&
                targetState.bounds.x == 4 && targetState.bounds.y == 0 &&
                targetState.bounds.width == 4 && targetState.bounds.height == 4 &&
                lastState.id == last && lastState.bounds.x == 8 &&
                lastState.bounds.y == 0 && lastState.bounds.width == 4 &&
                lastState.bounds.height == 4,
            "failed resize changed a window's bounds or handle");
    require(manager.canvas(first).width() == 4 && manager.canvas(target).width() == 4 &&
                manager.canvas(last).width() == 4,
            "failed resize changed a canvas dimension");
    require(manager.canvas(first).pixel(0, 0) == 2 &&
                manager.canvas(target).pixel(1, 1) == 5 &&
                manager.canvas(last).pixel(2, 2) == 9,
            "failed resize changed pixels or invalidated a window handle");
}

void testEveryPrintableAsciiCharacterHasAGlyph() {
    const auto fallback = classicGlyphColumns('\x01');
    for (int character = 32; character <= 126; ++character) {
        const auto pattern = classicGlyphColumns(static_cast<char>(character));
        require(pattern != fallback,
                "printable ASCII character fell through to the unknown-glyph box");
    }
}

void testTextCellsFillCharacterGapsWithPaper() {
    IndexedCanvas canvas(12, 8, PixelDepth::Four);
    canvas.setInk(3);
    canvas.setPaper(6);
    canvas.clear();
    canvas.text({0, 0}, "A");
    bool sawInk = false;
    for (std::uint32_t y = 0; y < 8; ++y) {
        require(canvas.pixel(5, y) == 6,
                "text cell's inter-character column was not filled with paper");
        for (std::uint32_t x = 0; x < 5; ++x)
            sawInk = sawInk || canvas.pixel(x, y) == 3;
    }
    for (std::uint32_t x = 0; x < 6; ++x)
        require(canvas.pixel(x, 7) == 6,
                "text cell's bottom row was not filled with paper");
    require(sawInk, "text glyph did not use the active ink color");
}

void testMirrorForwardsGraphicsAsIsToDestination() {
    auto graphics = std::make_shared<RecordingGraphics>();
    auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(nullptr);
    ql32::Interpreter interpreter({}, channels, {}, graphics);
    std::ostringstream output;
    interpreter.executeInteractive("w=OPEN(4)\n", output);
    interpreter.executeInteractive("INK #w,5\n", output);
    interpreter.executeInteractive("MIRROR #1 TO #w\n", output);
    interpreter.executeInteractive("INK 3\n", output);
    interpreter.executeInteractive("LINE 1,2,30,40\n", output);
    interpreter.executeInteractive("RECT 0,0,5,5,1\nWINDOW 1,1,50,50\n", output);
    interpreter.executeInteractive("MIRROR DELETE #w\nLINE 7,7,8,8\n", output);
    const auto count = [&](const char* name, ql32::graphics::ChannelId channel) {
        return std::count_if(graphics->events.begin(), graphics->events.end(),
            [&](const Event& event) { return event.name == name && event.channel == channel; });
    };
    require(count("INK", 2) == 2 && count("INK", 1) == 1,
            "state commands must be forwarded only after MIRROR is set up");
    require(count("LINE", 1) == 2 && count("LINE", 2) == 1,
            "LINE forms were not routed to the destination as-is");
    require(count("RECT", 2) == 1 && count("WINDOW", 2) == 0,
            "RECT must be forwarded and WINDOW must not");
}

void testCloseWindowCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("CLOSEWINDOW\n", output);
    require(graphics->events.size() == 1 &&
                graphics->events.front().name == "CLOSEWINDOW",
            "CLOSEWINDOW command was not dispatched");
}

void testAtPositionsDefaultAndOpenedScreenChannels() {
    auto graphics = std::make_shared<RecordingGraphics>();
    auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(nullptr);
    ql32::Interpreter interpreter({}, channels, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "AT 6,11\n"
        "PRINT \"default\"\n"
        "wchan=OPEN(4)\n"
        "AT #wchan,3,8\n"
        "PRINT #wchan,\"window\"\n"
        "AT #0,4,5\n"
        "PRINT #0,\"console\"\n", output);
    const auto atEvents = std::count_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "AT"; });
    const auto windowPrint = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "PRINT_TEXT" && event.channel == 2 &&
                   event.text == "window";
        });
    const auto defaultAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 1 &&
                   event.first.x == 5 && event.first.y == 10;
        });
    const auto windowAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 2 &&
                   event.first.x == 2 && event.first.y == 7;
        });
    const auto consoleAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 0 &&
                   event.first.x == 3 && event.first.y == 4;
        });
    require(atEvents == 3 && defaultAt != graphics->events.end() &&
                windowAt != graphics->events.end() && consoleAt != graphics->events.end() &&
                windowPrint != graphics->events.end(),
            "AT did not preserve row/column order and route each screen channel");
}

void testAtWorksOnNumberedBasicLineWithVariableCoordinates() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "10 a%=1\n"
        "15 b%=2\n"
        "20 AT a%,b%\n"
        "30 PRINT \"positioned\"\n", output);

    const auto positioned = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 1 &&
                   event.first.x == 0 && event.first.y == 1;
        });
    require(positioned != graphics->events.end(),
            "numbered AT statement with variable coordinates was not parsed");
}

void testAtRejectsZeroRowOrColumn() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    bool rejected = false;
    try {
        interpreter.execute("AT 0,1\n", output);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("positive integers") !=
                   std::string::npos;
    }
    require(rejected, "AT accepted a zero-based row coordinate");
    require(graphics->events.empty(), "invalid AT reached the graphics service");
}

void testScreenPrintPositionsArePerChannelAndSeparateFromText() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(80, 64, presenter);
    graphics->setDefaultWindowBounds(2, {0, 0, 32, 40});
    graphics->setDefaultWindowBounds(3, {40, 0, 32, 40});
    require(graphics->setTextPosition(9, 2, 2, 1),
            "screen channel rejected AT position");
    require(graphics->setTextPosition(9, 3, 0, 4),
            "second screen channel rejected AT position");
    graphics->text(9, 2, {24, 24}, "B");
    graphics->writeText(9, 2, "A");
    graphics->writeText(9, 3, "C");
    const auto hasInk = [&](std::uint32_t x, std::uint32_t y) {
        for (std::uint32_t row = y; row < y + 7; ++row)
            for (std::uint32_t column = x; column < x + 5; ++column)
                if (presenter->lastFrame.pixels[row * presenter->lastFrame.width + column] == 7)
                    return true;
        return false;
    };
    require(hasInk(6, 16), "AT did not position PRINT at the requested cell");
    require(hasInk(24, 24), "TEXT pixel positioning did not draw independently");
    require(hasInk(64, 0), "second screen channel did not retain its own PRINT position");
    require(!hasInk(24, 0), "TEXT changed the independent PRINT position");
}

void testTextOutputBatchPresentsOnceForMultiplePrintFragments() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(80, 64, presenter);
    graphics->beginTextOutputBatch(42, 1);
    graphics->writeText(42, 1, "value ");
    graphics->writeText(42, 1, "123");
    graphics->writeText(42, 1, "\n");
    require(presenter->presentCount == 0,
            "PRINT fragments presented before the statement completed");
    graphics->endTextOutputBatch(42, 1);
    require(presenter->presentCount == 1,
            "one PRINT statement did not produce exactly one presentation");

    graphics->beginTextOutputBatch(42, 1);
    graphics->beginTextOutputBatch(42, 1);
    graphics->writeText(42, 1, "nested");
    graphics->endTextOutputBatch(42, 1);
    require(presenter->presentCount == 1,
            "nested PRINT batch presented before its outer batch ended");
    graphics->endTextOutputBatch(42, 1);
    require(presenter->presentCount == 2,
            "nested PRINT batch did not present once when fully closed");

    int consoleBeginCount = 0;
    int consoleEndCount = 0;
    graphics->setConsoleOutputBatchCallbacks(
        [&] { ++consoleBeginCount; }, [&] { ++consoleEndCount; });
    graphics->beginTextOutputBatch(42, 0);
    graphics->endTextOutputBatch(42, 0);
    require(consoleBeginCount == 1 && consoleEndCount == 1 &&
            presenter->presentCount == 2,
            "channel-0 output batch did not reach the console or presented graphics");
}

void testBatchedGraphicsTextMatchesLegacyScrolling() {
    auto batchPresenter = std::make_shared<CapturingPresenter>();
    auto batched = std::make_shared<WindowedService>(32, 24, batchPresenter);
    batched->setDefaultWindowBounds(1, {0, 0, 32, 24});

    const std::string text = "ABCD EF\nline 2\n0123456789\nlast";
    batched->writeText(17, 1, text);

    ql32::graphics::IndexedCanvas reference(32, 24);
    reference.setInk(7);
    reference.setPaper(0);
    std::int32_t x = 0;
    std::int32_t y = 0;
    for (const auto character : text) {
        if (character == '\r') continue;
        if (character == '\n') {
            x = 0;
            y += 8;
        } else {
            if (x + 6 > 30) {
                x = 0;
                y += 8;
            }
            reference.printText({x, y}, std::string(1, character));
            x += 6;
        }
        if (y + 7 >= 24) {
            reference.scrollUp(8);
            y = std::max(0, y - 8);
        }
    }

    for (std::uint32_t yPixel = 0; yPixel < 24; ++yPixel)
        for (std::uint32_t xPixel = 0; xPixel < 32; ++xPixel)
            require(batchPresenter->lastFrame.pixels[yPixel * 32 + xPixel] ==
                        reference.pixel(xPixel, yPixel),
                    "batched graphics text changed wrapping or scroll results");
}

void testFailedPrintFlushesItsPartialOutputBatch() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(80, 64, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    bool failed = false;
    try {
        interpreter.execute("PRINT \"partial\";1/0\n", output);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "division by zero in PRINT expression was not reported");
    require(presenter->presentCount == 1,
            "failed PRINT left its partial output hidden in an open batch");
}

void testDisplayGeometryChangeResizesDefaultWindowsAndPresents() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(320, 200, presenter);
    graphics->setDefaultWindowBounds(0, {0, 176, 320, 24});
    graphics->setDefaultWindowBounds(1, {0, 0, 320, 176});
    Rect consoleBounds{};
    graphics->setConsoleBoundsCallback([&](Rect bounds) { consoleBounds = bounds; });
    graphics->writeText(1, 0, "console");
    graphics->writeText(1, 1, "graphics");

    require(graphics->setDisplaySize(640, 480),
            "windowed graphics refused supported display geometry change");
    require(graphics->displaySize() == std::pair<std::uint32_t, std::uint32_t>{640, 480},
            "windowed graphics did not expose its updated display dimensions");
    const auto console = graphics->windowInfo(1, 0);
    const auto screen = graphics->windowInfo(1, 1);
    require(console.width == 640 && console.height == 24,
            "bottom console window did not stretch with display width");
    require(screen.width == 640 && screen.height == 456,
            "main graphics window did not grow to the new display geometry");
    require(consoleBounds.x == 0 && consoleBounds.y == 456 &&
                consoleBounds.width == 640 && consoleBounds.height == 24,
            "console overlay did not receive its updated screen bounds");
    require(presenter->lastFrame.width == 640 && presenter->lastFrame.height == 480,
            "display geometry change did not present a full resized frame");
}

void testDisplayGeometryChangeRebuildsCanvasesAfterResizeFailure() {
    using namespace ql32::graphics;
    auto graphics = std::make_shared<WindowedService>(16, 8, nullptr);
    graphics->setDefaultWindowBounds(1, {0, 0, 16, 8});
    graphics->pixel(77, {2, 3});
    require(graphics->windowManager().size() == 1,
            "initial display window was not created");

    // Allow the resize bookkeeping allocations, then fail allocating the
    // replacement canvas so the service must use its clear-and-rebuild path.
    failAllocationAfter.store(4, std::memory_order_relaxed);
    require(graphics->setDisplaySize(32, 16),
            "display change did not recover from a canvas resize allocation failure");
    failAllocationAfter.store(-1, std::memory_order_relaxed);
    require(graphics->lastDisplayResizeDiscardedWindows() &&
                graphics->windowManager().size() == 0 &&
                graphics->displaySize() == std::pair<std::uint32_t, std::uint32_t>{32, 16},
            "fallback did not discard old canvases and apply the new geometry");

    graphics->pixel(77, {1, 1});
    const auto window = graphics->windowInfo(77);
    require(window.width == 32 && window.height == 16 &&
                graphics->windowManager().size() == 1 &&
                graphics->windowManager().canvas(
                    graphics->windowManager().compositionOrder().front().id).pixel(1, 1) == 7,
            "active session did not recreate its window at the new display size");
}

void testDisplayShrinkClipsDeferredConsoleDamage() {
    class GeometryCheckingPresenter final : public ql32::graphics::FramePresenter {
    public:
        void present(const ql32::graphics::Frame&) override {}
        bool presentRowsRegion(std::uint32_t width, std::uint32_t height,
                               const std::array<RgbColor, 256>&, Rect region,
                               const RowProvider&) override {
            require(region.x >= 0 && region.y >= 0 &&
                        static_cast<std::uint64_t>(region.x) + region.width <= width &&
                        static_cast<std::uint64_t>(region.y) + region.height <= height,
                    "deferred damage exceeds the resized display");
            return true;
        }
    };
    auto presenter = std::make_shared<GeometryCheckingPresenter>();
    WindowedService graphics(10, 12, presenter);
    graphics.setDefaultWindowBounds(0, {0, 10, 10, 2});
    std::uint64_t now = 0;
    graphics.setPresentCoalescing(500, 0, [&now] { return now; });
    graphics.invalidateSystemRegion({0, 10, 10, 2});
    now = 100;
    graphics.invalidateSystemRegion({0, 10, 10, 2});
    require(graphics.hasPendingPresentation(), "console damage was not deferred");
    graphics.setConsoleBoundsCallback([&](Rect bounds) {
        graphics.invalidateSystemRegion(bounds);
    });
    now = 1000;
    require(graphics.setDisplaySize(10, 10), "display shrink failed");
    require(graphics.displaySize() == std::pair<std::uint32_t, std::uint32_t>{10, 10} &&
                !graphics.hasPendingPresentation(),
            "display shrink left stale geometry or deferred damage");
}

void testSystemInfoFunctionsUseInjectedPortableProfile() {
    ql32::runtime::SystemInfoProfile profile;
    profile.values = {"0.1.0", "test-build", "test-board",
                      "vga 320x200", "ps2", "ffs,sd", "vga,ps2"};
    profile.capabilities = {{"display", true}, {"PS2", true},
                            {"spi bus", false}};
    auto systemInfo = std::make_shared<ql32::runtime::StaticSystemInfoService>(
        std::move(profile));
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, {}, {}, 0, {}, {}, {},
                                  systemInfo);
    std::ostringstream output;
    interpreter.executeResumable(
        "PRINT SYSINFO$(0);\"|\";SYSINFO$(1);\"|\";SYSINFO$(2);\"|\";"
        "SYSINFO$(3);\"|\";SYSINFO$(4);\"|\";SYSINFO$(5);\"|\";SYSINFO$(6)\n"
        "PRINT CAPABILITY(\"ps2\");\"|\";CAPABILITY(\"  SPI   BUS \");\"|\";"
        "CAPABILITY(\"missing\")\n", output);
    require(output.str() ==
                "0.1.0|test-build|test-board|vga 320x200|ps2|ffs,sd|vga,ps2\n1|0|-1\n",
            "SYSINFO$/CAPABILITY did not expose the injected portable profile");
}

void testSystemInfoRejectsInvalidSelector() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    bool rejected = false;
    try {
        interpreter.executeResumable("PRINT SYSINFO$(7)\n", output);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("system information selector") !=
                   std::string::npos;
    }
    require(rejected, "SYSINFO$ accepted an out-of-range selector");
}

class SupervisorTaskControl final : public ql32::runtime::TaskControlService {
public:
    explicit SupervisorTaskControl(bool isSupervisor = true)
        : isSupervisor_(isSupervisor) {}
    ql32::runtime::TaskId startTask(std::string, std::ostream&,
                                    ql32::runtime::TaskId, std::uint32_t,
                                    std::string) override {
        return 0;
    }
    bool killTask(ql32::runtime::TaskId) override { return false; }
    bool logon(ql32::runtime::TaskId) override { return false; }
    ql32::runtime::TaskId logoff() override { return 0; }
    ql32::runtime::TaskId focusedTask() const override { return 9; }
    bool suspendTask(ql32::runtime::TaskId) override { return false; }
    bool resumeTask(ql32::runtime::TaskId) override { return false; }
    std::size_t instructionBudget() const override { return 128; }
    bool setInstructionBudget(std::size_t) override { return false; }
    bool setTaskPriority(ql32::runtime::TaskId, std::uint32_t) override {
        return false;
    }
    std::vector<ql32::runtime::ManagedTaskInfo> listTasks() const override {
        ql32::runtime::ManagedTaskInfo supervisor;
        supervisor.id = 9;
        supervisor.supervisor = isSupervisor_;
        return {supervisor};
    }
    std::vector<ql32::runtime::ChannelInfo> listChannels(
        ql32::runtime::TaskId) const override { return {}; }
private:
    bool isSupervisor_;
};

void testSupervisorTileCommandUsesGraphicsWindowManager() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(32, 24, nullptr);
    auto tasks = std::make_shared<SupervisorTaskControl>();
    ql32::Interpreter interpreter({}, {}, {}, graphics, {}, {}, tasks, 9);
    graphics->pixel(interpreter.graphicsSessionId(), {2, 3});
    std::ostringstream output;
    interpreter.executeResumable("TILE\n", output);
    const auto window = graphics->windowInfo(interpreter.graphicsSessionId());
    require(window.originX == 0 && window.originY == 0 &&
                window.width == 32 && window.height == 24,
            "supervisor TILE did not arrange the retained graphics window");
}

void testTileCommandIsSupervisorOnly() {
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(32, 24, nullptr);
    auto tasks = std::make_shared<SupervisorTaskControl>(false);
    ql32::Interpreter interpreter({}, {}, {}, graphics, {}, {}, tasks, 9);
    std::ostringstream output;
    bool rejected = false;
    try {
        interpreter.executeResumable("TILE\n", output);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("TILE is supervisor-only") !=
                   std::string::npos;
    }
    require(rejected, "TILE was available to a non-supervisor session");
}

void testMenuEnableCommandChangesNextPresentation() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, ui);
    std::ostringstream output;
    const auto status = interpreter.executeResumable(
        "menu% = MINIT(\"Actions\")\n"
        "MCARD menu%, 41, \"Save\", \"S\"\n"
        "MENABLE menu%, 41, 0\n"
        "selected% = MENU(menu%)\n", output);
    require(status == ql32::ExecutionStatus::Suspended,
            "MENU did not suspend after applying its enabled-state update");
    const auto request = ui->takeNextRequest();
    require(request && request->kind == ql32::ui::RequestKind::Menu &&
                request->items.size() == 1 && request->items[0].commandId == 41 &&
                !request->items[0].enabled,
            "MENABLE did not disable the selected command in the next menu request");
    require(!ui->selectMenu(request->id, 41),
            "disabled menu item was still accepted as a selection");
}

void testMenuEnableRejectsInvalidValuesAndUnknownItems() {
    const std::array<std::pair<const char*, const char*>, 2> cases{{
        {"MENABLE menu%, 41, 2\n", "menu enabled flag must be 0 or 1"},
        {"MENABLE menu%, 42, 1\n", "unknown menu command ID"}
    }};
    for (const auto& test : cases) {
        ql32::Interpreter interpreter({}, {}, std::make_shared<ql32::ui::QueuedService>());
        std::ostringstream output;
        bool rejected = false;
        try {
            interpreter.executeResumable(
                "menu% = MINIT(\"Actions\")\n"
                "MCARD menu%, 41, \"Save\", \"S\"\n" +
                    std::string(test.first), output);
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find(test.second) != std::string::npos;
        }
        require(rejected, "MENABLE accepted invalid state or a missing command ID");
    }
}

} // namespace

int main() {
    testDrawingCallsAndSessionOwnership();
    testFractionalDrawingGeometryFloorsToPixels();
    testFontSelectionChangesNativeTextCellSize();
    testTextAndGraphicsFontStateAndStyles();
    testDefaultFontHandleResetsSelections();
    testLoadFontIsBoundedAndChannelScoped();
    testGraphicsUpdateCommandDispatch();
    testGupdateOffSuppressesVmDrawingUntilExplicitUpdate();
    testProgrammableGlobalPalette();
    testProgramControlledPaletteTransforms();
    testIndexedBitmapLoadBlitAndRelease();
    testSuperBasicLineCursorForms();
    testGraphicsUpdateSkipCommandDispatch();
    testUiInteractionResetsGraphicsUpdateStateAndKeepsIt();
    testWindowedGraphicsUpdateBatching();
    testWindowedGraphicsSkipsAutomaticPresentations();
    testWindowedGraphicsCoalescesPresentationsByTimeAndCost();
    testWindowedGraphicsResetUpdateStatePresentsAndDisablesSkip();
    testPresentationBatchCoalescesResetAndPageUpdate();
    testPresentationBatchPreservesConcurrentDrawingAcrossResize();
    testDrawingArgumentValidation();
    testClsChannelAndColorSyntax();
    testClsColorArgumentsClearWithNewPaper();
    testPixelAndFilledShapesRenderOnRetainedCanvas();
    testDisplayCommands();
    testPrintChannelRouting();
    testPrintStatementsBracketGraphicsOutputBatch();
    testChannelDefaultRoutingAndGraphicsSelectors();
    testListChannelRouting();
    testWindowPositionCommand();
    testWindowMovementRestoresCoveredContent();
    testCanvasReshapeReusesFullScreenStorage();
    testCloseWindowReleasesCanvasAndAllowsReuse();
    testCompletedSessionRemainsAvailableForCapture();
    testNewWindowCommandReclaimsFrozenCanvasStorage();
    testDiscardSessionReleasesActiveAndFrozenWindowsByOwner();
    testRepeatedWindowSessionBurstsReleaseAndRecreate();
    testDebugOverlayComposesAboveGraphics();
    testCanvasStorageReleasesAndReusesAllocations();
    testPackedCanvasScrollCopiesAlignedStorageAndHandlesOddRows();
    testRowCompositionMatchesFullFrameForPackedCanvasWidths();
    testTaskViewportTranslatesAndClipsOwnedWindows();
    testTouchHitTestingUsesTopmostVisibleClippedWindow();
    testTouchWindowResolvesToItsBASICChannel();
    testDefaultCanvasIsSizedToTaskViewport();
    testWindowedServiceTaskViewportRedrawAndAbsoluteOrigin();
    testCanvasResizePreservesPixelsAcrossOddWidthGrowAndShrink();
    testFailedCanvasResizePreservesAllWindows();
    testEveryPrintableAsciiCharacterHasAGlyph();
    testTextCellsFillCharacterGapsWithPaper();
    testCloseWindowCommandDispatch();
    testMirrorForwardsGraphicsAsIsToDestination();
    testAtPositionsDefaultAndOpenedScreenChannels();
    testAtWorksOnNumberedBasicLineWithVariableCoordinates();
    testAtRejectsZeroRowOrColumn();
    testScreenPrintPositionsArePerChannelAndSeparateFromText();
    testTextOutputBatchPresentsOnceForMultiplePrintFragments();
    testBatchedGraphicsTextMatchesLegacyScrolling();
    testFailedPrintFlushesItsPartialOutputBatch();
    testDisplayGeometryChangeResizesDefaultWindowsAndPresents();
    testDisplayGeometryChangeRebuildsCanvasesAfterResizeFailure();
    testDisplayShrinkClipsDeferredConsoleDamage();
    testSystemInfoFunctionsUseInjectedPortableProfile();
    testSystemInfoRejectsInvalidSelector();
    testSupervisorTileCommandUsesGraphicsWindowManager();
    testTileCommandIsSupervisorOnly();
    testMenuEnableCommandChangesNextPresentation();
    testMenuEnableRejectsInvalidValuesAndUnknownItems();
}
