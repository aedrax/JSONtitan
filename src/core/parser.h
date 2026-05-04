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

// Forward-declare ParserState for the header; full definition in parser.cpp.
// To allow unique_ptr<ParserState> destruction in translation units that
// include this header, we provide an explicit custom deleter.

struct ParserStateDeleter {
    void operator()(ParserState* p) const noexcept;
};

struct ParseChunkResult {
    std::vector<std::shared_ptr<const JsonNode>> emittedNodes;
    std::unique_ptr<ParserState, ParserStateDeleter> nextState;
    std::optional<ParseError> error;
};

struct ParseResult {
    std::shared_ptr<const JsonNode> root;
    std::optional<ParseError> error;
};

auto makeParserState() -> std::unique_ptr<ParserState, ParserStateDeleter>;

auto parseChunk(const ParserState& state, std::span<const std::byte> chunk)
    -> ParseChunkResult;

auto finalizeParse(const ParserState& state) -> ParseResult;

} // namespace jsontitan::core
