#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace jsontitan::core {

// A single contiguous memory block managed by the arena.
struct ArenaBlock {
    std::unique_ptr<std::byte[]> data;
    std::size_t capacity;
    std::size_t used;
};

// A monotonic bump allocator that pre-allocates large contiguous blocks
// and sub-allocates objects from them. All memory is released at once
// via reset() or when the arena is destroyed.
//
// Pointer stability: previously returned pointers remain valid after
// new block allocation. Individual objects are never freed.
class ArenaAllocator {
public:
    // Default block size: 1 MB.
    static constexpr std::size_t kDefaultBlockSize = 1024 * 1024;

    explicit ArenaAllocator(std::size_t blockSize = kDefaultBlockSize);

    // Non-copyable.
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Movable.
    ArenaAllocator(ArenaAllocator&&) noexcept;
    ArenaAllocator& operator=(ArenaAllocator&&) noexcept;

    ~ArenaAllocator() = default;

    // Allocate raw bytes with the given alignment.
    // Returns nullptr only if system allocation fails.
    [[nodiscard]] auto allocate(std::size_t size,
                                std::size_t alignment = alignof(std::max_align_t))
        -> void*;

    // Typed allocation: construct T in-place within the arena.
    template <typename T, typename... Args>
    [[nodiscard]] auto construct(Args&&... args) -> T* {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            return nullptr;
        }
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    // Allocate a char array in the arena and copy data into it.
    // Returns a string_view over the arena-owned copy.
    [[nodiscard]] auto copyString(std::string_view src) -> std::string_view;

    // Release all blocks. All pointers previously returned become invalid.
    void reset() noexcept;

    // Absorb all blocks from another arena into this one.
    // The other arena is left empty after this operation.
    // Pointers previously returned by the other arena remain valid
    // (they are now owned by this arena).
    void absorb(ArenaAllocator&& other) noexcept;

    // Total bytes allocated across all blocks (capacity).
    [[nodiscard]] auto totalAllocated() const noexcept -> std::size_t;

    // Total bytes used across all blocks.
    [[nodiscard]] auto totalUsed() const noexcept -> std::size_t;

private:
    std::size_t m_blockSize;
    std::vector<ArenaBlock> m_blocks;

    void allocateNewBlock(std::size_t minSize);

    static auto alignUp(std::size_t value, std::size_t alignment) noexcept
        -> std::size_t;
};

// Per-thread arena wrapper for parallel chunk parsing.
// Each thread gets its own ArenaAllocator to avoid contention.
// Thread safety: all public methods are safe to call from any thread.
class ThreadLocalArena {
public:
    explicit ThreadLocalArena(
        std::size_t blockSize = ArenaAllocator::kDefaultBlockSize);

    // Non-copyable, non-movable (contains mutex).
    ThreadLocalArena(const ThreadLocalArena&) = delete;
    ThreadLocalArena& operator=(const ThreadLocalArena&) = delete;
    ThreadLocalArena(ThreadLocalArena&&) = delete;
    ThreadLocalArena& operator=(ThreadLocalArena&&) = delete;

    ~ThreadLocalArena() = default;

    // Get the arena for the calling thread.
    // Creates a new arena on first access from a given thread.
    [[nodiscard]] auto get() -> ArenaAllocator&;

    // Merge all thread-local arenas into a single arena (post-parse).
    // After this call, the ThreadLocalArena is empty and should not be reused.
    [[nodiscard]] auto mergeAll() -> ArenaAllocator;

private:
    std::size_t m_blockSize;
    std::mutex m_mutex;
    std::unordered_map<std::thread::id,
                       std::unique_ptr<ArenaAllocator>> m_arenas;
};

} // namespace jsontitan::core
