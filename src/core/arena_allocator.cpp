#include "core/arena_allocator.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace jsontitan::core {

ArenaAllocator::ArenaAllocator(std::size_t blockSize)
    : m_blockSize(blockSize) {}

ArenaAllocator::ArenaAllocator(ArenaAllocator&&) noexcept = default;
ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&&) noexcept = default;

auto ArenaAllocator::alignUp(std::size_t value,
                             std::size_t alignment) noexcept -> std::size_t {
    // alignment must be a power of two
    return (value + alignment - 1) & ~(alignment - 1);
}

auto ArenaAllocator::allocate(std::size_t size, std::size_t alignment)
    -> void* {
    // Blocks come from new[], which only guarantees fundamental alignment;
    // over-aligned requests cannot be honored by the base pointer, so
    // reject them instead of returning a misaligned pointer.
    if (alignment > alignof(std::max_align_t)) {
        return nullptr;
    }

    // Zero-size allocations return a valid, unique pointer.
    if (size == 0) {
        size = 1;
    }

    // Try to allocate from the current (last) block.
    if (!m_blocks.empty()) {
        auto& block = m_blocks.back();
        std::size_t alignedOffset = alignUp(block.used, alignment);
        if (alignedOffset + size <= block.capacity) {
            block.used = alignedOffset + size;
            return block.data.get() + alignedOffset;
        }
    }

    // Current block is exhausted (or no blocks yet). Allocate a new one.
    std::size_t minSize = (std::max)(m_blockSize, size + alignment);
    std::size_t blockCountBefore = m_blocks.size();
    allocateNewBlock(minSize);
    if (m_blocks.size() == blockCountBefore) {
        return nullptr;  // OOM: no new block could be allocated
    }

    auto& block = m_blocks.back();
    std::size_t alignedOffset = alignUp(block.used, alignment);
    block.used = alignedOffset + size;
    return block.data.get() + alignedOffset;
}

auto ArenaAllocator::copyString(std::string_view src) -> std::string_view {
    if (src.empty()) {
        return std::string_view{};
    }
    void* mem = allocate(src.size(), 1);
    if (!mem) {
        return std::string_view{};
    }
    std::memcpy(mem, src.data(), src.size());
    return std::string_view{static_cast<const char*>(mem), src.size()};
}

void ArenaAllocator::reset() noexcept {
    m_blocks.clear();
}

auto ArenaAllocator::totalAllocated() const noexcept -> std::size_t {
    std::size_t total = 0;
    for (const auto& block : m_blocks) {
        total += block.capacity;
    }
    return total;
}

auto ArenaAllocator::totalUsed() const noexcept -> std::size_t {
    std::size_t total = 0;
    for (const auto& block : m_blocks) {
        total += block.used;
    }
    return total;
}

void ArenaAllocator::allocateNewBlock(std::size_t minSize) {
    std::size_t capacity = (std::max)(m_blockSize, minSize);
    // nothrow: allocation failure must be observable as a nullptr from
    // allocate()/construct() (callers check), not a bad_alloc mid-parse.
    std::unique_ptr<std::byte[]> data(new (std::nothrow) std::byte[capacity]);
    if (!data) {
        return;  // allocate() sees no usable block and returns nullptr
    }
    m_blocks.push_back(ArenaBlock{
        .data = std::move(data),
        .capacity = capacity,
        .used = 0,
    });
}

} // namespace jsontitan::core
