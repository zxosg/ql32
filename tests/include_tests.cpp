#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/directory_storage.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

class TempTree {
public:
    TempTree() {
        root = std::filesystem::temp_directory_path() /
            ("ql32_include_tests_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "lib");
    }
    ~TempTree() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }

    void write(const std::string& name, const std::string& contents) {
        const auto path = root / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << contents;
        if (!file) throw std::runtime_error("could not create include test file");
    }

    std::filesystem::path root;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testNestedIncludesAndLibraryFunctions() {
    TempTree files;
    files.write("lib/math.bas",
        "INCLUDE \"support.bas\"\n"
        "DEFine FuNction Twice(value)\n"
        "  RETurn Scale(value)\n"
        "END DEFine Twice\n");
    files.write("lib/support.bas",
        "DEFine FuNction Scale(value)\n"
        "  RETurn value * 2\n"
        "END DEFine Scale\n");
    auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(files.root);
    ql32::Interpreter interpreter(storage);
    std::ostringstream output;
    interpreter.executeResumable(
        "INCLUDE \"lib/math.bas\"\nPRINT Twice(21)\n", output);
    require(output.str() == "42\n",
            "nested source include did not expose its BASIC library procedures");
}

void testIncludeInNumberedProgram() {
    TempTree files;
    files.write("lib/math.bas",
        "DEFine FuNction Triple(value)\n"
        "  RETurn value * 3\n"
        "END DEFine Triple\n");
    auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(files.root);
    ql32::Interpreter interpreter(storage);
    std::ostringstream output;
    interpreter.executeInteractive(
        "10 REMark shared arithmetic\n"
        "20 INCLUDE \"lib/math.bas\"\n"
        "30 PRINT Triple(14)\nRUN\n", output);
    require(output.str() == "42\n",
            "numbered BASIC program did not expand its included library at RUN");
}

void testDialogFlagsAreAvailableFromQl32Include() {
    const auto sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(
        sourceRoot / "data");
    ql32::Interpreter interpreter(storage);
    std::ostringstream output;
    interpreter.executeResumable(
        "INCLUDE \"ql32.inc\"\n"
        "PRINT KDlgButRight;\",\";KDlgNoTitle;\",\";KDlgFillScreen;\",\";"
        "KDlgNoDrag;\",\";KDlgDensePack;\",\";KDlgHideButtons;\",\";"
        "KDlgPosTopLeft;\",\";KDlgPosTopRight;\",\";"
        "KDlgPosBottomLeft;\",\";KDlgPosBottomRight\n",
        output);
    require(output.str() == "1,2,4,8,16,32,64,128,256,512\n",
            "ql32.inc dialog option constants were missing or had incorrect values");
}

void testIncludeSafetyAndPlacement() {
    TempTree files;
    files.write("cycle.bas", "INCLUDE \"cycle.bas\"\n");
    files.write("plain.bas", "PRINT 1\n");
    auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(files.root);
    std::ostringstream output;

    bool cycleDetected = false;
    try {
        ql32::Interpreter interpreter(storage);
        interpreter.executeResumable("INCLUDE \"cycle.bas\"\n", output);
    } catch (const std::runtime_error& error) {
        cycleDetected = std::string(error.what()).find("INCLUDE cycle") != std::string::npos;
    }
    require(cycleDetected, "recursive include cycle was not reported");

    bool lateIncludeRejected = false;
    try {
        ql32::Interpreter interpreter(storage);
        interpreter.executeResumable("PRINT 1\nINCLUDE \"plain.bas\"\n", output);
    } catch (const std::runtime_error& error) {
        lateIncludeRejected = std::string(error.what()).find("before module code") !=
                              std::string::npos;
    }
    require(lateIncludeRejected, "include after executable module code was accepted");

    bool traversalRejected = false;
    try {
        ql32::Interpreter interpreter(storage);
        interpreter.executeResumable("INCLUDE \"../../escape.bas\"\n", output);
    } catch (const std::runtime_error& error) {
        traversalRejected = std::string(error.what()).find("escapes the storage root") !=
                            std::string::npos;
    }
    require(traversalRejected, "include path traversal was not rejected");
}

} // namespace

int main() {
    try {
        testNestedIncludesAndLibraryFunctions();
        testIncludeInNumberedProgram();
        testDialogFlagsAreAvailableFromQl32Include();
        testIncludeSafetyAndPlacement();
    } catch (const std::exception& error) {
        std::cerr << "include test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
