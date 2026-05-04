#pragma once

#include <string>
#include <variant>

#include "core/json_node.h"

namespace jsontitan::core {

struct CsvError {
    std::string description;
};

using CsvResult = std::variant<std::string, CsvError>;

auto exportCsv(const JsonNode& node) -> CsvResult;

} // namespace jsontitan::core
