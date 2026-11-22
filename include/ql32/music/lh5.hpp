#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ql32::music {

// Pulls up to `capacity` compressed bytes; returns 0 at end of input.
using Lh5ByteReader = std::function<std::size_t(std::uint8_t*, std::size_t)>;
// Receives decoded bytes in order. It may throw to abort decoding.
using Lh5OutputSink = std::function<void(const std::uint8_t*, std::size_t)>;

// Streaming decode: only the 8 KiB dictionary is held in memory.
bool decodeLh5Stream(const Lh5ByteReader& reader, std::size_t outputSize,
                     const Lh5OutputSink& sink, const char** error = nullptr);

// Decode the raw LH5 bitstream carried by an LHA member. The caller supplies
// the exact expected output size from the enclosing VTX header.
bool decodeLh5(const std::uint8_t* input, std::size_t inputSize,
               std::size_t outputSize, std::vector<std::uint8_t>& output,
               const char** error = nullptr);

} // namespace ql32::music
