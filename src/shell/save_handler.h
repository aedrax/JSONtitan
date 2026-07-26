#pragma once

#include <QString>

#include "core/node_view.h"

class SaveHandler {
public:
    // Save the tree (either backing, via NodeView) to the given file path
    // using an atomic streaming write. Returns empty QString on success,
    // error description on failure.
    static auto saveToFile(jsontitan::core::NodeView tree,
                           const QString& filePath) -> QString;
};
