#include "ql32/interpreter.hpp"
#include "ql32/platform/esp32/line_ui_presenter.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void testAlertAndConfirmAreIncremental() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    std::ostringstream output;
    ql32::platform::esp32::LineUiPresenter presenter(service, output);
    const auto alert = service->beginAlert(1, "Saved", "File written");
    require(presenter.beginNext() && presenter.active(), "alert was not presented");
    require(!service->poll(alert), "alert blocked instead of suspending");
    require(presenter.consumeLine(""), "Enter did not acknowledge alert");
    require(service->poll(alert)->kind == ql32::ui::ResultKind::Acknowledged,
            "alert result mismatch");

    const auto confirm = service->beginConfirm(1, "Quit", "Exit now?");
    require(presenter.beginNext(), "confirmation was not presented");
    require(!presenter.consumeLine("perhaps"), "invalid confirmation should stay active");
    require(presenter.consumeLine("yes"), "yes did not resolve confirmation");
    require(service->poll(confirm)->kind == ql32::ui::ResultKind::Confirmed,
            "confirmation result mismatch");
}

void testTypedDialogAndStableMenuId() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    std::ostringstream output;
    ql32::platform::esp32::LineUiPresenter presenter(service, output);
    const auto dialog = service->beginDialog(7, "Profile", {
        {ql32::ui::FieldKind::Text, "Edit profile", std::monostate{}},
        {ql32::ui::FieldKind::Edit, "Name", std::string("Ada")},
        {ql32::ui::FieldKind::Integer, "Age", 36.0, {}, 0.0, 150.0},
        {ql32::ui::FieldKind::Float, "Score", 1.5, {}, 0.0, 10.0},
        {ql32::ui::FieldKind::Choice, "Color", 1.0, {"Red", "Green", "Blue"}},
        {ql32::ui::FieldKind::Check, "Active", true}
    });
    require(presenter.beginNext(), "dialog was not presented");
    require(!presenter.consumeLine("Grace"), "dialog should remain active for each field");
    require(!presenter.consumeLine("200"), "invalid bounded integer should be rejected");
    require(!presenter.consumeLine("42"), "dialog should remain active before completion");
    require(!presenter.consumeLine("2.75"), "dialog should await remaining fields");
    require(!presenter.consumeLine("3"), "dialog should await checkbox field");
    require(!presenter.consumeLine("n"), "dialog should await submit confirmation");
    require(presenter.consumeLine(""), "blank submit should accept dialog");
    const auto submitted = service->poll(dialog);
    require(submitted && submitted->kind == ql32::ui::ResultKind::Submitted &&
                std::get<std::string>(submitted->fields[1]) == "Grace" &&
                std::get<double>(submitted->fields[2]) == 42.0 &&
                std::get<double>(submitted->fields[3]) == 2.75 &&
                std::get<double>(submitted->fields[4]) == 2.0 &&
                !std::get<bool>(submitted->fields[5]),
            "typed dialog values were not submitted correctly");

    const auto menu = service->beginMenu(7, "Main", {
        {17, "Open", "O", true}, {91, "Exit", "X", true}
    });
    require(presenter.beginNext(), "menu was not presented");
    require(presenter.consumeLine("X"), "menu shortcut did not resolve");
    const auto selected = service->poll(menu);
    require(selected && selected->kind == ql32::ui::ResultKind::Selected &&
                selected->commandId == 91,
            "menu returned an unstable or incorrect command ID");
}

void testInteractiveInterpreterSuspendResume() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    std::ostringstream output;
    ql32::platform::esp32::LineUiPresenter presenter(service, output);
    ql32::Interpreter interpreter({}, {}, service);

    auto status = interpreter.executeInteractiveResumable(
        "answer = CONFIRM(\"Continue\", \"Run next step?\")\nPRINT answer\n", output);
    require(status == ql32::ExecutionStatus::Suspended,
            "interactive UI call did not suspend the VM");
    require(presenter.beginNext(), "suspended VM request was not presented");
    require(presenter.consumeLine("yes"), "confirmation did not resolve");
    status = interpreter.resume(output);
    require(status == ql32::ExecutionStatus::Completed &&
                output.str().find("1") != std::string::npos,
            "interactive VM did not resume after UI response");
}

void testInputRequestConsumesOneCompletedLine() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    std::ostringstream output;
    ql32::platform::esp32::LineUiPresenter presenter(service, output);
    const auto request = service->beginInput(9, "");
    require(presenter.beginNext(), "line input was not presented");
    require(presenter.consumeLine("binary line"), "input line was not consumed");
    const auto result = service->poll(request);
    require(result && result->kind == ql32::ui::ResultKind::Submitted &&
                std::get<std::string>(result->fields.front()) == "binary line",
            "presenter did not submit the input line");
}

} // namespace

int main() {
    try {
        testAlertAndConfirmAreIncremental();
        testTypedDialogAndStableMenuId();
        testInteractiveInterpreterSuspendResume();
        testInputRequestConsumesOneCompletedLine();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
