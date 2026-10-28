#pragma once

#include <QString>

#include "core/json_format.h"
#include "core/node_view.h"

class SaveHandler {
public:
    // Save the tree (either backing, via NodeView) to the given file path
    // using an atomic streaming write. Returns empty QString on success,
    // error description on failure. JsonLines writes each element of the
    // root array as one compact line (and fails if the root is not an
    // array); every other format writes pretty-printed JSON.
    static auto saveToFile(jsontitan::core::NodeView tree,
                           const QString& filePath,
                           jsontitan::core::JsonFormat format =
                               jsontitan::core::JsonFormat::Json) -> QString;
};
