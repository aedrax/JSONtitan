#pragma once

#include <cstddef>
#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

struct PrettyPrintOptions {
    int indentWidth = 2;
    bool sortKeys = false;
    std::size_t maxOutputSize = 0;  // 0 = unlimited (backward compatible)
};

struct PrettyPrintResult {
    std::string output;
    bool truncated = false;
};

auto prettyPrint(const JsonNode& node, PrettyPrintOptions options = {}) -> std::string;

auto prettyPrintBounded(const JsonNode& node, PrettyPrintOptions options = {}) -> PrettyPrintResult;

} // namespace jsontitan::core
