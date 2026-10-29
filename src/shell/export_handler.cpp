#include "shell/export_handler.h"

#include "core/stream_export.h"

#include <QSaveFile>

#include <functional>

auto ExportHandler::streamAtomically(
    const std::function<jsontitan::core::StreamExportResult(
        const jsontitan::core::ByteSink&)>& doExport,
    const QString& filePath) -> QString {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open file for writing: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    jsontitan::core::ByteSink sink = [&file](std::string_view chunk) {
        auto written = file.write(chunk.data(), static_cast<qint64>(chunk.size()));
        return written == static_cast<qint64>(chunk.size())
            && file.error() == QFileDevice::NoError;
    };

    auto result = doExport(sink);
    if (!result.ok) {
        if (file.error() != QFileDevice::NoError) {
            return QStringLiteral("Failed to write to file: %1 (%2)")
                .arg(filePath, file.errorString());
        }
        // Validation failure (e.g. CSV shape error): report the core message.
        // Never an empty string, which callers read as success.
        return result.error.empty()
                   ? QStringLiteral("Failed to write to file: %1").arg(filePath)
                   : QString::fromStdString(result.error);
    }

    if (!file.commit()) {
        return QStringLiteral("Failed to save file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    return {};
}

auto ExportHandler::exportJsonToFile(jsontitan::core::NodeView node,
                                     const QString& filePath) -> QString {
    return streamAtomically(
        [node](const jsontitan::core::ByteSink& sink) {
            // Default options: pretty print, 2-space indent, trailing newline.
            return jsontitan::core::exportJsonStream(node, sink);
        },
        filePath);
}

auto ExportHandler::exportCsvToFile(jsontitan::core::NodeView node,
                                    const QString& filePath) -> QString {
    return streamAtomically(
        [node](const jsontitan::core::ByteSink& sink) {
            return jsontitan::core::exportCsvStream(node, sink);
        },
        filePath);
}

auto ExportHandler::exportXmlToFile(jsontitan::core::NodeView node,
                                    const QString& filePath,
                                    const std::string& rootElementName) -> QString {
    return streamAtomically(
        [node, &rootElementName](const jsontitan::core::ByteSink& sink) {
            return jsontitan::core::exportXmlStream(node, sink, rootElementName);
        },
        filePath);
}
