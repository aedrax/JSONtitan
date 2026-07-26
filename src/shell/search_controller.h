#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <cstdint>

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
    };

    SearchController(Ui ui, TreeModel* treeModel, FilterProxyModel* filterProxy,
                     DocumentSession* session, QObject* parent = nullptr);
    ~SearchController() override;

    // Invalidates any in-flight or pending search so its (stale) result is
    // discarded. Must be called whenever the displayed tree is replaced —
    // BEFORE the model reset, so no stale result can land in between.
    void invalidate();

private slots:
    void onSearchTextChanged(const QString& text);
    void executeSearch();
    void onSearchComplete(jsontitan::core::FilterResult result,
                          uint64_t generation);

private:
    Ui m_ui;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;

    // Debounce timer for search
    QTimer* m_debounceTimer = nullptr;
    uint64_t m_searchGeneration = 0;

    // Background search worker
    SearchWorker* m_searchWorker = nullptr;
    QThread* m_searchThread = nullptr;
};
