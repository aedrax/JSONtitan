#pragma once

#include <QString>

#include <string>

#include <functional>

#include "core/node_view.h"
#include "core/stream_export.h"

class ExportHandler {
public:
    // Streams the given export function's output to filePath via QSaveFile:
    // the target is only replaced after the full write succeeds, so a
    // failure never destroys an existing file. A non-write failure (e.g. a
    // CSV validation error) is returned verbatim. Empty on success.
    static auto streamAtomically(
        const std::function<jsontitan::core::StreamExportResult(
            const jsontitan::core::ByteSink&)>& doExport,
        const QString& filePath) -> QString;

    // Export the given subtree (either backing, via NodeView) as
    // pretty-printed JSON (2-space indent, trailing newline) and stream it
    // to filePath through an atomic QSaveFile write.
    // Returns an empty QString on success, or a descriptive error message on failure.
    static auto exportJsonToFile(jsontitan::core::NodeView node,
                                 const QString& filePath) -> QString;

    // Export the given subtree (either backing, via NodeView) to CSV and
    // stream it to filePath through an atomic QSaveFile write.
    // Returns an empty QString on success, or a descriptive error message on failure.
    static auto exportCsvToFile(jsontitan::core::NodeView node,
                                const QString& filePath) -> QString;

    // Export the given subtree (either backing, via NodeView) to XML and
    // stream it to filePath through an atomic QSaveFile write.
    // Returns an empty QString on success, or a descriptive error message on failure.
    static auto exportXmlToFile(jsontitan::core::NodeView node,
                                const QString& filePath,
                                const std::string& rootElementName = "root") -> QString;
};
