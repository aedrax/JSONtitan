#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

#include "core/arena_allocator.h"
#include "core/arena_json_node.h"
#include "core/parser.h"
#include "core/source_buffer.h"

namespace jsontitan::core {

// Result of parsing a complete buffer using the optimized pipeline.
// Owns all memory (arena + source buffer) for the lifetime of the tree.
struct ArenaParseResult {
    std::unique_ptr<ArenaAllocator> arena;   // Owns all node memory
    std::unique_ptr<SourceBuffer> source;    // Owns the input bytes
    ArenaJsonNode* root = nullptr;           // Root of the parsed tree
    std::optional<ParseError> error = std::nullopt;

    // Convert to the public JsonNode type (copies strings out of arena).
    [[nodiscard]] auto toParseResult() const -> ParseResult;

    // Check if the parse succeeded.
    [[nodiscard]] auto ok() const noexcept -> bool {
        return root != nullptr && !error;
    }
};

// Options controlling the parse pipeline.
struct ParseBufferOptions {
    std::function<void(float)> progressCallback = nullptr;  // Optional progress reporting (0.0–1.0)
    // Optional cooperative cancellation: polled periodically during parsing;
    // returning true aborts the parse with a "Parse cancelled" error.
    std::function<bool()> cancelCallback = nullptr;
};

// Parse a complete buffer via simdjson.
auto parseBuffer(std::string input,
                 ParseBufferOptions options = {}) -> ArenaParseResult;

// Overload accepting a pre-constructed SourceBuffer. Parses via simdjson.
auto parseBuffer(std::unique_ptr<SourceBuffer> source,
                 ParseBufferOptions options = {}) -> ArenaParseResult;

} // namespace jsontitan::core
