#pragma once

#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

auto exportXml(const JsonNode& node, const std::string& rootElementName = "root")
    -> std::string;

} // namespace jsontitan::core
