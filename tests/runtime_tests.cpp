#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/console_ui.hpp"
#include "ql32/platform/esp32/serial_key_decoder.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include "ql32/program_store.hpp"
#include <string>
#include <thread>

namespace {

class StopOnWriteBuffer final : public std::stringbuf {
public:
    explicit StopOnWriteBuffer(ql32::runtime::CancellationSource& source) : source_(source) {}

protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        const std::streamsize written = std::stringbuf::xsputn(data, size);
        source_.requestStop();
        return written;
    }

private:
    ql32::runtime::CancellationSource& source_;
};

class RecordingCpuFrequencyService final : public ql32::runtime::CpuFrequencyService {
public:
    bool setFrequencyMHz(std::uint16_t mhz) override {
        lastFrequency = mhz;
        ++calls;
        return succeeds;
    }
    std::uint16_t currentFrequencyMHz() const override { return lastFrequency; }

    std::uint16_t lastFrequency = 0;
    int calls = 0;
    bool succeeds = true;
};

class RecordingMemoryService final : public ql32::runtime::MemoryService {
public:
    std::uint64_t freeBytes() const override { return 123456; }
    std::optional<ql32::runtime::HeapFragmentationInfo> fragmentationInfo() const override {
        return ql32::runtime::HeapFragmentationInfo{65432, 17, 445};
    }
};

class FreeOnlyMemoryService final : public ql32::runtime::MemoryService {
public:
    std::uint64_t freeBytes() const override { return 123456; }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
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

void testDiagnosticsUseBasicLineNumbers() {
    ql32::Interpreter runtimeFailure;
    std::ostringstream output;
    bool runtimeHasBasicLine = false;
    try {
        runtimeFailure.execute("100 PRINT 1 / 0\n", output);
    } catch (const std::runtime_error& error) {
        runtimeHasBasicLine = std::string(error.what()).find("line 100, column") !=
                              std::string::npos;
    }
    require(runtimeHasBasicLine,
            "runtime diagnostic reported source order instead of BASIC line number");

    ql32::Interpreter syntaxFailure;
    bool syntaxHasBasicLine = false;
    try {
        syntaxFailure.execute("240 PRINT (1 + )\n", output);
    } catch (const std::runtime_error& error) {
        syntaxHasBasicLine = std::string(error.what()).find("line 240, column") !=
                             std::string::npos;
    }
    require(syntaxHasBasicLine,
            "syntax diagnostic reported source order instead of BASIC line number");

    ql32::Interpreter lexicalFailure;
    bool lexicalHasBasicLine = false;
    try {
        lexicalFailure.execute("360 PRINT @\n", output);
    } catch (const std::runtime_error& error) {
        lexicalHasBasicLine = std::string(error.what()).find("line 360, column") !=
                              std::string::npos;
    }
    require(lexicalHasBasicLine,
            "lexical diagnostic reported source order instead of BASIC line number");
}

void testEmbeddedDataReadRestoreAndEof() {
    ql32::Interpreter numbered;
    std::ostringstream numberedOutput;
    numbered.execute(
        "10 x=2\n"
        "20 DATA x*10,\"alpha\",3\n"
        "30 x=7\n"
        "40 READ n%,s$,value%\n"
        "50 PRINT n%\n"
        "60 PRINT s$\n"
        "70 PRINT value%\n"
        "80 PRINT EOF()\n", numberedOutput);
    require(numberedOutput.str() == "70\nalpha\n3\n1\n",
            "READ did not evaluate DATA expressions at read time or update EOF()");

    ql32::Interpreter lineRestore;
    std::ostringstream lineOutput;
    lineRestore.execute(
        "10 DATA \"one\"\n"
        "20 DATA \"two\"\n"
        "30 READ first$\n"
        "40 RESTORE 20\n"
        "50 READ second$\n"
        "60 PRINT first$\n"
        "70 PRINT second$\n", lineOutput);
    require(lineOutput.str() == "one\ntwo\n",
            "RESTORE line number did not select the first DATA item at that line");

    ql32::Interpreter labelRestore;
    std::ostringstream labelOutput;
    labelRestore.execute(
        "RESTORE later\n"
        "DATA \"first\"\n"
        "later: DATA \"second\",\"third\"\n"
        "READ selected$\n"
        "RESTORE\n"
        "READ beginning$\n"
        "PRINT selected$\n"
        "PRINT beginning$\n"
        "PRINT EOF()\n", labelOutput);
    require(labelOutput.str() == "second\nfirst\n0\n",
            "RESTORE label or bare RESTORE positioned the DATA cursor incorrectly");

    ql32::Interpreter arrayRead;
    std::ostringstream arrayOutput;
    arrayRead.execute(
        "DIM values%(1)\n"
        "DATA 12,34\n"
        "READ values%(0),values%(1)\n"
        "PRINT values%(0)\n"
        "PRINT values%(1)\n", arrayOutput);
    require(arrayOutput.str() == "12\n34\n",
            "READ did not assign items to indexed array targets");

    ql32::Interpreter procedureRead;
    std::ostringstream procedureOutput;
    procedureRead.execute(
        "DEFINE PROCEDURE getnext\n"
        "  READ item$\n"
        "END DEFINE getnext\n"
        "DATA \"A\",\"B\"\n"
        "getnext\n"
        "READ item$\n"
        "PRINT item$\n", procedureOutput);
    require(procedureOutput.str() == "B\n",
            "DATA pointer was not shared between main code and a procedure");

    ql32::Interpreter rerun;
    std::ostringstream rerunOutput;
    const std::string rerunnable =
        "READ item$\nPRINT item$\nDATA \"repeat\"\n";
    rerun.execute(rerunnable, rerunOutput);
    rerun.execute(rerunnable, rerunOutput);
    require(rerunOutput.str() == "repeat\nrepeat\n",
            "a new RUN did not reset the DATA pointer to the beginning");

    bool exhaustionReported = false;
    try {
        ql32::Interpreter exhausted;
        std::ostringstream ignored;
        exhausted.execute("READ a\nDATA 1\nREAD b\n", ignored);
    } catch (const std::runtime_error& error) {
        exhaustionReported = std::string(error.what()).find("End of DATA") !=
                             std::string::npos;
    }
    require(exhaustionReported, "READ past the end did not report End of DATA");
}

void testGotoLineNumbersAndLabels() {
    std::ostringstream output;
    ql32::Interpreter numbered;
    numbered.execute(
        "10 PRINT \"start\"\n"
        "20 GO TO 40\n"
        "30 PRINT \"skipped\"\n"
        "40 PRINT \"line target\"\n", output);
    require(output.str() == "start\nline target\n",
            "GO TO did not jump to the requested numbered source line");

    ql32::Interpreter storedProgram;
    std::ostringstream storedOutput;
    storedProgram.executeInteractive("10 PRINT \"stored start\"\n", storedOutput);
    storedProgram.executeInteractive("20 GOTO 40\n", storedOutput);
    storedProgram.executeInteractive("30 PRINT \"stored skip\"\n", storedOutput);
    storedProgram.executeInteractive("40 PRINT \"stored target\"\n", storedOutput);
    storedProgram.executeInteractive("RUN\n", storedOutput);
    require(storedOutput.str() == "stored start\nstored target\n",
            "stored program did not resolve a forward line target at RUN");

    std::ostringstream labelOutput;
    ql32::Interpreter labeled;
    labeled.execute(
        "PRINT \"start\"\n"
        "GOTO finish\n"
        "PRINT \"skipped\"\n"
        "finish: PRINT \"label target\"\n", labelOutput);
    require(labelOutput.str() == "start\nlabel target\n",
            "GOTO did not jump to a named label");

    bool missingTargetRejected = false;
    try {
        ql32::Interpreter missing;
        std::ostringstream ignored;
        missing.execute("GOTO nowhere\n", ignored);
    } catch (const std::runtime_error& error) {
        missingTargetRejected = std::string(error.what()).find("GOTO label does not exist") !=
                                std::string::npos;
    }
    require(missingTargetRejected, "undefined GOTO label was not diagnosed");

    bool loopCrossingRejected = false;
    try {
        ql32::Interpreter crossing;
        std::ostringstream ignored;
        crossing.execute(
            "GOTO inside\n"
            "REPEAT loop\n"
            "inside: PRINT 1\n"
            "END REPEAT loop\n", ignored);
    } catch (const std::runtime_error& error) {
        loopCrossingRejected = std::string(error.what()).find("cannot enter or leave") !=
                               std::string::npos;
    }
    require(loopCrossingRejected, "GOTO into a structured loop was not rejected");
}

void testCancellation() {
    ql32::Interpreter interpreter;
    ql32::runtime::CancellationSource source;
    StopOnWriteBuffer buffer(source);
    std::ostream output(&buffer);
    bool cancelled = false;
    try {
        interpreter.execute("PRINT \"first\"\nPRINT \"second\"\n", output,
                            source.token());
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

void testDeleteProgramLinesAtomically() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive(
        "10 FOR x = 1 TO 2\n"
        "20 FOR y = 1 TO 2\n"
        "30 PRINT x\n"
        "40 END FOR y\n"
        "50 END FOR x\n", output);

    interpreter.executeInteractive("DELETE 20,40\n", output);
    std::ostringstream listing;
    interpreter.executeInteractive("LIST\n", listing);
    require(listing.str().find("20 ") == std::string::npos &&
                listing.str().find("40 ") == std::string::npos &&
                listing.str().find("10 FOR x") != std::string::npos &&
                listing.str().find("50 END FOR x") != std::string::npos,
            "DELETE line list did not remove the inner loop while retaining the outer loop");

    std::ostringstream runOutput;
    interpreter.executeInteractive("RUN\n", runOutput);
    require(runOutput.str() == "1\n2\n",
            "outer loop did not remain executable after deleting the inner loop");

    // Cross-line block structure is checked when RUN compiles, not at DELETE.
    interpreter.executeInteractive("DELETE 10,30\n", output);
    bool brokenBlockRejectedAtRun = false;
    try {
        interpreter.executeInteractive("RUN\n", runOutput);
    } catch (const std::runtime_error&) {
        brokenBlockRejectedAtRun = true;
    }
    require(brokenBlockRejectedAtRun, "RUN accepted a program with a broken block structure");
}

void testStopContinueReturnsToPromptAndResumesNextStatement() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive(
        "10 DATA 7,8\n"
        "20 READ a%: STOP: READ b%: PRINT a% ! b%\n", output);
    interpreter.executeInteractive("RUN\n", output);
    require(output.str().empty(), "STOP did not return before the following statement");

    interpreter.executeInteractive("a%=9\n", output);
    interpreter.executeInteractive("CONTINUE\n", output);
    require(output.str() == "9 8\n",
            "CONTINUE did not resume after STOP with variables and DATA position intact");

    interpreter.executeInteractive("RUN\n", output);
    require(output.str() == "9 8\n",
            "RUN did not stop at the new run's STOP statement");
    interpreter.executeInteractive("20 PRINT \"edited\"\n", output);
    bool continuationRejected = false;
    try {
        interpreter.executeInteractive("CONTINUE\n", output);
    } catch (const std::runtime_error& error) {
        continuationRejected = std::string(error.what()).find("no BASIC program stopped") !=
                               std::string::npos;
    }
    require(continuationRejected,
            "CONTINUE resumed stale bytecode after the stored program changed");
}

void testListDefinitionsAndVariables() {
    ql32::Interpreter interpreter;
    std::ostringstream setup;
    interpreter.executeInteractive(
        "10 DEFINE PROCEDURE Greet(who$, n)\n"
        "20 PRINT who$\n"
        "30 END DEFINE\n"
        "40 DEF FN Twice(x)\n"
        "50 RETURN x * 2\n"
        "60 END DEF\n"
        "70 DEFINE PROCEDURE Quiet\n"
        "80 END DEFINE\n", setup);
    const auto run = [&](const std::string& command) {
        std::ostringstream out;
        interpreter.executeInteractive(command + "\n", out);
        return out.str();
    };
    require(run("LIST PROC").find("Greet(who$, n)\nQuiet\n") != std::string::npos &&
                run("LIST PROC").find("Twice") == std::string::npos &&
                run("list procedures").find("20 PRINT") == std::string::npos,
            "LIST PROC did not list procedure signatures only");
    require(run("LIST FN") == "Twice(x)\n", "LIST FN did not list function signatures");
    require(run("LIST #1, function twice") == "40 DEFine FuNction Twice(x)\n"
                "50 RETurn x * 2\n60 END DEFine\n",
            "LIST FN name did not list the definition lines");
    require(run("LIST PROC greet #1").find("20 PRINT who$") != std::string::npos,
            "LIST PROC name with trailing channel failed");
    require(run("LIST 20").find("20 PRINT who$") != std::string::npos,
            "numeric LIST regressed");
    bool unknown = false;
    try { run("LIST PROC missing"); } catch (const std::runtime_error&) { unknown = true; }
    require(unknown, "LIST PROC with an unknown name did not fail");
    bool wrongKind = false;
    try { run("LIST FN Greet"); } catch (const std::runtime_error&) { wrongKind = true; }
    require(wrongKind, "LIST FN accepted a procedure name");

    ql32::Interpreter empty;
    std::ostringstream none;
    empty.executeInteractive("10 PRINT 1\nLIST PROC\nLIST FN\nLIST LOCALS\nLIST GLOBALS\n", none);
    require(none.str() == "", "empty definition/variable lists printed output");

    ql32::Interpreter vars;
    std::ostringstream varOutput;
    vars.executeInteractive("b$=\"x\"\na=1\nLIST GLOBALS\nGLOBALS\n", varOutput);
    require(varOutput.str() == "A = 1\nB$ = \"x\"\nA = 1\nB$ = \"x\"\n",
            "GLOBALS were not listed sorted");
}

void testHelpCommandCatalog() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.executeInteractive("10 PRINT \"kept\"\n", output);

    interpreter.executeInteractive("help\nPRINT \"after help\"\n", output);
    const std::string help = output.str();
    require(help.find("Shell commands:\n") != std::string::npos &&
                help.find("HELP, RUN, LIST, EDIT") != std::string::npos,
            "HELP did not list the shell commands");
    require(help.find("DELETE lineNumber1,... atomically deletes numbered program lines") !=
                std::string::npos && help.find("DELETE path$ removes a file") != std::string::npos,
            "HELP did not explain the two DELETE forms");
    require(help.find("BASIC statements and commands:\n") != std::string::npos &&
                help.find("SELect") != std::string::npos && help.find("WRITE") != std::string::npos,
            "HELP did not list BASIC keywords and commands");
    require(help.find("Built-in functions:\n") != std::string::npos &&
                help.find("LEN") != std::string::npos && help.find("GPALETTE$") != std::string::npos,
            "HELP did not list built-in functions");
    require(help.find("optional runtime services") != std::string::npos,
            "HELP did not explain optional service requirements");
    require(endsWith(help, "after help\n"),
            "HELP did not continue with the following interactive statement");

    std::ostringstream listing;
    interpreter.executeInteractive("LIST\n", listing);
    require(listing.str() == "10 PRINT \"kept\"\n",
            "HELP modified the stored program");
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

void testEditStoredLineFromRepl() {
    ql32::Interpreter replace;
    std::istringstream replaceInput(
        "10 PRINT \"old\"\n"
        "EDIT 10\n"
        "10 PRINT \"new\"\n"
        "LIST\n"
        "RUN\n");
    std::ostringstream replaceOutput;
    std::ostringstream replaceDiagnostics;
    replace.runRepl(replaceInput, replaceOutput, replaceDiagnostics);
    require(replaceDiagnostics.str().empty(),
            "EDIT replacement produced a REPL error");
    require(replaceOutput.str().find("Editing line: 10 PRINT \"old\"") !=
                std::string::npos &&
                replaceOutput.str().find("10 PRINT \"new\"\n") != std::string::npos &&
                endsWith(replaceOutput.str(), "new\n"),
            "EDIT did not prefill, replace, list, and run the selected line");

    ql32::Interpreter erase;
    std::istringstream eraseInput(
        "10 PRINT \"gone\"\n"
        "EDIT 10\n"
        "\n"
        "LIST\n");
    std::ostringstream eraseOutput;
    std::ostringstream eraseDiagnostics;
    erase.runRepl(eraseInput, eraseOutput, eraseDiagnostics);
    require(eraseDiagnostics.str().empty() &&
                eraseOutput.str() ==
                    "Editing line: 10 PRINT \"gone\" (enter a replacement line; blank deletes)\n",
            "blank response to EDIT did not delete the selected line");

    ql32::Interpreter invalid;
    std::istringstream invalidInput("10 PRINT \"kept\"\nEDIT 0\nEDIT 99\nLIST\n");
    std::ostringstream invalidOutput;
    std::ostringstream invalidDiagnostics;
    invalid.runRepl(invalidInput, invalidOutput, invalidDiagnostics);
    require(invalidDiagnostics.str().find("line number in 1..32767") !=
                std::string::npos &&
                invalidDiagnostics.str().find("line not found: 99") != std::string::npos &&
                invalidOutput.str().find("10 PRINT \"kept\"\n") != std::string::npos,
            "EDIT did not reject invalid or missing lines without changing the program");
}

void testEditWithoutLineNumberUsesLastOrFirstLine() {
    ql32::Interpreter firstLine;
    std::istringstream firstInput(
        "30 PRINT \"thirty\"\n"
        "10 PRINT \"ten\"\n"
        "20 PRINT \"twenty\"\n"
        "EDIT\n"
        "10 PRINT \"ten edited\"\n"
        "LIST\n");
    std::ostringstream firstOutput;
    std::ostringstream firstDiagnostics;
    firstLine.runRepl(firstInput, firstOutput, firstDiagnostics);
    require(firstDiagnostics.str().empty() &&
                firstOutput.str().find("Editing line: 10 PRINT \"ten\"") !=
                    std::string::npos &&
                firstOutput.str().find("10 PRINT \"ten edited\"\n") !=
                    std::string::npos,
            "EDIT without a line number did not select the first program line");

    ql32::Interpreter lastLine;
    std::istringstream lastInput(
        "10 PRINT \"ten\"\n"
        "20 PRINT \"twenty\"\n"
        "EDIT 20\n"
        "20 PRINT \"twenty edited\"\n"
        "EDIT\n"
        "20 PRINT \"twenty again\"\n"
        "LIST\n");
    std::ostringstream lastOutput;
    std::ostringstream lastDiagnostics;
    lastLine.runRepl(lastInput, lastOutput, lastDiagnostics);
    const std::string listing = lastOutput.str();
    const auto firstSelected = listing.find("Editing line: 20 PRINT \"twenty\"");
    const auto secondSelected = listing.find("Editing line: 20 PRINT \"twenty edited\"");
    require(lastDiagnostics.str().empty() && firstSelected != std::string::npos &&
                secondSelected != std::string::npos && secondSelected > firstSelected &&
                listing.find("20 PRINT \"twenty again\"\n") != std::string::npos,
            "EDIT without a line number did not revisit the last edited line");
}

void testSerialKeyDecoder() {
    ql32::platform::esp32::SerialKeyDecoder decoder;
    require(!decoder.feed('\x1b') && !decoder.feed('['),
            "ANSI cursor sequence prefix was exposed as text");
    const auto left = decoder.feed('D');
    require(left && left->key == ql32::platform::esp32::SerialKey::Left,
            "ANSI left arrow was not decoded");
    require(!decoder.feed('\x1b') && !decoder.feed('['),
            "ANSI Up sequence prefix was exposed as text");
    const auto up = decoder.feed('A');
    require(up && up->key == ql32::platform::esp32::SerialKey::Up,
            "ANSI Up arrow was not decoded");
    require(!decoder.feed('\x1b') && !decoder.feed('[') && !decoder.feed('1'),
            "ANSI Home sequence prefix was exposed as text");
    const auto home = decoder.feed('~');
    require(home && home->key == ql32::platform::esp32::SerialKey::Home,
            "ANSI Home key was not decoded");
    require(!decoder.feed('\x1b') && !decoder.feed('[') && !decoder.feed('4'),
            "ANSI End sequence prefix was exposed as text");
    const auto end = decoder.feed('~');
    require(end && end->key == ql32::platform::esp32::SerialKey::End,
            "ANSI End key was not decoded");
    const auto character = decoder.feed('x');
    require(character && character->key == ql32::platform::esp32::SerialKey::Character &&
                character->character == 'x',
            "printable serial input was not preserved after cursor sequences");
    require(!decoder.feed('\x1b') && !decoder.feed('[') && !decoder.feed('1') &&
                !decoder.feed(';') && !decoder.feed('5'),
            "Ctrl+Left sequence prefix was exposed as text");
    const auto controlLeft = decoder.feed('D');
    require(controlLeft && controlLeft->key == ql32::platform::esp32::SerialKey::Left &&
                controlLeft->control,
            "ANSI Ctrl+Left was not decoded with its modifier");
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
            "StoreText values\n", output);
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

void testCompactBlockTerminatorsFromCommandLine() {
    ql32::Interpreter interpreter;
    std::istringstream input(
        "10 IF 1 THEN\n"
        "20 PRINT \"if\"\n"
        "30 ENDIF\n"
        "40 FOR i=1 TO 1\n"
        "50 PRINT \"for\"\n"
        "60 ENDFOR i\n"
        "70 REPEAT loop\n"
        "80 PRINT \"rep\"\n"
        "90 EXIT loop\n"
        "100 ENDREP loop\n"
        "110 SELECT ON 1\n"
        "120 = 1\n"
        "130 PRINT \"sel\"\n"
        "140 ENDSEL\n"
        "150 DEF PROC work()\n"
        "160 PRINT \"def\"\n"
        "170 ENDDEF\n"
        "180 work\n"
        "LIST\n"
        "RUN\n");
    std::ostringstream output;
    std::ostringstream diagnostics;
    interpreter.runRepl(input, output, diagnostics);
    const std::string result = output.str();
    require(diagnostics.str().empty(),
            ("compact block terminators produced a REPL error: " +
             diagnostics.str()).c_str());
    for (const std::string& line : {
             "30 END IF", "60 END FOR i", "100 END REPeat loop",
             "140 END SELect", "170 END DEFine"})
        require(result.find(line) != std::string::npos,
                ("compact block terminator did not expand to " + line).c_str());
    require(result.find("if\nfor\nrep\nsel\ndef\n") != std::string::npos,
            "program with compact block terminators did not execute correctly");
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

    // Cross-line semantics (here call arity) are checked at RUN, not at line entry.
    ql32::Interpreter semantic;
    std::ostringstream listing;
    semantic.executeInteractive(
        "10 DEF FN Square(n)\n"
        "20 RET n * n\n"
        "30 END DEF\n"
        "40 PRINT Square()\n", listing);
    bool badArityRejected = false;
    try {
        semantic.executeInteractive("RUN\n", listing);
    } catch (const std::runtime_error& error) {
        badArityRejected = std::string(error.what()).find("incorrect argument count") !=
                           std::string::npos;
    }
    require(badArityRejected, "RUN accepted a known function arity mismatch");
}

void testLineEntryChecksOnlyTheLineItself() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    // Cross-line references, orphan terminators, and arm lines are accepted at
    // entry; they are resolved when RUN compiles the whole program.
    for (const char* line : {"10 GOTO 999\n", "20 END IF\n", "30 NEXT i\n", "40 RETURN 1\n",
                             "50 = 1\n", "60 RESTORE 70\n", "70 PRINT Unknown(1, 2)\n"})
        interpreter.executeInteractive(line, output);

    bool lexical = false;
    bool syntax = false;
    try { interpreter.executeInteractive("80 PRINT \"open\n", output); }
    catch (const std::runtime_error&) { lexical = true; }
    try { interpreter.executeInteractive("90 PRINT (\n", output); }
    catch (const std::runtime_error&) { syntax = true; }
    require(lexical && syntax, "line entry accepted a lexically or syntactically bad line");

    std::ostringstream listing;
    interpreter.executeInteractive("LIST\n", listing);
    require(listing.str().find("80 ") == std::string::npos &&
                listing.str().find("90 ") == std::string::npos &&
                listing.str().find("10 GOTO 999") != std::string::npos,
            "rejected lines changed the stored program");

    bool runRejected = false;
    try { interpreter.executeInteractive("RUN\n", output); }
    catch (const std::runtime_error&) { runRejected = true; }
    require(runRejected, "RUN accepted a program with unresolved cross-line references");
}

class FixedMemory final : public ql32::runtime::MemoryService {
public:
    FixedMemory(std::uint64_t free, std::uint64_t largest) : free_(free), largest_(largest) {}
    std::uint64_t freeBytes() const override { return free_; }
    std::optional<ql32::runtime::HeapFragmentationInfo> fragmentationInfo() const override {
        return ql32::runtime::HeapFragmentationInfo{largest_, 1, 1};
    }
private:
    std::uint64_t free_, largest_;
};

void testProgramStoreKeepsOrderAcrossEditsAndCompaction() {
    ql32::ProgramStore store;
    for (int i = 50; i >= 1; --i) store.set(i * 10, "PRINT " + std::to_string(i));
    for (int round = 0; round < 40; ++round)
        for (int i = 1; i <= 50; i += 2) store.set(i * 10, "PRINT replaced " + std::to_string(round));
    store.erase(20);
    store.erase(21);
    require(store.size() == 49, "program store lost or kept lines on erase");
    int previous = 0;
    for (const auto& [number, body] : store) {
        require(number > previous && !body.empty(), "program store order broken");
        previous = number;
    }
    require(store.find(10)->second == "PRINT replaced 39", "replaced line has stale text");
    require(store.find(40)->second == "PRINT 4", "untouched line changed by compaction");
    require(store.find(20) == store.end(), "erased line still found");
    ql32::ProgramStore other;
    other.set(5, "REM");
    store.swap(other);
    require(store.size() == 1 && other.size() == 49, "program store swap failed");
}

void testRunRefusesProgramsThatCannotFit() {
    std::string program;
    for (int i = 1; i <= 100; ++i)
        program += std::to_string(i * 10) + " v = v + " + std::to_string(i) + " : PRINT v\n";
    auto run = [&](std::uint64_t free, std::uint64_t largest, std::string& message) {
        ql32::Interpreter interpreter({}, {}, {}, {}, {}, std::make_shared<FixedMemory>(free, largest));
        std::ostringstream output;
        try { interpreter.execute(program, output); return true; }
        catch (const std::runtime_error& error) { message = error.what(); return false; }
    };
    std::string message;
    require(run(1u << 20, 1u << 20, message), "roomy memory refused a small program");
    require(!run(1u << 20, 1024, message) &&
                message.find("not enough memory for program of ") != std::string::npos &&
                message.find("contiguous") != std::string::npos,
            "fragmented heap did not refuse the program with a clear message");
    require(!run(4096, 1u << 20, message) &&
                message.find("not enough memory for program of ") != std::string::npos,
            "low free memory did not refuse the program with a clear message");
}

void testAdmissionCountsBytecodeTokensNotBrackets() {
    // ~2400 tokens but only ~600 bytecode-producing ones: must fit in 100 KB.
    std::string program;
    for (int i = 1; i <= 200; ++i) program += "a = ((((((((1))))))))\n";
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, std::make_shared<FixedMemory>(100000, 1u << 20));
    std::ostringstream output;
    interpreter.execute(program, output);
    require(true, "bracket-heavy program admitted");
}

void testProcedureFunctionCallSyntax() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "DEFINE FUNCTION AddOne(value)\n"
        "  RETURN value + 1\n"
        "END DEFINE\n"
        "DEFINE PROCEDURE Show(value)\n"
        "  PRINT value\n"
        "END DEFINE\n"
        "Show AddOne(4)\n", output);
    require(output.str() == "5\n",
            "statement procedure and expression function syntax did not compose");

    bool parenthesizedProcedureRejected = false;
    try {
        interpreter.execute("Show(4)\n", output);
    } catch (const std::runtime_error& error) {
        parenthesizedProcedureRejected = std::string(error.what()).find(
            "procedure calls use command syntax") != std::string::npos;
    }
    require(parenthesizedProcedureRejected,
            "user-defined procedure accepted function-call parentheses");

    bool functionParenthesesRequired = false;
    try {
        ql32::Interpreter functionWithoutParens;
        functionWithoutParens.execute(
            "DEFINE FUNCTION AddOne(value)\n"
            "  RETURN value + 1\n"
            "END DEFINE\n"
            "PRINT AddOne\n", output);
    } catch (const std::runtime_error& error) {
        functionParenthesesRequired = std::string(error.what()).find(
            "function call requires parentheses") != std::string::npos;
    }
    require(functionParenthesesRequired,
            "user-defined function was accepted without parentheses");
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

void testConditionalRepeat() {
    auto run = [](const std::string& source) {
        ql32::Interpreter interpreter;
        std::ostringstream output;
        interpreter.execute(source, output);
        return output.str();
    };
    require(run("a=0\n"
                "REPeat pre WHILE a < 3\n"
                " a=a+1\n"
                " SELect ON a\n"
                " =REMAINDER\n"
                " NEXT pre\n"
                " END SELect\n"
                "END REPeat pre\n"
                "PRINT a\n"
                "REPeat post\n"
                " a=a-1\n"
                " NEXT post\n"
                "END REPeat post UNTIL a=0\n"
                "PRINT a\n"
                "REPeat zero WHILE 0\n"
                " PRINT \"never\"\n"
                "END REPeat zero\n"
                "PRINT \"ok\"\n") == "3\n0\nok\n",
            "conditional REPeat WHILE/UNTIL/NEXT behavior");
    for (const std::string source : {"REPeat x WHILE 1\nEND REPeat x UNTIL 1\n",
                                      "REPeat x WHILE 1: a=1\n"}) {
        bool rejected = false;
        try { run(source); } catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid conditional REPeat was accepted");
    }
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
        "PRINT \"Hello world\"(1 TO 5)\n"
        "PRINT \"Hello world\"(1)\n"
        "unicode$ = \"café🙂\"\n"
        "PRINT unicode$(4 TO 5)\n"
        "PRINT LEN(text$)\n"
        "PRINT LEN(unicode$)\n"
        "PRINT LEN(text$(TO 5))\n"
        "PRINT LEN(\"\")\n", output);
    require(output.str() == "H\nHello\nHello\nld\nlo world\nlo wo\nHello\nH\né🙂\n"
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

void testValAndDynamicEvaluation() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "text$ = \"ahoj\"\n"
        "expression$ = \"text$(1 TO 2)\"\n"
        "PRINT EVAL$(expression$)\n"
        "expression$ = \"\"\"ahoj\"\"(1 TO 2)\"\n"
        "PRINT EVAL$(expression$)\n"
        "PRINT VAL(\"1.25E2\")\n"
        "PRINT EVAL(\"2 + 3 * 4\")\n"
        "PRINT EVAL$(\"2 + 3\")\n"
        "PRINT EVAL$(\"LEFT$(text$, 2)\")\n"
        "expression$ = \"TakeTwo$(text$)\"\n"
        "PRINT EVAL$(expression$)\n"
        "PRINT Calculate(4)\n"
        "DEFine FuNction TakeTwo$(value$)\n"
        "RETurn LEFT$(value$, 2)\n"
        "END DEFine\n"
        "DEFine FuNction Calculate(x)\n"
        "LOCal expression$\n"
        "expression$ = \"x + 1\"\n"
        "RETurn EVAL(expression$)\n"
        "END DEFine\n", output);
    require(output.str() == "ah\nah\n125\n14\n5\nah\nah\n5\n",
            "VAL, EVAL, or EVAL$ returned an incorrect result");

    ql32::Interpreter doublePrecision;
    std::ostringstream doubleOutput;
    doublePrecision.execute("PRINT EVAL(\"16777216 + 1 - 16777216\")\n", doubleOutput);
    require(doubleOutput.str() == "1\n",
            "EVAL did not preserve double-precision expression semantics");
    ql32::Interpreter singlePrecision;
    std::ostringstream singleOutput;
    singlePrecision.execute(
        "CONST KOptMathDouble = 0\n"
        "CONST KOptMathSingle = 1\n"
        "OPTION KOptMathSingle\n"
        "PRINT EVAL(\"16777216 + 1 - 16777216\")\n", singleOutput);
    require(singleOutput.str() == "0\n",
            "EVAL did not honor single-precision expression semantics");

    for (const std::string& source : {
             "PRINT VAL(\"5.6.7\")\n",
             "PRINT VAL(\"45.6*3.1\")\n",
             "PRINT EVAL(\"1 +\")\n",
             "PRINT EVAL(\"\"\"text\"\"\")\n"}) {
        bool rejected = false;
        try {
            std::ostringstream ignored;
            interpreter.execute(source, ignored);
        } catch (const ql32::runtime::Error&) {
            rejected = true;
        }
        require(rejected, "VAL or EVAL accepted invalid input or result type");
    }
}

void testStringExtractionFunctions() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "text$=\"QL/32\"\n"
        "PRINT LEFT$(text$,0)\n"
        "PRINT LEFT$(text$,3)\n"
        "PRINT LEFT$(text$,99)\n"
        "PRINT RIGHT$(text$,2)\n"
        "PRINT RIGHT$(text$,0)\n"
        "PRINT MID$(text$,2,3)\n"
        "PRINT MID$(text$,3)\n"
        "PRINT MID$(text$,7,1)\n"
        "unicode$=\"caf\u00e9\U0001f642\"\n"
        "PRINT LEN(unicode$)\n"
        "PRINT LEFT$(unicode$,4)\n"
        "PRINT RIGHT$(unicode$,2)\n"
        "PRINT MID$(unicode$,4,2)\n", output);
    require(output.str() ==
                "\nQL/\nQL/32\n32\n\nL/3\n/32\n\n5\ncaf\u00e9\n\u00e9\U0001f642\n\u00e9\U0001f642\n",
            "string extraction functions returned incorrect character slices");

    std::ostringstream repeated;
    interpreter.execute(
        "PRINT REPT$(\"ab\",3)\n"
        "PRINT REPT$(\"🙂é\",2)\n"
        "PRINT REPT$(\"x\",0)\n"
        "PRINT REPT$(\"\",5)\n", repeated);
    require(repeated.str() == "ababab\n🙂é🙂é\n\n\n",
            "REPT$ did not repeat the complete string the requested number of times");

    std::ostringstream charactersAndSearch;
    interpreter.execute(
        "PRINT CODE(\"café🙂\")\n"
        "PRINT ASC(\"🙂\")\n"
        "PRINT CODE(\"\")\n"
        "PRINT CHR$(65)\n"
        "PRINT CHR$(&H1F642)\n"
        "PRINT LOC(\"Hello world\",\"WORLD\")\n"
        "PRINT LOC(\"café🙂\",\"é🙂\")\n"
        "PRINT LOC(\"abc\",\"\")\n"
        "PRINT LOC(\"abc\",\"z\")\n"
        "PRINT UPPER$(\"Hello ql/32 Hé\")\n"
        "PRINT LOWER$(\"Hello ql/32 Hé\")\n"
        "PRINT STR$(12.5)\n"
        "PRINT STR$(-0)\n", charactersAndSearch);
    require(charactersAndSearch.str() ==
                "99\n128578\n0\nA\n🙂\n7\n4\n1\n0\nHELLO QL/32 Hé\n"
                "hello ql/32 hé\n12.5\n0\n",
            "character conversion or LOC returned an unexpected result");

    for (const std::string& expression : {
             "PRINT LEFT$(\"x\",-1)\n",
             "PRINT RIGHT$(\"x\",1.5)\n",
             "PRINT MID$(\"x\",0)\n",
             "PRINT MID$(\"x\",1,-1)\n",
             "PRINT REPT$(\"x\",-1)\n",
             "PRINT REPT$(\"x\",1.5)\n",
             "PRINT CHR$(-1)\n",
             "PRINT CHR$(55296)\n",
             "PRINT CHR$(1.5)\n"}) {
        bool rejected = false;
        try {
            std::ostringstream ignored;
            interpreter.execute(expression, ignored);
        } catch (const ql32::runtime::Error& error) {
            rejected = error.code() == ql32::runtime::ErrorCode::InvalidArgument;
        }
        require(rejected, "string extraction accepted an invalid index or length");
    }
    for (const std::string& expression : {
             "PRINT UPPER$(3)\n",
             "PRINT LOWER$(3)\n",
             "PRINT STR$(\"3\")\n",
             "PRINT LOC(\"abc\",3)\n"}) {
        bool typeRejected = false;
        try {
            std::ostringstream ignored;
            interpreter.execute(expression, ignored);
        } catch (const ql32::runtime::Error& error) {
            typeRejected = error.code() == ql32::runtime::ErrorCode::TypeMismatch;
        }
        require(typeRejected, "string helper accepted an argument of the wrong type");
    }
}

void testUnsignedBitFunctionsAndRadixLiterals() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "PRINT &HCAFE\n"
        "PRINT &b101101\n"
        "PRINT BAND(&HCAFE,&H0FF0)\n"
        "PRINT HEX$(BAND(&HCAFE,&H0FF0))\n"
        "PRINT BOR(&HCAFE,&H0030)\n"
        "PRINT HEX$(BXOR(&HCAFE,&HFFFF))\n"
        "PRINT HEX$(BNOT(0))\n"
        "PRINT BIN$(0)\n"
        "PRINT ROL(&H80000001,1)\n"
        "PRINT HEX$(ROR(&H80000001,1))\n"
        "PRINT HEX$(SRA(&H80000000,1))\n"
        "PRINT HEX$(SRL(&H80000000,1))\n"
        "PRINT HEX$(SLA(&H80000001,1))\n"
        "PRINT HEX$(SLL(&H80000000,1))\n"
        "PRINT HEX$(SRA(&H80000000,32))\n"
        "PRINT HEX$(SRA(&H7FFFFFFF,40))\n"
        "PRINT HEX$(SRL(&HFFFFFFFF,32))\n"
        "PRINT HEX$(SLA(&HFFFFFFFF,40))\n"
        "PRINT HEX$(SLL(0,32))\n"
        "PRINT HEX$(SLL(0,40))\n"
        "PRINT ROL(1,-1)\n"
        "PRINT ROL(1,33)\n"
        "PRINT HEX$(&HFFFFFFFF)\n"
        "PRINT BIN$(&HFFFFFFFF)\n", output);
    require(output.str() ==
                "51966\n45\n2800\nAF0\n51966\n3501\nFFFFFFFF\n0\n3\nC0000000\nC0000000\n40000000\n2\n1\nFFFFFFFF\n0\n0\n0\nFFFFFFFF\nFFFFFFFF\n2147483648\n2\nFFFFFFFF\n11111111111111111111111111111111\n",
            "32-bit bit functions or radix literals returned incorrect values");

    for (const std::string& source : {
             "PRINT &H100000000\n", "PRINT &B102\n", "PRINT &H\n",
             "PRINT BAND(-1,0)\n", "PRINT BNOT(1.5)\n", "PRINT ROR(1,0.5)\n",
             "PRINT SRA(1,-1)\n", "PRINT SRL(1,0.5)\n",
             "PRINT SLA(1,-1)\n", "PRINT SLL(1,1.5)\n"}) {
        bool rejected = false;
        try {
            std::ostringstream ignored;
            interpreter.execute(source, ignored);
        } catch (const ql32::runtime::Error& error) {
            rejected = error.code() == ql32::runtime::ErrorCode::InvalidArgument ||
                       error.code() == ql32::runtime::ErrorCode::Syntax;
        }
        require(rejected, "bit functions accepted an invalid integer or radix literal");
    }
}

void testTrigonometricFunctionsUseRadians() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "PRINT SIN(0)\n"
        "PRINT COS(0)\n"
        "PRINT SIN(1.5707963267948966)\n"
        "PRINT COS(3.141592653589793)\n"
        "PRINT SIN(-0.5235987755982988)\n"
        "PRINT INT(2.8)\n"
        "PRINT INT(-2.8)\n", output);
    std::istringstream values(output.str());
    const double expected[] = {0, 1, 1, -1, -0.5, 2, -3};
    for (const double value : expected) {
        double actual = 0;
        require(static_cast<bool>(values >> actual) && std::fabs(actual - value) < 1e-12,
                "SIN or COS did not use radian input");
    }
    std::string extra;
    require(!(values >> extra), "SIN or COS produced extra values");
}

void testRandomNumberFunctions() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "RANDOMIZE 1234\n"
        "unit=RND()\n"
        "scaled=RND(10)\n"
        "ranged=RND(-2,3)\n"
        "PRINT unit>=0 AND unit<1\n"
        "PRINT scaled>=0 AND scaled<10\n"
        "PRINT ranged>=-2 AND ranged<3\n"
        "PRINT RND(0)\n"
        "PRINT RND(5,5)\n"
        "RANDOMIZE 1234\n"
        "PRINT RND()=unit\n", output);
    require(output.str() == "1\n1\n1\n0\n5\n1\n",
            "RND bounds, endpoint handling, or RANDOMIZE reproducibility failed");

    ql32::Interpreter first;
    ql32::Interpreter second;
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;
    first.execute("RANDOMIZE 42\nfirst=RND()\n", firstOutput);
    second.execute("RANDOMIZE 42\nsecond=RND()\nsecondNext=RND()\n", secondOutput);
    first.execute("firstNext=RND()\nPRINT first;firstNext\n", firstOutput);
    second.execute("PRINT second;secondNext\n", secondOutput);
    require(firstOutput.str() == secondOutput.str(),
            "random generator state was shared between BASIC sessions");

    for (const std::string& source : {
             "PRINT RND(1,2,3)\n", "RANDOMIZE 1.5\n",
             "RANDOMIZE 4294967296\n", "PRINT RND(-1)\n",
             "PRINT RND(2,1)\n"}) {
        bool rejected = false;
        try {
            ql32::Interpreter invalid;
            std::ostringstream ignored;
            invalid.execute(source, ignored);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "random-number function accepted invalid arguments");
    }
}

void testPauseDurationRoundsUpAndResumes() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    require(interpreter.executeResumable(
                "PAUSE 1.1\nPRINT \"resumed\"\n", output) ==
                ql32::ExecutionStatus::WaitingTime,
            "PAUSE did not yield a timed wait");
    require(interpreter.requestedPauseDurationMs() == 2,
            "fractional PAUSE delay did not round up to milliseconds");
    require(interpreter.resume(output) == ql32::ExecutionStatus::WaitingTime,
            "early resume bypassed the remaining PAUSE delay");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    require(interpreter.resume(output) == ql32::ExecutionStatus::Completed &&
                output.str() == "resumed\n",
            "timed execution did not resume at the instruction after PAUSE");

    bool rejectedNegative = false;
    try {
        interpreter.executeResumable("PAUSE -1\n", output);
    } catch (const ql32::runtime::Error&) {
        rejectedNegative = true;
    }
    require(rejectedNegative, "negative PAUSE delay was accepted");
}

void testFreeReturnsAllMetricsWithoutSelector() {
    auto memory = std::make_shared<RecordingMemoryService>();
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, memory);
    std::ostringstream output;
    interpreter.execute(
        "PRINT FREE()\n"
        "PRINT FREE(0)\n"
        "PRINT FREE(1)\n"
        "PRINT FREE(2)\n"
        "PRINT FREE(3)\n", output);
    require(output.str() == "123456,65432,17,445\n123456\n65432\n17\n445\n",
            "FREE() did not return all heap metrics or selector forms changed");
}

void testFreeLeavesUnavailableMetricsAsEmptyCsvFields() {
    auto memory = std::make_shared<FreeOnlyMemoryService>();
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, memory);
    std::ostringstream output;
    interpreter.execute("PRINT FREE()\n", output);
    require(output.str() == "123456,,,\n",
            "FREE() did not preserve CSV positions for unavailable metrics");
}

void testDimRejectsArrayLargerThanLargestFreeBlock() {
    auto memory = std::make_shared<RecordingMemoryService>();
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, memory);
    std::ostringstream output;
    interpreter.execute("DIM small(100)\nPRINT \"ok\"\n", output);
    require(output.str() == "ok\n", "DIM within the largest free block failed");
    // Numeric elements are 8 B, so 5001 elements (about 40 KB) fit in 65,432 B.
    interpreter.execute("DIM packed(5000)\nPACKED(5000)=7\nPRINT packed(5000)\n", output);
    require(output.str() == "ok\n7\n", "numeric array did not use compact elements");
    bool rejected = false;
    try {
        interpreter.execute("DIM big(9000)\n", output);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("array exceeds available memory") !=
            std::string::npos;
    }
    require(rejected, "DIM larger than the largest free block was not rejected");
}

void testEmergencyReserveCanBeReleasedAndRestored() {
    FreeOnlyMemoryService memory;
    require(memory.emergencyReserveHeld(),
            "disabled emergency reserve should not appear released");
    require(memory.initializeEmergencyReserve(4096) &&
                memory.emergencyReserveHeld() &&
                memory.emergencyReserveBytes() == 4096,
            "memory service did not hold its configured emergency reserve");
    {
        ql32::runtime::EmergencyReserveLease lease(&memory);
        require(!memory.emergencyReserveHeld(),
                "emergency reserve lease did not release reserved memory");
    }
    require(memory.emergencyReserveHeld() &&
                memory.emergencyReserveBytes() == 4096,
            "emergency reserve lease did not restore reserved memory");
    require(!memory.initializeEmergencyReserve(2048),
            "memory service silently changed an existing reserve size");
}

void testProgramNumericPrecisionOption() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute("PRINT 16777216 + 1 - 16777216\n", output);
    require(output.str() == "1\n", "default numeric precision is not double precision");

    interpreter.execute(
        "CONST KOptMathDouble = 0\n"
        "CONST KOptMathSingle = 1\n"
        "OPTION KOptMathSingle\n"
        "PRINT 16777216 + 1 - 16777216\n"
        "PRINT SIN(1)\n", output);
    std::istringstream singleValues(output.str().substr(2));
    double rounded = -1;
    double sine = 0;
    require(static_cast<bool>(singleValues >> rounded >> sine) && rounded == 0.0 &&
                std::fabs(sine - static_cast<double>(std::sin(1.0f))) < 1e-6,
            "SINGLE precision did not round intermediate arithmetic and trig to binary32");

    interpreter.execute(
        "CONST KOptMathDouble = 0\n"
        "CONST KOptMathSingle = 1\n"
        "OPTION KOptMathDouble\n"
        "PRINT 16777216 + 1 - 16777216\n", output);
    require(endsWith(output.str(), "1\n"),
            "DOUBLE precision option did not restore binary64 calculations");

    for (const std::string& source : {
             "CONST KOptMathSingle = 1\nPRINT 1\nOPTION KOptMathSingle\n",
             "CONST KOptMathSingle = 1\nIF 1 THEN\nOPTION KOptMathSingle\nEND IF\n",
             "CONST KOptMathSingle = 1\nOPTION KOptMathSingle\nOPTION KOptMathSingle\n"}) {
        bool rejected = false;
        try {
            std::ostringstream ignored;
            interpreter.execute(source, ignored);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "numeric precision option accepted an invalid location or duplicate");
    }
}

void testCpuFrequencyOption() {
    for (const std::uint16_t mhz : {80, 160, 240}) {
        auto service = std::make_shared<RecordingCpuFrequencyService>();
        ql32::Interpreter interpreter({}, {}, {}, {}, {}, {}, {}, 0, {}, service);
        std::ostringstream output;
        const std::string source = std::string(
            "CONST KEspCpuFreq80 = 80\n"
            "CONST KEspCpuFreq160 = 160\n"
            "CONST KEspCpuFreq240 = 240\n"
            "CONST KOptMathSingle = 1\n"
            "OPTION KOptMathSingle\n") +
            "OPTION KEspCpuFreq" + std::to_string(mhz) + "\n"
            "PRINT 16777216 + 1 - 16777216\n"
            "PRINT CPUFREQ()\n";
        interpreter.execute(source, output);
        require(service->calls == 1 && service->lastFrequency == mhz,
                "CPU frequency option did not call the platform service");
        require(output.str() == "0\n" + std::to_string(mhz) + "\n",
                "CPU frequency option did not coexist with math precision or readback");
    }

    ql32::Interpreter host;
    std::ostringstream ignoredOutput;
    host.execute(
        "CONST KEspCpuFreq160 = 160\n"
        "OPTION KEspCpuFreq160\n"
        "PRINT 42\n"
        "PRINT CPUFREQ()\n", ignoredOutput);
    require(ignoredOutput.str() == "42\n0\n",
            "CPU frequency option was not harmless without a platform adapter");

    for (const std::string& source : {
             "CONST KEspCpuFreq160 = 160\nOPTION KEspCpuFreq160\n"
             "OPTION KEspCpuFreq160\n",
             "CONST KEspCpuFreq120 = 120\nOPTION KEspCpuFreq120\n"}) {
        bool rejected = false;
        try {
            ql32::Interpreter interpreter;
            std::ostringstream output;
            interpreter.execute(source, output);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "unsupported or duplicate CPU frequency OPTION was accepted");
    }
}

void testQ32ErrorFunctions() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    interpreter.execute(
        "PRINT ERR\n"
        "PRINT ERR$(0)\n"
        "PRINT ERR$(22)\n"
        "PRINT ERR$(23)\n"
        "PRINT ERRX$\n", output);
    require(output.str() == "0\nNo error\nResource is already in use\nUnknown QL/32 error code 23\n\n",
            "ERR, ERR$, or ERRX$ returned an incorrect value");
}

void testUiCallsSuspendAndResumePerSession() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter first({}, {}, service);
    ql32::Interpreter second({}, {}, service);
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;

    const auto firstStatus = first.executeResumable(
        "value = 41\nALERT \"Saved\", \"Program saved\"\n"
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
    require(endsWith(replOutput.str(), "1\n") && replDiagnostics.str().empty(),
            "REPL host presenter did not resume a stored program after confirmation");
}

void testInfoToastStyleOptionChains() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, service);
    std::ostringstream output;
    interpreter.execute(
        "IPRINT INK 3, PAPER 6, FLASH 1, \"hello\", 4, 128\n", output);
    const auto toast = service->currentInfo();
    require(toast && toast->text == "hello" && toast->durationMs == 4000 &&
            toast->positionX == 1 && toast->positionY == -1 &&
            toast->ink == 3 && toast->paper == 6 && toast->displayMode == 1,
            "IPRINT style chain did not preserve color, mode, duration, and position");

    interpreter.execute(
        "BUSY PAPER 2, INK 5, FLASH 2, \"working\", 64\n", output);
    require(interpreter.executeResumable("BUSY OFF\n", output) ==
                ql32::ExecutionStatus::Completed,
            "BUSY style chain did not execute or BUSY OFF did not clear it");
}

void testDialogMenuBasicCallsSuspendAndResume() {
    auto service = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, service);
    std::ostringstream output;
    const std::string source =
        "dialogHandle = DINIT(\"Profile\", 127)\n"
        "DTEXT dialogHandle, \"Profile fields\"\n"
        "DEDIT dialogHandle, \"Name\", \"Ada\"\n"
        "DINT dialogHandle, \"Age\", 36, 0, 150\n"
        "DFLOAT dialogHandle, \"Score\", 1.5, 0, 10\n"
        "DCHOICE dialogHandle, \"Color\", 2, \"Red|Green|Blue\"\n"
        "DCHECK dialogHandle, \"Active\", 1\n"
        "DBUTTONS dialogHandle, \"Apply\", \"Cancel\"\n"
        "accepted = DIALOG(dialogHandle)\n"
        "PRINT accepted\nPRINT DGET$(dialogHandle, 2)\n"
        "PRINT DGET(dialogHandle, 3)\nPRINT DGET(dialogHandle, 4)\n"
        "PRINT DGET(dialogHandle, 5)\nPRINT DGET(dialogHandle, 6)\n"
        "menuHandle = MINIT(\"Main\")\n"
        "MCARD menuHandle, 17, \"Open\", \"O\"\n"
        "MCARD menuHandle, 91, \"Exit\", \"X\"\n"
        "selected = MENU(menuHandle)\nPRINT selected\n";
    require(interpreter.executeResumable(source, output) == ql32::ExecutionStatus::Suspended,
            "BASIC DIALOG did not suspend while awaiting a presenter");
    const auto dialog = service->takeNextRequest();
    require(dialog && dialog->kind == ql32::ui::RequestKind::Dialog &&
                dialog->fields.size() == 6 && dialog->acceptLabel == "Apply" &&
                dialog->cancelLabel == "Cancel" &&
                dialog->dialogOptions.buttonsRight && dialog->dialogOptions.noTitle &&
                dialog->dialogOptions.fullScreen && dialog->dialogOptions.noDrag &&
                dialog->dialogOptions.densePack && dialog->dialogOptions.hideButtons &&
                dialog->dialogOptions.positionX == -1 &&
                dialog->dialogOptions.positionY == -1,
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
                endsWith(output.str(), "91\n"),
            "BASIC MENU did not return the selected command ID");

    auto cancelService = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter cancelled({}, {}, cancelService);
    std::ostringstream cancelledOutput;
    require(cancelled.executeResumable(
                "dialogHandle = DINIT(\"Cancel test\")\naccepted = DIALOG(dialogHandle)\nPRINT accepted\n",
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
    repl.execute("dialogHandle = DINIT(\"REPL dialog\")\n", replOutput);
    repl.execute("DEDIT dialogHandle, \"Name\", \"Initial\"\n", replOutput);
    require(repl.executeResumable(
                "accepted = DIALOG(dialogHandle)\nPRINT DGET$(dialogHandle, 1)\n", replOutput) ==
                ql32::ExecutionStatus::Suspended,
            "dialog handle did not survive separate REPL entries");
    const auto replDialog = replService->takeNextRequest();
    require(replDialog && replService->submitDialog(replDialog->id, {std::string("Persistent")}),
            "REPL dialog did not accept its submitted value");
    require(repl.resume(replOutput) == ql32::ExecutionStatus::Completed &&
                replOutput.str() == "Persistent\n",
            "REPL dialog getter did not return the submitted value");
}

void testPrintLinesAreChargedToSliceBudget() {
    ql32::Interpreter plain;
    std::ostringstream plainOutput;
    int plainSlices = 1;
    auto status = plain.executeSlice(
        "FOR i = 1 TO 20\n a = a + 1\nEND FOR i\n", plainOutput, 128);
    while (status == ql32::ExecutionStatus::Yielded) {
        ++plainSlices;
        status = plain.resumeSlice(plainOutput, 128);
    }
    ql32::Interpreter printing;
    std::ostringstream printOutput;
    int printSlices = 1;
    status = printing.executeSlice(
        "FOR i = 1 TO 20\n PRINT \"x\"\nEND FOR i\n", printOutput, 128);
    while (status == ql32::ExecutionStatus::Yielded) {
        ++printSlices;
        status = printing.resumeSlice(printOutput, 128);
    }
    require(status == ql32::ExecutionStatus::Completed &&
                printOutput.str().size() == 40,
            "budgeted PRINT loop did not complete correctly");
    require(printSlices >= plainSlices * 3,
            "PRINT lines were not charged to the slice budget");
}

} // namespace

int main() {
    try {
        testPrintLinesAreChargedToSliceBudget();
        testInterpreterSessions();
        testDiagnosticsUseBasicLineNumbers();
        testEmbeddedDataReadRestoreAndEof();
        testGotoLineNumbersAndLabels();
        testCancellation();
        testAtomicProgramLineAndBlocks();
        testDeleteProgramLinesAtomically();
        testStopContinueReturnsToPromptAndResumesNextStatement();
        testListDefinitionsAndVariables();
        testHelpCommandCatalog();
        testPersistentRepl();
        testEditStoredLineFromRepl();
        testEditWithoutLineNumberUsesLastOrFirstLine();
        testSerialKeyDecoder();
        testStoredProgramDiagnosticsAndArrayTypes();
        testKeywordAbbreviationCoverage();
        testCompactBlockTerminatorsFromCommandLine();
        testForwardCallsAndKnownSemanticErrors();
        testLineEntryChecksOnlyTheLineItself();
        testProgramStoreKeepsOrderAcrossEditsAndCompaction();
        testRunRefusesProgramsThatCannotFit();
        testAdmissionCountsBytecodeTokensNotBrackets();
        testProcedureFunctionCallSyntax();
        testForRangesEvaluateOnceInSourceOrder();
        testConditionalRepeat();
        testSuperBasicStringSlices();
        testValAndDynamicEvaluation();
        testStringExtractionFunctions();
        testUnsignedBitFunctionsAndRadixLiterals();
        testTrigonometricFunctionsUseRadians();
        testRandomNumberFunctions();
        testPauseDurationRoundsUpAndResumes();
        testFreeReturnsAllMetricsWithoutSelector();
        testFreeLeavesUnavailableMetricsAsEmptyCsvFields();
        testDimRejectsArrayLargerThanLargestFreeBlock();
        testEmergencyReserveCanBeReleasedAndRestored();
        testProgramNumericPrecisionOption();
        testCpuFrequencyOption();
        testQ32ErrorFunctions();
        testUiCallsSuspendAndResumePerSession();
        testInfoToastStyleOptionChains();
        testDialogMenuBasicCallsSuspendAndResume();
    } catch (const std::exception& error) {
        std::cerr << "runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
