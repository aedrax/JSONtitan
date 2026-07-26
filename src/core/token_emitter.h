#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/json_node.h"
#include "core/node_view.h"
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

// Return the longest prefix of `s` that is at most maxBytes long and does not
// end inside a multi-byte UTF-8 sequence or inside a backslash escape as
// produced by escapeJsonString ("\n", "\\", "\uXXXX", ...).
auto truncateUtf8(std::string_view s, std::size_t maxBytes) -> std::string_view;

// Emit highlight tokens for a node over either tree backing.
// options.maxOutputSize (when > 0) is a hard byte budget over the
// concatenated token texts: the final token is cut at a UTF-8-safe,
// escape-safe boundary and `truncated` is set.
auto emitTokens(NodeView node, PrettyPrintOptions options = {}) -> TokenEmitResult;

// Thin forwarders so existing call sites keep working with concrete types.
auto emitTokens(const JsonNode& node, PrettyPrintOptions options = {}) -> TokenEmitResult;
auto emitTokens(const ArenaJsonNode& node, PrettyPrintOptions options = {}) -> TokenEmitResult;

} // namespace jsontitan::core
