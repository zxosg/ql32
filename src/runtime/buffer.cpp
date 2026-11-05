#include "ql32/runtime/buffer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ql32::runtime {

std::uint64_t BufferManager::allocate(std::size_t size) {
    if (size == 0) throw ServiceError(ServiceErrorCode::InvalidPath,
                                      "ALLOC size must be positive");
    if (size > std::numeric_limits<std::uint64_t>::max() - nextAddress_ - 16)
        throw ServiceError(ServiceErrorCode::Unavailable, "buffer address space exhausted");
    const auto base = nextAddress_;
    Allocation allocation{base, std::vector<std::uint8_t>(size, 0)};
    const auto position = std::lower_bound(allocations_.begin(), allocations_.end(), base,
        [](const Allocation& candidate, std::uint64_t key) { return candidate.base < key; });
    allocations_.insert(position, std::move(allocation));
    nextAddress_ += static_cast<std::uint64_t>(size) + 16;
    return base;
}

void BufferManager::clear() noexcept {
    std::vector<Allocation>().swap(allocations_);
}

void BufferManager::deallocate(std::uint64_t address) {
    const auto found = std::lower_bound(allocations_.begin(), allocations_.end(), address,
        [](const Allocation& candidate, std::uint64_t key) { return candidate.base < key; });
    if (found == allocations_.end() || found->base != address)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "DEALLOC requires an allocation base address");
    allocations_.erase(found);
}

std::pair<const BufferManager::Allocation*, std::size_t>
BufferManager::locate(std::uint64_t address, std::size_t count) const {
    auto found = std::upper_bound(allocations_.begin(), allocations_.end(), address,
        [](std::uint64_t key, const Allocation& candidate) { return key < candidate.base; });
    if (found == allocations_.begin())
        throw ServiceError(ServiceErrorCode::InvalidHandle, "buffer address is not allocated");
    --found;
    const auto offset64 = address - found->base;
    if (offset64 > found->bytes.size() ||
        count > found->bytes.size() - static_cast<std::size_t>(offset64))
        throw ServiceError(ServiceErrorCode::InvalidHandle, "buffer access exceeds allocation");
    return {&*found, static_cast<std::size_t>(offset64)};
}

std::pair<BufferManager::Allocation*, std::size_t>
BufferManager::locate(std::uint64_t address, std::size_t count) {
    auto found = std::upper_bound(allocations_.begin(), allocations_.end(), address,
        [](std::uint64_t key, const Allocation& candidate) { return key < candidate.base; });
    if (found == allocations_.begin())
        throw ServiceError(ServiceErrorCode::InvalidHandle, "buffer address is not allocated");
    --found;
    const auto offset64 = address - found->base;
    if (offset64 > found->bytes.size() ||
        count > found->bytes.size() - static_cast<std::size_t>(offset64))
        throw ServiceError(ServiceErrorCode::InvalidHandle, "buffer access exceeds allocation");
    return {&*found, static_cast<std::size_t>(offset64)};
}

std::size_t BufferManager::allocationSize(std::uint64_t address) const {
    const auto found = std::lower_bound(allocations_.begin(), allocations_.end(), address,
        [](const Allocation& candidate, std::uint64_t key) { return candidate.base < key; });
    if (found == allocations_.end() || found->base != address)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "buffer address is not an allocation base");
    return found->bytes.size();
}

std::uint8_t BufferManager::peek(std::uint64_t address) const {
    const auto [allocation, offset] = locate(address, 1);
    return allocation->bytes[offset];
}

void BufferManager::poke(std::uint64_t address, std::uint8_t value) {
    auto [allocation, offset] = locate(address, 1);
    allocation->bytes[offset] = value;
}

void BufferManager::read(std::uint64_t address, std::uint8_t* destination,
                         std::size_t count) const {
    const auto [allocation, offset] = locate(address, count);
    std::copy_n(allocation->bytes.data() + offset, count, destination);
}

void BufferManager::write(std::uint64_t address, const std::uint8_t* source,
                          std::size_t count) {
    auto [allocation, offset] = locate(address, count);
    std::copy_n(source, count, allocation->bytes.data() + offset);
}

std::uint64_t BufferManager::peekWord(std::uint64_t address, std::size_t width) const {
    if (width != 2 && width != 4)
        throw ServiceError(ServiceErrorCode::InvalidPath, "word width must be 2 or 4 bytes");
    const auto [allocation, offset] = locate(address, width);
    std::uint64_t value = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (std::size_t i = 0; i < width; ++i)
        value = (value << 8) | allocation->bytes[offset + i];
#else
    for (std::size_t i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(allocation->bytes[offset + i]) << (8 * i);
#endif
    return value;
}

void BufferManager::pokeWord(std::uint64_t address, std::size_t width,
                             std::uint64_t value) {
    if (width != 2 && width != 4)
        throw ServiceError(ServiceErrorCode::InvalidPath, "word width must be 2 or 4 bytes");
    auto [allocation, offset] = locate(address, width);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (std::size_t i = 0; i < width; ++i)
        allocation->bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * (width - i - 1)));
#else
    for (std::size_t i = 0; i < width; ++i)
        allocation->bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
#endif
}

} // namespace ql32::runtime
