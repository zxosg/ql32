#include "ql32/runtime/file_ops.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>

#include "ql32/runtime/path_glob.hpp"

namespace ql32::runtime::fileops {
namespace {

using Clock = std::chrono::steady_clock;

std::string upper(std::string value) {
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

bool sameName(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

std::string driveOf(const std::string& path) {
    const auto colon = path.find(':');
    return colon == std::string::npos ? std::string{} : upper(path.substr(0, colon));
}

[[noreturn]] void fail(ServiceErrorCode code, const std::string& message) {
    throw ServiceError(code, message);
}

bool isFile(const FileEntry& entry) { return !entry.isDirectory; }

// Files in `directory` matching the wildcard `pattern`, as full paths.
std::vector<std::string> expandWildcard(StorageService& storage, const std::string& directory,
                                        const std::string& pattern) {
    std::vector<std::string> result;
    for (const auto& entry : storage.list(directory))
        if (isFile(entry) && wildcardMatch(pattern, entry.name))
            result.push_back(joinPath(directory, entry.name));
    std::sort(result.begin(), result.end());
    return result;
}

struct Sources {
    std::vector<std::string> paths;
    bool wildcard = false;
};

Sources resolveSources(StorageService& storage, const std::string& source, const char* verb) {
    std::string directory, leaf;
    splitPath(source, directory, leaf);
    if (hasWildcard(directory))
        fail(ServiceErrorCode::InvalidPath, "wildcards are only allowed in the file name");
    Sources found;
    if (hasWildcard(leaf)) {
        found.wildcard = true;
        found.paths = expandWildcard(storage, directory, leaf);
        if (found.paths.empty())
            fail(ServiceErrorCode::NotFound, "no files match: " + source);
        return found;
    }
    const auto entry = statPath(storage, source);
    if (!entry) fail(ServiceErrorCode::NotFound, "file not found: " + source);
    if (entry->isDirectory)
        fail(ServiceErrorCode::InvalidPath,
             std::string(verb) + " of directories is not supported: " + source);
    found.paths.push_back(joinPath(directory, entry->name));
    return found;
}

void checkSpace(StorageService& storage, const std::string& destinationDirectory,
                std::uint64_t needed) {
    if (needed == 0) return;
    const StorageSpace space = storage.space(destinationDirectory);
    if (space.known && space.free < needed)
        fail(ServiceErrorCode::IoError,
             "not enough space on destination (need " + std::to_string(needed) +
             " bytes, " + std::to_string(space.free) + " free)");
}

std::string temporaryName(StorageService& storage, const std::string& destination) {
    static std::atomic<std::uint64_t> sequence{0};
    std::string directory, leaf;
    splitPath(destination, directory, leaf);
    std::string name;
    do {
        name = joinPath(directory, ".q32-c" + std::to_string(
            sequence.fetch_add(1, std::memory_order_relaxed) + 1));
    } while (storage.exists(name));
    return name;
}

// Install `temporary` as `destination`, keeping a backup of any prior file
// until the new one is in place.
void install(StorageService& storage, const std::string& temporary,
             const std::string& destination) {
    if (!storage.exists(destination)) {
        storage.rename(temporary, destination);
        return;
    }
    const std::string backup = temporaryName(storage, destination);
    storage.rename(destination, backup);
    try {
        storage.rename(temporary, destination);
    } catch (...) {
        try { storage.rename(backup, destination); } catch (...) {}
        throw;
    }
    try { storage.remove(backup); } catch (...) {}
}

class StorageByteSource final : public ByteSource {
public:
    explicit StorageByteSource(std::unique_ptr<StorageReadHandle> handle)
        : handle_(std::move(handle)) {}
    std::size_t read(std::uint8_t* destination, std::size_t capacity) override {
        const std::size_t count = handle_->readAt(offset_, destination, capacity);
        offset_ += count;
        return count;
    }
    std::uint64_t size() const { return handle_->size(); }
    std::uint64_t offset() const { return offset_; }

private:
    std::unique_ptr<StorageReadHandle> handle_;
    std::uint64_t offset_ = 0;
};

} // namespace

std::string joinPath(const std::string& directory, const std::string& leaf) {
    if (directory.empty()) return leaf;
    if (directory.back() == '/') return directory + leaf;
    return directory + "/" + leaf;
}

void splitPath(const std::string& path, std::string& directory, std::string& leaf) {
    const auto separator = path.find_last_of('/');
    if (separator != std::string::npos) {
        directory = path.substr(0, separator + 1);
        const auto colon = directory.find(':');
        const bool root = directory.size() == 1 ||
            (colon != std::string::npos && colon + 2 == directory.size());
        if (!root) directory.pop_back();
        leaf = path.substr(separator + 1);
        return;
    }
    const auto colon = path.find(':');
    if (colon != std::string::npos) {
        directory = path.substr(0, colon + 1) + "/";
        leaf = path.substr(colon + 1);
        return;
    }
    directory.clear();
    leaf = path;
}

bool isRootPath(const std::string& path) {
    if (path.empty() || path == "/") return true;
    const auto colon = path.find(':');
    if (colon == std::string::npos) return false;
    const std::string rest = path.substr(colon + 1);
    return rest.empty() || rest == "/";
}

std::optional<FileEntry> statPath(StorageService& storage, const std::string& path) {
    if (isRootPath(path)) return FileEntry{path, 0, true};
    std::string directory, leaf;
    splitPath(path, directory, leaf);
    if (leaf.empty()) return std::nullopt;
    std::vector<FileEntry> entries;
    try {
        entries = storage.list(directory);
    } catch (const ServiceError& error) {
        if (error.code() == ServiceErrorCode::NotFound) return std::nullopt;
        throw;
    }
    for (const auto& entry : entries)
        if (entry.name == leaf) return entry;
    for (const auto& entry : entries)
        if (sameName(entry.name, leaf)) return entry;
    // Flat filesystems (SPIFFS) have no directory entries; a prefix with files is a directory.
    try {
        if (!storage.list(path).empty()) return FileEntry{leaf, 0, true};
    } catch (const ServiceError& error) {
        if (error.code() != ServiceErrorCode::NotFound &&
            error.code() != ServiceErrorCode::InvalidPath) throw;
    }
    return std::nullopt;
}

void makeDirectory(StorageService& storage, const std::string& path) {
    if (hasWildcard(path)) fail(ServiceErrorCode::InvalidPath, "MKDIR does not accept wildcards");
    std::string target = path;
    while (target.size() > 1 && target.back() == '/' && !isRootPath(target)) target.pop_back();
    if (isRootPath(target) || statPath(storage, target))
        fail(ServiceErrorCode::IoError, "already exists: " + path);
    std::string directory, leaf;
    splitPath(target, directory, leaf);
    if (leaf.empty()) fail(ServiceErrorCode::InvalidPath, "invalid directory name: " + path);
    if (!directory.empty() && !isRootPath(directory)) {
        const auto parent = statPath(storage, directory);
        if (!parent || !parent->isDirectory)
            fail(ServiceErrorCode::NotFound, "parent directory not found: " + directory);
    }
    storage.createDirectory(target);
}

Plan planTransfer(StorageService& storage, const std::string& source,
                  const std::string& destination, bool destinationIsDirectoryHint,
                  bool force, bool move) {
    const char* verb = move ? "MOVE" : "COPY";
    const Sources sources = resolveSources(storage, source, verb);

    if (hasWildcard(destination))
        fail(ServiceErrorCode::InvalidPath, "destination must not contain wildcards");
    const auto destinationEntry = statPath(storage, destination);
    const bool destinationIsDirectory = destinationIsDirectoryHint ||
        isRootPath(destination) || (destinationEntry && destinationEntry->isDirectory);
    if (!destinationIsDirectory && sources.paths.size() > 1)
        fail(ServiceErrorCode::InvalidPath,
             std::string(verb) + " of several files needs a destination directory");

    Plan plan;
    plan.kind = move ? Plan::Kind::Move : Plan::Kind::Copy;
    std::uint64_t bytes = 0;
    for (const auto& path : sources.paths) {
        std::string directory, leaf;
        splitPath(path, directory, leaf);
        Step step;
        step.source = path;
        step.destination = destination;
        if (destinationIsDirectory) {
            std::string trimmed = destination;
            while (trimmed.size() > 1 && trimmed.back() == '/' && !isRootPath(trimmed))
                trimmed.pop_back();
            step.destination = joinPath(trimmed, leaf);
        }
        if (sameName(step.source, step.destination))
            fail(ServiceErrorCode::InvalidPath,
                 std::string(verb) + " source and destination are the same: " + path);
        const auto existing = statPath(storage, step.destination);
        if (existing) {
            if (existing->isDirectory)
                fail(ServiceErrorCode::InvalidPath,
                     "destination is a directory: " + step.destination);
            if (!force)
                fail(ServiceErrorCode::IoError,
                     "destination already exists: " + step.destination + " (use FORCE)");
            step.overwrite = true;
        }
        const bool sameDrive = driveOf(step.source) == driveOf(step.destination);
        if (move && sameDrive) {
            step.kind = Step::Kind::Rename;
        } else {
            step.kind = move ? Step::Kind::MoveCopy : Step::Kind::Copy;
            const auto sourceEntry = statPath(storage, path);
            if (sourceEntry) bytes += sourceEntry->size;
        }
        plan.steps.push_back(std::move(step));
    }
    std::string spaceDirectory, ignored;
    splitPath(plan.steps.front().destination, spaceDirectory, ignored);
    checkSpace(storage, spaceDirectory, bytes);
    return plan;
}

Plan planStreamToFile(StorageService& storage, const std::string& destination, bool force,
                      std::shared_ptr<ByteSource> source) {
    if (hasWildcard(destination))
        fail(ServiceErrorCode::InvalidPath, "destination must not contain wildcards");
    const auto existing = statPath(storage, destination);
    Step step;
    step.kind = Step::Kind::Copy;
    step.destination = destination;
    step.streamSource = std::move(source);
    if (existing) {
        if (existing->isDirectory)
            fail(ServiceErrorCode::InvalidPath, "destination is a directory: " + destination);
        if (!force)
            fail(ServiceErrorCode::IoError,
                 "destination already exists: " + destination + " (use FORCE)");
        step.overwrite = true;
    }
    Plan plan;
    plan.kind = Plan::Kind::Copy;
    plan.steps.push_back(std::move(step));
    return plan;
}

Plan planFileToStream(StorageService& storage, const std::string& source,
                      std::shared_ptr<ByteSink> sink) {
    const Sources sources = resolveSources(storage, source, "COPY");
    if (sources.paths.size() != 1)
        fail(ServiceErrorCode::InvalidPath, "COPY to a channel needs exactly one source file");
    Step step;
    step.kind = Step::Kind::Copy;
    step.source = sources.paths.front();
    step.streamSink = std::move(sink);
    Plan plan;
    plan.kind = Plan::Kind::Copy;
    plan.steps.push_back(std::move(step));
    return plan;
}

Plan planStream(std::shared_ptr<ByteSource> source, std::shared_ptr<ByteSink> sink) {
    Step step;
    step.kind = Step::Kind::Copy;
    step.streamSource = std::move(source);
    step.streamSink = std::move(sink);
    Plan plan;
    plan.kind = Plan::Kind::Copy;
    plan.steps.push_back(std::move(step));
    return plan;
}

Plan planDelete(StorageService& storage, const std::string& path, bool force) {
    const Sources sources = resolveSources(storage, path, "DELETE");
    Plan plan;
    plan.kind = Plan::Kind::Delete;
    for (const auto& file : sources.paths) {
        Step step;
        step.kind = Step::Kind::Delete;
        step.source = file;
        plan.steps.push_back(std::move(step));
    }
    if (sources.wildcard && plan.steps.size() > 1 && !force) {
        plan.needsConfirmation = true;
        plan.confirmation = "Delete " + std::to_string(plan.steps.size()) +
                            " files? (Y/N)";
    }
    return plan;
}

Plan planRemoveDirectory(StorageService& storage, const std::string& path,
                         const std::string& currentDirectory, bool force) {
    if (hasWildcard(path))
        fail(ServiceErrorCode::InvalidPath, "RMDIR does not accept wildcards");
    std::string target = path;
    while (target.size() > 1 && target.back() == '/' && !isRootPath(target)) target.pop_back();
    if (isRootPath(target))
        fail(ServiceErrorCode::InvalidPath, "cannot remove a drive or storage root");
    const std::string current = upper(currentDirectory);
    const std::string wanted = upper(target);
    if (current == wanted || current.rfind(wanted + "/", 0) == 0)
        fail(ServiceErrorCode::ResourceBusy,
             "cannot remove the current directory or one of its parents");
    const auto entry = statPath(storage, target);
    if (!entry) fail(ServiceErrorCode::NotFound, "directory not found: " + path);
    if (!entry->isDirectory)
        fail(ServiceErrorCode::InvalidPath, "path is not a directory: " + path);
    if (!force && !storage.list(target).empty())
        fail(ServiceErrorCode::IoError,
             "directory is not empty: " + path + " (use FORCE to remove recursively)");
    Plan plan;
    plan.kind = Plan::Kind::RemoveDirectory;
    plan.directory = target;
    plan.recursive = force;
    return plan;
}

struct Job::Active {
    std::shared_ptr<ByteSource> source;
    std::shared_ptr<ByteSink> sink;
    std::unique_ptr<StorageByteSource> fileSource;
    std::unique_ptr<StorageWriteHandle> writer;
    std::string temporary;
    std::uint64_t written = 0;
    std::vector<std::uint8_t> buffer;
};

Job::Job(StorageService& storage, Plan plan, std::size_t chunkBytes)
    : storage_(storage), plan_(std::move(plan)),
      chunkBytes_(std::max<std::size_t>(chunkBytes, 64)) {}

Job::~Job() { abandonActive(); }

void Job::abandonActive() noexcept {
    if (!active_) return;
    const std::string temporary = active_->temporary;
    active_->writer.reset();
    active_.reset();
    if (!temporary.empty()) {
        try { if (storage_.exists(temporary)) storage_.remove(temporary); } catch (...) {}
    }
}

std::string Job::summary() const {
    switch (plan_.kind) {
    case Plan::Kind::Copy: return std::to_string(completed_) + " file(s) copied";
    case Plan::Kind::Move: return std::to_string(completed_) + " file(s) moved";
    case Plan::Kind::Delete: return std::to_string(completed_) + " file(s) deleted";
    case Plan::Kind::RemoveDirectory:
        return plan_.recursive ? std::to_string(removedEntries_) + " file(s) deleted, " +
                                     "directory removed"
                               : "directory removed";
    }
    return {};
}

bool Job::run(std::chrono::milliseconds budget) {
    const auto deadline = Clock::now() + budget;
    std::string subject;
    try {
        if (plan_.kind == Plan::Kind::RemoveDirectory) {
            subject = plan_.directory;
            if (!plan_.recursive) {
                storage_.removeDirectory(plan_.directory);
                return true;
            }
            return runRemoveTree(deadline);
        }
        while (completed_ < plan_.steps.size()) {
            Step& step = plan_.steps[completed_];
            subject = step.source.empty() ? step.destination : step.source;
            if (!runStep(step, deadline)) return false;
            active_.reset();
            ++completed_;
            if (Clock::now() >= deadline && completed_ < plan_.steps.size()) return false;
        }
        return true;
    } catch (const ServiceError& error) {
        abandonActive();
        const std::string progress = plan_.steps.empty() ? std::string{} :
            " after " + std::to_string(completed_) + " of " +
            std::to_string(plan_.steps.size());
        throw ServiceError(error.code(),
            (subject.empty() ? std::string{} : subject + ": ") + error.what() +
            (progress.empty() ? std::string{} : " (stopped" + progress + ")"));
    } catch (...) {
        abandonActive();
        throw;
    }
}

bool Job::runStep(Step& step, Clock::time_point deadline) {
    switch (step.kind) {
    case Step::Kind::Delete:
        storage_.remove(step.source);
        return true;
    case Step::Kind::Rename:
        if (step.overwrite) {
            const std::string backup = temporaryName(storage_, step.destination);
            storage_.rename(step.destination, backup);
            try {
                storage_.rename(step.source, step.destination);
            } catch (...) {
                try { storage_.rename(backup, step.destination); } catch (...) {}
                throw;
            }
            try { storage_.remove(backup); } catch (...) {}
        } else {
            storage_.rename(step.source, step.destination);
        }
        return true;
    case Step::Kind::Copy:
    case Step::Kind::MoveCopy:
        break;
    }

    if (!active_) {
        active_ = std::make_unique<Active>();
        active_->buffer.resize(chunkBytes_);
        if (step.streamSource) {
            active_->source = step.streamSource;
        } else {
            active_->fileSource = std::make_unique<StorageByteSource>(
                storage_.openRead(step.source));
        }
        if (step.streamSink) {
            active_->sink = step.streamSink;
        } else {
            active_->temporary = temporaryName(storage_, step.destination);
            active_->writer = storage_.openWrite(active_->temporary);
        }
    }
    Active& active = *active_;
    ByteSource& source = active.fileSource ? static_cast<ByteSource&>(*active.fileSource)
                                           : *active.source;
    for (;;) {
        const std::size_t count = source.read(active.buffer.data(), active.buffer.size());
        if (count == ByteSource::NoData) return false;
        if (count == 0) break;
        if (active.writer) active.writer->write(active.buffer.data(), count);
        else active.sink->write(active.buffer.data(), count);
        active.written += count;
        if (Clock::now() >= deadline) return false;
    }
    if (active.fileSource && active.fileSource->offset() != active.fileSource->size())
        throw ServiceError(ServiceErrorCode::IoError, "source ended before its full size");
    if (active.writer) {
        active.writer->close();
        active.writer.reset();
        if (storage_.size(active.temporary) != active.written)
            throw ServiceError(ServiceErrorCode::IoError, "copy verification failed");
        install(storage_, active.temporary, step.destination);
        active.temporary.clear();
    }
    if (step.kind == Step::Kind::MoveCopy) storage_.remove(step.source);
    return true;
}

bool Job::runRemoveTree(Clock::time_point deadline) {
    if (!treeStarted_) {
        treeStarted_ = true;
        directoryStack_.push_back(plan_.directory);
    }
    while (!directoryStack_.empty()) {
        const std::string directory = directoryStack_.back();
        std::vector<FileEntry> entries;
        try {
            entries = storage_.list(directory);
        } catch (const ServiceError& error) {
            // Flat filesystems drop an emptied directory automatically.
            if (error.code() != ServiceErrorCode::NotFound) throw;
        }
        bool descended = false;
        for (const auto& entry : entries) {
            const std::string child = joinPath(directory, entry.name);
            if (entry.isDirectory) {
                directoryStack_.push_back(child);
                descended = true;
                break;
            }
            storage_.remove(child);
            ++removedEntries_;
        }
        if (!descended) {
            try {
                storage_.removeDirectory(directory);
            } catch (const ServiceError&) {
                if (storage_.exists(directory)) throw;
            }
            directoryStack_.pop_back();
        }
        if (Clock::now() >= deadline && !directoryStack_.empty()) return false;
    }
    return true;
}

} // namespace ql32::runtime::fileops
