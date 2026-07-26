#pragma once

#include <QString>

#include <optional>

#include "core/deletion_engine.h"  // NodePath
#include "core/node_view.h"

namespace jsontitan::shell {

// JSONPath-style text for a NodePath: "$" for the root, then ".key" for keys
// that are bare-safe ([A-Za-z_][A-Za-z0-9_]*), "['escaped']" (backslash and
// single quote escaped) for all other keys, and "[n]" for array indices.
auto jsonPathText(const jsontitan::core::NodePath& path) -> QString;

// Clipboard text for a node's value: scalars yield the raw value text
// ("null" for Null, "true"/"false" for Boolean, the unquoted text for
// String/Number); containers yield compact JSON of the whole subtree.
auto valueClipboardText(jsontitan::core::NodeView node) -> QString;

// Key text for the node `path` resolves to, or nullopt when it has none
// (empty path = root, or the last segment is an array index).
auto keyClipboardText(const jsontitan::core::NodePath& path)
    -> std::optional<QString>;

}  // namespace jsontitan::shell
