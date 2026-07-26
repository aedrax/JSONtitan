#pragma once

#include <QColor>
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

    // Background color for exact-match rows while filtered. Theme-dependent:
    // MainWindow updates it on ThemeManager::themeChanged. Defaults to the
    // dark theme's translucent accent.
    void setMatchHighlightColor(const QColor& color);

    // While filtered, rows whose exact path is a match (not mere ancestors)
    // get a distinct translucent-accent background.
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    // Represents a path from root to a visible node as a sequence of child indices
    using IndexPath = std::vector<std::size_t>;

    // Build the set of visible paths from a FilterResult
    void buildVisiblePaths(const jsontitan::core::FilterResult& result);

    // Force the (lazily-fetched) source model to expose every row on every
    // visible path, so filterAcceptsRow can actually be asked about them
    void ensureMatchesFetched();

    // Check if a given source path (built from row + parent) is in the visible set
    bool isPathVisible(int sourceRow, const QModelIndex& sourceParent) const;

    // Build the index path for a given source model index
    IndexPath buildPathForIndex(int sourceRow, const QModelIndex& sourceParent) const;

    bool m_filtered = false;
    bool m_rootMatched = false;  // a match had an empty path (root itself)

    // Translucent variant of the dark theme's accent (#89b4fa, as used for
    // selection in style_dark.qss) — light enough that selection and hover
    // still read clearly. Replaced via setMatchHighlightColor on theme
    // changes.
    QColor m_matchHighlight{0x89, 0xb4, 0xfa, 60};

    // Set of all visible paths (matched nodes + ancestors)
    // Each path is stored as a vector of child indices from root
    std::set<IndexPath> m_visiblePaths;

    // Exact match paths only (no ancestor prefixes) — drives the match-row
    // background highlight in data().
    std::set<IndexPath> m_exactMatchPaths;
};
