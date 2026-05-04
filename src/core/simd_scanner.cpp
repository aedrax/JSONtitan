#include "core/simd_scanner.h"

#include <bit>
#include <cstring>

#ifdef __SSE2__
#include <immintrin.h>
#endif

namespace jsontitan::core {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Prefix XOR: for each bit position i, result bit i = XOR of bits 0..i.
// This toggles the in-string state at each unescaped quote.
inline auto prefixXor(uint64_t x) -> uint64_t {
    x ^= (x << 1);
    x ^= (x << 2);
    x ^= (x << 4);
    x ^= (x << 8);
    x ^= (x << 16);
    x ^= (x << 32);
    return x;
}

// Compute a bitmask of positions that are "escaped" -- i.e., immediately
// following an odd-length run of backslashes.
//
// Algorithm:
//   1. Find the start of each backslash run: a bit is a run-start if it is
//      a backslash and the bit before it is NOT a backslash.
//   2. Split starts by parity (even vs odd bit positions).
//   3. Add each group to the backslash bits separately. The carry from a run
//      of length N starting at position S lands at position S+N.
//   4. A run has odd length iff S and S+N have different parities.
//      - Even-position starts with odd length produce carries at odd positions.
//      - Odd-position starts with odd length produce carries at even positions.
//   5. Combine the two groups to get all odd-length-run followers.
inline auto oddBackslashFollowers(uint64_t bsBits) -> uint64_t {
    if (bsBits == 0) {
        return 0;
    }

    constexpr uint64_t evenMask = 0x5555555555555555ULL; // bits 0,2,4,...
    constexpr uint64_t oddMask  = 0xAAAAAAAAAAAAAAAAULL; // bits 1,3,5,...

    uint64_t starts = bsBits & ~(bsBits << 1);

    // Runs starting at even positions: odd-length runs end at odd positions
    uint64_t evenStarts = starts & evenMask;
    uint64_t evenCarries = (evenStarts + bsBits) & ~bsBits;
    uint64_t oddFromEven = evenCarries & oddMask;

    // Runs starting at odd positions: odd-length runs end at even positions
    uint64_t oddStarts = starts & oddMask;
    uint64_t oddCarries = (oddStarts + bsBits) & ~bsBits;
    uint64_t oddFromOdd = oddCarries & evenMask;

    return oddFromEven | oddFromOdd;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// detectSimdLevel
// ---------------------------------------------------------------------------

auto detectSimdLevel() -> SimdLevel {
#if defined(__SSE2__)
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("avx2")) {
        return SimdLevel::AVX2;
    }
    if (__builtin_cpu_supports("sse2")) {
        return SimdLevel::SSE2;
    }
#elif defined(_MSC_VER)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0);
    int maxFunc = cpuInfo[0];
    if (maxFunc >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        if (cpuInfo[1] & (1 << 5)) {
            return SimdLevel::AVX2;
        }
    }
    if (maxFunc >= 1) {
        __cpuid(cpuInfo, 1);
        if (cpuInfo[3] & (1 << 26)) {
            return SimdLevel::SSE2;
        }
    }
#endif
#endif
    return SimdLevel::Scalar;
}

// ---------------------------------------------------------------------------
// scalarScan -- reference implementation
// ---------------------------------------------------------------------------

auto scalarScan(const char* input, std::size_t length)
    -> std::vector<ScanBlock> {
    if (!input || length == 0) {
        return {};
    }

    const std::size_t blockCount = (length + 63) / 64;
    std::vector<ScanBlock> blocks(blockCount);

    uint64_t prevInString = 0; // carry: all-ones if previous block ended inside a string

    for (std::size_t blockIdx = 0; blockIdx < blockCount; ++blockIdx) {
        const std::size_t blockStart = blockIdx * 64;
        const std::size_t blockEnd = (blockStart + 64 <= length) ? blockStart + 64 : length;
        const std::size_t blockLen = blockEnd - blockStart;

        uint64_t structural = 0;
        uint64_t whitespace = 0;
        uint64_t rawQuotes = 0;
        uint64_t backslashes = 0;

        for (std::size_t i = 0; i < blockLen; ++i) {
            const auto ch = static_cast<unsigned char>(input[blockStart + i]);
            const uint64_t bit = uint64_t{1} << i;

            switch (ch) {
            case '{': case '}': case '[': case ']': case ',': case ':':
                structural |= bit;
                break;
            default:
                break;
            }

            switch (ch) {
            case ' ': case '\t': case '\n': case '\r':
                whitespace |= bit;
                break;
            default:
                break;
            }

            if (ch == '"') {
                rawQuotes |= bit;
            }
            if (ch == '\\') {
                backslashes |= bit;
            }
        }

        // Determine which quotes are escaped by odd-length backslash runs.
        uint64_t escaped = oddBackslashFollowers(backslashes);
        uint64_t unescapedQuotes = rawQuotes & ~escaped;

        // Prefix XOR to compute in-string mask, carrying state from previous block.
        uint64_t stringMask = prefixXor(unescapedQuotes) ^ prevInString;

        // Update carry for next block: if the highest used bit is inside a string,
        // the next block starts inside a string.
        if (blockLen == 64) {
            prevInString = static_cast<uint64_t>(
                -static_cast<int64_t>((stringMask >> 63) & 1));
        } else {
            prevInString = static_cast<uint64_t>(
                -static_cast<int64_t>((stringMask >> (blockLen - 1)) & 1));
        }

        blocks[blockIdx].structuralBits = structural;
        blocks[blockIdx].whitespaceBits = whitespace;
        blocks[blockIdx].quoteBits = unescapedQuotes;
        blocks[blockIdx].stringMask = stringMask;
    }

    return blocks;
}

// ---------------------------------------------------------------------------
// SSE2 scan -- processes 16 bytes at a time
// ---------------------------------------------------------------------------

#ifdef __SSE2__

namespace {

// Scan 16 bytes using SSE2 intrinsics.
// Returns raw bitmasks (structural, whitespace, quotes, backslashes) as 16-bit values.
struct Sse2ChunkResult {
    uint32_t structural;
    uint32_t whitespace;
    uint32_t quotes;
    uint32_t backslashes;
};

inline auto scanChunkSse2(const char* ptr) -> Sse2ChunkResult {
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));

    // Structural characters: { } [ ] , :
    __m128i openBrace  = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('{'));
    __m128i closeBrace = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('}'));
    __m128i openBrack  = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('['));
    __m128i closeBrack = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(']'));
    __m128i comma      = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(','));
    __m128i colon      = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(':'));

    __m128i s = _mm_or_si128(openBrace, closeBrace);
    s = _mm_or_si128(s, openBrack);
    s = _mm_or_si128(s, closeBrack);
    s = _mm_or_si128(s, comma);
    s = _mm_or_si128(s, colon);

    // Whitespace: space(0x20), tab(0x09), LF(0x0A), CR(0x0D)
    __m128i space = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(' '));
    __m128i tab   = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\t'));
    __m128i nl    = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\n'));
    __m128i cr    = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\r'));

    __m128i w = _mm_or_si128(space, tab);
    w = _mm_or_si128(w, nl);
    w = _mm_or_si128(w, cr);

    // Quotes and backslashes
    __m128i q  = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('"'));
    __m128i bs = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\\'));

    return {
        static_cast<uint32_t>(_mm_movemask_epi8(s)),
        static_cast<uint32_t>(_mm_movemask_epi8(w)),
        static_cast<uint32_t>(_mm_movemask_epi8(q)),
        static_cast<uint32_t>(_mm_movemask_epi8(bs))
    };
}

} // anonymous namespace

#endif // __SSE2__

// ---------------------------------------------------------------------------
// simdScan -- dispatches to SSE2 or scalar
// ---------------------------------------------------------------------------

auto simdScan(const char* input, std::size_t length, SimdLevel level)
    -> std::vector<ScanBlock> {

    // For scalar level or if SSE2 is not compiled in, delegate to scalarScan.
    if (level == SimdLevel::Scalar) {
        return scalarScan(input, length);
    }

#ifdef __SSE2__
    if (level == SimdLevel::SSE2 || level == SimdLevel::AVX2) {
        // AVX2 path not yet implemented; use SSE2 for both.
        if (!input || length == 0) {
            return {};
        }

        const std::size_t blockCount = (length + 63) / 64;
        std::vector<ScanBlock> blocks(blockCount);

        uint64_t prevInString = 0;

        for (std::size_t blockIdx = 0; blockIdx < blockCount; ++blockIdx) {
            const std::size_t blockStart = blockIdx * 64;
            const std::size_t remaining = length - blockStart;

            uint64_t structural64 = 0;
            uint64_t whitespace64 = 0;
            uint64_t rawQuotes64 = 0;
            uint64_t backslashes64 = 0;

            // Process as many full 16-byte chunks as possible via SSE2.
            const std::size_t fullChunks = (remaining >= 64) ? 4 : remaining / 16;

            for (std::size_t c = 0; c < fullChunks; ++c) {
                auto [s, w, q, bs] = scanChunkSse2(input + blockStart + c * 16);
                const std::size_t shift = c * 16;
                structural64 |= static_cast<uint64_t>(s & 0xFFFF) << shift;
                whitespace64 |= static_cast<uint64_t>(w & 0xFFFF) << shift;
                rawQuotes64  |= static_cast<uint64_t>(q & 0xFFFF) << shift;
                backslashes64|= static_cast<uint64_t>(bs & 0xFFFF) << shift;
            }

            // Scalar fallback for tail bytes (after the last full 16-byte chunk).
            const std::size_t simdBytes = fullChunks * 16;
            const std::size_t blockLen = (remaining >= 64) ? 64 : remaining;

            for (std::size_t i = simdBytes; i < blockLen; ++i) {
                const auto ch = static_cast<unsigned char>(input[blockStart + i]);
                const uint64_t bit = uint64_t{1} << i;

                switch (ch) {
                case '{': case '}': case '[': case ']': case ',': case ':':
                    structural64 |= bit;
                    break;
                default:
                    break;
                }

                switch (ch) {
                case ' ': case '\t': case '\n': case '\r':
                    whitespace64 |= bit;
                    break;
                default:
                    break;
                }

                if (ch == '"') {
                    rawQuotes64 |= bit;
                }
                if (ch == '\\') {
                    backslashes64 |= bit;
                }
            }

            // Odd-backslash-run detection and string mask computation
            // (identical to scalar path).
            uint64_t escaped = oddBackslashFollowers(backslashes64);
            uint64_t unescapedQuotes = rawQuotes64 & ~escaped;
            uint64_t stringMask = prefixXor(unescapedQuotes) ^ prevInString;

            if (blockLen == 64) {
                prevInString = static_cast<uint64_t>(
                    -static_cast<int64_t>((stringMask >> 63) & 1));
            } else {
                prevInString = static_cast<uint64_t>(
                    -static_cast<int64_t>((stringMask >> (blockLen - 1)) & 1));
            }

            blocks[blockIdx].structuralBits = structural64;
            blocks[blockIdx].whitespaceBits = whitespace64;
            blocks[blockIdx].quoteBits = unescapedQuotes;
            blocks[blockIdx].stringMask = stringMask;
        }

        return blocks;
    }
#endif // __SSE2__

    // Fallback: if we reach here, use scalar.
    return scalarScan(input, length);
}

} // namespace jsontitan::core
