#include "shell/search_controller.h"

#include <QAbstractItemView>

#include <algorithm>

#include "shell/model_paths.h"

namespace {

// Auto-expand limits: with up to this many match paths, each match's
// ancestors are expanded individually (cheap, targeted). Beyond that, fall
// back to expandAll() only when the pruned proxy tree has few top-level
// rows; otherwise expand nothing extra to avoid pathological UI stalls.
constexpr std::size_t kMaxExpandMatches = 500;
constexpr int kMaxExpandAllRootRows = 100;

}  // namespace

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
    if (m_ui.jsonPathToggle) {
        m_defaultPlaceholder = m_ui.bar->placeholderText();
        // JSONPath and regex modes are mutually exclusive: checking one
        // unchecks the other. The case toggle is meaningless for JSONPath
        // (member names are exact), so it is disabled while "$" is active.
        connect(m_ui.jsonPathToggle, &QToolButton::toggled,
                this, [this](bool checked) {
                    if (checked && m_ui.regexToggle->isChecked()) {
                        m_ui.regexToggle->setChecked(false);
                    }
                    m_ui.caseToggle->setEnabled(!checked);
                    m_ui.bar->setPlaceholderText(
                        checked ? tr("JSONPath query ($.store.book[*].author)")
                                : m_defaultPlaceholder);
                    if (!m_ui.bar->text().isEmpty()) m_debounceTimer->start();
                });
        connect(m_ui.regexToggle, &QToolButton::toggled,
                this, [this](bool checked) {
                    if (checked && m_ui.jsonPathToggle->isChecked()) {
                        m_ui.jsonPathToggle->setChecked(false);
                    }
                });
    }
    // Return in the search bar jumps to the next match.
    connect(m_ui.bar, &QLineEdit::returnPressed,
            this, &SearchController::nextMatch);
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
    // The match list refers to the (about-to-be-replaced) tree; navigating
    // it would select unrelated nodes.
    clearMatchState();
}

void SearchController::clearMatchState() {
    m_matchPaths.clear();
    m_currentMatch = -1;
    if (m_ui.matchCountLabel) {
        m_ui.matchCountLabel->hide();
    }
}

void SearchController::nextMatch() {
    navigateMatch(1);
}

void SearchController::prevMatch() {
    navigateMatch(-1);
}

void SearchController::navigateMatch(int delta) {
    if (m_matchPaths.empty()) {
        return;
    }

    const int count = static_cast<int>(m_matchPaths.size());
    if (m_currentMatch < 0) {
        // First navigation: Next starts at the first match, Previous at the
        // last one.
        m_currentMatch = (delta >= 0) ? 0 : count - 1;
    } else {
        m_currentMatch = ((m_currentMatch + delta) % count + count) % count;
    }

    const auto& indices = m_matchPaths[static_cast<std::size_t>(m_currentMatch)];
    jsontitan::core::NodePath path;
    path.reserve(indices.size());
    for (std::size_t idx : indices) {
        path.emplace_back(idx);
    }

    QModelIndex sourceIndex = jsontitan::shell::indexForPath(*m_treeModel, path);
    if (!sourceIndex.isValid()) {
        return;
    }
    QModelIndex proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) {
        return;
    }
    m_ui.tree->setCurrentIndex(proxyIndex);
    m_ui.tree->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
}

void SearchController::expandMatchPaths() {
    if (m_matchPaths.empty()) {
        return;
    }

    if (m_matchPaths.size() <= kMaxExpandMatches) {
        for (const auto& indices : m_matchPaths) {
            jsontitan::core::NodePath path;
            path.reserve(indices.size());
            for (std::size_t idx : indices) {
                path.emplace_back(idx);
            }
            QModelIndex sourceIndex =
                jsontitan::shell::indexForPath(*m_treeModel, path);
            if (!sourceIndex.isValid()) {
                continue;
            }
            QModelIndex proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
            for (QModelIndex ancestor = proxyIndex.parent(); ancestor.isValid();
                 ancestor = ancestor.parent()) {
                m_ui.tree->expand(ancestor);
            }
        }
    } else if (m_filterProxy->rowCount() <= kMaxExpandAllRootRows) {
        m_ui.tree->expandAll();
    }
    // else: too many matches over a wide tree — leave expansion to the user.
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

    if (m_ui.jsonPathToggle && m_ui.jsonPathToggle->isChecked()) {
        // JSONPath toggle is ON: evaluate the text as a JSONPath expression
        // (caseSensitive is ignored by the engine in this mode).
        query.pattern = text.toStdString();
        query.mode = jsontitan::core::SearchMode::JsonPath;
    } else if (m_ui.regexToggle->isChecked()) {
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
        clearMatchState();
        m_ui.errorLabel->setText(
            QString::fromStdString(result.error->description));
        m_ui.errorLabel->show();
        return;
    }

    // Rebuild the navigable match list: sorted, deduplicated, and without
    // the bare root match (the root is not a selectable row).
    m_matchPaths.clear();
    m_matchPaths.reserve(result.matches.size());
    for (const auto& match : result.matches) {
        if (!match.ancestorIndices.empty()) {
            m_matchPaths.push_back(match.ancestorIndices);
        }
    }
    std::sort(m_matchPaths.begin(), m_matchPaths.end());
    m_matchPaths.erase(std::unique(m_matchPaths.begin(), m_matchPaths.end()),
                       m_matchPaths.end());
    m_currentMatch = -1;

    const std::size_t matchCount = result.matches.size();
    if (m_ui.matchCountLabel) {
        m_ui.matchCountLabel->setText(
            matchCount == 0
                ? tr("No matches")
                : (matchCount == 1 ? tr("1 match")
                                   : tr("%1 matches").arg(matchCount)));
        m_ui.matchCountLabel->show();
    }

    if (result.matches.empty()) {
        m_filterProxy->applyFilter(result);
        m_ui.noResultsLabel->show();
        m_ui.tree->hide();
    } else {
        m_ui.noResultsLabel->hide();
        m_ui.tree->show();
        m_filterProxy->applyFilter(result);
        expandMatchPaths();
    }
}
