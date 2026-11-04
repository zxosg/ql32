#include "ql32/platform/esp32/sd_storage.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testReadWriteAndDirectoryListing() {
    fs::FS filesystem;
    filesystem.contents("/hello.bas") = "10 PRINT \"ready\"\n";
    filesystem.contents("/zeta.bas") = "z";
    filesystem.contents("/alpha.bas") = "a";
    ql32::platform::esp32::SdStorage storage(filesystem);

    require(storage.readText("hello.bas") == "10 PRINT \"ready\"\n",
            "SD storage did not return file contents");
    storage.writeText("saved.bas", "20 PRINT \"saved\"\n");
    require(storage.readText("saved.bas") == "20 PRINT \"saved\"\n",
            "SD storage did not write and read back text");
    require(storage.exists("SDC:/saved.bas") && storage.size("SDC:/saved.bas") == 17,
            "SD drive prefix or size query was not handled");
    storage.rename("SDC:/saved.bas", "SDC:/renamed.bas");
    require(storage.readText("renamed.bas") == "20 PRINT \"saved\"\n",
            "SD storage did not rename a drive-qualified file");
    storage.remove("SDC:/renamed.bas");
    require(!storage.exists("renamed.bas"), "SD storage did not delete a file");
    storage.writeText("saved.bas", "20 PRINT \"saved\"\n");
    const auto entries = storage.list("");
    require(entries.size() == 4 && entries[0].name == "alpha.bas" &&
                entries[1].name == "hello.bas" && entries[2].name == "saved.bas" &&
                entries[3].name == "zeta.bas" &&
                entries[1].size == 17 && !entries[1].isDirectory,
            "SD root listing was not sorted or returned wrong metadata");
}

void testRejectsEscapingPaths() {
    fs::FS filesystem;
    ql32::platform::esp32::SdStorage storage(filesystem);
    for (const std::string path : {"../outside.bas", "sub/../../outside.bas", "/absolute.bas", "a\\..\\b"}) {
        bool rejected = false;
        try {
            (void)storage.readText(path);
        } catch (const ql32::runtime::ServiceError& error) {
            rejected = error.code() == ql32::runtime::ServiceErrorCode::InvalidPath;
        }
        require(rejected, "SD storage accepted an absolute or escaping path");
    }
}

void testUnmountedStorageReportsUnavailable() {
    fs::FS filesystem;
    filesystem.setMounted(false);
    ql32::platform::esp32::SdStorage storage(filesystem);
    bool unavailable = false;
    try {
        (void)storage.list("");
    } catch (const ql32::runtime::ServiceError& error) {
        unavailable = error.code() == ql32::runtime::ServiceErrorCode::Unavailable;
    }
    require(unavailable, "unmounted SD filesystem did not report unavailable");
}

void testFfsDriveQualification() {
    fs::FS filesystem;
    filesystem.contents("/flash.bin") = "data";
    ql32::platform::esp32::SdStorage storage(filesystem, "FFS");
    require(storage.readText("FFS:/flash.bin") == "data",
            "FFS drive prefix did not resolve to the configured filesystem");
    bool rejected = false;
    try {
        (void)storage.readText("SDC:/flash.bin");
    } catch (const ql32::runtime::ServiceError& error) {
        rejected = error.code() == ql32::runtime::ServiceErrorCode::Unavailable;
    }
    require(rejected, "FFS adapter accepted an unavailable SDC drive");
}

} // namespace

int main() {
    try {
        testReadWriteAndDirectoryListing();
        testRejectsEscapingPaths();
        testUnmountedStorageReportsUnavailable();
        testFfsDriveQualification();
    } catch (const std::exception& error) {
        std::cerr << "ESP32 storage adapter test failed: " << error.what() << '\n';
        return 1;
    }
}
