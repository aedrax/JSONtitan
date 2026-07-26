#pragma once

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
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
#include "core/deletion_engine.h"
#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "shell/detail_panel_presenter.h"
#include "shell/document_session.h"
#include "shell/edit_controller.h"
#include "shell/export_handler.h"
#include "shell/file_loader.h"
#include "shell/filter_proxy_model.h"
#include "shell/recent_files_manager.h"
#include "shell/search_controller.h"
#include "shell/theme_manager.h"
#include "shell/tree_model.h"
#include "shell/union_controller.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Open file(s) from CLI-provided paths (single file or union mode)
    void openFromCliArgs(const std::vector<std::string>& filePaths);

    // Reopens the last successfully opened single file, when the
    // "Reopen Last File on Startup" option is enabled and the file still
    // exists. Silent no-op (welcome screen stays) otherwise. Called at
    // startup only when no CLI files were given — CLI args take precedence.
    void restoreLastSession();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenFile();
    void onExportJson();
    void onExportCsv();
    void onExportXml();
    void onCopyValue();
    void onCopyKey();
    void onCopyPath();
    void onParseError(QString errorMessage);
    void onProgressUpdated(int percentage);
    void onRecentFileSelected(const QString& filePath);
    void onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result);
    void onAbout();
    void onShowKeyboardShortcuts();
    void onExpandAll();
    void onCollapseAll();
    void onReload();
    void onWatchedFileChanged(const QString& path);

private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupDropOverlay();
    void showDropOverlay();
    void hideDropOverlay();
    void showWelcomeMessage();
    void updateStatusBar(const QString& fileName, int nodeCount);
    void updateWindowTitle(bool modified);

    // Load-progress UI (progress bar + cancel button) shown while a
    // background parse is running.
    void showLoadProgress();
    void hideLoadProgress();
    // Cancels the active load (no-op when none is running): aborts the
    // parse, hides the progress UI, and keeps the previous document (parse
    // results only install on completion).
    void cancelActiveLoad();

    // NodePath of the tree view's current selection ({} when none).
    jsontitan::core::NodePath currentSelectionPath() const;

    // Expand the tree to the given 1-based level (level 1 = top-level rows
    // expanded). Uses QTreeView::expandToDepth.
    void expandToLevel(int level);

    // Force-fetch every row of the (lazily-fetched) source model under
    // sourceParent so a subsequent expandAll can actually expand them.
    void fetchAllRows(const QModelIndex& sourceParent);

    // External-change watching of the current single file. Started after a
    // successful single-file parse; stopped for union mode / welcome. Our
    // own saves are bracketed via EditController::aboutToSave/saved so they
    // never surface as external changes.
    void startWatchingCurrentFile();
    void stopWatchingFile();
    void showFileChangedBar();
    void hideFileChangedBar();

    // UI elements
    QLineEdit* m_searchBar = nullptr;
    QLabel* m_matchCountLabel = nullptr;
    QToolButton* m_caseSensitiveToggle = nullptr;
    QToolButton* m_regexToggle = nullptr;
    QToolButton* m_jsonPathToggle = nullptr;
    QLabel* m_searchErrorLabel = nullptr;
    QTreeView* m_treeView = nullptr;
    QTextEdit* m_detailPanel = nullptr;
    QLabel* m_breadcrumbLabel = nullptr;
    QLabel* m_welcomeLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelLoadButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_noResultsLabel = nullptr;
    QLabel* m_dropOverlay = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QWidget* m_fileChangedBar = nullptr;
    QLabel* m_fileChangedLabel = nullptr;

    // Menu actions
    QAction* m_openAction = nullptr;
    QAction* m_unionAction = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_exportJsonAction = nullptr;
    QAction* m_exportCsvAction = nullptr;
    QAction* m_exportXmlAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_expandAllAction = nullptr;
    QAction* m_collapseAllAction = nullptr;
    QAction* m_shortcutsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_reopenLastFileAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_lightThemeAction = nullptr;

    // External file-change watching (single-file documents only)
    QFileSystemWatcher* m_fileWatcher = nullptr;
    QTimer* m_fileChangeDebounce = nullptr;
    bool m_suppressWatchNotifications = false;

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

    // Detail panel rendering for the current selection
    DetailPanelPresenter* m_detailPresenter = nullptr;

    // Current document state (tree backing, file identity, modified flag)
    DocumentSession* m_session = nullptr;

    // Application theme (QSS + syntax palette + match-highlight color)
    ThemeManager* m_themeManager = nullptr;
};
