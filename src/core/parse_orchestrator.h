#pragma once

#include <cstddef>
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

// Options controlling the parallel parse pipeline.
struct ParseBufferOptions {
    std::size_t parallelThreshold = 1024 * 1024;  // 1 MB
    unsigned maxThreads = 0;  // 0 = use hardware_concurrency()
    SimdLevel simdLevel = detectSimdLevel();
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
