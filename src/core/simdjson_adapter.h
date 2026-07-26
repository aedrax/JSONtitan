#pragma once

#include <functional>
#include <optional>

#include "core/arena_allocator.h"
#include "core/arena_json_node.h"
#include "core/parser.h"
#include "core/source_buffer.h"

namespace jsontitan::core {

/// Options for the simdjson parsing adapter.
struct SimdjsonParseOptions {
    /// Optional progress callback invoked with values in [0.0, 1.0].
    /// If nullptr, progress reporting is skipped.
    std::function<void(float)> progressCallback = nullptr;
    /// Optional cooperative cancellation: polled every few thousand nodes
    /// during tree construction; returning true aborts with "Parse cancelled".
    std::function<bool()> cancelCallback = nullptr;
};

/// Result of parsing via the simdjson adapter.
struct SimdjsonResult {
    ArenaJsonNode* root = nullptr;
    std::optional<ParseError> error = std::nullopt;
};

/// Parse a SourceBuffer using simdjson's DOM API.
/// All ArenaJsonNode instances and string data are allocated in the provided arena.
/// simdjson's internal parser memory is released before this function returns.
auto simdjsonParse(const SourceBuffer& source,
                   ArenaAllocator& arena,
                   SimdjsonParseOptions options = {}) -> SimdjsonResult;

} // namespace jsontitan::core
