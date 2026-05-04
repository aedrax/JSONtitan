#pragma once

#include <QString>

#include <memory>
#include <string>

namespace jsontitan::core {
struct JsonNode;
}

class ExportHandler {
public:
    static auto exportCsvToFile(const jsontitan::core::JsonNode& node,
                                const QString& filePath) -> QString;

    static auto exportXmlToFile(const jsontitan::core::JsonNode& node,
                                const QString& filePath,
                                const std::string& rootElementName = "root") -> QString;
};
