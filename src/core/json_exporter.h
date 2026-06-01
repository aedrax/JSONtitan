#pragma once

#include <cstddef>
#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

enum class IndentMode {
    Compact,      // No whitespace outside strings
    PrettyPrint   // Newlines + configurable space indentation
};

struct JsonExportOptions {
    IndentMode mode = IndentMode::PrettyPrint;
    int indentWidth = 2;          // Spaces per level (1-8, clamped)
    bool trailingNewline = true;  // Append '\n' after final bracket
};

// Serialize a JsonNode tree to a UTF-8 JSON string.
// Pure function: no side effects, deterministic output.
// Conforms to RFC 8259.
auto exportJson(const JsonNode& node, JsonExportOptions options = {}) -> std::string;

} // namespace jsontitan::core
