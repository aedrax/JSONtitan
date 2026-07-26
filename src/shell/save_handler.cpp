#include "shell/save_handler.h"

#include "core/stream_export.h"

#include <QSaveFile>

auto SaveHandler::saveToFile(jsontitan::core::NodeView tree,
                             const QString& filePath) -> QString {
    // Serialize as JSON text (2-space indent, trailing newline), streamed
    // straight into the file instead of materializing the whole document.
    jsontitan::core::JsonExportOptions options;
    options.mode = jsontitan::core::IndentMode::PrettyPrint;
    options.indentWidth = 2;
    options.trailingNewline = true;

    // Atomic write: QSaveFile writes to a temp file and atomically replaces the
    // target on commit(), preserving the original file (and its permissions) if
    // anything fails before then.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Failed to open %1 for writing: %2")
            .arg(filePath, file.errorString());
    }

    jsontitan::core::ByteSink sink = [&file](std::string_view chunk) {
        auto written = file.write(chunk.data(), static_cast<qint64>(chunk.size()));
        return written == static_cast<qint64>(chunk.size())
            && file.error() == QFileDevice::NoError;
    };

    auto result = jsontitan::core::exportJsonStream(tree, sink, options);
    if (!result.ok || file.error() != QFileDevice::NoError) {
        return QStringLiteral("Failed to write %1: %2")
            .arg(filePath, file.errorString());
    }

    if (!file.commit()) {
        return QStringLiteral("Failed to save %1: %2")
            .arg(filePath, file.errorString());
    }

    return {};
}
