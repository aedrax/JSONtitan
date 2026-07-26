#include "shell/export_handler.h"

#include "core/stream_export.h"

#include <QSaveFile>

#include <functional>

namespace {

// Streams the given export function's output to filePath via QSaveFile: the
// target is only replaced after the full write succeeds, so a failure never
// destroys an existing file. A non-write failure (e.g. CSV validation error)
// is returned verbatim.
auto streamAtomically(
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
        return QString::fromStdString(result.error);
    }

    if (!file.commit()) {
        return QStringLiteral("Failed to save file: %1 (%2)")
            .arg(filePath, file.errorString());
    }

    return {};
}

} // namespace

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
