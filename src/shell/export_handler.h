#pragma once

#include <QString>

#include <memory>
#include <string>

namespace jsontitan::core {
struct JsonNode;
struct ArenaJsonNode;
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

    // Arena-aware overloads: convert only the selected subtree on-demand,
    // then delegate to the existing JsonNode-based exporters.
    // This is acceptable because exports are user-initiated on small selections.
    static auto exportCsvToFile(const jsontitan::core::ArenaJsonNode& node,
                                const QString& filePath) -> QString;

    static auto exportXmlToFile(const jsontitan::core::ArenaJsonNode& node,
                                const QString& filePath,
                                const std::string& rootElementName = "root") -> QString;
};
