#pragma once

#include <QString>

#include <string>

#include "core/node_view.h"

class ExportHandler {
public:
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
