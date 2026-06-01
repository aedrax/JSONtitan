#include "core/json_exporter.h"

#include <algorithm>
#include <cstdio>

namespace jsontitan::core {

namespace {

// Escape a string value for JSON output per RFC 8259 Section 7.
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
                    // Control character — emit \uXXXX
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Normal ASCII or multi-byte UTF-8 byte — pass through
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

void serializeNode(const JsonNode& node,
                   const JsonExportOptions& options,
                   int depth,
                   std::string& out) {
    const int indentWidth = std::clamp(options.indentWidth, 1, 8);

    switch (node.type) {
        case NodeType::Object: {
            if (node.children.empty()) {
                out += "{}";
                return;
            }

            out += '{';
            if (options.mode == IndentMode::PrettyPrint) {
                out += '\n';
            }

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (options.mode == IndentMode::PrettyPrint) {
                    out.append(static_cast<std::size_t>((depth + 1) * indentWidth), ' ');
                }
                out += escapeJsonString(node.children[i]->key);
                if (options.mode == IndentMode::PrettyPrint) {
                    out += ": ";
                } else {
                    out += ':';
                }
                serializeNode(*node.children[i], options, depth + 1, out);
                if (i + 1 < node.children.size()) {
                    out += ',';
                }
                if (options.mode == IndentMode::PrettyPrint) {
                    out += '\n';
                }
            }

            if (options.mode == IndentMode::PrettyPrint) {
                out.append(static_cast<std::size_t>(depth * indentWidth), ' ');
            }
            out += '}';
            return;
        }

        case NodeType::Array: {
            if (node.children.empty()) {
                out += "[]";
                return;
            }

            out += '[';
            if (options.mode == IndentMode::PrettyPrint) {
                out += '\n';
            }

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (options.mode == IndentMode::PrettyPrint) {
                    out.append(static_cast<std::size_t>((depth + 1) * indentWidth), ' ');
                }
                serializeNode(*node.children[i], options, depth + 1, out);
                if (i + 1 < node.children.size()) {
                    out += ',';
                }
                if (options.mode == IndentMode::PrettyPrint) {
                    out += '\n';
                }
            }

            if (options.mode == IndentMode::PrettyPrint) {
                out.append(static_cast<std::size_t>(depth * indentWidth), ' ');
            }
            out += ']';
            return;
        }

        case NodeType::String:
            out += escapeJsonString(node.value);
            return;

        case NodeType::Number:
            out += node.value;
            return;

        case NodeType::Boolean:
            out += node.value;  // "true" or "false"
            return;

        case NodeType::Null:
            out += "null";
            return;
    }
}

} // anonymous namespace

auto exportJson(const JsonNode& node, JsonExportOptions options) -> std::string {
    std::string result;
    serializeNode(node, options, 0, result);
    if (options.trailingNewline) {
        result += '\n';
    }
    return result;
}

} // namespace jsontitan::core
