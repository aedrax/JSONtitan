#pragma once

#include <string>
#include <vector>

#include "core/json_node.h"
#include "core/pretty_printer.h"

namespace jsontitan::core {

enum class TokenType {
    Key,
    StringValue,
    Number,
    Boolean,
    Null,
    BraceOpen,    // {
    BraceClose,   // }
    BracketOpen,  // [
    BracketClose, // ]
    Colon,
    Comma,
    Whitespace
};

struct Token {
    TokenType type;
    std::string text;
    int depth = 0;  // Meaningful only for Brace/Bracket tokens
};

struct TokenEmitResult {
    std::vector<Token> tokens;
    bool truncated = false;
};

auto emitTokens(const JsonNode& node, PrettyPrintOptions options = {}) -> TokenEmitResult;

} // namespace jsontitan::core
