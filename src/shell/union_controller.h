#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QTreeView>
#include <QWidget>

#include "shell/document_session.h"
#include "shell/edit_controller.h"
#include "shell/filter_proxy_model.h"
#include "shell/recent_files_manager.h"
#include "shell/search_controller.h"
#include "shell/tree_model.h"

// Owns the multi-file union flows: File > Union Files, Remove from Union,
// and the shared synchronous union-load body also used by the CLI
// multi-file entry point. Owned by MainWindow via Qt parent ownership.
class UnionController : public QObject {
    Q_OBJECT
public:
    // Raw pointers to the widgets the union flows touch; all owned by
    // MainWindow and guaranteed to outlive the controller.
    struct Ui {
        QTreeView* tree = nullptr;
        QLabel* welcomeLabel = nullptr;
        QLabel* noResultsLabel = nullptr;
        QLabel* searchErrorLabel = nullptr;
        QLineEdit* searchBar = nullptr;
        QTextEdit* detailPanel = nullptr;
    };

    UnionController(QWidget* dialogParent, Ui ui, TreeModel* treeModel,
                    FilterProxyModel* filterProxy, DocumentSession* session,
                    SearchController* searchController,
                    EditController* editController,
                    RecentFilesManager* recentFilesManager,
                    QObject* parent = nullptr);

    // Parses every file synchronously, unions the trees, and installs the
    // result. Shared body of File > Union Files and the CLI multi-file
    // branch (still synchronous; a later phase moves it to a worker).
    void loadUnionSynchronously(const QStringList& filePaths,
                                bool recordInRecentFiles);

public slots:
    void unionFiles();
    void removeFromUnion();

signals:
    // Forwarded to MainWindow's status-bar update.
    void statusUpdated(const QString& fileName, int nodeCount);

private:
    QWidget* m_dialogParent = nullptr;
    Ui m_ui;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;
    SearchController* m_searchController = nullptr;
    EditController* m_editController = nullptr;
    RecentFilesManager* m_recentFilesManager = nullptr;
};
