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
            std::move(source),
            nullptr,
            ParseError{0, "Empty input"}
        };
    }

    // Parse via simdjson
    SimdjsonParseOptions sjOpts = {.progressCallback = options.progressCallback,
                                   .cancelCallback = options.cancelCallback};
    auto result = simdjsonParse(*source, *arena, sjOpts);
    return ArenaParseResult{std::move(arena), std::move(source),
                            result.root, result.error};
}

} // namespace jsontitan::core
