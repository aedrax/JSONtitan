#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

#include "core/arena_allocator.h"
#include "core/arena_json_node.h"
#include "core/parser.h"
#include "core/simd_scanner.h"
#include "core/source_buffer.h"

namespace jsontitan::core {

// Result of parsing a complete buffer using the optimized pipeline.
// Owns all memory (arena + source buffer) for the lifetime of the tree.
struct ArenaParseResult {
    std::unique_ptr<ArenaAllocator> arena;   // Owns all node memory
    std::unique_ptr<SourceBuffer> source;    // Owns the input bytes
    ArenaJsonNode* root = nullptr;           // Root of the parsed tree
    std::optional<ParseError> error;

    // Convert to the public JsonNode type (copies strings out of arena).
    [[nodiscard]] auto toParseResult() const -> ParseResult;

    // Check if the parse succeeded.
    [[nodiscard]] auto ok() const noexcept -> bool {
        return root != nullptr && !error;
    }
};

// Selects which parsing backend to use.
enum class ParserBackend {
    Simdjson,  // Default: use simdjson DOM API
    Custom     // Existing parallel pipeline (simd_scanner → structural_scanner → chunk_parser)
};

// Options controlling the parse pipeline.
struct ParseBufferOptions {
    ParserBackend backend = ParserBackend::Simdjson;
    std::size_t parallelThreshold = 1024 * 1024;  // 1 MB (custom backend only)
    unsigned maxThreads = 0;  // 0 = use hardware_concurrency() (custom backend only)
    SimdLevel simdLevel = detectSimdLevel();       // custom backend only
    std::function<void(float)> progressCallback = nullptr;  // Optional progress reporting (0.0–1.0)
    // Optional cooperative cancellation: polled periodically during parsing;
    // returning true aborts the parse with a "Parse cancelled" error.
    std::function<bool()> cancelCallback = nullptr;
};

// Parse a complete buffer using the parallel pipeline.
// For inputs <= parallelThreshold, uses single-threaded parsing.
// For larger inputs, uses SIMD scan + parallel chunk parsing.
auto parseBuffer(std::string input,
                 ParseBufferOptions options = {}) -> ArenaParseResult;

// Overload accepting a pre-constructed SourceBuffer.
auto parseBuffer(std::unique_ptr<SourceBuffer> source,
                 ParseBufferOptions options = {}) -> ArenaParseResult;

} // namespace jsontitan::core
