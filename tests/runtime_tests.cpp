#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/console_ui.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <streambuf>
#include <string>

namespace {

class StopOnWriteBuffer final : public std::stringbuf {
public:
    explicit StopOnWriteBuffer(std::stop_source& source) : source_(source) {}

protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        const std::streamsize written = std::stringbuf::xsputn(data, size);
        source_.request_stop();
        return written;
    }

private:
    std::stop_source& source_;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testInterpreterSessions() {
    ql32::Interpreter first;
    ql32::Interpreter second;
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;
    first.execute("value = 42\n", firstOutput);
    second.execute("PRINT value\n", secondOutput);
    require(secondOutput.str() == "0\n", "session variables leaked into another interpreter");
    first.execute("PRINT value\n", firstOutput);
    require(firstOutput.str() == "42\n", "interpreter did not retain its own variables");
}

void testCancellation() {
    ql32::Interpreter interpreter;
    std::stop_source source;
    StopOnWriteBuffer buffer(source);
    std::ostream output(&buffer);
    bool cancelled = false;
    try {
        interpreter.execute("PRINT \"first\"\nPRINT \"second\"\n", output,
                            source.get_token());
    } catch (const std::runtime_error& error) {
        cancelled = std::string(error.what()) == "execution cancelled";
    }
    require(cancelled, "stop request did not cancel execution");
    require(buffer.str() == "first", "execution continued after cancellation");
}

void testAtomicProgramLineAndBlocks() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive("10 PRINT \"kept\"\n", output);
    bool rejected = false;
    try {
        interpreter.executeInteractive("10 PRINT (\n", output);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "invalid replacement line was accepted");

    std::ostringstream listing;
    interpreter.executeInteractive("LIST\n", listing);
    require(listing.str() == "10 PRINT \"kept\"\n",
            "invalid replacement did not preserve the previous line");

    interpreter.executeInteractive(
        "20 IF 1 THEN\n30 PRINT \"inside\"\n40 END IF\nRUN\n", output);
    require(output.str() == "kept\ninside\n",
            "multi-line block could not be stored and run");
}

void testPersistentRepl() {
    ql32::Interpreter interpreter;
    std::istringstream input(
        "value = 40\n"
        "value = value + 2\n"
        "PRINT value\n"
        "10 PRINT (\n"
        "PRINT value + 1\n"
        "10 PRINT \"stored\"\n"
        "RUN\n");
    std::ostringstream output;
    std::ostringstream diagnostics;
    interpreter.runRepl(input, output, diagnostics);
    require(output.str() == "42\n43\nstored\n",
            "REPL did not keep state or recover after a bad entry");
    require(diagnostics.str().find("expected expression") != std::string::npos,
            "REPL did not report a bad entry");
}

void testStoredProgramDiagnosticsAndArrayTypes() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive("10 PRINT MissingFunction()\n", output);
    bool unresolved = false;
    try {
        interpreter.executeInteractive("RUN\n", output);
    } catch (const std::runtime_error& error) {
        unresolved = std::string(error.what()).find("undefined procedure or function") !=
                     std::string::npos;
    }
    require(unresolved, "RUN did not diagnose an unresolved stored-program call");

    ql32::Interpreter typeMismatch;
    bool arrayTypeRejected = false;
    try {
        typeMismatch.execute(
            "DEFine PROCedure StoreText(values$)\n"
            "values$(0) = \"text\"\n"
            "END DEFine\n"
            "DIM values(0)\n"
            "StoreText(values)\n", output);
    } catch (const std::runtime_error& error) {
        arrayTypeRejected = std::string(error.what()).find(
            "array argument type does not match parameter") != std::string::npos;
    }
    require(arrayTypeRejected, "array parameter type mismatch was not diagnosed");

    ql32::Interpreter rankMismatch;
    bool arrayRankRejected = false;
    try {
        rankMismatch.execute("DIM values(1)\nPRINT values(0, 0)\n", output);
    } catch (const std::runtime_error& error) {
        arrayRankRejected = std::string(error.what()).find("incorrect array rank") !=
                            std::string::npos;
    }
    require(arrayRankRejected, "array rank mismatch was not diagnosed");
}

void testKeywordAbbreviationCoverage() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive(
        "10 DEFI PROCE Work()\n"
        "20 LOCA item\n"
        "30 item = 1\n"
        "40 RETU\n"
        "50 eNd DEFI\n"
        "60 REPE Loop\n"
        "70 exIt Loop\n"
        "80 eNd REPE Loop\n"
        "90 SELET oN 1\n"
        "100 = 1\n"
        "110 pRiNt \"one\"\n"
        "120 = REMAINDER\n"
        "130 pRiNt \"other\"\n"
        "140 eNd sEl\n"
        "150 REMA stored comment\n", output);
    std::ostringstream listing;
    interpreter.executeInteractive("LIST\n", listing);
    const std::string canonical = listing.str();
    for (const std::string& spelling : {
             "10 DEFine PROCedure Work()", "20 LOCal item", "40 RETurn",
             "50 END DEFine", "60 REPeat Loop", "80 END REPeat Loop",
             "90 SELect ON 1", "120 = REMAINDER", "140 END SELect",
             "150 REMA stored comment"}) {
        require(canonical.find(spelling) != std::string::npos,
                ("supported keyword abbreviation did not canonicalize: " + spelling +
                 "\n" + canonical).c_str());
    }
}

void testForwardCallsAndKnownSemanticErrors() {
    ql32::Interpreter interpreter;
    std::istringstream input(
        "10 PRINT Square(6)\n"
        "20 DEF FN Square(n)\n"
        "30 RET n * n\n"
        "40 END DEF\n"
        "RUN\n");
    std::ostringstream output;
    std::ostringstream diagnostics;
    interpreter.runRepl(input, output, diagnostics);
    require(output.str() == "36\n" && diagnostics.str().empty(),
            "valid forward function reference did not resolve at RUN");

    ql32::Interpreter semantic;
    std::ostringstream listing;
    semantic.executeInteractive(
        "10 DEF FN Square(n)\n"
        "20 RET n * n\n"
        "30 END DEF\n", listing);
    bool badArityRejected = false;
    try {
        semantic.executeInteractive("40 PRINT Square()\n", listing);
    } catch (const std::runtime_error& error) {
        badArityRejected = std::string(error.what()).find("incorrect argument count") !=
                           std::string::npos;
    }
    require(badArityRejected, "line entry accepted a known function arity mismatch");
    listing.str({});
    listing.clear();
    semantic.executeInteractive("LIST\n", listing);
    require(listing.str().find("40 PRINT") == std::string::npos,
            "rejected semantic line was added to the stored program");
}

void testForRangesEvaluateOnceInSourceOrder() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "counter = 0\n"
        "DEF FN Bump()\n"
        "  counter = counter + 1\n"
        "  RET counter\n"
        "END DEF\n"
        "FOR item = Bump(), Bump() TO Bump()\n"
        "  PRINT item\n"
        "END FOR item\n"
        "PRINT counter\n", output);
    require(output.str() == "1\n2\n3\n3\n",
            "FOR range expressions were not evaluated once in source order");
}

void testSuperBasicStringSlices() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "text$ = \"Hello world\"\n"
        "PRINT text$(1)\n"
        "PRINT text$(1 TO 5)\n"
        "PRINT text$(TO 5)\n"
        "PRINT text$(10 TO)\n"
        "PRINT text$(4 TO 40)\n"
        "PRINT text$(2 + 2 TO 5 + 3)\n"
        "unicode$ = \"café🙂\"\n"
        "PRINT unicode$(4 TO 5)\n"
        "PRINT LEN(text$)\n"
        "PRINT LEN(unicode$)\n"
        "PRINT LEN(text$(TO 5))\n"
        "PRINT LEN(\"\")\n", output);
    require(output.str() == "H\nHello\nHello\nld\nlo world\nlo wo\né🙂\n"
                            "11\n5\n5\n0\n",
            "SuperBASIC-style string slicing returned incorrect text");

    ql32::Interpreter forwardFunction;
    std::ostringstream functionOutput;
    forwardFunction.execute(
        "PRINT Greeting$(\"QL/32\")\n"
        "DEFine FuNction Greeting$(name$)\n"
        "RETurn \"Hello \" + name$\n"
        "END DEFine\n", functionOutput);
    require(functionOutput.str() == "Hello QL/32\n",
            "string slicing parsing broke forward string-function calls");
}

void testUiCallsSuspendAndResumePerSession() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter first({}, {}, service);
    ql32::Interpreter second({}, {}, service);
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;

    const auto firstStatus = first.executeResumable(
        "value = 41\nALERT(\"Saved\", \"Program saved\")\n"
        "value = value + 1\nPRINT value\n", firstOutput);
    const auto secondStatus = second.executeResumable(
        "answer = CONFIRM(\"Continue\", \"Keep going?\")\n"
        "PRINT answer\n", secondOutput);
    require(firstStatus == ql32::ExecutionStatus::Suspended &&
                secondStatus == ql32::ExecutionStatus::Suspended,
            "UI calls did not suspend their sessions");
    require(firstOutput.str().empty() && secondOutput.str().empty(),
            "a suspended session continued execution");

    const auto alert = service->takeNextRequest();
    const auto confirm = service->takeNextRequest();
    require(alert && alert->kind == ql32::ui::RequestKind::Alert &&
                confirm && confirm->kind == ql32::ui::RequestKind::Confirm,
            "BASIC UI calls did not enqueue their corresponding requests");
    require(service->resolve(confirm->id, ql32::ui::ResultKind::Confirmed),
            "confirmation request did not resolve");
    require(second.resume(secondOutput) == ql32::ExecutionStatus::Completed &&
                secondOutput.str() == "1\n",
            "confirmation result was not returned to BASIC");
    require(first.resume(firstOutput) == ql32::ExecutionStatus::Suspended,
            "an unresolved alert did not keep only its session suspended");
    require(service->resolve(alert->id, ql32::ui::ResultKind::Acknowledged),
            "alert acknowledgement did not resolve");
    require(first.resume(firstOutput) == ql32::ExecutionStatus::Completed &&
                firstOutput.str() == "42\n",
            "alert acknowledgement did not resume BASIC at the next instruction");

    ql32::Interpreter cancelled({}, {}, service);
    std::ostringstream cancelledOutput;
    require(cancelled.executeResumable(
                "PRINT CONFIRM(\"Continue\", \"Keep going?\")\n", cancelledOutput) ==
                ql32::ExecutionStatus::Suspended,
            "confirmation cancellation test did not suspend");
    const auto cancelledRequest = service->takeNextRequest();
    require(cancelledRequest &&
                service->resolve(cancelledRequest->id, ql32::ui::ResultKind::Cancelled),
            "confirmation cancellation result was not accepted");
    require(cancelled.resume(cancelledOutput) == ql32::ExecutionStatus::Completed &&
                cancelledOutput.str() == "0\n",
            "cancelled confirmation did not return false");

    auto replService = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter repl({}, {}, replService);
    std::istringstream replInput(
        "10 answer = CONFIRM(\"Continue\", \"Keep going?\")\n"
        "20 PRINT answer\nRUN\nyes\n");
    std::ostringstream replOutput;
    std::ostringstream replDiagnostics;
    ql32::platform::linux_host::ConsoleUi console(replService, replInput, replOutput);
    repl.runRepl(replInput, replOutput, replDiagnostics, false,
                 [&] { return console.dispatchNext(); });
    require(replOutput.str().ends_with("1\n") && replDiagnostics.str().empty(),
            "REPL host presenter did not resume a stored program after confirmation");
}

void testDialogMenuBasicCallsSuspendAndResume() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, service);
    std::ostringstream output;
    const std::string source =
        "dialog = DINIT(\"Profile\")\n"
        "DTEXT(dialog, \"Profile fields\")\n"
        "DEDIT(dialog, \"Name\", \"Ada\")\n"
        "DINT(dialog, \"Age\", 36, 0, 150)\n"
        "DFLOAT(dialog, \"Score\", 1.5, 0, 10)\n"
        "DCHOICE(dialog, \"Color\", 2, \"Red|Green|Blue\")\n"
        "DCHECK(dialog, \"Active\", 1)\n"
        "DBUTTONS(dialog, \"Apply\", \"Cancel\")\n"
        "accepted = DIALOG(dialog)\n"
        "PRINT accepted\nPRINT DGET$(dialog, 2)\n"
        "PRINT DGET(dialog, 3)\nPRINT DGET(dialog, 4)\n"
        "PRINT DGET(dialog, 5)\nPRINT DGET(dialog, 6)\n"
        "menu = MINIT(\"Main\")\n"
        "MCARD(menu, 17, \"Open\", \"O\")\n"
        "MCARD(menu, 91, \"Exit\", \"X\")\n"
        "selected = MENU(menu)\nPRINT selected\n";
    require(interpreter.executeResumable(source, output) == ql32::ExecutionStatus::Suspended,
            "BASIC DIALOG did not suspend while awaiting a presenter");
    const auto dialog = service->takeNextRequest();
    require(dialog && dialog->kind == ql32::ui::RequestKind::Dialog &&
                dialog->fields.size() == 6 && dialog->acceptLabel == "Apply" &&
                dialog->cancelLabel == "Cancel",
            "BASIC dialog builders did not produce the declared dialog schema");
    require(service->submitDialog(dialog->id, {
                std::monostate{}, std::string("Grace"), 42.0, 2.75, 1.0, false}),
            "presenter could not submit BASIC dialog field values");
    require(interpreter.resume(output) == ql32::ExecutionStatus::Suspended,
            "BASIC VM did not resume from DIALOG and suspend at MENU");
    require(output.str() == "1\nGrace\n42\n2.75\n2\n0\n",
            "BASIC dialog getters returned incorrect typed results");
    const auto menu = service->takeNextRequest();
    require(menu && menu->kind == ql32::ui::RequestKind::Menu &&
                menu->items.size() == 2 && menu->items[1].commandId == 91,
            "BASIC menu builders did not produce stable menu command IDs");
    require(service->selectMenu(menu->id, 91), "presenter could not select BASIC menu item");
    require(interpreter.resume(output) == ql32::ExecutionStatus::Completed &&
                output.str().ends_with("91\n"),
            "BASIC MENU did not return the selected command ID");

    auto cancelService = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter cancelled({}, {}, cancelService);
    std::ostringstream cancelledOutput;
    require(cancelled.executeResumable(
                "dialog = DINIT(\"Cancel test\")\naccepted = DIALOG(dialog)\nPRINT accepted\n",
                cancelledOutput) == ql32::ExecutionStatus::Suspended,
            "cancel test dialog did not suspend");
    const auto pending = cancelService->takeNextRequest();
    require(pending && cancelService->resolve(pending->id, ql32::ui::ResultKind::Cancelled),
            "presenter could not cancel BASIC dialog");
    require(cancelled.resume(cancelledOutput) == ql32::ExecutionStatus::Completed &&
                cancelledOutput.str() == "0\n",
            "cancelled BASIC dialog did not return false");

    auto replService = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter repl({}, {}, replService);
    std::ostringstream replOutput;
    repl.execute("dialog = DINIT(\"REPL dialog\")\n", replOutput);
    repl.execute("DEDIT(dialog, \"Name\", \"Initial\")\n", replOutput);
    require(repl.executeResumable(
                "accepted = DIALOG(dialog)\nPRINT DGET$(dialog, 1)\n", replOutput) ==
                ql32::ExecutionStatus::Suspended,
            "dialog handle did not survive separate REPL entries");
    const auto replDialog = replService->takeNextRequest();
    require(replDialog && replService->submitDialog(replDialog->id, {std::string("Persistent")}),
            "REPL dialog did not accept its submitted value");
    require(repl.resume(replOutput) == ql32::ExecutionStatus::Completed &&
                replOutput.str() == "Persistent\n",
            "REPL dialog getter did not return the submitted value");
}

} // namespace

int main() {
    try {
        testInterpreterSessions();
        testCancellation();
        testAtomicProgramLineAndBlocks();
        testPersistentRepl();
        testStoredProgramDiagnosticsAndArrayTypes();
        testKeywordAbbreviationCoverage();
        testForwardCallsAndKnownSemanticErrors();
        testForRangesEvaluateOnceInSourceOrder();
        testSuperBasicStringSlices();
        testUiCallsSuspendAndResumePerSession();
        testDialogMenuBasicCallsSuspendAndResume();
    } catch (const std::exception& error) {
        std::cerr << "runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
