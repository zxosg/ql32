#pragma once

#include <cstdint>
#include <string>

namespace ql32::runtime {

using MusicSessionId = std::uint64_t;

enum class MusicPlaybackState : std::uint8_t { Stopped, Playing, Paused };

class MusicService {
public:
    virtual ~MusicService() = default;
    virtual void play(MusicSessionId session, const std::string& path) = 0;
    virtual void stop(MusicSessionId session) = 0;
    virtual void pause(MusicSessionId session) = 0;
    virtual void resume(MusicSessionId session) = 0;
    virtual MusicPlaybackState state() const noexcept = 0;
    virtual void stopSession(MusicSessionId session) noexcept = 0;
};

} // namespace ql32::runtime
