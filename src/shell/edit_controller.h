#pragma once

#include <QObject>
#include <QTreeView>
#include <QWidget>

#include "shell/document_session.h"
#include "shell/filter_proxy_model.h"
#include "shell/search_controller.h"
#include "shell/tree_model.h"

// Owns the document-mutation flows: node deletion (confirmation dialog +
// selection restore via model_paths), Save / Save As, and the
// unsaved-changes prompt. Owned by MainWindow via Qt parent ownership.
class EditController : public QObject {
    Q_OBJECT
public:
    EditController(QWidget* dialogParent, QTreeView* treeView,
                   TreeModel* treeModel, FilterProxyModel* filterProxy,
                   DocumentSession* session,
                   SearchController* searchController,
                   QObject* parent = nullptr);

    // Returns true when it is safe to discard the current document: either
    // there are no unsaved changes, or the user chose Save (and it
    // succeeded) or Discard. Returns false when the user cancelled.
    bool confirmDiscardChanges();

public slots:
    void deleteSelectedNode();
    void save();
    void saveAs();

private:
    QWidget* m_dialogParent = nullptr;
    QTreeView* m_treeView = nullptr;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;
    SearchController* m_searchController = nullptr;
};
