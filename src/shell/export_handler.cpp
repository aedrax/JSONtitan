#include "shell/export_handler.h"

#include "core/arena_json_node.h"
#include "core/csv_exporter.h"
#include "core/xml_exporter.h"

#include <QFile>
#include <QTextStream>

auto ExportHandler::exportCsvToFile(const jsontitan::core::JsonNode& node,
                                    const QString& filePath) -> QString {
    // Call the pure core CSV exporter
    auto result = jsontitan::core::exportCsv(node);

    // Check if the core returned an error
    if (auto* error = std::get_if<jsontitan::core::CsvError>(&result)) {
        return QString::fromStdString(error->description);
    }

    // Write the CSV content to the file
    const auto& csvContent = std::get<std::string>(result);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open file for writing: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    auto bytesWritten = file.write(csvContent.data(),
                                   static_cast<qint64>(csvContent.size()));
    if (bytesWritten < 0 || file.error() != QFileDevice::NoError) {
        return QStringLiteral("Failed to write to file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    file.close();
    return {};
}

auto ExportHandler::exportXmlToFile(const jsontitan::core::JsonNode& node,
                                    const QString& filePath,
                                    const std::string& rootElementName) -> QString {
    // Call the pure core XML exporter
    std::string xmlContent = jsontitan::core::exportXml(node, rootElementName);

    // Write the XML content to the file
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open file for writing: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    auto bytesWritten = file.write(xmlContent.data(),
                                   static_cast<qint64>(xmlContent.size()));
    if (bytesWritten < 0 || file.error() != QFileDevice::NoError) {
        return QStringLiteral("Failed to write to file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    file.close();
    return {};
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
