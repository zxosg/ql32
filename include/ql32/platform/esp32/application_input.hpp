#pragma once
#include "ql32/input/session.hpp"
#include <array>
#include <string_view>
namespace ql32::platform::esp32 {
// Preserve monitor/debug framing while an application owns serial keys. Only
// recognized protocol prefixes are withheld; ordinary '@' arrives after 30ms.
class ApplicationProtocolGate {
public:
    void reset() noexcept { size_ = 0; }
    void flush(std::uint64_t now, input::SerialDecoder& decoder, input::KeyRouter& keys) {
        if (size_ && now - started_ >= 30) publish(decoder, keys, now);
    }
    template<class Protocol>
    void feed(unsigned char byte, std::uint64_t now, input::SerialDecoder& decoder,
              input::KeyRouter& keys, Protocol protocol) {
        if (!size_ && byte != '@') { if (auto event=decoder.feed(byte,now)) keys.publish(*event); return; }
        if (!size_) started_ = now;
        prefix_[size_++] = static_cast<char>(byte);
        const std::string_view value(prefix_.data(),size_);
        bool possible = false;
        for (const auto wanted : {std::string_view("@QL32DBG/1\t"), std::string_view("@SCREEN")}) {
            if (wanted.substr(0,value.size()) != value) continue;
            possible = true;
            if (value.size() == wanted.size()) { protocol(value); size_ = 0; return; }
        }
        if (!possible || size_ == prefix_.size()) publish(decoder,keys,now);
    }
private:
    void publish(input::SerialDecoder& decoder, input::KeyRouter& keys, std::uint64_t now) {
        for (unsigned i=0;i<size_;++i) if (auto event=decoder.feed(static_cast<unsigned char>(prefix_[i]),now)) keys.publish(*event);
        size_ = 0;
    }
    std::array<char,11> prefix_{};
    unsigned size_ = 0;
    std::uint64_t started_ = 0;
};
}
