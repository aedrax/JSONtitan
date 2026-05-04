#include "core/parser.h"

namespace jsontitan::core {

struct ParserState {
    std::string buffer;
    std::size_t bytesConsumed = 0;
    int currentDepth = 0;
};

auto makeParserState() -> std::unique_ptr<ParserState> {
    return std::make_unique<ParserState>();
}

auto parseChunk(const ParserState& /*state*/, std::span<const std::byte> /*chunk*/)
    -> ParseChunkResult {
    // Stub — will be implemented in Task 3
    return ParseChunkResult{
        .emittedNodes = {},
        .nextState = makeParserState(),
        .error = std::nullopt
    };
}

auto finalizeParse(const ParserState& /*state*/) -> ParseResult {
    // Stub — will be implemented in Task 3
    return ParseResult{
        .root = nullptr,
        .error = std::nullopt
    };
}

} // namespace jsontitan::core
