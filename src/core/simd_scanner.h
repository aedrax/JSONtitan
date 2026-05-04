#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jsontitan::core {

// Result of scanning a 64-byte block (or smaller tail).
// Each bit in the uint64_t fields corresponds to one byte in the block.
struct ScanBlock {
    uint64_t structuralBits = 0; // Bit set for each structural char: { } [ ] , :
    uint64_t whitespaceBits = 0; // Bit set for each whitespace char: space, tab, LF, CR
    uint64_t quoteBits = 0;      // Bit set for each unescaped " character
    uint64_t stringMask = 0;     // Bit set for bytes inside a JSON string literal
};

// Dispatch tag for SIMD implementation selection.
enum class SimdLevel { Scalar, SSE2, AVX2 };

// Detect the best available SIMD level at runtime.
auto detectSimdLevel() -> SimdLevel;

// Scalar reference implementation. Always available on all platforms.
// Processes the input and produces one ScanBlock per 64-byte chunk.
// Correctly handles backslash-escaped quotes via odd-backslash-run detection.
auto scalarScan(const char* input, std::size_t length)
    -> std::vector<ScanBlock>;

// SIMD-accelerated scan with runtime dispatch.
// SSE2 path processes 16 bytes at a time (4x per 64-byte block).
// Falls back to scalar for tail bytes and unsupported SIMD levels.
auto simdScan(const char* input, std::size_t length,
              SimdLevel level = SimdLevel::Scalar)
    -> std::vector<ScanBlock>;

} // namespace jsontitan::core
