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
#include <string>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "shell/export_handler.h"
#include "shell/file_loader.h"
#include "shell/filter_proxy_model.h"
#include "shell/recent_files_manager.h"
#include "shell/search_worker.h"
#include "shell/syntax_highlighter.h"
#include "shell/tree_model.h"

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
    void onUnionFiles();
    void onExportCsv();
    void onExportXml();
    void onSearchTextChanged(const QString& text);
    void onParseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root);
    void onParseError(QString errorMessage);
    void onProgressUpdated(int percentage);
    void onTreeSelectionChanged();
    void onRemoveFromUnion();
    void onRecentFileSelected(const QString& filePath);
    void executeSearch();
    void onSearchComplete(jsontitan::core::FilterResult result, uint64_t generation);
    void onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result);
    void onDeleteNode();
    void onSave();
    void onSaveAs();

private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupDropOverlay();
    void showDropOverlay();
    void hideDropOverlay();
    void showWelcomeMessage();
    void updateStatusBar(const QString& fileName, int nodeCount);
    int countNodes(const jsontitan::core::JsonNode& node) const;
    int countArenaNodes(const jsontitan::core::ArenaJsonNode& node) const;
    std::shared_ptr<const jsontitan::core::JsonNode> getSelectedNode() const;
    void setModified(bool modified);

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

    // Debounce timer for search
    QTimer* m_debounceTimer = nullptr;
    uint64_t m_searchGeneration = 0;

    // Background search worker
    SearchWorker* m_searchWorker = nullptr;
    QThread* m_searchThread = nullptr;

    // Syntax highlighting
    jsontitan::shell::SyntaxTheme m_syntaxTheme = jsontitan::shell::catppuccinMochaTheme();

    // Current data
    std::shared_ptr<const jsontitan::core::JsonNode> m_currentRoot;
    std::shared_ptr<jsontitan::core::ArenaParseResult> m_arenaResult;
    QString m_currentFileName;
    QString m_currentFilePath;
    bool m_isUnionMode = false;
    bool m_modified = false;
};
