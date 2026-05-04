#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jsontitan::core {

// A reference to a string within the SourceBuffer or arena.
// For strings without escapes: points into the raw source buffer (ownsData == false).
// For strings with escapes: points to a resolved copy in the arena (ownsData == true).
struct StringRef {
    const char* data = nullptr;
    std::size_t length = 0;
    bool ownsData = false;

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
class SourceBuffer {
public:
    // Take ownership of a string buffer.
    explicit SourceBuffer(std::string data) : m_data(std::move(data)) {}

    // Take ownership of a byte vector (converted to string internally).
    explicit SourceBuffer(std::vector<std::byte> data)
        : m_data(reinterpret_cast<const char*>(data.data()), data.size()) {}

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

    // Size in bytes.
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return m_data.size();
    }

    // View as a span of bytes.
    [[nodiscard]] auto span() const noexcept -> std::span<const std::byte> {
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m_data.data()), m_data.size());
    }

    // Create a StringRef pointing into this buffer.
    // Caller must ensure offset + length <= size().
    [[nodiscard]] auto ref(std::size_t offset, std::size_t length) const
        -> StringRef {
        return StringRef{m_data.data() + offset, length, false};
    }

private:
    std::string m_data;
};

} // namespace jsontitan::core
