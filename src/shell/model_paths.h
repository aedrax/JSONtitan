#pragma once

#include <QModelIndex>

#include "core/deletion_engine.h"
#include "shell/tree_model.h"

namespace jsontitan::shell {

// Computes the NodePath for a source-model index by walking up the
// QModelIndex parent chain. Works regardless of whether the model is
// arena-backed or JsonNode-backed.
auto nodePathForIndex(const TreeModel& model, const QModelIndex& sourceIndex)
    -> jsontitan::core::NodePath;

// Fetch-aware descend from the (possibly just-reset) model root along `path`.
// Rows are fetched BEFORE each by-key scan, or rowCount would be 0 and the
// key never found. Returns the index the path resolves to, or an invalid
// QModelIndex when the walk fails at any level. Note that an empty path
// resolves to the root, which is also the invalid index — callers must
// treat the empty path specially themselves.
auto indexForPath(TreeModel& model, const jsontitan::core::NodePath& path)
    -> QModelIndex;

}  // namespace jsontitan::shell
