#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "core/json_exporter.h"  // JsonExportOptions
#include "core/node_view.h"

namespace jsontitan::core {

// Incremental byte consumer for streaming exports. Returning false aborts
// the export; the sink is never called again afterwards.
using ByteSink = std::function<bool(std::string_view chunk)>;

struct StreamExportResult {
    bool ok = true;
    std::string error;
};

// Streaming counterparts of exportJson/exportCsv/exportXml, working over
// either tree backing via NodeView and delivering output in ~64 KB chunks
// instead of materializing the whole document in memory. The byte output is
// identical to the string-returning APIs (which are thin wrappers over
// these).
auto exportJsonStream(NodeView root, const ByteSink& sink,
                      JsonExportOptions opts = {}) -> StreamExportResult;
auto exportCsvStream(NodeView root, const ByteSink& sink) -> StreamExportResult;
auto exportXmlStream(NodeView root, const ByteSink& sink,
                     const std::string& rootElementName = "root") -> StreamExportResult;

} // namespace jsontitan::core
