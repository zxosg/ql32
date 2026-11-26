#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ql32/runtime/storage.hpp"

// Platform-neutral file commands behind COPY, MOVE, DELETE and RMDIR. Paths
// passed in are already normalized by the language layer (drive prefix kept,
// no "." or ".." components). Planning validates everything before any
// change is made; a Job then executes the plan in bounded time slices so a
// session never monopolizes the scheduler.
namespace ql32::runtime::fileops {

// Finite byte stream endpoints let a channel act as a COPY source or target
// without this module knowing about channels.
class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Returns 0 only at end of stream, or NoData when none is available yet
    // (the job then yields and retries on its next slice).
    static constexpr std::size_t NoData = static_cast<std::size_t>(-1);
    virtual std::size_t read(std::uint8_t* destination, std::size_t capacity) = 0;
};

class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual void write(const std::uint8_t* data, std::size_t size) = 0;
};

struct Step {
    enum class Kind {
        Copy,      // source -> destination, source kept
        Rename,    // same-drive move
        MoveCopy,  // cross-drive move: copy, verify, then remove source
        Delete,
    };
    Kind kind = Kind::Copy;
    std::string source;       // empty when streamed from a ByteSource
    std::string destination;  // empty when streamed to a ByteSink
    bool overwrite = false;
    std::shared_ptr<ByteSource> streamSource;
    std::shared_ptr<ByteSink> streamSink;
};

struct Plan {
    enum class Kind { Copy, Move, Delete, RemoveDirectory };
    Kind kind = Kind::Copy;
    // A deque keeps allocations small for large wildcard transfers.
    std::deque<Step> steps;
    // Wildcard DELETE streams the directory instead of listing a step per file.
    std::string streamDirectory, streamPattern;
    std::size_t streamTotal = 0;
    bool streaming() const noexcept { return streamTotal != 0; }
    std::size_t count() const noexcept { return streaming() ? streamTotal : steps.size(); }
    std::string directory;      // RemoveDirectory target
    bool recursive = false;     // RemoveDirectory with FORCE
    bool needsConfirmation = false;
    std::string confirmation;   // prompt text when needsConfirmation
};

std::string joinPath(const std::string& directory, const std::string& leaf);
void splitPath(const std::string& path, std::string& directory, std::string& leaf);
bool isRootPath(const std::string& path);
// MKDIR: fails if the path exists or its parent directory is missing.
void makeDirectory(StorageService& storage, const std::string& path);
std::optional<FileEntry> statPath(StorageService& storage, const std::string& path);

// `destinationIsDirectoryHint` is true when the user wrote a trailing '/'.
Plan planTransfer(StorageService& storage, const std::string& source,
                  const std::string& destination, bool destinationIsDirectoryHint,
                  bool force, bool move);
Plan planStreamToFile(StorageService& storage, const std::string& destination,
                      bool force, std::shared_ptr<ByteSource> source);
Plan planFileToStream(StorageService& storage, const std::string& source,
                      std::shared_ptr<ByteSink> sink);
Plan planStream(std::shared_ptr<ByteSource> source, std::shared_ptr<ByteSink> sink);
Plan planDelete(StorageService& storage, const std::string& path, bool force);
// `currentDirectory` is the session directory, which may never be removed.
Plan planRemoveDirectory(StorageService& storage, const std::string& path,
                         const std::string& currentDirectory, bool force);

// Process-wide disk-activity snapshot for status displays such as the debug
// bar. `total` is 0 when the running job cannot know it (recursive RMDIR).
struct Activity {
    unsigned activeJobs = 0;
    std::size_t completed = 0;
    std::size_t total = 0;
};
Activity activity() noexcept;

class Job {
public:
    Job(StorageService& storage, Plan plan, std::size_t chunkBytes = 2048);
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Runs until finished or the budget is spent. Returns true when done.
    // Failures throw ServiceError naming the failing item and progress; the
    // job is then finished and nothing further is executed.
    bool run(std::chrono::milliseconds budget);
    std::size_t completed() const noexcept { return completed_; }
    std::size_t total() const noexcept { return plan_.count(); }
    const Plan& plan() const noexcept { return plan_; }
    std::string summary() const;
    // Short running status such as "Deleting 37/120"; cheap and allocation-light.
    std::string progressText() const;

private:
    struct Active;
    bool runStep(Step& step, std::chrono::steady_clock::time_point deadline);
    bool runStreamingDelete(std::string& subject, std::chrono::steady_clock::time_point deadline);
    bool runRemoveTree(std::chrono::steady_clock::time_point deadline);
    void abandonActive() noexcept;

    StorageService& storage_;
    Plan plan_;
    std::size_t chunkBytes_;
    std::size_t completed_ = 0;
    std::size_t removedEntries_ = 0;
    std::unique_ptr<Active> active_;
    std::vector<std::string> directoryStack_;
    bool treeStarted_ = false;
};

} // namespace ql32::runtime::fileops
