#include "core/csv_exporter.h"

#include "core/stream_export.h"

namespace jsontitan::core {

auto exportCsv(const JsonNode& node) -> CsvResult {
    // Thin wrapper over the streaming exporter with a string-appending sink.
    // Validation semantics (array-of-objects requirement, error messages) and
    // formula-injection neutralization live in exportCsvStream.
    std::string result;
    ByteSink sink = [&result](std::string_view chunk) {
        result += chunk;
        return true;
    };
    auto stream = exportCsvStream(NodeView(node), sink);
    if (!stream.ok) {
        // The string sink never aborts, so a failure is always a validation
        // error carrying the CsvError description.
        return CsvError{.description = stream.error};
    }
    return result;
}

} // namespace jsontitan::core
