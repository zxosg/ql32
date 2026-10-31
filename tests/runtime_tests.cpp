#include "ql32/interpreter.hpp"

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
    } catch (const std::exception& error) {
        std::cerr << "runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
