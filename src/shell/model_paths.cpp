#include "shell/model_paths.h"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>

#include <string>
#include <variant>

namespace jsontitan::shell {

auto nodePathForIndex(const TreeModel& model, const QModelIndex& sourceIndex)
    -> jsontitan::core::NodePath {
    jsontitan::core::NodePath path;
    QModelIndex walkIndex = sourceIndex;
    // Walk all the way up INCLUDING the top-level row: its parent is the
    // invalid index, which nodeForIndex-style accessors resolve to the JSON
    // root. (Stopping at parent().isValid() used to drop the top-level
    // segment, truncating every depth>=2 path.)
    while (walkIndex.isValid()) {
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
    // The model may have just been reset, so nothing is fetched yet. Rows
    // are fetched in batches only as far as the segment needs: up to the
    // index, or until the key is found. (Fetching whole levels made every
    // edit in a multi-million-row array insert every row into the view.)
    QModelIndex current;  // starts as invalid (root)
    for (const auto& segment : path) {
        int row = -1;
        if (const auto* index = std::get_if<std::size_t>(&segment)) {
            row = static_cast<int>(*index);
            while (model.rowCount(current) <= row && model.canFetchMore(current)) {
                model.fetchMore(current);
            }
        } else {
            const auto& key = std::get<std::string>(segment);
            for (int r = 0; row < 0;) {
                if (r == model.rowCount(current)) {
                    if (!model.canFetchMore(current)) {
                        break;
                    }
                    model.fetchMore(current);
                    continue;
                }
                const QModelIndex childIdx = model.index(r, 0, current);
                if (const auto* jn = model.jsonNodeForIndex(childIdx)) {
                    row = jn->key == key ? r : -1;
                } else if (const auto* an = model.arenaNodeForIndex(childIdx)) {
                    row = an->keyView() == key ? r : -1;
                }
                ++r;
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

auto selectedSourceIndex(const QAbstractItemView& view,
                         const QSortFilterProxyModel& proxy) -> QModelIndex {
    const QModelIndex proxyIndex = view.currentIndex();
    if (!proxyIndex.isValid() || !view.selectionModel() ||
        !view.selectionModel()->isSelected(proxyIndex)) {
        return {};
    }
    return proxy.mapToSource(proxyIndex);
}

auto selectedPath(const QAbstractItemView& view, const QSortFilterProxyModel& proxy,
                  const TreeModel& model) -> jsontitan::core::NodePath {
    const QModelIndex sourceIndex = selectedSourceIndex(view, proxy);
    return sourceIndex.isValid() ? nodePathForIndex(model, sourceIndex)
                                 : jsontitan::core::NodePath{};
}

}  // namespace jsontitan::shell
