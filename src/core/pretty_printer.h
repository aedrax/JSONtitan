#pragma once

#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

struct PrettyPrintOptions {
    int indentWidth = 2;
    bool sortKeys = false;
};

auto prettyPrint(const JsonNode& node, PrettyPrintOptions options = {}) -> std::string;

} // namespace jsontitan::core
