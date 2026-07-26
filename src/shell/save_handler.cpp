#include "shell/save_handler.h"

#include "core/json_exporter.h"

#include <QSaveFile>

auto SaveHandler::saveToFile(const jsontitan::core::JsonNode& tree,
                             const QString& filePath) -> QString {
    // Serialize the tree to JSON text (2-space indent, trailing newline)
    jsontitan::core::JsonExportOptions options;
    options.mode = jsontitan::core::IndentMode::PrettyPrint;
    options.indentWidth = 2;
    options.trailingNewline = true;

    std::string jsonText = jsontitan::core::exportJson(tree, options);

    // Atomic write: QSaveFile writes to a temp file and atomically replaces the
    // target on commit(), preserving the original file (and its permissions) if
    // anything fails before then.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open %1 for writing: %2")
            .arg(filePath, file.errorString());
    }

    auto bytesWritten = file.write(jsonText.data(),
                                   static_cast<qint64>(jsonText.size()));
    if (bytesWritten != static_cast<qint64>(jsonText.size())
        || file.error() != QFileDevice::NoError) {
        return QStringLiteral("Failed to write %1: %2")
            .arg(filePath, file.errorString());
    }

    if (!file.commit()) {
        return QStringLiteral("Failed to save %1: %2")
            .arg(filePath, file.errorString());
    }

    return {};
}
