#include "shell/export_handler.h"

#include "core/arena_json_node.h"
#include "core/csv_exporter.h"
#include "core/xml_exporter.h"

#include <QSaveFile>

namespace {

// Writes content to filePath via QSaveFile: the target is only replaced after
// the full write succeeds, so a failure never destroys an existing file.
auto writeAtomically(const std::string& content, const QString& filePath) -> QString {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open file for writing: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    auto bytesWritten = file.write(content.data(),
                                   static_cast<qint64>(content.size()));
    if (bytesWritten != static_cast<qint64>(content.size())
        || file.error() != QFileDevice::NoError) {
        return QStringLiteral("Failed to write to file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    if (!file.commit()) {
        return QStringLiteral("Failed to save file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    return {};
}

} // namespace

auto ExportHandler::exportCsvToFile(const jsontitan::core::JsonNode& node,
                                    const QString& filePath) -> QString {
    // Call the pure core CSV exporter
    auto result = jsontitan::core::exportCsv(node);

    // Check if the core returned an error
    if (auto* error = std::get_if<jsontitan::core::CsvError>(&result)) {
        return QString::fromStdString(error->description);
    }

    // Write the CSV content to the file (atomic: commit() replaces the target
    // only after the full write succeeded).
    const auto& csvContent = std::get<std::string>(result);
    return writeAtomically(csvContent, filePath);
}

auto ExportHandler::exportXmlToFile(const jsontitan::core::JsonNode& node,
                                    const QString& filePath,
                                    const std::string& rootElementName) -> QString {
    // Call the pure core XML exporter
    std::string xmlContent = jsontitan::core::exportXml(node, rootElementName);

    return writeAtomically(xmlContent, filePath);
}

auto ExportHandler::exportCsvToFile(const jsontitan::core::ArenaJsonNode& node,
                                    const QString& filePath) -> QString {
    // Convert only the selected subtree on-demand (not the full tree).
    // This is acceptable because exports are user-initiated on small selections.
    auto jsonNode = node.toJsonNode();
    return exportCsvToFile(*jsonNode, filePath);
}

auto ExportHandler::exportXmlToFile(const jsontitan::core::ArenaJsonNode& node,
                                    const QString& filePath,
                                    const std::string& rootElementName) -> QString {
    // Convert only the selected subtree on-demand (not the full tree).
    // This is acceptable because exports are user-initiated on small selections.
    auto jsonNode = node.toJsonNode();
    return exportXmlToFile(*jsonNode, filePath, rootElementName);
}
