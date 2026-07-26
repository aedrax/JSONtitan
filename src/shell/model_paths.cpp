#include "shell/model_paths.h"

#include <string>
#include <variant>

namespace jsontitan::shell {

auto nodePathForIndex(const TreeModel& model, const QModelIndex& sourceIndex)
    -> jsontitan::core::NodePath {
    jsontitan::core::NodePath path;
    QModelIndex walkIndex = sourceIndex;
    while (walkIndex.isValid() && walkIndex.parent().isValid()) {
        QModelIndex parentIndex = walkIndex.parent();
        // Determine if the parent is an Object or Array
        jsontitan::core::NodeType parentType = jsontitan::core::NodeType::Object;
        if (auto* jn = model.jsonNodeForIndex(parentIndex)) {
            parentType = jn->type;
        } else if (auto* an = model.arenaNodeForIndex(parentIndex)) {
            parentType = an->type;
        }

        if (parentType == jsontitan::core::NodeType::Array) {
            path.insert(path.begin(), static_cast<std::size_t>(walkIndex.row()));
        } else {
            // Object: get the key from the child node
            std::string key;
            if (auto* jn = model.jsonNodeForIndex(walkIndex)) {
                key = jn->key;
            } else if (auto* an = model.arenaNodeForIndex(walkIndex)) {
                key = std::string(an->keyView());
            }
            path.insert(path.begin(), key);
        }
        walkIndex = parentIndex;
    }
    return path;
}

auto indexForPath(TreeModel& model, const jsontitan::core::NodePath& path)
    -> QModelIndex {
    QModelIndex current;  // starts as invalid (root)
    for (std::size_t i = 0; i < path.size(); ++i) {
        // The model may have just been reset, so nothing is fetched yet:
        // rows must be fetched BEFORE the by-key scan below, or rowCount is
        // 0, the key is never found, and the walk silently stops at the
        // wrong level.
        while (model.canFetchMore(current)) {
            model.fetchMore(current);
        }

        int row = -1;
        if (auto* idx = std::get_if<std::size_t>(&path[i])) {
            row = static_cast<int>(*idx);
        } else {
            // Find the row by key
            auto* keyStr = std::get_if<std::string>(&path[i]);
            int rowCount = model.rowCount(current);
            for (int r = 0; r < rowCount; ++r) {
                QModelIndex childIdx = model.index(r, 0, current);
                if (auto* jn = model.jsonNodeForIndex(childIdx)) {
                    if (jn->key == *keyStr) {
                        row = r;
                        break;
                    }
                }
            }
        }
        if (row < 0) {
            return {};
        }
        current = model.index(row, 0, current);
        if (!current.isValid()) {
            return {};
        }
    }
    return current;
}

}  // namespace jsontitan::shell
