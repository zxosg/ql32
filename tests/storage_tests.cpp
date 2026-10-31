#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/interpreter.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testDirectoryStorage() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-storage-test-" + std::to_string(unique));
    std::filesystem::create_directories(root / "subdir");
    try {
        ql32::platform::linux_host::DirectoryStorage storage(root);
        storage.writeText("subdir/program.bas", "10 PRINT \"ready\"\n");
        require(storage.readText("subdir/program.bas") == "10 PRINT \"ready\"\n",
                "stored text did not round-trip");
        storage.writeText("subdir/program.bas", "20 PRINT \"saved\"\n");
        require(storage.readText("subdir/program.bas") == "20 PRINT \"saved\"\n",
                "atomic overwrite did not replace the previous file");

        const auto entries = storage.list("subdir");
        require(entries.size() == 1 && entries.front().name == "program.bas" &&
                    entries.front().size == 17 && !entries.front().isDirectory,
                "directory listing returned incorrect file metadata");
        const auto rootEntries = storage.list("");
        require(rootEntries.size() == 1 && rootEntries.front().name == "subdir" &&
                    rootEntries.front().isDirectory,
                "root directory listing omitted its directory");

        bool escaped = false;
        try {
            (void)storage.readText("../outside.txt");
        } catch (const ql32::runtime::ServiceError& error) {
            escaped = error.code() == ql32::runtime::ServiceErrorCode::InvalidPath;
        }
        require(escaped, "path traversal was not rejected");

        bool missing = false;
        try {
            (void)storage.readText("missing.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            missing = error.code() == ql32::runtime::ServiceErrorCode::NotFound;
        }
        require(missing, "missing file did not return a stable not-found error");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testInteractiveFileCommands() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-interpreter-files-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(root);
        ql32::Interpreter interpreter(storage);
        std::ostringstream output;
        interpreter.executeInteractive("10 PRINT \"loaded\"\nSAVE \"demo.bas\"\n", output);
        require(storage->readText("demo.bas") == "10 PRINT \"loaded\"\n",
                "SAVE did not serialize the numbered program");

        interpreter.executeInteractive("NEW\nLOAD \"demo.bas\"\nLIST\nRUN\nDIR\n", output);
        require(output.str() == "10 PRINT \"loaded\"\nloaded\ndemo.bas\n",
                "LOAD, LIST, RUN, or DIR returned unexpected output");

        storage->writeText("broken.bas", "this is not numbered source\n");
        bool rejected = false;
        try {
            interpreter.executeInteractive("LOAD \"broken.bas\"\n", output);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "LOAD accepted a file without numbered source lines");
        std::ostringstream listing;
        interpreter.executeInteractive("LIST\n", listing);
        require(listing.str() == "10 PRINT \"loaded\"\n",
                "failed LOAD replaced the current program");

        bool traversalRejected = false;
        try {
            interpreter.executeInteractive("LOAD \"../outside.bas\"\n", output);
        } catch (const ql32::runtime::ServiceError& error) {
            traversalRejected = error.code() == ql32::runtime::ServiceErrorCode::InvalidPath;
        }
        require(traversalRejected, "interpreter file command escaped its storage root");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testDirectoryStorage();
        testInteractiveFileCommands();
    } catch (const std::exception& error) {
        std::cerr << "storage test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
