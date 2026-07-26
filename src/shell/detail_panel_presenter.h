#pragma once

#include <QObject>
#include <QTextEdit>
#include <QTreeView>

#include <cstddef>
#include <optional>

#include "core/node_view.h"
#include "shell/document_session.h"
#include "shell/filter_proxy_model.h"
#include "shell/syntax_highlighter.h"
#include "shell/tree_model.h"

// Renders the currently selected node into the detail panel (token emission
// over NodeView + syntax highlighting, size-capped). Owned by MainWindow via
// Qt parent ownership; wires itself to the tree view's selection model.
class DetailPanelPresenter : public QObject {
    Q_OBJECT
public:
    DetailPanelPresenter(QTextEdit* detailPanel, QTreeView* treeView,
                         FilterProxyModel* filterProxy, TreeModel* treeModel,
                         DocumentSession* session, QObject* parent = nullptr);

    // View of the currently selected node (either backing), or nullopt when
    // no valid selection resolves to a node. Also used by the export flows.
    std::optional<jsontitan::core::NodeView> selectedNodeView() const;

private slots:
    void onSelectionChanged();

private:
    // Maximum number of pretty-printed bytes rendered into the detail panel.
    static constexpr std::size_t kDetailPanelByteLimit = 65536;  // 64 KB

    QTextEdit* m_detailPanel = nullptr;
    QTreeView* m_treeView = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    TreeModel* m_treeModel = nullptr;
    DocumentSession* m_session = nullptr;

    // Syntax highlighting
    jsontitan::shell::SyntaxTheme m_syntaxTheme =
        jsontitan::shell::catppuccinMochaTheme();
};
