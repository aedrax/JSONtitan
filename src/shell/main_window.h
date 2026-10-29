#pragma once

#include <functional>

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

    // Opens a file the operating system handed us (macOS Finder "Open
    // With" / double-click), after the usual unsaved-changes prompt, and
    // brings the window forward.
    void openFileFromSystem(const QString& filePath);

    // Reopens the last successfully opened single file, when the
    // "Reopen Last File on Startup" option is enabled and the file still
    // exists. Silent no-op (welcome screen stays) otherwise. Called at
    // startup only when no CLI files were given — CLI args take precedence.
    void restoreLastSession();

    // Select and reveal the node a definite JSONPath names ("$.a.b[2]", the
    // '$' optional). Clears an active search first when it hides the target.
    // Returns false with a user-facing reason in *errorMessage on failure.
    bool goToPath(const QString& expression, QString* errorMessage = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewDocument();
    void onOpenFile();
    void onExportJson();
    void onExportCsv();
    void onExportXml();
    void onCopyValue();
    void onCopyKey();
    void onCopyPath();
    void onGoToPath();
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
    // Starts parsing one file in the background. The session keeps the
    // current document's identity until the result installs, so a cancelled
    // or failed load leaves Save pointing at the file on screen.
    void beginSingleFileLoad(const QString& filePath);
    // Adds the current single file to the recent files and session restore.
    void rememberCurrentFile();
    // Shared Export flow: the selection (or the whole document) written by
    // `write` to a file the user picks.
    void exportSelection(
        const QString& formatName, const QString& fileFilter,
        const std::function<QString(jsontitan::core::NodeView, const QString&)>& write);

    // Enable/disable the structural edit actions for the current row (or
    // the root when no row is selected).
    void updateEditActions();

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
    QAction* m_newAction = nullptr;
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
    QAction* m_renameKeyAction = nullptr;
    QAction* m_addChildAction = nullptr;
    QAction* m_insertAfterAction = nullptr;
    QAction* m_editAsJsonAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_pasteAsChildAction = nullptr;
    QAction* m_duplicateAction = nullptr;
    QAction* m_moveUpAction = nullptr;
    QAction* m_moveDownAction = nullptr;
    QAction* m_sortKeysAction = nullptr;
    QAction* m_findReplaceAction = nullptr;
    QAction* m_expandAllAction = nullptr;
    QAction* m_collapseAllAction = nullptr;
    QAction* m_goToPathAction = nullptr;
    QAction* m_shortcutsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_reopenLastFileAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_lightThemeAction = nullptr;

    // External file-change watching (single-file documents only)
    QFileSystemWatcher* m_fileWatcher = nullptr;
    QTimer* m_fileChangeDebounce = nullptr;
    bool m_suppressWatchNotifications = false;

    // The file the running single-file load will install as.
    QString m_pendingLoadPath;

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
