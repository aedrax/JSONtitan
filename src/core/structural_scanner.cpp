#include "core/structural_scanner.h"

#include <bit>

namespace jsontitan::core {

// ---------------------------------------------------------------------------
// StructuralIndex::findPartitionPoints
// ---------------------------------------------------------------------------

auto StructuralIndex::findPartitionPoints() const
    -> std::vector<std::size_t> {
    std::vector<std::size_t> points;

    for (const auto& entry : entries) {
        // A closing bracket at depth 0 marks the end of a complete top-level value.
        // The partition point is the byte immediately after the closing bracket.
        if ((entry.character == '}' || entry.character == ']') && entry.depth == 0) {
            points.push_back(entry.offset + 1);
        }
    }

    return points;
}

// ---------------------------------------------------------------------------
// buildStructuralIndex
// ---------------------------------------------------------------------------

auto buildStructuralIndex(const char* input, std::size_t length,
                          const std::vector<ScanBlock>& scanBlocks)
    -> StructuralIndex {
    StructuralIndex index;
    int depth = 0;
    std::size_t blockOffset = 0;

    for (const auto& block : scanBlocks) {
        // Only consider structural bits NOT inside strings
        uint64_t effectiveBits = block.structuralBits & ~block.stringMask;

        while (effectiveBits != 0) {
            // Find position of lowest set bit
            int bitPos = std::countr_zero(effectiveBits);
            std::size_t offset = blockOffset + static_cast<std::size_t>(bitPos);

            // Guard against out-of-bounds access (tail blocks may have
            // fewer than 64 valid bytes)
            if (offset >= length) {
                break;
            }

            char ch = input[offset];

            if (ch == '{' || ch == '[') {
                // Opening brackets record depth BEFORE incrementing
                index.entries.push_back({offset, ch, depth});
                ++depth;
            } else if (ch == '}' || ch == ']') {
                // Closing brackets record depth AFTER decrementing
                --depth;
                index.entries.push_back({offset, ch, depth});
            } else {
                // Comma or colon — record at current depth
                index.entries.push_back({offset, ch, depth});
            }

            // Clear the lowest set bit
            effectiveBits &= (effectiveBits - 1);
        }

        blockOffset += 64;
    }

    return index;
}

} // namespace jsontitan::core
