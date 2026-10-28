#include "core/parse_orchestrator.h"

#include "core/simdjson_adapter.h"

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
            nullptr,
            ParseError{0, "Empty input"},
            0
        };
    }

    SimdjsonParseOptions sjOpts = {.progressCallback = options.progressCallback,
                                   .cancelCallback = options.cancelCallback};
    JsonFormat format = options.format;
    bool hadComments = false;
    SimdjsonResult result;

    switch (options.format) {
        case JsonFormat::JsonLines:
            result = simdjsonParseLines(*source, *arena, sjOpts);
            break;

        case JsonFormat::JsonWithComments: {
            auto stripped = stripJsonc(source->mutableSpan());
            if (stripped.error) {
                return ArenaParseResult{std::move(arena), nullptr, stripped.error, 0};
            }
            hadComments = stripped.hadComments;
            result = simdjsonParse(*source, *arena, sjOpts);
            break;
        }

        case JsonFormat::Json:
            result = simdjsonParse(*source, *arena, sjOpts);
            break;
    }

    const bool cancelled = options.cancelCallback && options.cancelCallback();
    if (result.error && options.format == JsonFormat::Json &&
        options.lenientFallback && !cancelled) {
        // Each retry starts from a fresh arena so a failed attempt's nodes
        // are not carried into the final tree.
        auto stripped = stripJsonc(source->mutableSpan());
        if (!stripped.error && stripped.changed) {
            auto retryArena = std::make_unique<ArenaAllocator>();
            auto retry = simdjsonParse(*source, *retryArena, sjOpts);
            if (!retry.error) {
                arena = std::move(retryArena);
                result = retry;
                format = JsonFormat::JsonWithComments;
                hadComments = stripped.hadComments;
            }
        }

        // One record would have parsed strictly; requiring two keeps a
        // whitespace-only file an error instead of an empty array.
        const bool cancelledDuringRetry =
            options.cancelCallback && options.cancelCallback();
        if (result.error && !cancelledDuringRetry) {
            auto retryArena = std::make_unique<ArenaAllocator>();
            auto retry = simdjsonParseLines(*source, *retryArena, sjOpts);
            if (!retry.error && retry.root->childCount >= 2) {
                arena = std::move(retryArena);
                result = retry;
                format = JsonFormat::JsonLines;
                hadComments = stripped.hadComments;
            }
        }
    }

    // convertElement copied every string into the arena, so nothing
    // references the source bytes anymore — release them now instead of
    // carrying a (potentially multi-GB) buffer for the tree's lifetime.
    source.reset();

    return ArenaParseResult{std::move(arena), result.root, result.error,
                            result.nodeCount, format, hadComments};
}

} // namespace jsontitan::core
