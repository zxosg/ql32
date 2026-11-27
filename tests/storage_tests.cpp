#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/interpreter.hpp"
#include "ql32/runtime/file_ops.hpp"

#include <chrono>
#include <array>
#include <filesystem>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> largestAllocation{0};
} // namespace

#ifdef __GLIBC__
#include <execinfo.h>
#include <unistd.h>
#endif
void* operator new(std::size_t size) {
    if (trackAllocations.load(std::memory_order_relaxed)) {
#ifdef __GLIBC__
        if (size > 4096) { void* f[24]; int n = backtrace(f, 24); dprintf(2, "BIGALLOC %zu\n", size); backtrace_symbols_fd(f, n, 2); }
#endif
        std::size_t seen = largestAllocation.load(std::memory_order_relaxed);
        while (size > seen && !largestAllocation.compare_exchange_weak(seen, size)) {}
    }
    if (void* block = std::malloc(size ? size : 1)) return block;
    throw std::bad_alloc();
}
void operator delete(void* block) noexcept { std::free(block); }
void operator delete(void* block, std::size_t) noexcept { std::free(block); }

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

struct CountingBuffer final : std::streambuf {
    std::size_t size = 0;
    int overflow(int c) override { ++size; return c; }
    std::streamsize xsputn(const char*, std::streamsize n) override { size += static_cast<std::size_t>(n); return n; }
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

        std::ostringstream detailedRoot;
        interpreter.executeInteractive("CAT\n", detailedRoot);
        const auto demoSize = std::filesystem::file_size(root / "demo.bas");
        const std::string demoSizeText = std::to_string(demoSize);
        const std::string demoLine = "demo.bas " +
            std::string(demoSizeText.size() < 5 ? 5 - demoSizeText.size() : 0, ' ') +
            demoSizeText + "\n";
        require(detailedRoot.str().rfind(demoLine, 0) == 0,
                "CAT did not report the raw file size");
        require(detailedRoot.str().find("1 file(s), 0 dir(s), " + demoSizeText + " bytes\n") !=
                    std::string::npos,
                "CAT did not report the file and byte totals");
        require(detailedRoot.str().find("Total: ") != std::string::npos &&
                    detailedRoot.str().find("Used:  ") != std::string::npos &&
                    detailedRoot.str().find("Free:  ") != std::string::npos,
                "CAT did not report drive space");
        std::ostringstream legacyDetails;
        bool legacyRejected = false;
        try {
            interpreter.executeInteractive("DIR DETAILS\n", legacyDetails);
        } catch (const std::exception&) {
            legacyRejected = true;
        }
        require(legacyRejected, "DIR DETAILS should be replaced by CAT");

        std::filesystem::create_directories(root / "work" / "child");
        std::filesystem::create_directories(root / "Alpha");
        storage->writeText("work/child/item.bas", "10 PRINT \"item\"\n");
        storage->writeText("Zeta.txt", "z");
        storage->writeText("beta.TXT", "bb");
        std::ostringstream detailedDirectory;
        interpreter.executeInteractive("CAT\n", detailedDirectory);
        const std::string catalog = detailedDirectory.str();
        const auto alpha = catalog.find("Alpha/");
        const auto work = catalog.find("work/");
        const auto beta = catalog.find("beta.TXT");
        const auto demo = catalog.find("demo.bas");
        const auto zeta = catalog.find("Zeta.txt");
        require(alpha != std::string::npos && alpha < work && work < beta &&
                    beta < demo && demo < zeta,
                "CAT did not list directories first with case-insensitive order");
        require(catalog.find("<DIR>") != std::string::npos &&
                    catalog.find("3 file(s), 2 dir(s), ") != std::string::npos,
                "CAT did not label directories or count entries");

        std::ostringstream wildcard;
        interpreter.executeInteractive("DIR *.txt\nCAT #1 \"z*\"\n", wildcard);
        require(wildcard.str().rfind("Zeta.txt\nbeta.TXT\n", 0) == 0 ||
                    wildcard.str().rfind("beta.TXT\nZeta.txt\n", 0) == 0,
                "DIR wildcard did not match case-insensitively");
        require(wildcard.str().find("Zeta.txt 5") == std::string::npos &&
                    wildcard.str().find("1 file(s), 0 dir(s), 1 bytes\n") != std::string::npos,
                "CAT wildcard did not filter and total entries");
        std::ostringstream wildcardPath;
        interpreter.executeInteractive("CAT work/child/i?em.*\n", wildcardPath);
        require(wildcardPath.str().rfind("item.bas ", 0) == 0,
                "CAT wildcard in a subdirectory did not match");
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

        storage->writeText("options.bas",
            "CONST KOptMathSingle = 1\n"
            "OPTION KOptMathSingle\n"
            "PRINT 16777216 + 1 - 16777216\n");
        std::ostringstream optionsOutput;
        interpreter.executeInteractive("LOAD \"options.bas\"\nRUN\n", optionsOutput);
        require(optionsOutput.str() == "0\n",
                "LOAD did not auto-number module-level CONST and OPTION lines");

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

bool fails(ql32::Interpreter& interpreter, const std::string& line) {
    std::ostringstream sink;
    try {
        interpreter.executeInteractive(line + "\n", sink);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void testCopyMoveDeleteRmdir() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-fileops-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(root);
        ql32::Interpreter interpreter(storage);
        std::ostringstream out;
        const std::string big(5000, 'x');
        storage->writeText("a.txt", big);
        storage->writeText("b.txt", "bee");
        storage->writeText("c.dat", "see");
        std::filesystem::create_directories(root / "dir");

        interpreter.executeInteractive("COPY \"a.txt\" TO \"copy.txt\"\n", out);
        require(storage->readText("copy.txt") == big && storage->exists("a.txt"),
                "COPY did not duplicate the file");
        require(fails(interpreter, "COPY \"b.txt\" TO \"copy.txt\""),
                "COPY overwrote without FORCE");
        require(storage->readText("copy.txt") == big, "refused COPY changed destination");
        interpreter.executeInteractive("COPY \"b.txt\" TO \"copy.txt\" FORCE\n", out);
        require(storage->readText("copy.txt") == "bee", "COPY FORCE did not overwrite");
        require(fails(interpreter, "COPY \"a.txt\" TO \"a.txt\""), "COPY onto itself allowed");
        require(fails(interpreter, "COPY \"nope*\" TO \"dir/\""), "COPY with no match allowed");
        require(fails(interpreter, "COPY \"*.txt\" TO \"single.txt\""),
                "multiple sources into one file allowed");

        interpreter.executeInteractive("COPY \"*.txt\" TO \"dir/\"\n", out);
        require(storage->exists("dir/a.txt") && storage->exists("dir/b.txt") &&
                    storage->exists("dir/copy.txt") && !storage->exists("dir/c.dat"),
                "wildcard COPY into a directory failed");

        interpreter.executeInteractive("MOVE \"c.dat\" TO \"moved.dat\"\n", out);
        require(!storage->exists("c.dat") && storage->readText("moved.dat") == "see",
                "MOVE failed");
        require(fails(interpreter, "MOVE \"moved.dat\" TO \"b.txt\""), "MOVE overwrote");
        interpreter.executeInteractive("MOVE \"moved.dat\" TO \"b.txt\" FORCE\n", out);
        require(storage->readText("b.txt") == "see" && !storage->exists("moved.dat"),
                "MOVE FORCE failed");
        interpreter.executeInteractive("RENAME \"b.txt\" TO \"r.txt\"\n", out);
        require(storage->exists("r.txt") && !storage->exists("b.txt"), "RENAME TO failed");

        require(fails(interpreter, "DELETE \"dir/*.txt\""),
                "multi-file DELETE without UI did not require FORCE");
        require(storage->exists("dir/a.txt"), "refused DELETE removed files");
        interpreter.executeInteractive("DELETE \"dir/*.txt\" FORCE\n", out);
        require(!storage->exists("dir/a.txt") && !storage->exists("dir/copy.txt"),
                "DELETE wildcard FORCE failed");
        require(fails(interpreter, "DELETE \"dir/*.txt\" FORCE"), "DELETE no match allowed");
        interpreter.executeInteractive("DELETE \"r.txt\"\n", out);
        require(!storage->exists("r.txt"), "single DELETE failed");

        interpreter.executeInteractive("MKDIR \"made\"\nMKDIR \"made/inner\"\n", out);
        require(std::filesystem::is_directory(root / "made" / "inner"), "MKDIR failed");
        require(fails(interpreter, "MKDIR \"made\""), "MKDIR of existing directory allowed");
        require(fails(interpreter, "MKDIR \"nope/child\""), "MKDIR with missing parent allowed");
        require(fails(interpreter, "MKDIR \"w*\""), "MKDIR accepted a wildcard");
        interpreter.executeInteractive("COPY \"a.txt\" TO \"made/inner/\"\n", out);
        require(storage->exists("made/inner/a.txt"), "COPY into MKDIR directory failed");
        interpreter.executeInteractive("RMDIR \"made\" FORCE\n", out);
        require(!storage->exists("made"), "RMDIR of MKDIR tree failed");

        std::filesystem::create_directories(root / "tree" / "sub");
        storage->writeText("tree/sub/f.txt", "1");
        storage->writeText("tree/g.txt", "2");
        require(fails(interpreter, "RMDIR \"tree\""), "RMDIR removed non-empty dir");
        interpreter.executeInteractive("RMDIR \"tree\" FORCE\n", out);
        require(!storage->exists("tree"), "RMDIR FORCE did not remove tree");
        interpreter.executeInteractive("RMDIR \"dir\"\n", out);
        std::filesystem::create_directories(root / "cwd");
        interpreter.executeInteractive("CD \"/\"\nCD cwd\n", out);
        require(fails(interpreter, "RMDIR \"/cwd\" FORCE"), "RMDIR removed current directory");
        require(fails(interpreter, "RMDIR \"/\" FORCE"), "RMDIR removed root");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testFileJobProgress() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-fileops-progress-" + std::to_string(unique));
    std::filesystem::create_directories(root / "big");
    try {
        ql32::platform::linux_host::DirectoryStorage storage(root);
        for (int i = 0; i < 20; ++i) storage.writeText("big/f" + std::to_string(i), "x");
        namespace fo = ql32::runtime::fileops;
        require(fo::activity().activeJobs == 0, "activity not idle");
        {
            fo::Job job(storage, fo::planRemoveDirectory(storage, "big", "/", true));
            require(fo::activity().activeJobs == 1, "activity missed running job");
            int slices = 0;
            // A zero budget must bound every slice to one removal, not one directory.
            while (!job.run(std::chrono::milliseconds(0))) {
                require(++slices < 100, "tree removal did not finish");
                require(job.progressText().rfind("Removing big", 0) == 0, "bad progress text");
            }
            require(slices >= 10, "tree removal slice is not time bounded");
        }
        require(fo::activity().activeJobs == 0, "activity not released");
        for (int i = 0; i < 4; ++i) storage.writeText("d" + std::to_string(i), "x");
        fo::Job job(storage, fo::planDelete(storage, "d*", true));
        job.run(std::chrono::milliseconds(0));
        require(job.progressText() == "Deleting 1/4", "bad delete progress text");
        require(fo::activity().completed == 1 && fo::activity().total == 4, "bad activity counts");
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}

// Wildcard delete, copy and recursive removal over 1200 files must not need
// any allocation that grows with the directory (the CYD heap is fragmented).
void testLargeDirectoryMemory() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-fileops-large-" + std::to_string(unique));
    std::filesystem::create_directories(root / "src");
    std::filesystem::create_directories(root / "dst");
    std::filesystem::create_directories(root / "tree");
    try {
        ql32::platform::linux_host::DirectoryStorage storage(root);
        namespace fo = ql32::runtime::fileops;
        constexpr int count = 1200;
        for (int i = 0; i < count; ++i) {
            const auto name = std::to_string(i);
            storage.writeText("src/f" + name + ".t", "x");
            storage.writeText("tree/t" + name, "x");
        }
        {
            // DIR and CAT stream the directory: the largest allocation is the
            // listing text itself, never a vector of every entry.
            auto shared = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(root);
            ql32::Interpreter interpreter(shared);
            for (const char* command : {"DIR \"src\"\n", "CAT \"src\"\n", "CAT \"src/f1*\"\n"}) {
                CountingBuffer counter;
                std::ostream listing(&counter);
                largestAllocation = 0;
                trackAllocations = true;
                interpreter.executeInteractive(command, listing);
                trackAllocations = false;
                require(counter.size > 1000, "listing too short");
                require(largestAllocation.load() <= counter.size + 4096,
                        "DIR/CAT needed an allocation larger than its listing");
            }
        }
        auto drain = [](fo::Job& job) { while (!job.run(std::chrono::milliseconds(20))) {} };
        largestAllocation = 0;
        trackAllocations = true;
        {
            fo::Job job(storage, fo::planTransfer(storage, "src/*.t", "dst/", true, false, false));
            drain(job);
        }
        {
            fo::Job job(storage, fo::planDelete(storage, "src/f*.t", true));
            require(job.total() == count, "streaming delete total wrong");
            drain(job);
            require(job.completed() == count, "streaming delete count wrong");
        }
        {
            fo::Job job(storage, fo::planRemoveDirectory(storage, "tree", "/", true));
            drain(job);
        }
        trackAllocations = false;
        require(largestAllocation.load() <= 4096, "large directory needed a big allocation");
        require(!std::filesystem::exists(root / "tree"), "tree not removed");
        require(std::distance(std::filesystem::directory_iterator(root / "dst"),
                              std::filesystem::directory_iterator{}) == count, "copy incomplete");
        require(std::distance(std::filesystem::directory_iterator(root / "src"),
                              std::filesystem::directory_iterator{}) == 0, "delete incomplete");
    } catch (...) {
        trackAllocations = false;
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testDirectoryStorage();
        testInteractiveFileCommands();
        testCopyMoveDeleteRmdir();
        testFileJobProgress();
        testLargeDirectoryMemory();
    } catch (const std::exception& error) {
        std::cerr << "storage test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
