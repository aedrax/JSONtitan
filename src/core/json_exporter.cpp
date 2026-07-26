#include "core/json_exporter.h"

#include "core/stream_export.h"

namespace jsontitan::core {

auto exportJson(const JsonNode& node, JsonExportOptions options) -> std::string {
    // Thin wrapper over the streaming exporter with a string-appending sink.
    std::string result;
    ByteSink sink = [&result](std::string_view chunk) {
        result += chunk;
        return true;
    };
    exportJsonStream(NodeView(node), sink, options);
    return result;
}

} // namespace jsontitan::core
