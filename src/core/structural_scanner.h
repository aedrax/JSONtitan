#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/simd_scanner.h"

namespace jsontitan::core {

/// A single structural character with its byte offset and nesting depth.
struct StructuralEntry {
    std::size_t offset;  // Byte offset in the source buffer
    char character;      // The structural character: { } [ ] , :
    int depth;           // Nesting depth at this position (0 = top level)
};

/// Ordered collection of structural entries produced by scanning the input.
struct StructuralIndex {
    std::vector<StructuralEntry> entries;

    /// Find all depth-0 boundaries suitable for partitioning.
    /// Returns offsets immediately AFTER a closing } or ] at depth 0.
    /// For example, if } at depth 0 is at offset 100, the partition point is 101.
    [[nodiscard]] auto findPartitionPoints() const
        -> std::vector<std::size_t>;
};

/// Build a structural index from SIMD scan results.
/// Filters out structural characters that are inside strings (using stringMask).
/// Tracks nesting depth by incrementing on { [ and decrementing on } ].
///
/// Opening brackets record depth BEFORE incrementing.
/// Closing brackets record depth AFTER decrementing.
/// Entries are ordered by offset (ascending).
auto buildStructuralIndex(const char* input, std::size_t length,
                          const std::vector<ScanBlock>& scanBlocks)
    -> StructuralIndex;

} // namespace jsontitan::core
