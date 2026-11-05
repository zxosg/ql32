#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ql32/runtime/error.hpp"

namespace ql32::runtime {

// Session-local managed byte allocations used by binary device transfers.
class BufferManager {
public:
    std::uint64_t allocate(std::size_t size);
    void deallocate(std::uint64_t address);
    std::size_t allocationSize(std::uint64_t address) const;
    std::uint8_t peek(std::uint64_t address) const;
    void poke(std::uint64_t address, std::uint8_t value);
    void read(std::uint64_t address, std::uint8_t* destination, std::size_t count) const;
    void write(std::uint64_t address, const std::uint8_t* source, std::size_t count);
    std::uint64_t peekWord(std::uint64_t address, std::size_t width) const;
    void pokeWord(std::uint64_t address, std::size_t width, std::uint64_t value);
    void clear() noexcept;

private:
    struct Allocation { std::uint64_t base; std::vector<std::uint8_t> bytes; };
    std::pair<const Allocation*, std::size_t> locate(std::uint64_t address,
                                                      std::size_t count) const;
    std::pair<Allocation*, std::size_t> locate(std::uint64_t address,
                                               std::size_t count);
    // Allocation bases are monotonically increasing. A sorted vector avoids
    // one tree node allocation per BASIC ALLOC while retaining logarithmic
    // address lookup. Entries are never exposed, so vector relocation is safe.
    std::vector<Allocation> allocations_;
    std::uint64_t nextAddress_ = 0x10000;
};

} // namespace ql32::runtime
