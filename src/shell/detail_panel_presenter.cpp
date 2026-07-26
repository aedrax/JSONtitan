#include "shell/detail_panel_presenter.h"

#include <QItemSelectionModel>

#include "core/pretty_printer.h"
#include "core/token_emitter.h"

DetailPanelPresenter::DetailPanelPresenter(QTextEdit* detailPanel,
                                           QTreeView* treeView,
                                           FilterProxyModel* filterProxy,
                                           TreeModel* treeModel,
                                           DocumentSession* session,
                                           QObject* parent)
    : QObject(parent),
      m_detailPanel(detailPanel),
      m_treeView(treeView),
      m_filterProxy(filterProxy),
      m_treeModel(treeModel),
      m_session(session) {
    // Connect tree selection changes
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &DetailPanelPresenter::onSelectionChanged);
}

std::optional<jsontitan::core::NodeView> DetailPanelPresenter::selectedNodeView() const {
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return std::nullopt;
    }

    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return std::nullopt;
    }

    // The model hands out raw pointers into whichever backing is live; a
    // NodeView over them is valid as long as that backing is kept alive,
    // which DocumentSession guarantees.
    if (const auto* jn = m_treeModel->jsonNodeForIndex(sourceIndex)) {
        return jsontitan::core::NodeView(*jn);
    }
    if (const auto* an = m_treeModel->arenaNodeForIndex(sourceIndex)) {
        return jsontitan::core::NodeView(*an);
    }
    return std::nullopt;
}

void DetailPanelPresenter::onSelectionChanged() {
    // Emit tokens directly over whichever backing the selection resolves to —
    // no toJsonNode() deep copy for arena-backed nodes.
    auto node = selectedNodeView();
    if (!node && m_session->currentRoot()) {
        // Legacy behavior: an unresolved selection over a JsonNode-backed
        // tree falls back to showing the root.
        node = jsontitan::core::NodeView(*m_session->currentRoot());
    }
    if (!node) {
        m_detailPanel->clear();
        return;
    }

    jsontitan::core::PrettyPrintOptions opts;
    opts.maxOutputSize = kDetailPanelByteLimit;

    auto tokenResult = jsontitan::core::emitTokens(*node, opts);
    jsontitan::shell::renderHighlighted(m_detailPanel, tokenResult, m_syntaxTheme);
}
