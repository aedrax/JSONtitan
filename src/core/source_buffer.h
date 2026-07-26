#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jsontitan::core {

// Number of zero-padded bytes appended after logical data for SIMD safety.
inline constexpr std::size_t kSimdjsonPadding = 64;

// A reference to a string within the SourceBuffer or arena.
// StringRef never owns its bytes: the referenced storage (source buffer or
// arena) must outlive every StringRef pointing into it.
struct StringRef {
    const char* data = nullptr;
    std::size_t length = 0;

    // Materialize as std::string (always copies).
    [[nodiscard]] auto toString() const -> std::string {
        if (!data || length == 0) {
            return std::string{};
        }
        return std::string(data, length);
    }

    // View without copying.
    [[nodiscard]] auto view() const -> std::string_view {
        if (!data) {
            return std::string_view{};
        }
        return std::string_view(data, length);
    }

    // Equality comparison (compares content, not pointer identity).
    [[nodiscard]] auto operator==(const StringRef& other) const -> bool {
        return view() == other.view();
    }
};

// An immutable contiguous buffer holding the raw JSON input bytes.
// String references point into this buffer for zero-copy access.
// The buffer is padded with kSimdjsonPadding zero bytes after the logical data
// so that simdjson can parse directly without copying.
class SourceBuffer {
public:
    // Take ownership of a string buffer, appending SIMDJSON_PADDING zero bytes.
    explicit SourceBuffer(std::string data)
        : m_logicalSize(data.size()) {
        // Append padding bytes (all zeros) after the logical data.
        data.resize(data.size() + kSimdjsonPadding, '\0');
        m_data = std::move(data);
    }

    // Take ownership of a byte vector (converted to string internally, with padding).
    explicit SourceBuffer(std::vector<std::byte> data)
        : m_logicalSize(data.size()),
          m_data(reinterpret_cast<const char*>(data.data()), data.size()) {
        m_data.resize(m_data.size() + kSimdjsonPadding, '\0');
    }

    // Non-copyable.
    SourceBuffer(const SourceBuffer&) = delete;
    SourceBuffer& operator=(const SourceBuffer&) = delete;

    // Movable.
    SourceBuffer(SourceBuffer&&) noexcept = default;
    SourceBuffer& operator=(SourceBuffer&&) noexcept = default;

    // Raw pointer to the buffer contents.
    [[nodiscard]] auto data() const noexcept -> const char* {
        return m_data.data();
    }

    // Logical size in bytes (excluding padding).
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return m_logicalSize;
    }

    // Total size including padding bytes, for callers that need padded capacity.
    [[nodiscard]] auto paddedSize() const noexcept -> std::size_t {
        return m_logicalSize + kSimdjsonPadding;
    }

    // Whether this buffer has SIMDJSON_PADDING bytes after the logical data.
    [[nodiscard]] static constexpr auto hasPadding() noexcept -> bool {
        return true;
    }

    // View as a span of bytes (logical size only, excluding padding).
    [[nodiscard]] auto span() const noexcept -> std::span<const std::byte> {
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m_data.data()), m_logicalSize);
    }

    // Create a StringRef pointing into this buffer.
    // Caller must ensure offset + length <= size().
    [[nodiscard]] auto ref(std::size_t offset, std::size_t length) const
        -> StringRef {
        return StringRef{m_data.data() + offset, length};
    }

private:
    std::size_t m_logicalSize = 0;
    std::string m_data;
};

} // namespace jsontitan::core
