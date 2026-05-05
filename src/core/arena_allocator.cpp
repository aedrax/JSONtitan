#include "core/arena_allocator.h"

#include <algorithm>
#include <cstring>

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
    std::size_t minSize = std::max(m_blockSize, size + alignment);
    allocateNewBlock(minSize);

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

void ArenaAllocator::absorb(ArenaAllocator&& other) noexcept {
    for (auto& block : other.m_blocks) {
        m_blocks.push_back(std::move(block));
    }
    other.m_blocks.clear();
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
    std::size_t capacity = std::max(m_blockSize, minSize);
    auto data = std::make_unique<std::byte[]>(capacity);
    m_blocks.push_back(ArenaBlock{
        .data = std::move(data),
        .capacity = capacity,
        .used = 0,
    });
}

// --- ThreadLocalArena ---

ThreadLocalArena::ThreadLocalArena(std::size_t blockSize)
    : m_blockSize(blockSize) {}

auto ThreadLocalArena::get() -> ArenaAllocator& {
    auto id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_arenas.find(id);
    if (it == m_arenas.end()) {
        auto [inserted, _] = m_arenas.emplace(
            id, std::make_unique<ArenaAllocator>(m_blockSize));
        return *inserted->second;
    }
    return *it->second;
}

auto ThreadLocalArena::mergeAll() -> ArenaAllocator {
    std::lock_guard<std::mutex> lock(m_mutex);
    ArenaAllocator merged(m_blockSize);
    for (auto& [id, arena] : m_arenas) {
        merged.absorb(std::move(*arena));
    }
    m_arenas.clear();
    return merged;
}

} // namespace jsontitan::core
