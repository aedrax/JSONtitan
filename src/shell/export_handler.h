#pragma once

#include <QString>

#include <memory>
#include <string>

namespace jsontitan::core {
struct JsonNode;
}

class ExportHandler {
public:
    // Export the given JsonNode subtree to CSV and write to filePath.
    // Returns an empty QString on success, or a descriptive error message on failure.
    static auto exportCsvToFile(const jsontitan::core::JsonNode& node,
                                const QString& filePath) -> QString;

    // Export the given JsonNode subtree to XML and write to filePath.
    // Returns an empty QString on success, or a descriptive error message on failure.
    static auto exportXmlToFile(const jsontitan::core::JsonNode& node,
                                const QString& filePath,
                                const std::string& rootElementName = "root") -> QString;
};
