#include "shell/save_handler.h"

#include "core/json_exporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>

auto SaveHandler::saveToFile(const jsontitan::core::JsonNode& tree,
                             const QString& filePath) -> QString {
    // Serialize the tree to JSON text (2-space indent, trailing newline)
    jsontitan::core::JsonExportOptions options;
    options.mode = jsontitan::core::IndentMode::PrettyPrint;
    options.indentWidth = 2;
    options.trailingNewline = true;

    std::string jsonText = jsontitan::core::exportJson(tree, options);

    // Atomic write: write to a temporary file in the same directory, then rename.
    QFileInfo targetInfo(filePath);
    QString dir = targetInfo.absolutePath();

    // Create a temporary file in the same directory as the target
    QTemporaryFile tempFile(dir + QStringLiteral("/jsontitan_XXXXXX.tmp"));
    tempFile.setAutoRemove(false);

    if (!tempFile.open()) {
        return QStringLiteral("Failed to create temporary file in %1: %2")
            .arg(dir, tempFile.errorString());
    }

    // Write the JSON content to the temp file
    auto bytesWritten = tempFile.write(jsonText.data(),
                                       static_cast<qint64>(jsonText.size()));
    if (bytesWritten < 0 || tempFile.error() != QFileDevice::NoError) {
        QString error = QStringLiteral("Failed to write to temporary file: %1")
            .arg(tempFile.errorString());
        tempFile.close();
        tempFile.remove();
        return error;
    }

    // Flush to ensure all data is on disk before rename
    if (!tempFile.flush()) {
        QString error = QStringLiteral("Failed to flush temporary file: %1")
            .arg(tempFile.errorString());
        tempFile.close();
        tempFile.remove();
        return error;
    }

    QString tempPath = tempFile.fileName();
    tempFile.close();

    // Remove the target file if it exists (rename won't overwrite on all platforms)
    if (QFile::exists(filePath)) {
        if (!QFile::remove(filePath)) {
            QFile::remove(tempPath);
            return QStringLiteral("Failed to remove existing file: %1")
                .arg(filePath);
        }
    }

    // Rename the temp file to the target path (atomic on most filesystems)
    if (!QFile::rename(tempPath, filePath)) {
        return QStringLiteral("Failed to rename temporary file to target: %1")
            .arg(filePath);
    }

    return {};
}
