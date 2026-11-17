#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ql32::music {

// Decode the raw LH5 bitstream carried by an LHA member. The caller supplies
// the exact expected output size from the enclosing VTX header.
bool decodeLh5(const std::uint8_t* input, std::size_t inputSize,
               std::size_t outputSize, std::vector<std::uint8_t>& output,
               const char** error = nullptr);

} // namespace ql32::music
