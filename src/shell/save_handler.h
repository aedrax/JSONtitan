#pragma once

#include <QString>

#include "core/json_format.h"
#include "core/node_view.h"

class SaveHandler {
public:
    // Output style for JSON (not JSON Lines) saves.
    enum class Style { Indent2, Indent4, Compact };

    // The style chosen under File > Save Format (QSettings "save/style"),
    // Indent2 by default.
    static auto savedStyle() -> Style;
    static void setSavedStyle(Style style);

    // Save the tree (either backing, via NodeView) to the given file path
    // using an atomic streaming write. Returns empty QString on success,
    // error description on failure. JsonLines writes each element of the
    // root array as one compact line (and fails if the root is not an
    // array), whatever the style; every other format writes JSON in
    // `style`.
    static auto saveToFile(jsontitan::core::NodeView tree,
                           const QString& filePath,
                           jsontitan::core::JsonFormat format =
                               jsontitan::core::JsonFormat::Json,
                           Style style = Style::Indent2) -> QString;
};
