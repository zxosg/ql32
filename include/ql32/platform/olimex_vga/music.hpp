#pragma once

#include "ql32/runtime/music.hpp"
#include "ql32/runtime/storage.hpp"

#include <memory>

namespace ql32::platform::olimex_vga {

class MusicService final : public runtime::MusicService {
public:
    explicit MusicService(std::shared_ptr<runtime::StorageService> storage);
    ~MusicService() override;
    MusicService(const MusicService&) = delete;
    MusicService& operator=(const MusicService&) = delete;

    void play(runtime::MusicSessionId session, const std::string& path) override;
    void stop(runtime::MusicSessionId session) override;
    void pause(runtime::MusicSessionId session) override;
    void resume(runtime::MusicSessionId session) override;
    runtime::MusicPlaybackState state() const noexcept override;
    void stopSession(runtime::MusicSessionId session) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ql32::platform::olimex_vga
