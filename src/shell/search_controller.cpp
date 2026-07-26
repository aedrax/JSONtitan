#include "shell/search_controller.h"

SearchController::SearchController(Ui ui, TreeModel* treeModel,
                                   FilterProxyModel* filterProxy,
                                   DocumentSession* session, QObject* parent)
    : QObject(parent),
      m_ui(ui),
      m_treeModel(treeModel),
      m_filterProxy(filterProxy),
      m_session(session) {
    // Initialize debounce timer (single-shot, 250ms)
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(250);
    connect(m_debounceTimer, &QTimer::timeout,
            this, &SearchController::executeSearch);

    // Initialize background search worker on dedicated thread
    m_searchThread = new QThread(this);
    m_searchWorker = new SearchWorker();
    m_searchWorker->moveToThread(m_searchThread);

    connect(m_searchWorker, &SearchWorker::searchComplete,
            this, &SearchController::onSearchComplete, Qt::QueuedConnection);

    m_searchThread->start();

    // Wire the search UI
    connect(m_ui.bar, &QLineEdit::textChanged,
            this, &SearchController::onSearchTextChanged);
    connect(m_ui.caseToggle, &QToolButton::toggled,
            this, [this]() { if (!m_ui.bar->text().isEmpty()) m_debounceTimer->start(); });
    connect(m_ui.regexToggle, &QToolButton::toggled,
            this, [this]() { if (!m_ui.bar->text().isEmpty()) m_debounceTimer->start(); });
}

SearchController::~SearchController() {
    m_searchThread->quit();
    m_searchThread->wait();
    delete m_searchWorker;
}

void SearchController::invalidate() {
    // Any in-flight worker result now fails the generation check in
    // onSearchComplete, and no debounced search fires against stale UI state.
    ++m_searchGeneration;
    m_debounceTimer->stop();
    // Also tell the worker directly (atomic) so a running search aborts
    // instead of scanning the rest of the tree for a doomed result.
    if (m_searchWorker) {
        m_searchWorker->updateLatestGeneration(m_searchGeneration);
    }
}

void SearchController::onSearchTextChanged(const QString& text) {
    m_ui.errorLabel->hide();
    m_ui.noResultsLabel->hide();

    if (text.isEmpty()) {
        // Clear filter immediately — no debounce needed. Also invalidate any
        // in-flight search so its result cannot re-apply the filter afterwards.
        invalidate();
        m_filterProxy->clearFilter();
        m_ui.tree->show();
        return;
    }

    if (!m_session->currentRoot() && !m_session->arenaResult()) {
        return;
    }

    // Restart debounce timer — coalesces rapid keystrokes
    m_debounceTimer->start();
}

void SearchController::executeSearch() {
    QString text = m_ui.bar->text();
    if (text.isEmpty() || (!m_session->currentRoot() && !m_session->arenaResult())) {
        return;
    }

    // Increment generation counter to track this search request; the direct
    // update lets the worker abort any older search immediately.
    ++m_searchGeneration;
    m_searchWorker->updateLatestGeneration(m_searchGeneration);

    // Build SearchQuery from current UI state
    jsontitan::core::SearchQuery query;
    query.caseSensitive = m_ui.caseToggle->isChecked();

    if (m_ui.regexToggle->isChecked()) {
        // Regex toggle is ON: treat entire text as regex pattern
        query.pattern = text.toStdString();
        query.mode = jsontitan::core::SearchMode::Regex;
    } else if (text.startsWith('/') && text.endsWith('/') && text.size() > 2) {
        // Regex toggle is OFF but /pattern/ convention used: fallback for discoverability
        query.pattern = text.mid(1, text.size() - 2).toStdString();
        query.mode = jsontitan::core::SearchMode::Regex;
    } else {
        // Default: substring search
        query.pattern = text.toStdString();
        query.mode = jsontitan::core::SearchMode::Substring;
    }

    // Dispatch search to background worker — use arena path when available
    if (m_session->arenaResult()) {
        QMetaObject::invokeMethod(m_searchWorker, "executeArenaSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<jsontitan::core::ArenaParseResult>, m_session->arenaResult()),
                                  Q_ARG(uint64_t, m_searchGeneration));
    } else {
        QMetaObject::invokeMethod(m_searchWorker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, m_session->currentRoot()),
                                  Q_ARG(uint64_t, m_searchGeneration));
    }
}

void SearchController::onSearchComplete(jsontitan::core::FilterResult result,
                                        uint64_t generation) {
    // Discard stale results from previous searches
    if (generation != m_searchGeneration) {
        return;
    }

    if (result.error) {
        m_ui.errorLabel->setText(
            QString::fromStdString(result.error->description));
        m_ui.errorLabel->show();
        return;
    }

    if (result.matches.empty()) {
        m_filterProxy->applyFilter(result);
        m_ui.noResultsLabel->show();
        m_ui.tree->hide();
    } else {
        m_ui.noResultsLabel->hide();
        m_ui.tree->show();
        m_filterProxy->applyFilter(result);
    }
}
