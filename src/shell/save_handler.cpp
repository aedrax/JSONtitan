#include "shell/save_handler.h"

#include "core/stream_export.h"
#include "shell/export_handler.h"

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

    return ExportHandler::streamAtomically(
        [&](const jsontitan::core::ByteSink& sink) {
            if (!jsonLines) {
                return jsontitan::core::exportJsonStream(tree, sink, options);
            }
            jsontitan::core::StreamExportResult result;
            for (std::size_t i = 0; i < tree.childCount() && result.ok; ++i) {
                result = jsontitan::core::exportJsonStream(tree.child(i), sink, options);
                if (result.ok && !sink("\n")) {
                    result.ok = false;
                }
            }
            return result;
        },
        filePath);
}
