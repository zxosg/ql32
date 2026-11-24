#include "ql32/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <new>

// Failure injection verifies that VM recovery itself does not allocate.
static bool rejectAllocations = false;
void* operator new(std::size_t size) {
    if (rejectAllocations) throw std::bad_alloc();
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace ql32;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string run(const std::string& source) { Interpreter interpreter; std::ostringstream out; interpreter.execute(source, out); return out.str(); }
void loopsAndErrors() {
    require(run(R"(
a=0
REPeat pre WHILE a < 3
 a=a+1
 SELect ON a
 =REMAINDER
 NEXT pre
 END SELect
END REPeat pre
PRINT a
REPeat post
 a=a-1
 NEXT post
END REPeat post UNTIL a=0
PRINT a
REPeat zero WHILE 0
 RAISE 1
END REPeat zero
PRINT "ok"
)") == "3\n0\nok\n", "conditional loop/NEXT/SELECT behavior");
    require(run(R"(
a=42
TRAP a=1/0 : PRINT ERR
PRINT a
TRAP a$=3
PRINT ERR
TRAP a%=2147483648
PRINT ERR
TRAP a=2
PRINT ERR
TRAP Fail
PRINT ERR
PRINT "alive"
DEFine PROCedure Fail
 LOCal i
 FOR i=1 TO 3
 SELect ON i
 =1
 RAISE 10
 END SELect
 END FOR i
END DEFine Fail
)") == "7\n42\n4\n5\n0\n10\nalive\n", "TRAP recovery, stores and callee unwinding");
    require(run(R"(
DEFine FuNction Worker(n)
 LOCal i,total
 total=0
 FOR i=1 TO 2
 IF n>0 THEN
 total=total+Worker(n-1)
 ELSE
 total=total+1
 END IF
 END FOR i
 RETurn total
END DEFine Worker
PRINT Worker(3)
)") == "16\n", "recursive FOR frame ownership");
    require(run("TRAP x=EVAL(\"1/0\")\nPRINT ERR\nTRAP x=EVAL(\"2+3\")\nPRINT x\n") == "7\n5\n", "dynamic code unwind");
    require(run("TRAP RAISE 3\nPRINT ERR\nTRAP x=EVAL(\"1+\")\nPRINT ERR\n") == "3\n3\n", "runtime syntax errors must be trappable");
    require(run("DEFine PROCedure Bad\nREPeat test\nx=1\nEND REPeat test UNTIL \"bad\"\nEND DEFine Bad\nTRAP Bad\nPRINT ERRX$\n").find("line 4, column 17") != std::string::npos, "UNTIL error lost closing-condition source location");
    for (const std::string source : {"TRAP REPeat x\nEND REPeat x\n", "TRAP TRAP a=1\n", "REPeat x WHILE 1\nEND REPeat x UNTIL 1\n"}) {
        bool rejected=false; try { run(source); } catch (...) { rejected=true; } require(rejected,"invalid syntax accepted");
    }
}
void uiTrapIsolation() {
    auto broker=std::make_shared<ui::QueuedService>();
    Interpreter interpreter({}, {}, broker); std::ostringstream out;
    auto status=interpreter.executeResumable("IPRINT \"working\",10000\nTRAP INPUT n%\nPRINT ERR\n",out);
    require(status==ExecutionStatus::Suspended,"trapped INPUT did not suspend");
    const auto request=broker->takeNextRequest();
    require(request && broker->submitInput(request->id,"bad"),"could not supply invalid numeric input");
    status=interpreter.resumeSlice(out,1);
    const auto info=broker->currentInfo();
    require(status==ExecutionStatus::Yielded && interpreter.errorCode()==runtime::ErrorCode::TypeMismatch,
        "resumed INPUT error escaped TRAP");
    require(info && info->text=="working","trap recovery cancelled unrelated session UI state");
    interpreter.abortExecution();
}
}
int main() {
    try { loopsAndErrors(); uiTrapIsolation(); }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
