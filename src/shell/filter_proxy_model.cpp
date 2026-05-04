#include "shell/filter_proxy_model.h"

FilterProxyModel::FilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
}

void FilterProxyModel::applyFilter(const jsontitan::core::FilterResult& result) {
    beginFilterChange();
    m_filtered = true;
    buildVisiblePaths(result);
    endFilterChange();
}

void FilterProxyModel::clearFilter() {
    beginFilterChange();
    m_filtered = false;
    m_visiblePaths.clear();
    endFilterChange();
}

bool FilterProxyModel::filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const {
    // When no filter is active, accept all rows
    if (!m_filtered) {
        return true;
    }

    // Check if this row's path (or any descendant path) is in the visible set
    return isPathVisible(sourceRow, sourceParent);
}

void FilterProxyModel::buildVisiblePaths(const jsontitan::core::FilterResult& result) {
    m_visiblePaths.clear();

    for (const auto& match : result.matches) {
        const auto& indices = match.ancestorIndices;

        // Add the full path (the matched node itself)
        m_visiblePaths.insert(indices);

        // Add all ancestor prefixes so the structural context is preserved
        // For a path [0, 2, 1], we add [0], [0, 2], and [0, 2, 1]
        IndexPath prefix;
        for (auto idx : indices) {
            prefix.push_back(idx);
            m_visiblePaths.insert(prefix);
        }
    }
}

bool FilterProxyModel::isPathVisible(int sourceRow, const QModelIndex& sourceParent) const {
    IndexPath path = buildPathForIndex(sourceRow, sourceParent);

    // Check if this exact path is visible
    if (m_visiblePaths.count(path) > 0) {
        return true;
    }

    // Also check if any path in the visible set is a descendant of this path
    // (i.e., starts with this path as a prefix). This ensures that parent nodes
    // of matched nodes are shown even if not explicitly in the set.
    // Since we already add all prefixes in buildVisiblePaths, this exact-match
    // check is sufficient.

    return false;
}

FilterProxyModel::IndexPath FilterProxyModel::buildPathForIndex(
    int sourceRow, const QModelIndex& sourceParent) const {

    // Build the path by walking up from the given row to the root
    IndexPath reversePath;
    reversePath.push_back(static_cast<std::size_t>(sourceRow));

    QModelIndex current = sourceParent;
    while (current.isValid()) {
        reversePath.push_back(static_cast<std::size_t>(current.row()));
        current = current.parent();
    }

    // Reverse to get root-to-leaf order
    std::reverse(reversePath.begin(), reversePath.end());
    return reversePath;
}
