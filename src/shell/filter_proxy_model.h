#pragma once

#include <QSortFilterProxyModel>

#include <set>
#include <vector>

#include "core/search_engine.h"

class FilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject* parent = nullptr);

    void applyFilter(const jsontitan::core::FilterResult& result);
    void clearFilter();

    bool isFiltered() const { return m_filtered; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    // Represents a path from root to a visible node as a sequence of child indices
    using IndexPath = std::vector<std::size_t>;

    // Build the set of visible paths from a FilterResult
    void buildVisiblePaths(const jsontitan::core::FilterResult& result);

    // Check if a given source path (built from row + parent) is in the visible set
    bool isPathVisible(int sourceRow, const QModelIndex& sourceParent) const;

    // Build the index path for a given source model index
    IndexPath buildPathForIndex(int sourceRow, const QModelIndex& sourceParent) const;

    bool m_filtered = false;

    // Set of all visible paths (matched nodes + ancestors)
    // Each path is stored as a vector of child indices from root
    std::set<IndexPath> m_visiblePaths;
};
