#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/virtual_hardware.hpp"
#include "ql32/vm/scheduler.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void apiAndSyntaxTest() {
    using namespace ql32;
    auto hardware = std::make_shared<platform::linux_host::VirtualHardware>();
    auto channels = std::make_shared<runtime::ChannelRegistry>(nullptr,
        runtime::ChannelRegistry::SerialWriter{}, hardware);
    Interpreter interpreter({}, channels);
    std::ostringstream output;
    interpreter.execute(
        "button=OPEN(5,12,0,1)\n"
        "PRINT IN(button)\n"
        "b=ALLOC(8)\n"
        "POKE b,17,34,51,68\n"
        "PRINT PEEK(b+1)\n"
        "POKE_W b+1,4660\n"
        "PRINT PEEK_W(b+1)\n"
        "POKE_L b+2,305419896\n"
        "PRINT PEEK_L(b+2)\n"
        "i2c=OPEN(8,0,104)\n"
        "OUT i2c,10,85\n"
        "PRINT IN(i2c,10)\n"
        "tx=ALLOC(3)\n"
        "rx=ALLOC(3)\n"
        "POKE tx,7,8,9\n"
        "status=IOWRITE(i2c,32,tx)\n"
        "count=IOREAD(i2c,32,rx)\n"
        "PRINT status;count;PEEK(rx);PEEK(rx+1);PEEK(rx+2)\n"
        "spi=OPEN(7,0,11,1000000,0)\n"
        "XFER #spi,tx,3,rx,3\n"
        "PRINT PEEK(rx);PEEK(rx+1);PEEK(rx+2)\n"
        "par=OPEN(9,20,21,22,23,24,25,26,27,100,2)\n"
        "OUT par,170\n"
        "OUT par,100,1\n"
        "OUT par,100,0\n"
        "PRINT IN(par)\n"
        "adc=OPEN(6,32,12)\n"
        "PRINT IN(adc)\n"
        "touch=OPEN(11,33)\n"
        "PRINT IN(touch)\n"
        "timer=OPEN(12,1,1)\n"
        "PRINT IN(timer)\n"
        "uart=OPEN(2,1,9600,0,0)\n"
        "CLOSE button\nCLOSE i2c\nCLOSE spi\nCLOSE par\nCLOSE adc\n"
        "CLOSE touch\nCLOSE timer\nCLOSE uart\n"
        "DEALLOC b\nDEALLOC tx\nDEALLOC rx\n", output);
    require(output.str() == "1\n34\n4660\n305419896\n85\n03789\n789\n170\n0\n0\n0\n",
            "new device and buffer syntax did not execute through the VM");
    require(interpreter.openChannelCount() == 0, "hardware channels were not closed");
}

void externalInputsAndLifetimeTest() {
    using namespace ql32;
    auto hardware = std::make_shared<platform::linux_host::VirtualHardware>();
    hardware->setPinInput(7, 0);
    hardware->setAnalogInput(8, 1234);
    auto channels = std::make_shared<runtime::ChannelRegistry>(nullptr,
        runtime::ChannelRegistry::SerialWriter{}, hardware);
    Interpreter interpreter({}, channels);
    std::ostringstream output;
    interpreter.execute("g=OPEN(5,7,0)\na=OPEN(6,8,12)\nPRINT IN(g);IN(a)\n", output);
    require(output.str() == "01234\n", "GPIO/ADC input values were not returned");
    bool duplicateRejected = false;
    try { interpreter.execute("other=OPEN(5,7,0)\n", output); }
    catch (const std::exception&) { duplicateRejected = true; }
    require(duplicateRejected, "duplicate pin reservation was accepted");
    interpreter.execute("CLOSE g\nCLOSE a\n", output);
    interpreter.execute("other=OPEN(5,7,0)\nCLOSE other\n", output);
}

void invalidSyntaxTest() {
    ql32::Interpreter interpreter;
    std::ostringstream output;
    for (const std::string& source : {"PRINT IN()\n", "x=ALLOC()\n", "POKE 1\n",
                                      "OUT 1\n", "XFER 1,2,3,4\n"}) {
        bool rejected = false;
        try { interpreter.execute(source, output); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid new command syntax was accepted");
    }
}

void transferLimitTest() {
    using namespace ql32;
    auto hardware = std::make_shared<platform::linux_host::VirtualHardware>();
    auto channels = std::make_shared<runtime::ChannelRegistry>(nullptr,
        runtime::ChannelRegistry::SerialWriter{}, hardware);
    Interpreter interpreter({}, channels);
    std::ostringstream output;
    bool rejected = false;
    try {
        interpreter.execute(
            "spi=OPEN(7,0,11,1000000,0)\n"
            "XFER #spi,0,4096,0,1\n", output);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "XFER accepted a combined payload larger than 4096 bytes");
}

void serialTouchAndTimerTest() {
    using namespace ql32;
    auto hardware = std::make_shared<platform::linux_host::VirtualHardware>();
    const auto blocker = hardware->open(900, runtime::ChannelDevice::Serial,
        {1.0, 9600.0, 0.0, 0.0});
    hardware->setTouchInput(9, 1);
    auto channels = std::make_shared<runtime::ChannelRegistry>(nullptr,
        runtime::ChannelRegistry::SerialWriter{}, hardware);
    Interpreter interpreter({}, channels);
    std::ostringstream output;
    interpreter.execute("uart=OPEN(2,1,9600,0,0)\n", output);
    interpreter.execute(
        "touch=OPEN(11,9)\nPRINT IN(touch)\n"
        "WRITE uart,\"PING\"\n"
        "buffer=ALLOC(2)\nPOKE buffer,65,66\n"
        "status=IOWRITE(uart,buffer)\nPRINT status\n"
        "timer=OPEN(12,1,0)\n", output);
    auto readStatus = interpreter.executeResumable("reply$=READ$(uart)\nPRINT reply$\n", output);
    require(readStatus == ExecutionStatus::WaitingChannel,
            "serial READ$ did not suspend the BASIC session when no line was ready");
    hardware->queueSerialLine(blocker + 1, "REPLY");
    require(interpreter.channelReadReady(), "serial line did not wake its waiting session");
    readStatus = interpreter.resume(output);
    require(readStatus == ExecutionStatus::Completed, "serial READ$ did not resume to completion");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    interpreter.execute("ticks=IN(timer)\nPRINT ticks>0\n", output);
    require(output.str() == "1\n0\nREPLY\n1\n",
            "touch, UART line/buffer, or timer channel behavior failed");
    const auto uartData = hardware->takeSerialOutput(blocker + 1);
    require(uartData == "PING\nAB", "UART text and binary output were not preserved");
    interpreter.execute("CLOSE touch\nCLOSE uart\nCLOSE timer\nDEALLOC buffer\n", output);
    hardware->close(900, blocker);
}

void schedulerWaitsPerSessionTest() {
    using namespace ql32;
    auto hardware = std::make_shared<platform::linux_host::VirtualHardware>();
    auto channels = std::make_shared<runtime::ChannelRegistry>(nullptr,
        runtime::ChannelRegistry::SerialWriter{}, hardware);
    vm::Services services;
    services.channels = channels;
    vm::SessionScheduler scheduler(services, 8);
    std::ostringstream serialOutput;
    std::ostringstream otherOutput;
    const auto waiting = scheduler.launchWorker(
        "uart=OPEN(2,1,9600,0,0)\nreply$=READ$(uart)\nPRINT reply$\n", serialOutput);
    for (int i = 0; i < 16 &&
         (!scheduler.task(waiting) || scheduler.task(waiting)->status != vm::TaskStatus::WaitingChannel); ++i)
        (void)scheduler.tick();
    require(scheduler.task(waiting) &&
                scheduler.task(waiting)->status == vm::TaskStatus::WaitingChannel,
            "serial line read did not put only its session into WaitingChannel");
    const auto other = scheduler.launchWorker("PRINT \"OTHER\"\n", otherOutput);
    for (int i = 0; i < 16 && scheduler.task(other)->status != vm::TaskStatus::Completed; ++i)
        (void)scheduler.tick();
    require(otherOutput.str() == "OTHER\n",
            "scheduler did not run another session while serial input was pending");
    hardware->queueSerialLine(1, "READY");
    for (int i = 0; i < 16 && scheduler.task(waiting)->status != vm::TaskStatus::Completed; ++i)
        (void)scheduler.tick();
    require(serialOutput.str() == "READY\n",
            "serial input did not wake the waiting session through the scheduler");
}
} // namespace

int main() {
    try {
        apiAndSyntaxTest();
        externalInputsAndLifetimeTest();
        invalidSyntaxTest();
        transferLimitTest();
        serialTouchAndTimerTest();
        schedulerWaitsPerSessionTest();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
