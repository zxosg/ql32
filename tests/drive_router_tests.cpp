#include "ql32/platform/esp32/drive_router.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class MemoryStorage final : public ql32::runtime::StorageService {
public:
    std::string readText(const std::string& path) override { return files.at(path); }
    void writeText(const std::string& path, const std::string& contents) override {
        files[path] = contents;
    }
    std::vector<ql32::runtime::FileEntry> list(const std::string& path) override {
        lastListPath = path;
        return {{name, 1, false}};
    }
    bool exists(const std::string& path) override { return files.find(path) != files.end(); }
    std::uintmax_t size(const std::string& path) override { return files.at(path).size(); }
    void rename(const std::string& path, const std::string& newPath) override {
        files[newPath] = files.at(path);
        files.erase(path);
    }
    void remove(const std::string& path) override { files.erase(path); }

    std::map<std::string, std::string> files;
    std::string name = "entry";
    std::string lastListPath;
};

class MemoryChannels final : public ql32::runtime::ChannelService {
public:
    ql32::runtime::ChannelId open(ql32::runtime::SessionId session,
            const std::string& path, ql32::runtime::ChannelMode) override {
        const auto id = next++;
        owners[id] = session;
        contents[id] = path;
        return id;
    }
    std::optional<std::string> readLine(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel) override {
        check(session, channel);
        return contents.at(channel);
    }
    std::string readBytes(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel, std::size_t) override {
        check(session, channel);
        return contents.at(channel);
    }
    void writeBytes(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel, std::string_view bytes) override {
        check(session, channel);
        contents[channel].assign(bytes);
    }
    void seek(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel, std::uint64_t) override {
        check(session, channel);
    }
    bool atEnd(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel) override {
        check(session, channel);
        return true;
    }
    void write(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel, const std::string& text) override {
        check(session, channel);
        contents[channel] = text;
    }
    void close(ql32::runtime::SessionId session,
            ql32::runtime::ChannelId channel) override {
        check(session, channel);
        owners.erase(channel);
        contents.erase(channel);
    }
    void closeSession(ql32::runtime::SessionId session) override {
        for (auto it = owners.begin(); it != owners.end();) {
            if (it->second == session) {
                contents.erase(it->first);
                it = owners.erase(it);
            } else ++it;
        }
    }
    std::vector<ql32::runtime::ChannelInfo> listOpen(
            ql32::runtime::SessionId session) override {
        std::vector<ql32::runtime::ChannelInfo> result;
        for (const auto& [channel, owner] : owners)
            if (owner == session) result.push_back({channel, ql32::runtime::ChannelType::File});
        return result;
    }

    std::map<ql32::runtime::ChannelId, std::string> contents;

private:
    void check(ql32::runtime::SessionId session, ql32::runtime::ChannelId channel) const {
        const auto found = owners.find(channel);
        if (found == owners.end() || found->second != session)
            throw ql32::runtime::ServiceError(ql32::runtime::ServiceErrorCode::InvalidHandle,
                "channel is not open in this session");
    }

    ql32::runtime::ChannelId next = 2;
    std::map<ql32::runtime::ChannelId, ql32::runtime::SessionId> owners;
};

void testStorageDriveRouting() {
    MemoryStorage ffs, sdc;
    ql32::platform::esp32::DriveRouterStorage storage(ffs, sdc);
    storage.writeText("FFS:/same.bas", "flash");
    storage.writeText("SDC:/same.bas", "card");
    storage.writeText("default.bas", "default card");
    require(storage.readText("FFS:/same.bas") == "flash", "FFS path did not route to flash");
    require(storage.readText("SDC:/same.bas") == "card", "SDC path did not route to card");
    require(storage.readText("default.bas") == "default card", "unqualified path did not use SDC");
    require(storage.list("FFS:/").front().name == "entry" && ffs.lastListPath == "FFS:/",
            "FFS directory listing used the wrong service");

    bool crossDriveRenameRejected = false;
    try { storage.rename("FFS:/same.bas", "SDC:/copy.bas"); }
    catch (const ql32::runtime::ServiceError&) { crossDriveRenameRejected = true; }
    require(crossDriveRenameRejected, "cross-drive rename was not rejected");
}

void testChannelDriveRoutingAndOwnership() {
    MemoryChannels ffs, sdc;
    ql32::platform::esp32::DriveRouterChannels channels(ffs, sdc);
    const auto flash = channels.open(7, "FFS:/test.txt", ql32::runtime::ChannelMode::Write);
    const auto card = channels.open(7, "SDC:/test.txt", ql32::runtime::ChannelMode::Write);
    require(flash != card, "routed handles collided across drives");
    channels.write(7, flash, "flash data");
    channels.write(7, card, "card data");
    require(ffs.contents.at(2) == "flash data" && sdc.contents.at(2) == "card data",
            "file channels did not route to their backing drives");
    require(channels.listOpen(7).size() == 2, "routed channel listing lost an open handle");

    bool crossSessionRejected = false;
    try { (void)channels.readLine(8, flash); }
    catch (const ql32::runtime::ServiceError&) { crossSessionRejected = true; }
    require(crossSessionRejected, "routed channel crossed session ownership");
    channels.closeSession(7);
    require(channels.listOpen(7).empty(), "session cleanup left routed channels open");
}

} // namespace

int main() {
    testStorageDriveRouting();
    testChannelDriveRoutingAndOwnership();
}
