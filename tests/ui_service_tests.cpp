#include "ql32/ui/service.hpp"
#include "ql32/platform/linux/console_ui.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testQueuedRequests() {
    ql32::ui::QueuedService service;
    const auto alert = service.beginAlert(11, "Saved", "Program saved");
    const auto confirm = service.beginConfirm(22, "Replace", "Overwrite file?");
    require(!service.poll(alert) && !service.poll(confirm),
            "new UI request was not left pending");

    const auto first = service.takeNextRequest();
    require(first && first->id == alert && first->session == 11 &&
                first->kind == ql32::ui::RequestKind::Alert &&
                first->title == "Saved" && first->message == "Program saved",
            "alert request metadata was not preserved");
    require(service.resolve(alert, ql32::ui::ResultKind::Acknowledged),
            "alert acknowledgement was rejected");
    const auto alertResult = service.poll(alert);
    require(alertResult && alertResult->kind == ql32::ui::ResultKind::Acknowledged &&
                !service.poll(alert),
            "alert result was not delivered exactly once");

    const auto second = service.takeNextRequest();
    require(second && second->id == confirm &&
                second->kind == ql32::ui::RequestKind::Confirm,
            "confirmation request was not dispatched");
    require(!service.resolve(confirm, ql32::ui::ResultKind::Acknowledged),
            "confirmation accepted an alert-only result");
    require(service.resolve(confirm, ql32::ui::ResultKind::Confirmed),
            "confirmation result was rejected");
    require(service.poll(confirm)->kind == ql32::ui::ResultKind::Confirmed,
            "confirmation result was not returned to its caller");
}

void testSessionCancellation() {
    ql32::ui::QueuedService service;
    const auto cancelled = service.beginConfirm(7, "Confirm", "Continue?");
    const auto retained = service.beginAlert(8, "Notice", "Other session");
    service.cancelSession(7);
    const auto next = service.takeNextRequest();
    require(next && next->id == retained,
            "cancelled session request remained queued");
    require(!service.resolve(cancelled, ql32::ui::ResultKind::Confirmed),
            "cancelled request accepted a stale response");

    service.cancel(retained);
    require(!service.takeNextRequest(), "cancelled request remained dispatchable");
}

void testConsolePresenter() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    const auto alert = service->beginAlert(1, "Saved", "File saved");
    const auto confirm = service->beginConfirm(1, "Exit", "Quit now?");
    std::istringstream input("\nyes\n");
    std::ostringstream output;
    ql32::platform::linux_host::ConsoleUi console(service, input, output);

    require(console.dispatchNext(), "console presenter did not dispatch alert");
    require(service->poll(alert)->kind == ql32::ui::ResultKind::Acknowledged,
            "console alert did not resolve on Enter");
    require(console.dispatchNext(), "console presenter did not dispatch confirmation");
    require(service->poll(confirm)->kind == ql32::ui::ResultKind::Confirmed,
            "console confirmation did not accept yes");
    require(!console.dispatchNext(), "console presenter reported a nonexistent request");
    require(output.str().find("File saved") != std::string::npos &&
                output.str().find("Quit now?") != std::string::npos,
            "console presenter omitted request text");
}

void testDeclarativeDialogAndMenuRequests() {
    ql32::ui::QueuedService service;
    const auto dialog = service.beginDialog(31, "Profile", {
        {ql32::ui::FieldKind::Text, "Enter profile details", std::monostate{}},
        {ql32::ui::FieldKind::Edit, "Name", std::string("Ada")},
        {ql32::ui::FieldKind::Integer, "Age", 36.0, {}, 0.0, 150.0},
        {ql32::ui::FieldKind::Check, "Active", true}
    });
    const auto menu = service.beginMenu(31, "Main", {
        {1001, "Open", "O", true}, {7007, "Exit", "X", true}
    });
    const auto dialogRequest = service.takeNextRequest();
    require(dialogRequest && dialogRequest->kind == ql32::ui::RequestKind::Dialog &&
                dialogRequest->fields.size() == 4 && dialogRequest->session == 31,
            "dialog schema was not delivered to the presenter");
    require(!service.submitDialog(dialog, {std::monostate{}, std::string("Grace"),
                                           200.0, false}),
            "dialog accepted a value outside its declared range");
    require(service.submitDialog(dialog, {std::monostate{}, std::string("Grace"),
                                          42.0, false}),
            "valid dialog submission was rejected");
    const auto result = service.poll(dialog);
    require(result && result->kind == ql32::ui::ResultKind::Submitted &&
                std::get<std::string>(result->fields[1]) == "Grace" &&
                std::get<double>(result->fields[2]) == 42.0 &&
                !std::get<bool>(result->fields[3]),
            "dialog values were not returned transactionally");

    const auto menuRequest = service.takeNextRequest();
    require(menuRequest && menuRequest->kind == ql32::ui::RequestKind::Menu &&
                menuRequest->items.size() == 2,
            "menu items were not delivered to the presenter");
    require(!service.selectMenu(menu, 1), "menu accepted an item position instead of an ID");
    require(service.selectMenu(menu, 7007), "menu rejected a stable command ID");
    const auto selection = service.poll(menu);
    require(selection && selection->kind == ql32::ui::ResultKind::Selected &&
                selection->commandId == 7007,
            "menu selection did not return its stable command ID");
}

void testConsoleDialogAndMenuPresenter() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    const auto dialog = service->beginDialog(44, "Profile", {
        {ql32::ui::FieldKind::Text, "Update your profile", std::monostate{}},
        {ql32::ui::FieldKind::Edit, "Name", std::string("Ada")},
        {ql32::ui::FieldKind::Integer, "Age", 36.0, {}, 0.0, 150.0},
        {ql32::ui::FieldKind::Float, "Score", 1.5, {}, 0.0, 10.0},
        {ql32::ui::FieldKind::Choice, "Color", 1.0, {"Red", "Green", "Blue"}},
        {ql32::ui::FieldKind::Check, "Active", true}
    });
    const auto menu = service->beginMenu(44, "Main", {
        {17, "Open", "O", true}, {91, "Exit", "X", true}
    });

    std::istringstream input("Grace\n42\n2.75\n3\nn\n\nx\n");
    std::ostringstream output;
    ql32::platform::linux_host::ConsoleUi console(service, input, output);
    require(console.dispatchNext(), "console did not present the dialog");
    const auto submitted = service->poll(dialog);
    require(submitted && submitted->kind == ql32::ui::ResultKind::Submitted &&
                std::get<std::string>(submitted->fields[1]) == "Grace" &&
                std::get<double>(submitted->fields[2]) == 42.0 &&
                std::get<double>(submitted->fields[3]) == 2.75 &&
                std::get<double>(submitted->fields[4]) == 2.0 &&
                !std::get<bool>(submitted->fields[5]),
            "console dialog did not submit all typed fields transactionally");
    require(console.dispatchNext(), "console did not present the menu");
    const auto selected = service->poll(menu);
    require(selected && selected->kind == ql32::ui::ResultKind::Selected &&
                selected->commandId == 91,
            "console menu shortcut did not return the stable command ID");
    require(output.str().find("Update your profile") != std::string::npos &&
                output.str().find("3) Blue") != std::string::npos &&
                output.str().find("Exit [X]") != std::string::npos,
            "console presenter omitted dialog or menu choices");
}

} // namespace

int main() {
    try {
        testQueuedRequests();
        testSessionCancellation();
        testConsolePresenter();
        testDeclarativeDialogAndMenuRequests();
        testConsoleDialogAndMenuPresenter();
    } catch (const std::exception& error) {
        std::cerr << "UI service test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
