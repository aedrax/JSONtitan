#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QTreeView>
#include <QWidget>

#include <memory>

#include "core/json_node.h"
#include "shell/document_session.h"
#include "shell/edit_controller.h"
#include "shell/file_loader.h"
#include "shell/filter_proxy_model.h"
#include "shell/recent_files_manager.h"
#include "shell/search_controller.h"
#include "shell/tree_model.h"

// Owns the multi-file union flows: File > Union Files, Remove from Union,
// and the shared asynchronous union-load body also used by the CLI
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
                    FileLoader* fileLoader,
                    QObject* parent = nullptr);

    // Starts an asynchronous union load on the FileLoader worker thread:
    // cancels any in-flight parse, shows the load-progress UI (via
    // loadStarted), and installs the union tree when the worker completes.
    // Callers keep their own pre-checks (confirmDiscardChanges etc.).
    void loadUnion(const QStringList& filePaths, bool recordInRecentFiles);

public slots:
    void unionFiles();
    void removeFromUnion();

signals:
    // Forwarded to MainWindow's status-bar update.
    void statusUpdated(const QString& fileName, int nodeCount);
    // Show/hide hooks for MainWindow's load-progress UI (progress bar +
    // cancel button) — same pattern as the single-file flow.
    void loadStarted();
    void loadFinished();

private slots:
    void onUnionParseComplete(
        std::shared_ptr<const jsontitan::core::JsonNode> root,
        const QStringList& filePaths);
    void onUnionParseError(const QString& fileName,
                           const QString& errorMessage);

private:
    QWidget* m_dialogParent = nullptr;
    Ui m_ui;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;
    SearchController* m_searchController = nullptr;
    EditController* m_editController = nullptr;
    RecentFilesManager* m_recentFilesManager = nullptr;
    FileLoader* m_fileLoader = nullptr;

    // Whether the union load in flight should record its files in the
    // recent-files list on completion (CLI/drop entry points only).
    bool m_recordInRecentFiles = false;
};
