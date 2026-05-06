#include "core/parse_orchestrator.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <system_error>
#include <thread>
#include <vector>

#include "core/chunk_parser.h"
#include "core/simdjson_adapter.h"
#include "core/structural_scanner.h"

namespace jsontitan::core {

// ---------------------------------------------------------------------------
// ArenaParseResult::toParseResult
// ---------------------------------------------------------------------------

auto ArenaParseResult::toParseResult() const -> ParseResult {
    if (error) {
        return ParseResult{nullptr, error};
    }
    if (!root) {
        return ParseResult{nullptr, ParseError{0, "No root node"}};
    }
    return ParseResult{root->toJsonNode(), std::nullopt};
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Determine the effective number of threads to use.
auto effectiveThreadCount(unsigned maxThreads) -> unsigned {
    if (maxThreads == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        return (hw == 0) ? 1 : hw;
    }
    return maxThreads;
}

// Distribute partition points into chunk ranges for parallel parsing.
// Each chunk is defined by [start, end) byte offsets.
struct ChunkRange {
    std::size_t start;
    std::size_t end;
};

auto distributeChunks(const std::vector<std::size_t>& partitionPoints,
                      std::size_t inputSize,
                      unsigned numThreads)
    -> std::vector<ChunkRange> {
    // Partition points mark the end of complete top-level values.
    // We create chunks: [0, pp[0]), [pp[0], pp[1]), ..., [pp[n-2], inputSize)
    // Then distribute them across numThreads workers.

    std::vector<ChunkRange> allChunks;
    std::size_t prevEnd = 0;

    for (auto pp : partitionPoints) {
        if (pp > prevEnd) {
            allChunks.push_back({prevEnd, pp});
        }
        prevEnd = pp;
    }

    // If there's remaining content after the last partition point
    if (prevEnd < inputSize) {
        allChunks.push_back({prevEnd, inputSize});
    }

    if (allChunks.empty()) {
        return {};
    }

    // If we have fewer chunks than threads, just return all chunks
    if (allChunks.size() <= numThreads) {
        return allChunks;
    }

    // Merge adjacent chunks to reduce to numThreads workers
    std::vector<ChunkRange> merged;
    std::size_t chunksPerThread = allChunks.size() / numThreads;
    std::size_t remainder = allChunks.size() % numThreads;
    std::size_t idx = 0;

    for (unsigned t = 0; t < numThreads && idx < allChunks.size(); ++t) {
        std::size_t count = chunksPerThread + (t < remainder ? 1 : 0);
        std::size_t mergedStart = allChunks[idx].start;
        std::size_t mergedEnd = allChunks[idx + count - 1].end;
        merged.push_back({mergedStart, mergedEnd});
        idx += count;
    }

    return merged;
}

// Single-threaded parse path.
auto parseSingleThreaded(std::unique_ptr<SourceBuffer> source,
                         std::unique_ptr<ArenaAllocator> arena)
    -> ArenaParseResult {
    auto result = parseChunkRange(*source, 0, source->size(), *arena);

    if (result.error) {
        return ArenaParseResult{
            std::move(arena),
            std::move(source),
            nullptr,
            result.error
        };
    }

    return ArenaParseResult{
        std::move(arena),
        std::move(source),
        result.root,
        std::nullopt
    };
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// parseBuffer (string overload)
// ---------------------------------------------------------------------------

auto parseBuffer(std::string input, ParseBufferOptions options)
    -> ArenaParseResult {
    auto source = std::make_unique<SourceBuffer>(std::move(input));
    return parseBuffer(std::move(source), options);
}

// ---------------------------------------------------------------------------
// parseBuffer (SourceBuffer overload)
// ---------------------------------------------------------------------------

auto parseBuffer(std::unique_ptr<SourceBuffer> source,
                 ParseBufferOptions options) -> ArenaParseResult {
    auto arena = std::make_unique<ArenaAllocator>();

    // Empty input
    if (source->size() == 0) {
        return ArenaParseResult{
            std::move(arena),
            std::move(source),
            nullptr,
            ParseError{0, "Empty input"}
        };
    }

    // Route to simdjson backend when selected
    if (options.backend == ParserBackend::Simdjson) {
        SimdjsonParseOptions sjOpts{.progressCallback = options.progressCallback};
        auto result = simdjsonParse(*source, *arena, sjOpts);
        return ArenaParseResult{std::move(arena), std::move(source),
                                result.root, result.error};
    }

    // Single-threaded fast path for small inputs
    if (source->size() <= options.parallelThreshold) {
        auto result = parseSingleThreaded(std::move(source), std::move(arena));
        if (result.ok() && options.progressCallback) {
            options.progressCallback(1.0f);
        }
        return result;
    }

    // Phase 1: SIMD structural scan
    auto scanBlocks = simdScan(source->data(), source->size(), options.simdLevel);

    // Phase 2: Build structural index
    auto structIndex = buildStructuralIndex(
        source->data(), source->size(), scanBlocks);

    // Phase 3: Partition at depth-0 boundaries
    auto partitionPoints = structIndex.findPartitionPoints();

    // If no partition points found (single top-level value), use single-threaded
    if (partitionPoints.empty()) {
        auto result = parseSingleThreaded(std::move(source), std::move(arena));
        if (result.ok() && options.progressCallback) {
            options.progressCallback(1.0f);
        }
        return result;
    }

    unsigned numThreads = effectiveThreadCount(options.maxThreads);
    numThreads = std::min(numThreads,
                          static_cast<unsigned>(partitionPoints.size()));

    auto chunks = distributeChunks(partitionPoints, source->size(), numThreads);

    if (chunks.empty()) {
        auto result = parseSingleThreaded(std::move(source), std::move(arena));
        if (result.ok() && options.progressCallback) {
            options.progressCallback(1.0f);
        }
        return result;
    }

    // If only one chunk after distribution, use single-threaded
    if (chunks.size() == 1) {
        auto result = parseSingleThreaded(std::move(source), std::move(arena));
        if (result.ok() && options.progressCallback) {
            options.progressCallback(1.0f);
        }
        return result;
    }

    // Phase 4: Parallel chunk parsing
    // Each thread gets its own arena to avoid contention.
    ThreadLocalArena threadArenas;

    // Atomic flag to signal cancellation on first error
    std::atomic<bool> errorOccurred{false};

    struct ChunkResult {
        ChunkParseResult parseResult;
        std::size_t chunkStart;  // For error offset translation
    };

    std::vector<std::future<ChunkResult>> futures;
    futures.reserve(chunks.size());

    // Try to launch async tasks; fall back to single-threaded on failure
    try {
        for (const auto& chunk : chunks) {
            futures.push_back(std::async(
                std::launch::async,
                [&source, &threadArenas, &errorOccurred, chunk]() -> ChunkResult {
                    // Early exit if another chunk already failed
                    if (errorOccurred.load(std::memory_order_relaxed)) {
                        return ChunkResult{
                            {nullptr, ParseError{0, "Cancelled"}},
                            chunk.start
                        };
                    }

                    auto& localArena = threadArenas.get();
                    auto result = parseChunkRange(
                        *source, chunk.start, chunk.end, localArena);

                    if (result.error) {
                        errorOccurred.store(true, std::memory_order_relaxed);
                    }

                    return ChunkResult{std::move(result), chunk.start};
                }
            ));
        }
    } catch (const std::system_error&) {
        // Thread creation failure: fall back to single-threaded
        // Wait for any already-launched futures to complete
        for (auto& f : futures) {
            if (f.valid()) {
                f.wait();
            }
        }
        auto result = parseSingleThreaded(std::move(source), std::move(arena));
        if (result.ok() && options.progressCallback) {
            options.progressCallback(1.0f);
        }
        return result;
    }

    // Collect results
    std::vector<ArenaJsonNode*> subtrees;
    subtrees.reserve(futures.size());

    const float totalChunks = static_cast<float>(futures.size());

    for (std::size_t i = 0; i < futures.size(); ++i) {
        auto chunkResult = futures[i].get();

        if (chunkResult.parseResult.error) {
            // Translate chunk-local offset to absolute offset
            auto& err = *chunkResult.parseResult.error;
            std::size_t absoluteOffset = err.byteOffset;
            // The error byteOffset from parseChunkRange is already absolute
            // (it operates on the source buffer directly), so no translation needed
            // unless the error description says "Cancelled"
            if (err.description == "Cancelled") {
                continue;  // Skip cancelled chunks
            }

            // Wait for remaining futures to avoid dangling references
            for (std::size_t j = i + 1; j < futures.size(); ++j) {
                if (futures[j].valid()) {
                    futures[j].wait();
                }
            }

            // Merge thread-local arenas into main arena to keep memory valid
            auto merged = threadArenas.mergeAll();
            arena->absorb(std::move(merged));

            return ArenaParseResult{
                std::move(arena),
                std::move(source),
                nullptr,
                ParseError{absoluteOffset, err.description}
            };
        }

        subtrees.push_back(chunkResult.parseResult.root);

        // Report progress after each chunk completes
        if (options.progressCallback) {
            float progress = static_cast<float>(i + 1) / totalChunks;
            options.progressCallback(progress);
        }
    }

    // Phase 5: Merge thread-local arenas into main arena
    auto merged = threadArenas.mergeAll();
    arena->absorb(std::move(merged));

    // Merge subtrees under a root array node
    // The parallel path only activates when there are multiple top-level values,
    // which means the input is effectively a sequence of values (treated as array).
    auto** children = static_cast<ArenaJsonNode**>(
        arena->allocate(subtrees.size() * sizeof(ArenaJsonNode*),
                        alignof(ArenaJsonNode*)));
    for (std::size_t i = 0; i < subtrees.size(); ++i) {
        children[i] = subtrees[i];
    }

    auto* root = arena->construct<ArenaJsonNode>();
    root->type = NodeType::Array;
    root->key = StringRef{};
    root->value = StringRef{};
    root->children = children;
    root->childCount = subtrees.size();

    return ArenaParseResult{
        std::move(arena),
        std::move(source),
        root,
        std::nullopt
    };
}

} // namespace jsontitan::core
