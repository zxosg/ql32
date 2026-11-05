#include "ql32/ui/graphic_presenter.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Capture final : public ql32::graphics::FramePresenter {
public:
    void present(const ql32::graphics::Frame& frame) override {
        latest = frame;
        ++count;
    }
    ql32::graphics::WindowInfo setRotation(std::uint16_t degrees) override {
        return {degrees == 90 ? 240u : 320u, degrees == 90 ? 320u : 240u};
    }
    ql32::graphics::Frame latest;
    int count = 0;
};

ql32::input::KeyEvent key(ql32::input::Key value, char32_t character = 0) {
    ql32::input::KeyEvent event;
    event.key = value;
    event.character = character;
    return event;
}

void testFitsEveryDisplay() {
    for (const auto [width, height] : {
             std::pair{640u, 480u}, std::pair{320u, 240u},
             std::pair{320u, 200u}, std::pair{240u, 320u}}) {
        auto service = std::make_shared<ql32::ui::QueuedService>();
        auto capture = std::make_shared<Capture>();
        auto graphics = std::make_shared<ql32::graphics::WindowedService>(
            width, height, capture);
        ql32::ui::GraphicPresenter ui(service, graphics);
        std::vector<ql32::ui::DialogField> fields;
        for (int i = 0; i < 25; ++i)
            fields.push_back({ql32::ui::FieldKind::Edit,
                "A deliberately long field label that must wrap", std::string("value")});
        const auto id = service->beginDialog(1,
            "A very long title that is wider than any supported display", fields,
            "Apply changes", "Discard changes");
        require(ui.beginNext(), "dialog did not open");
        const auto bounds = ui.bounds();
        require(bounds.x >= 0 && bounds.y >= 0 &&
                static_cast<std::uint64_t>(bounds.x) + bounds.width <= width &&
                static_cast<std::uint64_t>(bounds.y) + bounds.height <= height,
                "panel exceeded display bounds");
        for (int i = 0; i < 24; ++i)
            ui.handleKey(key(ql32::input::Key::Down));
        require(ui.bounds().height <= height && ui.active(),
                "scrolling lost the dialog");
        ui.handleKey(key(ql32::input::Key::Escape));
        require(service->poll(id)->kind == ql32::ui::ResultKind::Cancelled,
                "dialog did not cancel");
    }
}

void testCommitValidationRotationAndMenu() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    auto capture = std::make_shared<Capture>();
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(320, 240, capture);
    ql32::ui::GraphicPresenter ui(service, graphics);
    const auto dialog = service->beginDialog(1, "Edit", {
        {ql32::ui::FieldKind::Integer, "Age", 3.0, {}, 0.0, 100.0},
        {ql32::ui::FieldKind::Check, "Active", true}
    });
    require(ui.beginNext(), "dialog did not open");
    ui.handleKey(key(ql32::input::Key::Backspace));
    ui.handleKey(key(ql32::input::Key::Character, '9'));
    graphics->setRotation(1, 90);
    ui.refresh();
    require(ui.bounds().width <= 240 && ui.bounds().height <= 320,
            "rotation did not reflow dialog");
    ui.handleKey(key(ql32::input::Key::Enter));
    const auto result = service->poll(dialog);
    require(result && result->kind == ql32::ui::ResultKind::Submitted &&
            std::get<double>(result->fields[0]) == 9,
            "dialog edit was lost on rotation");

    const auto invalid = service->beginDialog(2, "Number", {
        {ql32::ui::FieldKind::Integer, "Count", 2.0, {}, 0.0, 10.0}});
    ui.beginNext();
    ui.handleKey(key(ql32::input::Key::Character, '9'));
    ui.handleKey(key(ql32::input::Key::Enter));
    require(ui.active() && !service->poll(invalid), "invalid number was submitted");
    ui.handleKey(key(ql32::input::Key::Escape));
    require(service->poll(invalid)->kind == ql32::ui::ResultKind::Cancelled,
            "invalid dialog did not cancel");

    const auto menu = service->beginMenu(3, "Actions", {
        {7, "Unavailable", "U", false}, {19, "Open", "O", true}});
    ui.beginNext();
    ui.handleKey(key(ql32::input::Key::Enter));
    require(service->poll(menu)->commandId == 19,
            "menu did not skip disabled item or preserve ID");
}

void testCancellationAndLayers() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    auto capture = std::make_shared<Capture>();
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(320, 200, capture);
    graphics->setConsoleLayer([](std::uint32_t, std::uint32_t,
                                 std::vector<ql32::graphics::ColorIndex>& row) {
        std::fill(row.begin(), row.end(), 8);
    });
    ql32::ui::GraphicPresenter ui(service, graphics);
    graphics->setUpdateMode(1, ql32::graphics::UpdateMode::Off);
    const auto before = capture->count;
    const auto first = service->beginAlert(1, "Alert", "A message");
    const auto second = service->beginAlert(2, "Next", "Another message");
    ui.beginNext();
    require(capture->count > before, "system UI was suppressed by GUPDATE 0");
    const auto bounds = ui.bounds();
    const auto center = static_cast<std::size_t>(bounds.y + bounds.height / 2) * 320 +
                        static_cast<std::size_t>(bounds.x + bounds.width / 2);
    require(capture->latest.pixels[center] != 8, "console covered the UI layer");
    service->cancelSession(1);
    ui.refresh();
    require(!ui.active() && capture->latest.pixels[center] == 8,
            "cancelled UI failed to restore console");
    require(ui.beginNext() && ui.active(), "second session request was lost");
    ui.handleKey(key(ql32::input::Key::Enter));
    require(service->poll(second)->kind == ql32::ui::ResultKind::Acknowledged &&
            !service->poll(first), "request completion crossed sessions");
}

void testConfirmSerialAndLongMessage() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    auto capture = std::make_shared<Capture>();
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(240, 320, capture);
    ql32::ui::GraphicPresenter ui(service, graphics);
    const auto confirm = service->beginConfirm(1, "Delete?",
        std::string(700, 'x'));
    ui.beginNext();
    ui.handleSerialByte(27, 0);
    ui.handleSerialByte('[', 1);
    ui.handleSerialByte('B', 2);
    require(ui.active(), "arrow navigation dismissed a long confirmation");
    ui.handleSerialByte(27, 3);
    ui.handleSerialByte('[', 4);
    ui.handleSerialByte('C', 5);
    ui.handleSerialByte('\r', 6);
    require(service->poll(confirm)->kind == ql32::ui::ResultKind::Confirmed,
            "serial arrow did not focus Yes");
    const auto safe = service->beginConfirm(2, "Delete?", "Really?");
    ui.beginNext();
    ui.handleKey(key(ql32::input::Key::Enter));
    require(service->poll(safe)->kind == ql32::ui::ResultKind::Cancelled,
            "confirmation did not default to No");
}

void testFrameVisibleOnLightAndDarkBackgrounds() {
    for (const auto paper : {ql32::graphics::ColorIndex{0},
                             ql32::graphics::ColorIndex{7}}) {
        auto service = std::make_shared<ql32::ui::QueuedService>();
        auto capture = std::make_shared<Capture>();
        auto graphics = std::make_shared<ql32::graphics::WindowedService>(
            320, 240, capture);
        graphics->setConsoleLayer([paper](std::uint32_t, std::uint32_t,
                                          std::vector<ql32::graphics::ColorIndex>& row) {
            std::fill(row.begin(), row.end(), paper);
        });
        ql32::ui::GraphicPresenter ui(service, graphics);
        service->beginAlert(1, "Visible title", "Visible message");
        ui.beginNext();
        const auto bounds = ui.bounds();
        const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
            return capture->latest.pixels[
                static_cast<std::size_t>(bounds.y + y) * 320 + bounds.x + x];
        };
        require(pixel(0, 0) != pixel(1, 1),
                "dialog frame has no contrasting light and dark edges");
        require(pixel(paper == 0 ? 0 : 1, paper == 0 ? 0 : 1) != paper,
                "dialog frame disappears into the background");
    }
}

void testLineInputPromptIsVisibleAndKeyboardAccessible() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    auto capture = std::make_shared<Capture>();
    auto graphics = std::make_shared<ql32::graphics::WindowedService>(320, 200, capture);
    ql32::ui::GraphicPresenter ui(service, graphics);
    const auto request = service->beginInput(1,
        "--More-- Enter=page, L=line, Q=quit:");
    require(ui.beginNext() && ui.graphicActive() && ui.inputActive(),
            "line input prompt was not presented as an active graphics UI");
    const auto bounds = ui.bounds();
    require(bounds.width > 0 && bounds.height > 0 && capture->count > 0,
            "line input prompt was not drawn on screen");
    ui.handleKey(key(ql32::input::Key::Character, 'L'));
    ui.handleKey(key(ql32::input::Key::Enter));
    const auto result = service->poll(request);
    require(result && result->kind == ql32::ui::ResultKind::Submitted &&
            std::get<std::string>(result->fields.at(0)) == "L" && !ui.active(),
            "line input did not accept and submit keyboard text");
}
} // namespace

int main() {
    testFitsEveryDisplay();
    testCommitValidationRotationAndMenu();
    testCancellationAndLayers();
    testConfirmSerialAndLongMessage();
    testFrameVisibleOnLightAndDarkBackgrounds();
    testLineInputPromptIsVisibleAndKeyboardAccessible();
}
