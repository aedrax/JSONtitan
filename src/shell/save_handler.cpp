#include "shell/save_handler.h"

#include "core/stream_export.h"

#include <QSaveFile>
#include <QSettings>

namespace {

const auto kStyleKey = QStringLiteral("save/style");

}  // namespace

auto SaveHandler::savedStyle() -> Style {
    const QString value = QSettings().value(kStyleKey).toString();
    if (value == QStringLiteral("indent4")) {
        return Style::Indent4;
    }
    if (value == QStringLiteral("compact")) {
        return Style::Compact;
    }
    return Style::Indent2;
}

void SaveHandler::setSavedStyle(Style style) {
    QString value = QStringLiteral("indent2");
    if (style == Style::Indent4) {
        value = QStringLiteral("indent4");
    } else if (style == Style::Compact) {
        value = QStringLiteral("compact");
    }
    QSettings().setValue(kStyleKey, value);
}

auto SaveHandler::saveToFile(jsontitan::core::NodeView tree,
                             const QString& filePath,
                             jsontitan::core::JsonFormat format,
                             Style style) -> QString {
    const bool jsonLines = format == jsontitan::core::JsonFormat::JsonLines;
    // Checked before opening so a refused save leaves no temp file behind.
    if (jsonLines && tree.type() != jsontitan::core::NodeType::Array) {
        return QStringLiteral("%1 is a JSON Lines file, which needs a "
                              "top-level array. Save under a .json name "
                              "instead.")
            .arg(filePath);
    }

    // Serialize as JSON text in the requested style (with a trailing
    // newline), streamed straight into the file instead of materializing
    // the whole document. JSON Lines records are compact, one per line.
    jsontitan::core::JsonExportOptions options;
    options.mode = jsonLines || style == Style::Compact
                       ? jsontitan::core::IndentMode::Compact
                       : jsontitan::core::IndentMode::PrettyPrint;
    options.indentWidth = style == Style::Indent4 ? 4 : 2;
    options.trailingNewline = !jsonLines;

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

    jsontitan::core::StreamExportResult result;
    if (jsonLines) {
        for (std::size_t i = 0; i < tree.childCount() && result.ok; ++i) {
            result = jsontitan::core::exportJsonStream(tree.child(i), sink, options);
            if (result.ok && !sink("\n")) {
                result.ok = false;
            }
        }
    } else {
        result = jsontitan::core::exportJsonStream(tree, sink, options);
    }
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
