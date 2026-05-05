#pragma once

#include <cstddef>
#include <optional>

#include "core/arena_allocator.h"
#include "core/arena_json_node.h"
#include "core/parser.h"
#include "core/source_buffer.h"

namespace jsontitan::core {

// Result of parsing a byte range from the source buffer.
struct ChunkParseResult {
    ArenaJsonNode* root = nullptr;
    std::optional<ParseError> error;
};

// Parse a byte range [startOffset, endOffset) from the source buffer into
// an arena-allocated subtree. The range must contain complete JSON values.
//
// Parameters:
//   source      - The source buffer containing the raw JSON bytes.
//   startOffset - Byte offset where parsing begins (inclusive).
//   endOffset   - Byte offset where parsing ends (exclusive).
//   arena       - Arena allocator for all node and string allocations.
//   key         - Key to assign to the root node (default: empty).
//
// Returns a ChunkParseResult with either a valid root node or an error.
auto parseChunkRange(const SourceBuffer& source,
                     std::size_t startOffset,
                     std::size_t endOffset,
                     ArenaAllocator& arena,
                     StringRef key = {}) -> ChunkParseResult;

} // namespace jsontitan::core
