#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/interpreter.hpp"

#include <chrono>
#include <array>
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

class ThrowAfterPartialWrite final : public ql32::runtime::TextStreamSource {
public:
    void writeTo(ql32::runtime::TextStreamSink& sink) const override {
        sink.write("partial replacement");
        throw std::runtime_error("injected stream failure");
    }
};

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
        const std::string binary("a\0bcd", 5);
        storage.writeText("subdir/font.bin", binary);
        auto reader = storage.openRead("subdir/font.bin");
        std::array<std::uint8_t, 3> range{};
        require(reader->size() == binary.size() && reader->readAt(1, range.data(), range.size()) == 3 &&
                range[0] == 0 && range[1] == 'b' && range[2] == 'c' &&
                reader->readAt(binary.size(), range.data(), range.size()) == 0,
            "bounded storage reads did not preserve bytes and absolute offsets");
        reader.reset();
        storage.remove("subdir/font.bin");
        storage.writeText("subdir/program.bas", "20 PRINT \"saved\"\n");
        require(storage.readText("subdir/program.bas") == "20 PRINT \"saved\"\n",
                "atomic overwrite did not replace the previous file");

        ThrowAfterPartialWrite failingSource;
        bool streamFailed = false;
        try {
            storage.writeTextStreamAtomic("subdir/program.bas", failingSource);
        } catch (const std::runtime_error&) {
            streamFailed = true;
        }
        require(streamFailed &&
                    storage.readText("subdir/program.bas") == "20 PRINT \"saved\"\n" &&
                    storage.list("subdir").size() == 1,
                "failed streamed replacement damaged the previous file");

        const auto entries = storage.list("subdir");
        require(entries.size() == 1 && entries.front().name == "program.bas" &&
                    entries.front().size == 17 && !entries.front().isDirectory,
                "directory listing returned incorrect file metadata");
        require(storage.exists("subdir/program.bas") &&
                    storage.size("subdir/program.bas") == 17,
                "storage existence or size query returned incorrect metadata");
        storage.rename("subdir/program.bas", "subdir/renamed.bas");
        require(!storage.exists("subdir/program.bas") &&
                    storage.readText("subdir/renamed.bas") == "20 PRINT \"saved\"\n",
                "storage rename did not move the file");
        storage.remove("subdir/renamed.bas");
        require(!storage.exists("subdir/renamed.bas"), "storage delete did not remove file");
        const auto rootEntries = storage.list("");
        require(rootEntries.size() == 1 && rootEntries.front().name == "subdir" &&
                    rootEntries.front().isDirectory,
                "root directory listing omitted its directory");
        std::filesystem::create_directories(root / "subdir" / "nested");
        storage.writeText("subdir/nested.txt", "nested");
        storage.writeText("subdir/nested/child.txt", "child");
        storage.format("subdir");
        require(storage.list("subdir").empty(), "format did not clear a directory recursively");

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

        interpreter.execute("source$ = \"demo.bas\"\ndestination$ = \"renamed.bas\"\n", output);
        interpreter.executeInteractive("RENAME source$, destination$\n", output);
        require(!storage->exists("demo.bas") && storage->exists("renamed.bas"),
                "RENAME string variables did not move the file");
        interpreter.execute("source$ = \"renamed.bas\"\n", output);
        interpreter.executeInteractive("DELETE source$\n", output);
        require(!storage->exists("renamed.bas"), "DELETE string variable did not remove file");
        storage->writeText("demo.bas", "10 PRINT \"loaded\"\n");

        interpreter.executeInteractive("NEW\nLOAD \"demo.bas\"\nLIST\nRUN\nDIR\n", output);
        require(output.str() == "10 PRINT \"loaded\"\nloaded\ndemo.bas\n",
                "LOAD, LIST, RUN, or DIR returned unexpected output");

        std::filesystem::create_directories(root / "work" / "child");
        storage->writeText("work/child/item.bas", "10 PRINT \"item\"\n");
        interpreter.executeInteractive("CD \"work/child\"\nDIR\n", output);
        require(output.str().size() >= 9 &&
                    output.str().compare(output.str().size() - 9, 9, "item.bas\n") == 0,
                "CD or DIR did not use the current directory");
        interpreter.executeInteractive("CD ..\nDIR\n", output);
        require(output.str().size() >= 7 &&
                    output.str().compare(output.str().size() - 7, 7, "child/\n") == 0,
                "CD .. did not walk to the parent directory");
        interpreter.executeInteractive("CD child\nDELETE \"item.bas\"\nCD ../..\n", output);
        require(!storage->exists("work/child/item.bas"), "DELETE did not resolve relative to CD");
        storage->writeText("work/child/item.bas", "content");
        interpreter.executeInteractive("CD work/child\nRENAME \"item.bas\", \"moved.bas\"\n", output);
        require(storage->exists("work/child/moved.bas") && !storage->exists("work/child/item.bas"),
                "RENAME did not resolve both paths relative to CD");

        storage->writeText("from-ffs.bas", "10 PRINT \"wrong directory\"\n");
        storage->writeText("work/child/from-ffs.bas",
                           "10 PRINT \"loaded from FFS current directory\"\n");
        std::ostringstream cdLoadOutput;
        interpreter.executeInteractive(
            "CD \"FFS:/work/child\"\nLOAD \"from-ffs.bas\"\nLIST\nRUN\n",
            cdLoadOutput);
        require(cdLoadOutput.str() ==
                    "10 PRINT \"loaded from FFS current directory\"\n"
                    "loaded from FFS current directory\n",
                "LOAD did not resolve relative to a drive-qualified CD path");

        interpreter.executeInteractive(
            "10 PRINT \"saved to FFS current directory\"\nSAVE \"saved-from-ffs.bas\"\n",
            output);
        require(storage->exists("work/child/saved-from-ffs.bas") &&
                    !storage->exists("saved-from-ffs.bas"),
                "SAVE did not resolve relative to a drive-qualified CD path");

        storage->writeText("work/child/item.bas", "content");
        interpreter.execute("drive$ = \"SDC:/\"\n", output);
        interpreter.executeInteractive("FORMAT drive$\n", output);
        require(storage->list("").empty(), "FORMAT drive path did not erase storage recursively");

        interpreter.executeInteractive("CD \"/\"\n", output);
        storage->writeText("plain.bas",
            "PRINT \"first\"\n\nPRINT \"second\"\n");
        std::ostringstream plainOutput;
        interpreter.executeInteractive("LOAD \"plain.bas\"\nLIST\nRUN\n", plainOutput);
        require(plainOutput.str() ==
                    "10 PRINT \"first\"\n20 PRINT \"second\"\nfirst\nsecond\n",
                "LOAD did not auto-number and run unnumbered BASIC source");

        storage->writeText("broken.bas", "PRINT )\n");
        std::ostringstream brokenOutput;
        interpreter.executeInteractive("LOAD \"broken.bas\"\nLIST\n", brokenOutput);
        require(brokenOutput.str() == "10 PRINT )\n",
                "LOAD did not preserve invalid source for a later RUN diagnostic");
        bool rejected = false;
        try {
            interpreter.executeInteractive("RUN\n", output);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "RUN accepted invalid BASIC source loaded from disk");
        std::ostringstream listing;
        interpreter.executeInteractive("LIST\n", listing);
        require(listing.str() == "10 PRINT )\n",
                "RUN failure discarded the loaded source");

        storage->writeText("mixed.bas", "10 PRINT \"numbered\"\nPRINT \"plain\"\n");
        bool mixedRejected = false;
        try {
            interpreter.executeInteractive("LOAD \"mixed.bas\"\n", output);
        } catch (const std::runtime_error&) {
            mixedRejected = true;
        }
        require(mixedRejected, "LOAD accepted a mixture of numbered and unnumbered lines");

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
