#pragma once

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "shell/document_session.h"
#include "shell/edit_controller.h"
#include "shell/export_handler.h"
#include "shell/file_loader.h"
#include "shell/filter_proxy_model.h"
#include "shell/recent_files_manager.h"
#include "shell/search_controller.h"
#include "shell/syntax_highlighter.h"
#include "shell/tree_model.h"
#include "shell/union_controller.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Open file(s) from CLI-provided paths (single file or union mode)
    void openFromCliArgs(const std::vector<std::string>& filePaths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenFile();
    void onExportCsv();
    void onExportXml();
    void onParseError(QString errorMessage);
    void onProgressUpdated(int percentage);
    void onTreeSelectionChanged();
    void onRecentFileSelected(const QString& filePath);
    void onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result);

private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupDropOverlay();
    void showDropOverlay();
    void hideDropOverlay();
    void showWelcomeMessage();
    void updateStatusBar(const QString& fileName, int nodeCount);
    // View of the currently selected node (either backing), or nullopt when
    // no valid selection resolves to a node.
    std::optional<jsontitan::core::NodeView> selectedNodeView() const;
    void updateWindowTitle(bool modified);

    // UI elements
    QLineEdit* m_searchBar = nullptr;
    QToolButton* m_caseSensitiveToggle = nullptr;
    QToolButton* m_regexToggle = nullptr;
    QLabel* m_searchErrorLabel = nullptr;
    QTreeView* m_treeView = nullptr;
    QTextEdit* m_detailPanel = nullptr;
    QLabel* m_welcomeLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_noResultsLabel = nullptr;
    QLabel* m_dropOverlay = nullptr;

    // Menu actions
    QAction* m_openAction = nullptr;
    QAction* m_unionAction = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_exportCsvAction = nullptr;
    QAction* m_exportXmlAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;

    // Recent files
    RecentFilesManager* m_recentFilesManager = nullptr;

    // Models
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;

    // Background loader
    FileLoader* m_fileLoader = nullptr;

    // Search pipeline (debounce, worker thread, invalidation)
    SearchController* m_searchController = nullptr;

    // Document-mutation flows (delete / save / unsaved-changes prompt)
    EditController* m_editController = nullptr;

    // Multi-file union flows
    UnionController* m_unionController = nullptr;

    // Syntax highlighting
    jsontitan::shell::SyntaxTheme m_syntaxTheme = jsontitan::shell::catppuccinMochaTheme();

    // Current document state (tree backing, file identity, modified flag)
    DocumentSession* m_session = nullptr;
};
