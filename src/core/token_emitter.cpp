#include "core/token_emitter.h"

#include "core/indent_util.h"

#include <algorithm>
#include <cstdio>

namespace jsontitan::core {

namespace {

// Escape a string value for JSON output (mirrors pretty_printer.cpp).
// Preserves multi-byte UTF-8 sequences as-is; escapes control characters below 0x20.
auto escapeJsonString(const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

// Emit a single token, tracking cumulative size.
// Returns false if truncation limit was already reached before this call.
auto emit(std::vector<Token>& tokens,
          std::size_t& currentSize,
          std::size_t maxSize,
          bool& truncated,
          TokenType type,
          std::string text,
          int depth) -> bool {
    if (truncated) return false;
    tokens.push_back(Token{type, std::move(text), depth});
    currentSize += tokens.back().text.size();
    return true;
}

void emitNode(const JsonNode& node,
              const PrettyPrintOptions& options,
              int depth,
              std::vector<Token>& tokens,
              std::size_t& currentSize,
              bool& truncated) {
    // Early exit if already truncated
    if (truncated) return;

    // Check size limit before doing any work (mirrors prettyPrint)
    if (options.maxOutputSize > 0 && currentSize >= options.maxOutputSize) {
        truncated = true;
        return;
    }

    const std::string indent(static_cast<std::size_t>(depth * options.indentWidth), ' ');
    const std::string childIndent(static_cast<std::size_t>((depth + 1) * options.indentWidth), ' ');

    switch (node.type) {
        case NodeType::Object: {
            if (node.children.empty()) {
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::BraceOpen, "{", depth);
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::BraceClose, "}", depth);
                return;
            }

            // Optionally sort children by key
            std::vector<std::shared_ptr<const JsonNode>> children = node.children;
            if (options.sortKeys) {
                std::sort(children.begin(), children.end(),
                    [](const auto& a, const auto& b) {
                        return a->key < b->key;
                    });
            }

            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::BraceOpen, "{", depth);
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Whitespace, "\n", 0);

            for (std::size_t i = 0; i < children.size(); ++i) {
                if (truncated) return;
                if (options.maxOutputSize > 0 && currentSize >= options.maxOutputSize) {
                    truncated = true;
                    return;
                }
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Whitespace, childIndent, 0);
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Key, escapeJsonString(children[i]->key), 0);
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Colon, ": ", 0);
                emitNode(*children[i], options, depth + 1, tokens, currentSize, truncated);
                if (truncated) return;
                if (i + 1 < children.size()) {
                    emit(tokens, currentSize, options.maxOutputSize, truncated,
                         TokenType::Comma, ",", 0);
                }
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Whitespace, "\n", 0);
            }
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Whitespace, indent, 0);
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::BraceClose, "}", depth);
            return;
        }

        case NodeType::Array: {
            if (node.children.empty()) {
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::BracketOpen, "[", depth);
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::BracketClose, "]", depth);
                return;
            }

            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::BracketOpen, "[", depth);
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Whitespace, "\n", 0);

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (truncated) return;
                if (options.maxOutputSize > 0 && currentSize >= options.maxOutputSize) {
                    truncated = true;
                    return;
                }
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Whitespace, childIndent, 0);
                emitNode(*node.children[i], options, depth + 1, tokens, currentSize, truncated);
                if (truncated) return;
                if (i + 1 < node.children.size()) {
                    emit(tokens, currentSize, options.maxOutputSize, truncated,
                         TokenType::Comma, ",", 0);
                }
                emit(tokens, currentSize, options.maxOutputSize, truncated,
                     TokenType::Whitespace, "\n", 0);
            }
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Whitespace, indent, 0);
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::BracketClose, "]", depth);
            return;
        }

        case NodeType::String:
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::StringValue, escapeJsonString(node.value), 0);
            return;

        case NodeType::Number:
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Number, node.value, 0);
            return;

        case NodeType::Boolean:
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Boolean, node.value, 0);
            return;

        case NodeType::Null:
            emit(tokens, currentSize, options.maxOutputSize, truncated,
                 TokenType::Null, "null", 0);
            return;
    }
}

} // anonymous namespace

auto emitTokens(const JsonNode& node, PrettyPrintOptions options) -> TokenEmitResult {
    options.indentWidth = clampIndentWidth(options.indentWidth);
    TokenEmitResult result;
    std::size_t currentSize = 0;
    bool truncated = false;
    emitNode(node, options, 0, result.tokens, currentSize, truncated);
    result.truncated = truncated;
    return result;
}

} // namespace jsontitan::core
