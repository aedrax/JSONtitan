#include "shell/main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

#include <filesystem>

#include "core/parse_orchestrator.h"
#include "shell/drop_validator.h"
#include "shell/wait_cursor.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("JSONTitan");
    resize(1200, 800);
    setAcceptDrops(true);

    m_treeModel = new TreeModel(this);
    m_filterProxy = new FilterProxyModel(this);
    m_filterProxy->setSourceModel(m_treeModel);
    m_fileLoader = new FileLoader(this);

    setupMenuBar();
    setupCentralWidget();
    setupStatusBar();
    setupDropOverlay();

    // Document state holder — constructed after widget setup, wired with
    // raw model pointers (Qt parent ownership).
    m_session = new DocumentSession(m_treeModel, this);
    connect(m_session, &DocumentSession::modifiedChanged,
            this, &MainWindow::updateWindowTitle);

    // Search pipeline — owns the debounce timer, the worker thread, and the
    // generation counter; wired with raw widget/model pointers.
    m_searchController = new SearchController(
        SearchController::Ui{m_searchBar, m_caseSensitiveToggle, m_regexToggle,
                             m_searchErrorLabel, m_noResultsLabel, m_treeView,
                             m_matchCountLabel},
        m_treeModel, m_filterProxy, m_session, this);

    // Search shortcuts: Ctrl+F focuses the search bar, F3 / Shift+F3 step
    // through matches (Return in the search bar also steps forward).
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });
    auto* nextMatchShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(nextMatchShortcut, &QShortcut::activated,
            m_searchController, &SearchController::nextMatch);
    auto* prevMatchShortcut =
        new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(prevMatchShortcut, &QShortcut::activated,
            m_searchController, &SearchController::prevMatch);

    // Document-mutation flows (delete / save / unsaved-changes prompt)
    m_editController = new EditController(this, m_treeView, m_treeModel,
                                          m_filterProxy, m_session,
                                          m_searchController, this);
    connect(m_saveAction, &QAction::triggered,
            m_editController, &EditController::save);
    connect(m_saveAsAction, &QAction::triggered,
            m_editController, &EditController::saveAs);
    connect(m_deleteAction, &QAction::triggered,
            m_editController, &EditController::deleteSelectedNode);
    // Keep Edit > Delete's enabled state in sync with the tree selection.
    connect(m_treeView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                m_deleteAction->setEnabled(current.isValid());
            });

    // Multi-file union flows
    m_unionController = new UnionController(
        this,
        UnionController::Ui{m_treeView, m_welcomeLabel, m_noResultsLabel,
                            m_searchErrorLabel, m_searchBar, m_detailPanel},
        m_treeModel, m_filterProxy, m_session, m_searchController,
        m_editController, m_recentFilesManager, this);
    connect(m_unionAction, &QAction::triggered,
            m_unionController, &UnionController::unionFiles);
    connect(m_unionController, &UnionController::statusUpdated,
            this, &MainWindow::updateStatusBar);

    // Detail panel rendering for the current tree selection
    m_detailPresenter = new DetailPanelPresenter(
        m_detailPanel, m_treeView, m_filterProxy, m_treeModel, m_session, this);

    showWelcomeMessage();

    // Connect file loader signals
    connect(m_fileLoader, &FileLoader::progressUpdated,
            this, &MainWindow::onProgressUpdated);
    connect(m_fileLoader, &FileLoader::arenaParseComplete,
            this, &MainWindow::onArenaParseComplete);
    connect(m_fileLoader, &FileLoader::parseError,
            this, &MainWindow::onParseError);

    // Restore persisted window state (saved in closeEvent). First run: the
    // keys are absent and the resize(1200, 800) default above stands.
    QSettings settings;
    const QByteArray geometry =
        settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray windowState =
        settings.value(QStringLiteral("window/state")).toByteArray();
    if (!windowState.isEmpty()) {
        restoreState(windowState);
    }
    const QByteArray splitterState =
        settings.value(QStringLiteral("window/splitterState")).toByteArray();
    if (!splitterState.isEmpty()) {
        m_mainSplitter->restoreState(splitterState);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::openFromCliArgs(const std::vector<std::string>& filePaths) {
    if (filePaths.empty()) {
        return;
    }

    // Validate all files exist and are readable before loading anything
    for (const auto& path : filePaths) {
        std::filesystem::path fsPath(path);
        std::error_code ec;

        if (!std::filesystem::exists(fsPath, ec)) {
            QMessageBox::warning(this, tr("File Not Found"),
                tr("The file \"%1\" does not exist.")
                    .arg(QString::fromStdString(path)));
            return;
        }

        // Check readability by attempting to open the file
        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("File Not Readable"),
                tr("The file \"%1\" cannot be read: %2")
                    .arg(QString::fromStdString(path), file.errorString()));
            return;
        }
        file.close();
    }

    if (filePaths.size() == 1) {
        // Single file: use the same mechanism as File > Open
        QString qPath = QString::fromStdString(filePaths[0]);
        m_session->setFileIdentity(qPath, QFileInfo(qPath).fileName(), false);
        showLoadProgress();
        m_statusLabel->setText(tr("Parsing %1...").arg(m_session->fileName()));
        m_fileLoader->startParse(qPath);
    } else {
        // Multiple files: use the same union logic as File > Union Files
        QStringList qPaths;
        for (const auto& path : filePaths) {
            qPaths.append(QString::fromStdString(path));
        }
        m_unionController->loadUnionSynchronously(qPaths,
                                                  /*recordInRecentFiles=*/true);
    }
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_openAction = fileMenu->addAction(tr("&Open..."));
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Open a JSON file"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_unionAction = fileMenu->addAction(tr("&Union Files..."));
    m_unionAction->setStatusTip(
        tr("Combine several JSON files into a single tree"));

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    m_recentMenu->menuAction()->setStatusTip(
        tr("Reopen a recently opened file"));
    m_recentFilesManager = new RecentFilesManager(m_recentMenu, this);
    connect(m_recentFilesManager, &RecentFilesManager::recentFileSelected,
            this, &MainWindow::onRecentFileSelected);

    fileMenu->addSeparator();

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("Save the document to its current file"));

    m_saveAsAction = fileMenu->addAction(tr("Save &As..."));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("Save the document to a new file"));

    fileMenu->addSeparator();

    m_exportCsvAction = fileMenu->addAction(tr("Export &CSV..."));
    m_exportCsvAction->setStatusTip(
        tr("Export the selection (or the whole document) as CSV"));
    connect(m_exportCsvAction, &QAction::triggered, this, &MainWindow::onExportCsv);

    m_exportXmlAction = fileMenu->addAction(tr("Export &XML..."));
    m_exportXmlAction->setStatusTip(
        tr("Export the selection (or the whole document) as XML"));
    connect(m_exportXmlAction, &QAction::triggered, this, &MainWindow::onExportXml);

    fileMenu->addSeparator();

    // Session-restore option: persisted, default ON. CLI arguments always
    // take precedence over the restored file (see main.cpp).
    m_reopenLastFileAction = fileMenu->addAction(tr("Reopen Last File on Startup"));
    m_reopenLastFileAction->setStatusTip(
        tr("Automatically reopen the last file the next time JSONTitan starts"));
    m_reopenLastFileAction->setCheckable(true);
    m_reopenLastFileAction->setChecked(
        QSettings().value(QStringLiteral("session/reopenLastFile"), true).toBool());
    connect(m_reopenLastFileAction, &QAction::toggled, this, [](bool checked) {
        QSettings().setValue(QStringLiteral("session/reopenLastFile"), checked);
    });

    fileMenu->addSeparator();

    m_exitAction = fileMenu->addAction(tr("E&xit"));
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("Exit JSONTitan"));
    // close() (not QApplication::quit) so closeEvent runs the
    // unsaved-changes prompt before exiting.
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    m_deleteAction = editMenu->addAction(tr("&Delete"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setStatusTip(tr("Delete the selected node"));
    // Disabled until the tree has a valid current index; the trigger and the
    // enabled-state sync are wired in the constructor once EditController
    // and the tree's selection model exist.
    m_deleteAction->setEnabled(false);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    m_shortcutsAction = helpMenu->addAction(tr("&Keyboard Shortcuts"));
    m_shortcutsAction->setStatusTip(tr("Show the list of keyboard shortcuts"));
    connect(m_shortcutsAction, &QAction::triggered,
            this, &MainWindow::onShowKeyboardShortcuts);
    m_aboutAction = helpMenu->addAction(tr("&About JSONTitan"));
    m_aboutAction->setStatusTip(tr("Show version and build information"));
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onAbout() {
    QMessageBox::about(
        this, tr("About JSONTitan"),
        tr("<h3>JSONTitan %1</h3>"
           "<p>A fast viewer and editor for large JSON documents: "
           "background parsing, search with filtering, multi-file union, "
           "node deletion, and CSV/XML export.</p>"
           "<p>Built with Qt %2.</p>")
            .arg(QStringLiteral("0.1.0"),
                 QString::fromLatin1(qVersion())));
}

void MainWindow::onShowKeyboardShortcuts() {
    const QString rows =
        tr("<tr><td><b>Ctrl+O</b></td><td>Open file</td></tr>"
           "<tr><td><b>Ctrl+S</b></td><td>Save</td></tr>"
           "<tr><td><b>Ctrl+Shift+S</b></td><td>Save As</td></tr>"
           "<tr><td><b>Ctrl+F</b></td><td>Focus search bar</td></tr>"
           "<tr><td><b>Return</b></td><td>Next match (in search bar)</td></tr>"
           "<tr><td><b>F3</b></td><td>Next match</td></tr>"
           "<tr><td><b>Shift+F3</b></td><td>Previous match</td></tr>"
           "<tr><td><b>Del</b></td><td>Delete selected node</td></tr>"
           "<tr><td><b>Esc</b></td><td>Cancel load in progress</td></tr>"
           "<tr><td><b>Ctrl+Q</b></td><td>Exit</td></tr>");
    QMessageBox::information(
        this, tr("Keyboard Shortcuts"),
        QStringLiteral("<table cellspacing=\"8\">%1</table>").arg(rows));
}

void MainWindow::setupCentralWidget() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Search bar with toggle buttons in a horizontal layout
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(2);

    m_searchBar = new QLineEdit(centralWidget);
    m_searchBar->setObjectName("searchBar");
    m_searchBar->setPlaceholderText(tr("Search keys and values... (supports regex with /pattern/)"));
    m_searchBar->setToolTip(
        tr("Search (Ctrl+F). Return/F3: next match, Shift+F3: previous match"));
    searchLayout->addWidget(m_searchBar);

    // Match count ("N matches") — hidden while no search is active
    m_matchCountLabel = new QLabel(centralWidget);
    m_matchCountLabel->setObjectName("matchCountLabel");
    m_matchCountLabel->hide();
    searchLayout->addWidget(m_matchCountLabel);

    m_caseSensitiveToggle = new QToolButton(centralWidget);
    m_caseSensitiveToggle->setObjectName("caseSensitiveToggle");
    m_caseSensitiveToggle->setText(tr("Aa"));
    m_caseSensitiveToggle->setCheckable(true);
    m_caseSensitiveToggle->setChecked(false);
    m_caseSensitiveToggle->setToolTip(tr("Case Sensitive"));
    searchLayout->addWidget(m_caseSensitiveToggle);

    m_regexToggle = new QToolButton(centralWidget);
    m_regexToggle->setObjectName("regexToggle");
    m_regexToggle->setText(tr(".*"));
    m_regexToggle->setCheckable(true);
    m_regexToggle->setChecked(false);
    m_regexToggle->setToolTip(tr("Regex Mode"));
    searchLayout->addWidget(m_regexToggle);

    mainLayout->addLayout(searchLayout);

    // Search error label (hidden by default)
    m_searchErrorLabel = new QLabel(centralWidget);
    m_searchErrorLabel->setObjectName("searchErrorLabel");
    m_searchErrorLabel->hide();
    mainLayout->addWidget(m_searchErrorLabel);

    // Splitter for tree view and detail panel
    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    auto* splitter = m_mainSplitter;

    // Tree view area with overlay labels
    auto* treeContainer = new QWidget(splitter);
    auto* treeLayout = new QVBoxLayout(treeContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);

    m_treeView = new QTreeView(treeContainer);
    m_treeView->setModel(m_filterProxy);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* menu = new QMenu(m_treeView);
        // popup() menus are not deleted on dismissal; without this attribute
        // every right-click would leak a QMenu (and its actions) until exit.
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto* exportCsvAction = menu->addAction(tr("Export as CSV..."));
        connect(exportCsvAction, &QAction::triggered, this, &MainWindow::onExportCsv);
        auto* exportXmlAction = menu->addAction(tr("Export as XML..."));
        connect(exportXmlAction, &QAction::triggered, this, &MainWindow::onExportXml);

        menu->addSeparator();
        auto* deleteAction = menu->addAction(tr("Delete"));
        // Every row is deletable: top-level rows are the root's children,
        // not the root (the root itself is never a selectable row).
        QModelIndex proxyIndex = m_treeView->indexAt(pos);
        if (!proxyIndex.isValid()) {
            deleteAction->setEnabled(false);
        }
        connect(deleteAction, &QAction::triggered,
                m_editController, &EditController::deleteSelectedNode);

        if (m_session->isUnionMode()) {
            menu->addSeparator();
            auto* removeAction = menu->addAction(tr("Remove from Union"));
            connect(removeAction, &QAction::triggered,
                    m_unionController, &UnionController::removeFromUnion);
        }
        menu->popup(m_treeView->viewport()->mapToGlobal(pos));
    });
    treeLayout->addWidget(m_treeView);

    // "No results found" label (hidden by default)
    m_noResultsLabel = new QLabel(tr("No results found"), treeContainer);
    m_noResultsLabel->setObjectName("noResultsLabel");
    m_noResultsLabel->setAlignment(Qt::AlignCenter);
    m_noResultsLabel->hide();
    treeLayout->addWidget(m_noResultsLabel);

    // Welcome label (shown when no file is loaded)
    m_welcomeLabel = new QLabel(treeContainer);
    m_welcomeLabel->setObjectName("welcomeLabel");
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setWordWrap(true);
    m_welcomeLabel->setText(tr("Welcome to JSONTitan\n\n"
                               "Open a JSON file using File > Open\n"
                               "or combine multiple files with File > Union Files"));
    treeLayout->addWidget(m_welcomeLabel);

    splitter->addWidget(treeContainer);

    // Detail panel
    m_detailPanel = new QTextEdit(splitter);
    m_detailPanel->setObjectName("detailPanel");
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setPlaceholderText(tr("Select a node to view its full value"));

    splitter->addWidget(m_detailPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    setCentralWidget(centralWidget);
}

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addWidget(m_statusLabel, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFormat("%p%");
    m_progressBar->hide();
    statusBar()->addPermanentWidget(m_progressBar);

    m_cancelLoadButton = new QPushButton(tr("Cancel"), this);
    m_cancelLoadButton->setObjectName("cancelLoadButton");
    m_cancelLoadButton->setToolTip(tr("Cancel the current load (Esc)"));
    m_cancelLoadButton->hide();
    statusBar()->addPermanentWidget(m_cancelLoadButton);
    connect(m_cancelLoadButton, &QPushButton::clicked,
            this, &MainWindow::cancelActiveLoad);
}

void MainWindow::showLoadProgress() {
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_cancelLoadButton->show();
}

void MainWindow::hideLoadProgress() {
    m_progressBar->hide();
    m_cancelLoadButton->hide();
}

void MainWindow::cancelActiveLoad() {
    if (!m_progressBar->isVisible()) {
        return;
    }
    // The old document is untouched: parse results only install in
    // onArenaParseComplete, and cancelParse() invalidates the request id so
    // any in-flight result is dropped instead of forwarded.
    m_fileLoader->cancelParse();
    hideLoadProgress();
    m_statusLabel->setText(tr("Load cancelled"));
}

void MainWindow::setupDropOverlay() {
    m_dropOverlay = new QLabel(this);
    m_dropOverlay->setObjectName("dropOverlay");
    m_dropOverlay->setText(tr("Drop JSON file here"));
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    m_dropOverlay->hide();
}

void MainWindow::showDropOverlay() {
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->raise();
    m_dropOverlay->show();
}

void MainWindow::hideDropOverlay() {
    m_dropOverlay->hide();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Accept whenever at least one payload file is plausible so dropEvent
    // gets the chance to report precisely; explain rejections right away.
    auto result = DropValidator::validate(event->mimeData());
    if (result.accepted) {
        event->acceptProposedAction();
        showDropOverlay();
    } else {
        statusBar()->showMessage(result.rejectReason, 3000);
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    hideDropOverlay();
}

void MainWindow::dropEvent(QDropEvent* event) {
    hideDropOverlay();

    auto result = DropValidator::validate(event->mimeData());
    if (!result.accepted) {
        // Transient feedback on why the drop was rejected.
        statusBar()->showMessage(result.rejectReason, 5000);
        return;
    }

    // Ordering preserved: validate, confirm unsaved changes, then load.
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }

    event->acceptProposedAction();

    // Cancel any in-progress parse
    m_fileLoader->cancelParse();

    // Clear search bar, detail panel, and exit union mode
    m_searchBar->clear();
    m_searchErrorLabel->hide();
    m_detailPanel->clear();

    if (result.filePaths.size() > 1) {
        // Multiple files: same union flow as File > Union Files / CLI.
        m_unionController->loadUnionSynchronously(result.filePaths,
                                                  /*recordInRecentFiles=*/true);
        return;
    }

    const QString& filePath = result.filePaths.first();

    // Set current file name from the dropped file path and exit union mode
    m_session->setFileIdentity(filePath, QFileInfo(filePath).fileName(), false);

    // Show progress bar and start parsing
    showLoadProgress();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_session->fileName()));

    m_fileLoader->startParse(filePath);
}

void MainWindow::showWelcomeMessage() {
    m_welcomeLabel->show();
    m_treeView->hide();
    m_noResultsLabel->hide();
    m_statusLabel->setText(tr("Ready"));
}

void MainWindow::updateStatusBar(const QString& fileName, int nodeCount) {
    m_statusLabel->setText(tr("%1 — %2 nodes").arg(fileName).arg(nodeCount));
}

// --- Task 16.2: File Open and background parsing ---

void MainWindow::onOpenFile() {
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open JSON File"), QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    m_session->setFileIdentity(filePath, QFileInfo(filePath).fileName(), false);
    showLoadProgress();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_session->fileName()));

    m_fileLoader->startParse(filePath);
}

void MainWindow::onProgressUpdated(int percentage) {
    m_progressBar->setValue(percentage);
}

void MainWindow::onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result) {
    hideLoadProgress();

    m_searchController->invalidate();
    // File identity was already recorded at parse start; keep it.
    m_session->setArenaRoot(result, m_session->filePath(), m_session->fileName());
    m_filterProxy->clearFilter();

    // Show tree, hide welcome
    m_welcomeLabel->hide();
    m_treeView->show();
    m_noResultsLabel->hide();

    // Update status bar: the parse pipeline already counted the nodes, so a
    // full-tree walk here would be pure waste.
    int nodeCount = result && result->root ? static_cast<int>(result->nodeCount) : 0;
    updateStatusBar(m_session->fileName(), nodeCount);

    // Clear detail panel and search
    m_detailPanel->clear();
    m_searchBar->clear();
    m_searchErrorLabel->hide();

    // Clear modified flag on file open
    m_session->setModified(false);

    // Record file in recent files list
    if (!m_session->filePath().isEmpty()) {
        m_recentFilesManager->fileOpened(m_session->filePath());
        // Remember the last successfully opened single file for session
        // restore on the next launch (arena completions are always
        // single-file loads; unions never take this path).
        QSettings().setValue(QStringLiteral("session/lastFilePath"),
                             m_session->filePath());
    }
}

void MainWindow::restoreLastSession() {
    QSettings settings;
    if (!settings.value(QStringLiteral("session/reopenLastFile"), true).toBool()) {
        return;
    }
    const QString lastFile =
        settings.value(QStringLiteral("session/lastFilePath")).toString();
    // Broken or missing last file: fall back to the welcome screen silently.
    if (lastFile.isEmpty() || !QFile::exists(lastFile)) {
        return;
    }

    m_session->setFileIdentity(lastFile, QFileInfo(lastFile).fileName(), false);
    showLoadProgress();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_session->fileName()));
    m_fileLoader->startParse(lastFile);
}

void MainWindow::onParseError(QString errorMessage) {
    hideLoadProgress();
    m_statusLabel->setText(tr("Parse failed"));

    QMessageBox::critical(this, tr("Parse Error"),
                          tr("Failed to parse %1:\n\n%2")
                              .arg(m_session->fileName(), errorMessage));
}

void MainWindow::onRecentFileSelected(const QString& filePath) {
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, tr("File Not Found"),
            tr("The file \"%1\" no longer exists and will be removed from the recent files list.")
                .arg(filePath));
        m_recentFilesManager->removeFile(filePath);
        return;
    }

    if (!m_editController->confirmDiscardChanges()) {
        return;
    }

    m_session->setFileIdentity(filePath, QFileInfo(filePath).fileName(), false);
    showLoadProgress();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_session->fileName()));
    m_fileLoader->startParse(filePath);
}

// --- Task 16.5: Export actions and detail panel ---

void MainWindow::onExportCsv() {
    // Export the selection if one resolves, otherwise the live root —
    // uniformly over both backings via NodeView (no deep copies).
    auto node = m_detailPresenter->selectedNodeView();
    if (!node) {
        node = m_session->rootView();
    }
    if (!node) {
        QMessageBox::information(this, tr("Export CSV"),
                                 tr("No data to export. Please open a file first."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export CSV"), QString(),
        tr("CSV Files (*.csv);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    jsontitan::shell::WaitCursorGuard waitCursor;
    QString error = ExportHandler::exportCsvToFile(*node, filePath);
    waitCursor.restore();
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), error);
    } else {
        m_statusLabel->setText(tr("Exported CSV to %1").arg(QFileInfo(filePath).fileName()));
    }
}

void MainWindow::onExportXml() {
    // Export the selection if one resolves, otherwise the live root —
    // uniformly over both backings via NodeView (no deep copies).
    auto node = m_detailPresenter->selectedNodeView();
    if (!node) {
        node = m_session->rootView();
    }
    if (!node) {
        QMessageBox::information(this, tr("Export XML"),
                                 tr("No data to export. Please open a file first."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export XML"), QString(),
        tr("XML Files (*.xml);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    jsontitan::shell::WaitCursorGuard waitCursor;
    QString error = ExportHandler::exportXmlToFile(*node, filePath);
    waitCursor.restore();
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), error);
    } else {
        m_statusLabel->setText(tr("Exported XML to %1").arg(QFileInfo(filePath).fileName()));
    }
}

// --- Task 7.1: Modified flag and title management ---

void MainWindow::updateWindowTitle(bool modified) {
    QString title = QStringLiteral("JSONTitan");
    if (!m_session->fileName().isEmpty()) {
        title = m_session->fileName() + QStringLiteral(" — JSONTitan");
    }
    if (modified) {
        title = QStringLiteral("*") + title;
    }
    setWindowTitle(title);
}

// --- Task 6.3: Key press event override ---

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        m_editController->deleteSelectedNode();
        return;
    }
    // Esc cancels an in-progress load — only while the progress UI is up.
    if (event->key() == Qt::Key_Escape && m_progressBar->isVisible()) {
        cancelActiveLoad();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// --- Task 7.2: Close event override ---

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_editController->confirmDiscardChanges()) {
        QSettings settings;
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState());
        settings.setValue(QStringLiteral("window/splitterState"),
                          m_mainSplitter->saveState());
        event->accept();
    } else {
        event->ignore();
    }
}
