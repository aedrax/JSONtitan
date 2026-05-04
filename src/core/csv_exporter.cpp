#include "core/csv_exporter.h"

namespace jsontitan::core {

auto exportCsv(const JsonNode& /*node*/) -> CsvResult {
    // Stub — will be implemented in Task 8
    return CsvError{.description = "Not implemented"};
}

} // namespace jsontitan::core
