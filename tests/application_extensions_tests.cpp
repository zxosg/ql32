#include "ql32/interpreter.hpp"
#include "ql32/input/session.hpp"
#include "ql32/platform/esp32/application_input.hpp"
#include <chrono>
#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/platform/posix/directory_cursor.hpp"
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
void keyboard() {
    auto ui = std::make_shared<ui::QueuedService>();
    auto router = std::make_shared<input::KeyRouter>(); ui->setKeyInput(router);
    Interpreter interpreter({}, {}, ui); router->focus(interpreter.graphicsSessionId());
    std::ostringstream out;
    auto status = interpreter.executeSlice(R"(
DEFine FuNction Once
 calls=calls+1
 RETurn 10
END DEFine Once
TRAP value=Once()+INKEY(-1)
PRINT ERR
PRINT calls
PRINT value
)",out,1000);
    require(status==ExecutionStatus::WaitingChannel,"INKEY did not yield");
    require(!interpreter.channelReadReady(),"empty keyboard reports ready");
    router->focus(interpreter.graphicsSessionId()); // platform reattaches focus after execution startup
    input::KeyEvent event; event.key=input::Key::Character; event.character='A'; router->publish(event);
    require(interpreter.channelReadReady(),"key did not wake reader");
    status=interpreter.resumeSlice(out,1000);
    require(status==ExecutionStatus::Completed && out.str()=="0\n1\n75\n","INKEY expression was replayed or trap lost");
    Interpreter poller({}, {}, ui); router->focus(poller.graphicsSessionId());
    std::ostringstream polls;
    poller.execute("PRINT INKEY(0)\nPRINT INKEY$(0)\nTRAP x=INKEY(0.5)\nPRINT ERR\n",polls);
    require(polls.str()=="0\n\n5\n","keyboard poll/validation");
    {
        const std::pair<input::Key,const char*> named[]={{input::Key::Up,"UP"},{input::Key::Down,"DOWN"},
            {input::Key::Left,"LEFT"},{input::Key::Right,"RIGHT"},{input::Key::Enter,"ENTER"},
            {input::Key::Escape,"ESC"},{input::Key::Backspace,"BACKSPACE"},{input::Key::Tab,"TAB"},
            {input::Key::Insert,"INS"},{input::Key::Delete,"DEL"},{input::Key::Home,"HOME"},
            {input::Key::End,"END"},{input::Key::PageUp,"PGUP"},{input::Key::PageDown,"PGDN"}};
        const auto read=[&](const input::KeyEvent& e){
            Interpreter reader({}, {}, ui); std::ostringstream o;
            auto st=reader.executeResumable("PRINT \"[\";INKEY$(-1);\"]\"\n",o,runtime::CancellationToken{});
            require(st==ExecutionStatus::WaitingChannel,"INKEY$ did not wait");
            router->focus(reader.graphicsSessionId()); router->publish(e);
            st=reader.resumeSlice(o,1000);
            require(st==ExecutionStatus::Completed,"INKEY$ did not complete");
            return o.str();
        };
        for(const auto& [key,name]:named){
            input::KeyEvent special; special.key=key;
            require(read(special)==std::string("[")+name+"]\n","INKEY$ special key name");
        }
        input::KeyEvent fn; fn.key=input::Key::Function; fn.functionNumber=12;
        require(read(fn)=="[F12]\n","INKEY$ function key name");
        input::KeyEvent ch; ch.key=input::Key::Character; ch.character=' ';
        require(read(ch)=="[ ]\n","INKEY$ printable key");
    }
    Interpreter cancelled({}, {}, ui); router->focus(cancelled.graphicsSessionId());
    runtime::CancellationSource cancellation;
    status=cancelled.executeResumable("TRAP x=INKEY(-1)\nPRINT 99\n",polls,cancellation.token());
    require(status==ExecutionStatus::WaitingChannel,"cancel test not waiting");
    cancellation.requestStop(); bool stopped=false;
    try { cancelled.resume(polls,cancellation.token()); } catch (...) { stopped=true; }
    require(stopped && polls.str().find("99")==std::string::npos,"TRAP swallowed cancellation");
    input::SerialDecoder decoder;
    require(!decoder.feed(27,0) && !decoder.flush(29),"Escape completed too early");
    auto escape=decoder.flush(30); require(escape && escape->key==input::Key::Escape,"lone Escape lost");
    decoder.feed(27,100); decoder.feed('[',101);
    auto down=decoder.feed('B',102); require(down && down->key==input::Key::Down,"Down decode");
    decoder.feed(0xc3,200); auto text=decoder.feed(0xa9,201);
    require(text && text->character==0xe9,"UTF-8 decode");
    router->focus(100);
    for(unsigned i=0;i<20;++i) router->publish(event);
    require(router->dropped()==4,"key queue unbounded");
    require(!router->poll(101),"cross-session key delivery");
    router->focus(101); require(!router->poll(101),"keys leaked across focus change");
    platform::esp32::ApplicationProtocolGate gate;
    bool protocol=false;
    gate.feed('@',300,decoder,*router,[&](std::string_view){ protocol=true; });
    require(!router->poll(101),"serial protocol prefix delivered too early");
    gate.flush(330,decoder,*router);
    require(router->poll(101)==64 && !protocol,"ordinary @ key swallowed by monitor framing");
    for(char byte : std::string("@QL32DBG/1\t"))
        gate.feed(static_cast<unsigned char>(byte),400,decoder,*router,[&](std::string_view prefix){ protocol=prefix=="@QL32DBG/1\t"; });
    require(protocol && !router->poll(101),"debugger protocol leaked into application input");
    require(run("TRAP x=INKEY(-1)\nPRINT ERR\n")=="13\n","missing keyboard failed to trap");
}
class GeneratedCursor final : public runtime::DirectoryCursor {
public:
    explicit GeneratedCursor(bool fail = false) : fail_(fail) {}
    runtime::DirectoryCursorResult next(runtime::DirectoryEntry& entry) override {
        if (fail_) { rejectAllocations = true; throw std::bad_alloc(); }
        if (position_ == 1000) return runtime::DirectoryCursorResult::End;
        entry.name = "track" + std::to_string(position_);
        entry.metadata = {}; entry.metadata.size = position_++; entry.metadata.known = 1;
        return runtime::DirectoryCursorResult::Entry;
    }
private:
    unsigned position_ = 0;
    bool fail_;
};
class FailingCursorStorage final : public runtime::StorageService {
public:
    std::unique_ptr<runtime::DirectoryCursor> openDirectory(const std::string&) override { return std::make_unique<GeneratedCursor>(true); }
    std::string readText(const std::string&) override { return {}; }
    void writeText(const std::string&,const std::string&) override {}
    std::vector<runtime::FileEntry> list(const std::string&) override { throw std::logic_error("unbounded list used"); }
    bool exists(const std::string&) override { return false; }
    std::uintmax_t size(const std::string&) override { return 0; }
    void rename(const std::string&,const std::string&) override {}
    void remove(const std::string&) override {}
};
void memoryBoundaries() {
    runtime::DirectoryLimits limits; limits.batch = 1;
    runtime::DirectoryStream stream(std::make_unique<GeneratedCursor>(),"*",0,limits);
    while(!stream.prepare()) {}
    unsigned count=0;
    for (;;) {
        while(!stream.ready()) {}
        if (stream.atEnd()) break;
        require(!stream.nextName().empty() && stream.snapshotBytes()==0,"stream allocated snapshot storage");
        stream.commitRead(); ++count;
    }
    require(count==1000 && stream.info(2,false)==999,"large streaming enumeration");
    auto storage=std::make_shared<FailingCursorStorage>();
    auto channels=std::make_shared<runtime::ChannelRegistry>(nullptr);
    Interpreter interpreter(storage,channels); std::ostringstream out;
    auto status=interpreter.executeSlice("TRAP d=OPEN(14,\"/\",\"*\",2)\nPRINT ERR\n",out,1);
    bool injected=false;
    try {
        for(unsigned turn=0;turn<100 && status!=ExecutionStatus::Completed;++turn) {
            if(rejectAllocations) { injected=true; rejectAllocations=false; }
            status=interpreter.resumeSlice(out,1);
        }
    } catch (...) { rejectAllocations=false; throw; }
    rejectAllocations=false;
    require(injected && status==ExecutionStatus::Completed && out.str()=="8\n", "out-of-memory recovery allocated or failed to resume");
    require(channels->listOpen(interpreter.graphicsSessionId()).empty(),"allocation failure published a directory handle");
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
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path()/("ql32-application-extension-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::remove_all(path); std::filesystem::create_directory(path); }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};
void directories() {
    TemporaryDirectory temporary;
    std::ofstream(temporary.path/"b.mod") << "bb";
    std::ofstream(temporary.path/"A.mod") << "a";
    std::ofstream(temporary.path/"other.txt") << "x";
    std::filesystem::create_directory(temporary.path/"folder");
    auto storage=std::make_shared<platform::linux_host::DirectoryStorage>(temporary.path);
    auto files=std::make_shared<platform::linux_host::DirectoryChannels>(temporary.path);
    auto channels=std::make_shared<runtime::ChannelRegistry>(files);
    Interpreter interpreter(storage,channels); std::ostringstream out;
    auto status=interpreter.executeResumable(R"(
d=OPEN(14,"/","*.mod",3)
REPeat entries WHILE EOF(d)=0
 name$=READ$(d)
 PRINT name$
END REPeat entries
PRINT EOF(d)
PRINT DIRINFO(d,2)
CLOSE d
TRAP d=OPEN(14,"/","*",6)
PRINT ERR
)",out);
    unsigned yields=0;
    while(status==ExecutionStatus::Yielded) { ++yields; status=interpreter.resume(out); }
    require(status==ExecutionStatus::Completed && out.str()=="folder\nA.mod\nb.mod\n1\n2\n2\n","directory sorting/filter/metadata/options");
    // A filtered stream scans multiple batches, yielding without returning EOF prematurely.
    for(unsigned i=0;i<60;++i) std::ofstream(temporary.path/("skip"+std::to_string(i)+".txt"));
    Interpreter stream(storage,channels); std::ostringstream streamOut;
    stream.execute("d=OPEN(14,\"/\",\"no-match\",0)\nPRINT EOF(d)\nTRAP n$=READ$(d)\nPRINT ERR\nCLOSE d\n",streamOut);
    require(streamOut.str()=="1\n12\n","streaming EOF or empty directory");
    auto limited=std::make_shared<runtime::ChannelRegistry>(files);
    runtime::DirectoryLimits limits; limits.maxEntries=2; limits.batch=1;
    limited->directoryLimits(limits);
    Interpreter budget(storage,limited); std::ostringstream budgetOut;
    budget.execute("TRAP d=OPEN(14,\"/\",\"*\",2)\nPRINT ERR\nPRINT GETCHANNEL(0)\nTRAP d=OPEN(14,\"/\",\"*\",0)\nPRINT ERR\nCLOSE d\n",budgetOut);
    require(budgetOut.str()=="18\n2\n0\n","quota failure leaked channel or prevented streaming fallback");
    Interpreter repl(storage,channels); std::ostringstream replOut;
    status=repl.executeInteractiveResumable("d=OPEN(14,\"/\",\"*\",2)\n",replOut);
    while(status==ExecutionStatus::Yielded) status=repl.resume(replOut);
    require(status==ExecutionStatus::Completed && repl.openChannelCount()==1,"yielded REPL OPEN lost its channel");
    repl.executeInteractive("CLOSE d\n",replOut);
    Interpreter print(storage,channels); std::ostringstream printOut;
    print.execute(R"(
f=OPEN(1,"trap-print.txt","WRITE")
PRINT #f,"before";HandleError();"after"
CLOSE f
DEFine FuNction HandleError
 TRAP x=1/0
 RETurn 2
END DEFine HandleError
)",printOut);
    require(storage->readText("trap-print.txt")=="before2after\n" && printOut.str().empty(),"inner trap damaged caller PRINT state");
    require(runtime::directoryWildcardMatch("café.MOD","caf?.mod"),"wildcard UTF-8 scalar matching");
    require(!runtime::directoryWildcardMatch("ab","?"),"wildcard question matching");
    Interpreter arguments({},channels); std::ostringstream argumentOut;
    arguments.execute("n=OPEN(3)\nTRAP OUT n,\"bad\"\nPRINT ERR\nTRAP value=IN(n,\"bad\")\nPRINT ERR\nb=ALLOC(2)\nTRAP value=IOREAD(n,\"bad\",b)\nPRINT ERR\nCLOSE n\nDEALLOC b\n",argumentOut);
    require(argumentOut.str()=="4\n4\n4\n","device argument type errors escaped TRAP");
    // SPIFFS virtual-flat VFS behavior additionally needs board acceptance.
    require(channels->listOpen(interpreter.graphicsSessionId()).empty(),"directory channel not closed");
}
}
int main() {
    try { loopsAndErrors(); keyboard(); directories(); memoryBoundaries(); uiTrapIsolation(); }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
