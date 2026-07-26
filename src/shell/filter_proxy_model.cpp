#include "shell/filter_proxy_model.h"

FilterProxyModel::FilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
}

void FilterProxyModel::setMatchHighlightColor(const QColor& color) {
    if (m_matchHighlight == color) {
        return;
    }
    m_matchHighlight = color;
    // Rare (theme switch): a full invalidate is the simplest way to repaint
    // every currently-highlighted row across the lazily-built tree.
    if (m_filtered) {
        invalidate();
    }
}

void FilterProxyModel::applyFilter(const jsontitan::core::FilterResult& result) {
    m_filtered = true;
    buildVisiblePaths(result);
    // The source model exposes children lazily (fetchMore batches); a match
    // beyond the fetched rows has no source row yet and could never be
    // accepted. Force-fetch along every matched path first.
    ensureMatchesFetched();
    invalidateFilter();
}

void FilterProxyModel::clearFilter() {
    m_filtered = false;
    m_rootMatched = false;
    m_visiblePaths.clear();
    m_exactMatchPaths.clear();
    invalidateFilter();
}

QVariant FilterProxyModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::BackgroundRole && m_filtered && index.isValid() &&
        !m_exactMatchPaths.empty()) {
        QModelIndex sourceIndex = mapToSource(index);
        if (sourceIndex.isValid()) {
            IndexPath path =
                buildPathForIndex(sourceIndex.row(), sourceIndex.parent());
            if (m_exactMatchPaths.count(path) > 0) {
                return m_matchHighlight;
            }
        }
    }
    return QSortFilterProxyModel::data(index, role);
}

bool FilterProxyModel::filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const {
    // When no filter is active, accept all rows
    if (!m_filtered) {
        return true;
    }

    // A match on the root itself (empty ancestor path) means the whole tree
    // is its context: accept everything. Without this the tree rendered
    // completely empty while the "no results" label stayed hidden.
    if (m_rootMatched) {
        return true;
    }

    // Check if this row's path (or any descendant path) is in the visible set
    return isPathVisible(sourceRow, sourceParent);
}

void FilterProxyModel::buildVisiblePaths(const jsontitan::core::FilterResult& result) {
    m_visiblePaths.clear();
    m_exactMatchPaths.clear();
    m_rootMatched = false;

    for (const auto& match : result.matches) {
        const auto& indices = match.ancestorIndices;

        if (indices.empty()) {
            // The root node itself matched.
            m_rootMatched = true;
            continue;
        }

        // Add the full path (the matched node itself)
        m_visiblePaths.insert(indices);
        m_exactMatchPaths.insert(indices);

        // Add all ancestor prefixes so the structural context is preserved
        // For a path [0, 2, 1], we add [0], [0, 2], and [0, 2, 1]
        IndexPath prefix;
        for (auto idx : indices) {
            prefix.push_back(idx);
            m_visiblePaths.insert(prefix);
        }
    }
}

void FilterProxyModel::ensureMatchesFetched() {
    auto* source = sourceModel();
    if (!source) {
        return;
    }

    if (m_rootMatched) {
        // All rows are accepted; prime the first batch at root level and let
        // the view's normal scroll-driven fetching load the rest.
        if (source->rowCount() == 0 && source->canFetchMore(QModelIndex())) {
            source->fetchMore(QModelIndex());
        }
    }

    // m_visiblePaths contains every matched path plus all its prefixes, so
    // walking the full paths (those not a prefix of their std::set successor)
    // would suffice — but simply walking every stored path is correct and the
    // per-level work is idempotent.
    for (const auto& path : m_visiblePaths) {
        QModelIndex parent;  // invalid = root
        for (std::size_t level = 0; level < path.size(); ++level) {
            const int row = static_cast<int>(path[level]);
            // Fetch batches until the row exists (or nothing more to fetch).
            while (source->rowCount(parent) <= row && source->canFetchMore(parent)) {
                source->fetchMore(parent);
            }
            QModelIndex child = source->index(row, 0, parent);
            if (!child.isValid()) {
                break;  // stale path for this tree — nothing to fetch
            }
            parent = child;
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
