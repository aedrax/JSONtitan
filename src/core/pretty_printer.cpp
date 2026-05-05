#include "core/pretty_printer.h"

#include <algorithm>
#include <cstdio>

namespace jsontitan::core {

namespace {

// Escape a string value for JSON output.
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

void printNode(const JsonNode& node,
               const PrettyPrintOptions& options,
               int depth,
               std::string& out,
               bool& truncated) {
    // Early exit if already truncated
    if (truncated) return;

    // Check size limit before doing any work
    if (options.maxOutputSize > 0 && out.size() >= options.maxOutputSize) {
        truncated = true;
        return;
    }

    const std::string indent(static_cast<std::size_t>(depth * options.indentWidth), ' ');
    const std::string childIndent(static_cast<std::size_t>((depth + 1) * options.indentWidth), ' ');

    switch (node.type) {
        case NodeType::Object: {
            if (node.children.empty()) {
                out += "{}";
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

            out += "{\n";
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (truncated) return;
                if (options.maxOutputSize > 0 && out.size() >= options.maxOutputSize) {
                    truncated = true;
                    return;
                }
                out += childIndent;
                out += escapeJsonString(children[i]->key);
                out += ": ";
                printNode(*children[i], options, depth + 1, out, truncated);
                if (truncated) return;
                if (i + 1 < children.size()) {
                    out += ',';
                }
                out += '\n';
            }
            out += indent;
            out += '}';
            return;
        }

        case NodeType::Array: {
            if (node.children.empty()) {
                out += "[]";
                return;
            }

            out += "[\n";
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (truncated) return;
                if (options.maxOutputSize > 0 && out.size() >= options.maxOutputSize) {
                    truncated = true;
                    return;
                }
                out += childIndent;
                printNode(*node.children[i], options, depth + 1, out, truncated);
                if (truncated) return;
                if (i + 1 < node.children.size()) {
                    out += ',';
                }
                out += '\n';
            }
            out += indent;
            out += ']';
            return;
        }

        case NodeType::String:
            out += escapeJsonString(node.value);
            return;

        case NodeType::Number:
            // Use the raw string representation to preserve precision
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

auto prettyPrint(const JsonNode& node, PrettyPrintOptions options) -> std::string {
    // Backward compatible: use maxOutputSize = 0 (unlimited)
    options.maxOutputSize = 0;
    std::string result;
    bool truncated = false;
    printNode(node, options, 0, result, truncated);
    return result;
}

auto prettyPrintBounded(const JsonNode& node, PrettyPrintOptions options) -> PrettyPrintResult {
    std::string output;
    bool truncated = false;
    printNode(node, options, 0, output, truncated);
    return PrettyPrintResult{.output = std::move(output), .truncated = truncated};
}

} // namespace jsontitan::core
