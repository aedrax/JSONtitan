#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/json_node.h"

namespace jsontitan::core {

struct ParseError {
    std::size_t byteOffset;
    std::string description;
};

struct ParserState;

struct ParseChunkResult {
    std::vector<std::shared_ptr<const JsonNode>> emittedNodes;
    std::unique_ptr<ParserState> nextState;
    std::optional<ParseError> error;
};

struct ParseResult {
    std::shared_ptr<const JsonNode> root;
    std::optional<ParseError> error;
};

auto makeParserState() -> std::unique_ptr<ParserState>;

auto parseChunk(const ParserState& state, std::span<const std::byte> chunk)
    -> ParseChunkResult;

auto finalizeParse(const ParserState& state) -> ParseResult;

} // namespace jsontitan::core
