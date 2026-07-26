#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/search_engine.h"
#include "shell/document_session.h"
#include "shell/filter_proxy_model.h"
#include "shell/search_worker.h"
#include "shell/tree_model.h"

// Owns the search pipeline: the debounce timer, the background SearchWorker
// thread, generation-based invalidation, and the search-related UI state
// (error / no-results labels, tree visibility, toggle re-triggering).
// Owned by MainWindow via Qt parent ownership.
class SearchController : public QObject {
    Q_OBJECT
public:
    // Raw pointers to the widgets the controller drives; all owned by
    // MainWindow and guaranteed to outlive the controller.
    struct Ui {
        QLineEdit* bar = nullptr;
        QToolButton* caseToggle = nullptr;
        QToolButton* regexToggle = nullptr;
        QLabel* errorLabel = nullptr;
        QLabel* noResultsLabel = nullptr;
        QTreeView* tree = nullptr;
        // Small "N matches" label next to the search bar; hidden whenever no
        // search is active.
        QLabel* matchCountLabel = nullptr;
    };

    SearchController(Ui ui, TreeModel* treeModel, FilterProxyModel* filterProxy,
                     DocumentSession* session, QObject* parent = nullptr);
    ~SearchController() override;

    // Invalidates any in-flight or pending search so its (stale) result is
    // discarded. Must be called whenever the displayed tree is replaced —
    // BEFORE the model reset, so no stale result can land in between.
    void invalidate();

    // Match paths of the last completed search (sorted, deduplicated,
    // root-matches excluded). Exposed for navigation tests.
    const std::vector<std::vector<std::size_t>>& matchPaths() const {
        return m_matchPaths;
    }

public slots:
    // F3 / Shift+F3 / Return-in-search-bar navigation over the last search's
    // matches. Wraps around; no-op when there are no matches.
    void nextMatch();
    void prevMatch();

private slots:
    void onSearchTextChanged(const QString& text);
    void executeSearch();
    void onSearchComplete(jsontitan::core::FilterResult result,
                          uint64_t generation);

private:
    // Steps m_currentMatch by delta (with wrap-around) and selects/scrolls
    // the tree to the resulting match.
    void navigateMatch(int delta);

    // Expands the (pruned) proxy tree so matches are visible. Bounded: see
    // kMaxExpandMatches / kMaxExpandAllRootRows in the implementation.
    void expandMatchPaths();

    // Clears match state and hides the match-count label.
    void clearMatchState();

    Ui m_ui;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;

    // Sorted, deduplicated ancestor paths of the last search's matches
    // (excluding a bare root match, which has no selectable row).
    std::vector<std::vector<std::size_t>> m_matchPaths;
    int m_currentMatch = -1;

    // Debounce timer for search
    QTimer* m_debounceTimer = nullptr;
    uint64_t m_searchGeneration = 0;

    // Background search worker
    SearchWorker* m_searchWorker = nullptr;
    QThread* m_searchThread = nullptr;
};
