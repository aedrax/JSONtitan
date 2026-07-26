#include "core/token_emitter.h"

#include "core/indent_util.h"
#include "core/json_escape.h"

#include <algorithm>

namespace jsontitan::core {

auto truncateUtf8(std::string_view s, std::size_t maxBytes) -> std::string_view {
    if (s.size() <= maxBytes) {
        return s;
    }

    // Walk forward in indivisible units (a whole UTF-8 sequence or a whole
    // backslash escape) and stop before the unit that would cross maxBytes.
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t unitLen = 1;
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == '\\' && i + 1 < s.size()) {
            // Escape produced by escapeJsonString: "\uXXXX" is 6 bytes,
            // every other escape ("\n", "\\", "\"", ...) is 2 bytes.
            unitLen = (s[i + 1] == 'u') ? 6 : 2;
            unitLen = std::min(unitLen, s.size() - i);
        } else if ((c & 0x80U) != 0) {
            // UTF-8 lead byte determines the sequence length. A continuation
            // byte here means malformed input; treat it as a single byte.
            if ((c & 0xE0U) == 0xC0U) {
                unitLen = 2;
            } else if ((c & 0xF0U) == 0xE0U) {
                unitLen = 3;
            } else if ((c & 0xF8U) == 0xF0U) {
                unitLen = 4;
            }
            unitLen = std::min(unitLen, s.size() - i);
        }
        if (i + unitLen > maxBytes) {
            break;
        }
        i += unitLen;
    }
    return s.substr(0, i);
}

namespace {

// Emit a single token, enforcing the byte budget.
// When maxSize > 0 and the token would exceed the remaining budget, only a
// UTF-8-safe / escape-safe prefix is appended and `truncated` is set.
// Returns false once truncation has occurred (callers must stop emitting).
auto emit(std::vector<Token>& tokens,
          std::size_t& currentSize,
          std::size_t maxSize,
          bool& truncated,
          TokenType type,
          std::string text,
          int depth) -> bool {
    if (truncated) {
        return false;
    }
    if (maxSize > 0) {
        const std::size_t remaining =
            maxSize > currentSize ? maxSize - currentSize : 0;
        if (text.size() > remaining) {
            auto prefix = truncateUtf8(text, remaining);
            if (!prefix.empty()) {
                tokens.push_back(Token{type, std::string(prefix), depth});
                currentSize += prefix.size();
            }
            truncated = true;
            return false;
        }
    }
    tokens.push_back(Token{type, std::move(text), depth});
    currentSize += tokens.back().text.size();
    return true;
}

void emitNode(NodeView node,
              const PrettyPrintOptions& options,
              int depth,
              std::vector<Token>& tokens,
              std::size_t& currentSize,
              bool& truncated) {
    // Stop descending once the budget has been exhausted.
    if (truncated) {
        return;
    }

    const std::size_t max = options.maxOutputSize;
    const std::string indent(static_cast<std::size_t>(depth * options.indentWidth), ' ');
    const std::string childIndent(static_cast<std::size_t>((depth + 1) * options.indentWidth), ' ');

    switch (node.type()) {
        case NodeType::Object: {
            const std::size_t count = node.childCount();
            if (count == 0) {
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::BraceOpen, "{", depth)) return;
                emit(tokens, currentSize, max, truncated,
                     TokenType::BraceClose, "}", depth);
                return;
            }

            // Optionally sort children by key
            std::vector<NodeView> children;
            children.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                children.push_back(node.child(i));
            }
            if (options.sortKeys) {
                std::sort(children.begin(), children.end(),
                    [](const NodeView& a, const NodeView& b) {
                        return a.key() < b.key();
                    });
            }

            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::BraceOpen, "{", depth)) return;
            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::Whitespace, "\n", 0)) return;

            for (std::size_t i = 0; i < children.size(); ++i) {
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Whitespace, childIndent, 0)) return;
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Key, escapeJsonString(children[i].key()), 0)) return;
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Colon, ": ", 0)) return;
                emitNode(children[i], options, depth + 1, tokens, currentSize, truncated);
                if (truncated) return;
                if (i + 1 < children.size()) {
                    if (!emit(tokens, currentSize, max, truncated,
                              TokenType::Comma, ",", 0)) return;
                }
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Whitespace, "\n", 0)) return;
            }
            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::Whitespace, indent, 0)) return;
            emit(tokens, currentSize, max, truncated,
                 TokenType::BraceClose, "}", depth);
            return;
        }

        case NodeType::Array: {
            const std::size_t count = node.childCount();
            if (count == 0) {
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::BracketOpen, "[", depth)) return;
                emit(tokens, currentSize, max, truncated,
                     TokenType::BracketClose, "]", depth);
                return;
            }

            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::BracketOpen, "[", depth)) return;
            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::Whitespace, "\n", 0)) return;

            for (std::size_t i = 0; i < count; ++i) {
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Whitespace, childIndent, 0)) return;
                emitNode(node.child(i), options, depth + 1, tokens, currentSize, truncated);
                if (truncated) return;
                if (i + 1 < count) {
                    if (!emit(tokens, currentSize, max, truncated,
                              TokenType::Comma, ",", 0)) return;
                }
                if (!emit(tokens, currentSize, max, truncated,
                          TokenType::Whitespace, "\n", 0)) return;
            }
            if (!emit(tokens, currentSize, max, truncated,
                      TokenType::Whitespace, indent, 0)) return;
            emit(tokens, currentSize, max, truncated,
                 TokenType::BracketClose, "]", depth);
            return;
        }

        case NodeType::String:
            emit(tokens, currentSize, max, truncated,
                 TokenType::StringValue, escapeJsonString(node.value()), 0);
            return;

        case NodeType::Number:
            emit(tokens, currentSize, max, truncated,
                 TokenType::Number, std::string(node.value()), 0);
            return;

        case NodeType::Boolean:
            emit(tokens, currentSize, max, truncated,
                 TokenType::Boolean, std::string(node.value()), 0);
            return;

        case NodeType::Null:
            emit(tokens, currentSize, max, truncated,
                 TokenType::Null, "null", 0);
            return;
    }
}

} // anonymous namespace

auto emitTokens(NodeView node, PrettyPrintOptions options) -> TokenEmitResult {
    options.indentWidth = clampIndentWidth(options.indentWidth);
    TokenEmitResult result;
    std::size_t currentSize = 0;
    bool truncated = false;
    emitNode(node, options, 0, result.tokens, currentSize, truncated);
    result.truncated = truncated;
    return result;
}

auto emitTokens(const JsonNode& node, PrettyPrintOptions options) -> TokenEmitResult {
    return emitTokens(NodeView(node), options);
}

auto emitTokens(const ArenaJsonNode& node, PrettyPrintOptions options) -> TokenEmitResult {
    return emitTokens(NodeView(node), options);
}

} // namespace jsontitan::core
