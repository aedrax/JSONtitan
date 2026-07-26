#include "shell/main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QVBoxLayout>

#include <filesystem>

#include "core/deletion_engine.h"
#include "core/parser.h"
#include "core/parse_orchestrator.h"
#include "core/pretty_printer.h"
#include "core/search_engine.h"
#include "core/token_emitter.h"
#include "core/union_engine.h"
#include "shell/drop_validator.h"
#include "shell/save_handler.h"
#include "shell/syntax_highlighter.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("JSONTitan");
    resize(1200, 800);
    setAcceptDrops(true);

    m_treeModel = new TreeModel(this);
    m_filterProxy = new FilterProxyModel(this);
    m_filterProxy->setSourceModel(m_treeModel);
    m_fileLoader = new FileLoader(this);

    // Initialize debounce timer (single-shot, 250ms)
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(250);
    connect(m_debounceTimer, &QTimer::timeout, this, &MainWindow::executeSearch);

    // Initialize background search worker on dedicated thread
    m_searchThread = new QThread(this);
    m_searchWorker = new SearchWorker();
    m_searchWorker->moveToThread(m_searchThread);

    connect(m_searchWorker, &SearchWorker::searchComplete,
            this, &MainWindow::onSearchComplete, Qt::QueuedConnection);

    m_searchThread->start();

    setupMenuBar();
    setupCentralWidget();
    setupStatusBar();
    setupDropOverlay();
    showWelcomeMessage();

    // Connect file loader signals
    connect(m_fileLoader, &FileLoader::progressUpdated,
            this, &MainWindow::onProgressUpdated);
    connect(m_fileLoader, &FileLoader::arenaParseComplete,
            this, &MainWindow::onArenaParseComplete);
    connect(m_fileLoader, &FileLoader::parseError,
            this, &MainWindow::onParseError);
}

MainWindow::~MainWindow() {
    m_searchThread->quit();
    m_searchThread->wait();
    delete m_searchWorker;
}

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
        m_isUnionMode = false;
        m_currentFileName = QFileInfo(qPath).fileName();
        m_currentFilePath = qPath;
        m_progressBar->setValue(0);
        m_progressBar->show();
        m_statusLabel->setText(tr("Parsing %1...").arg(m_currentFileName));
        m_fileLoader->startParse(qPath);
    } else {
        // Multiple files: use the same union logic as File > Union Files
        std::vector<jsontitan::core::FileEntry> entries;

        for (const auto& path : filePaths) {
            QString qPath = QString::fromStdString(path);
            QFile file(qPath);
            if (!file.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("File Error"),
                    tr("Cannot open file: %1").arg(qPath));
                return;
            }

            QByteArray data = file.readAll();
            file.close();

            auto state = jsontitan::core::makeParserState();
            auto chunk = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(data.constData()),
                static_cast<std::size_t>(data.size()));

            auto chunkResult = jsontitan::core::parseChunk(*state, chunk);
            if (chunkResult.error) {
                QMessageBox::warning(this, tr("Parse Error"),
                    tr("Failed to parse %1:\n\n%2")
                        .arg(QFileInfo(qPath).fileName(),
                             QString::fromStdString(chunkResult.error->description)));
                return;
            }

            auto parseResult = jsontitan::core::finalizeParse(*chunkResult.nextState);
            if (parseResult.error) {
                QMessageBox::warning(this, tr("Parse Error"),
                    tr("Failed to parse %1:\n\n%2")
                        .arg(QFileInfo(qPath).fileName(),
                             QString::fromStdString(parseResult.error->description)));
                return;
            }

            jsontitan::core::FileEntry entry;
            entry.filename = QFileInfo(qPath).fileName().toStdString();
            entry.root = parseResult.root;
            entries.push_back(std::move(entry));
        }

        auto unionRoot = jsontitan::core::unionTrees(entries);

        m_isUnionMode = true;
        m_currentRoot = unionRoot;
        m_arenaResult.reset();  // Search must target the union, not a previously opened file
        m_currentFileName = tr("Union (%1 files)").arg(filePaths.size());

        invalidateActiveSearch();
        m_treeModel->setRootNode(unionRoot);
        m_filterProxy->clearFilter();

        m_welcomeLabel->hide();
        m_treeView->show();
        m_noResultsLabel->hide();

        int nodeCount = unionRoot ? countNodes(*unionRoot) : 0;
        updateStatusBar(m_currentFileName, nodeCount);

        m_detailPanel->clear();
        m_searchBar->clear();
        m_searchErrorLabel->hide();

        // Record all files in recent files list
        for (const auto& path : filePaths) {
            m_recentFilesManager->fileOpened(QString::fromStdString(path));
        }
    }
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_openAction = fileMenu->addAction(tr("&Open..."));
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_unionAction = fileMenu->addAction(tr("&Union Files..."));
    connect(m_unionAction, &QAction::triggered, this, &MainWindow::onUnionFiles);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    m_recentFilesManager = new RecentFilesManager(m_recentMenu, this);
    connect(m_recentFilesManager, &RecentFilesManager::recentFileSelected,
            this, &MainWindow::onRecentFileSelected);

    fileMenu->addSeparator();

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSave);

    m_saveAsAction = fileMenu->addAction(tr("Save &As..."));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    fileMenu->addSeparator();

    m_exportCsvAction = fileMenu->addAction(tr("Export &CSV..."));
    connect(m_exportCsvAction, &QAction::triggered, this, &MainWindow::onExportCsv);

    m_exportXmlAction = fileMenu->addAction(tr("Export &XML..."));
    connect(m_exportXmlAction, &QAction::triggered, this, &MainWindow::onExportXml);

    fileMenu->addSeparator();

    m_exitAction = fileMenu->addAction(tr("E&xit"));
    m_exitAction->setShortcut(QKeySequence::Quit);
    // close() (not QApplication::quit) so closeEvent runs the
    // unsaved-changes prompt before exiting.
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::close);

    menuBar()->addMenu(tr("&Edit"));
    menuBar()->addMenu(tr("&Help"));
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
    connect(m_searchBar, &QLineEdit::textChanged,
            this, &MainWindow::onSearchTextChanged);
    searchLayout->addWidget(m_searchBar);

    m_caseSensitiveToggle = new QToolButton(centralWidget);
    m_caseSensitiveToggle->setObjectName("caseSensitiveToggle");
    m_caseSensitiveToggle->setText(tr("Aa"));
    m_caseSensitiveToggle->setCheckable(true);
    m_caseSensitiveToggle->setChecked(false);
    m_caseSensitiveToggle->setToolTip(tr("Case Sensitive"));
    connect(m_caseSensitiveToggle, &QToolButton::toggled,
            this, [this]() { if (!m_searchBar->text().isEmpty()) m_debounceTimer->start(); });
    searchLayout->addWidget(m_caseSensitiveToggle);

    m_regexToggle = new QToolButton(centralWidget);
    m_regexToggle->setObjectName("regexToggle");
    m_regexToggle->setText(tr(".*"));
    m_regexToggle->setCheckable(true);
    m_regexToggle->setChecked(false);
    m_regexToggle->setToolTip(tr("Regex Mode"));
    connect(m_regexToggle, &QToolButton::toggled,
            this, [this]() { if (!m_searchBar->text().isEmpty()) m_debounceTimer->start(); });
    searchLayout->addWidget(m_regexToggle);

    mainLayout->addLayout(searchLayout);

    // Search error label (hidden by default)
    m_searchErrorLabel = new QLabel(centralWidget);
    m_searchErrorLabel->setObjectName("searchErrorLabel");
    m_searchErrorLabel->hide();
    mainLayout->addWidget(m_searchErrorLabel);

    // Splitter for tree view and detail panel
    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget);

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
        // Disable Delete when root node is selected (no parent index)
        QModelIndex proxyIndex = m_treeView->indexAt(pos);
        if (proxyIndex.isValid()) {
            QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
            if (!sourceIndex.parent().isValid()) {
                // This is the root node — disable delete
                deleteAction->setEnabled(false);
            }
        } else {
            deleteAction->setEnabled(false);
        }
        connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteNode);

        if (m_isUnionMode) {
            menu->addSeparator();
            auto* removeAction = menu->addAction(tr("Remove from Union"));
            connect(removeAction, &QAction::triggered, this, &MainWindow::onRemoveFromUnion);
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

    // Connect tree selection changes
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTreeSelectionChanged);
}

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFormat("%p%");
    m_progressBar->hide();
    statusBar()->addPermanentWidget(m_progressBar);
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
    auto result = DropValidator::validate(event->mimeData());
    if (result.accepted) {
        event->acceptProposedAction();
        showDropOverlay();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    hideDropOverlay();
}

void MainWindow::dropEvent(QDropEvent* event) {
    hideDropOverlay();

    auto result = DropValidator::validate(event->mimeData());
    if (!result.accepted) {
        return;
    }

    if (!confirmDiscardChanges()) {
        return;
    }

    event->acceptProposedAction();

    // Cancel any in-progress parse
    m_fileLoader->cancelParse();

    // Clear search bar, detail panel, and exit union mode
    m_searchBar->clear();
    m_searchErrorLabel->hide();
    m_detailPanel->clear();
    m_isUnionMode = false;

    // Set current file name from the dropped file path
    m_currentFileName = QFileInfo(result.filePath).fileName();
    m_currentFilePath = result.filePath;

    // Show progress bar and start parsing
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_currentFileName));

    m_fileLoader->startParse(result.filePath);
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

int MainWindow::countNodes(const jsontitan::core::JsonNode& node) const {
    int count = 1;
    for (const auto& child : node.children) {
        count += countNodes(*child);
    }
    return count;
}

int MainWindow::countArenaNodes(const jsontitan::core::ArenaJsonNode& node) const {
    int count = 1;
    for (std::size_t i = 0; i < node.childCount; ++i) {
        count += countArenaNodes(*node.children[i]);
    }
    return count;
}

std::shared_ptr<const jsontitan::core::JsonNode> MainWindow::getSelectedNode() const {
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return m_currentRoot;
    }

    // Map proxy index back to source model to get the JsonNode
    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return m_currentRoot;
    }

    // Get the raw JsonNode pointer from the TreeModel
    const jsontitan::core::JsonNode* nodePtr = m_treeModel->jsonNodeForIndex(sourceIndex);
    if (!nodePtr) {
        return m_currentRoot;
    }

    // Find the shared_ptr that owns this node by walking the tree
    std::function<std::shared_ptr<const jsontitan::core::JsonNode>(
        const std::shared_ptr<const jsontitan::core::JsonNode>&,
        const jsontitan::core::JsonNode*)> findNode;

    findNode = [&findNode](const std::shared_ptr<const jsontitan::core::JsonNode>& current,
                           const jsontitan::core::JsonNode* target)
        -> std::shared_ptr<const jsontitan::core::JsonNode> {
        if (current.get() == target) {
            return current;
        }
        for (const auto& child : current->children) {
            auto result = findNode(child, target);
            if (result) {
                return result;
            }
        }
        return nullptr;
    };

    if (m_currentRoot) {
        return findNode(m_currentRoot, nodePtr);
    }
    return m_currentRoot;
}

// --- Task 16.2: File Open and background parsing ---

void MainWindow::onOpenFile() {
    if (!confirmDiscardChanges()) {
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open JSON File"), QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    m_isUnionMode = false;
    m_currentFileName = QFileInfo(filePath).fileName();
    m_currentFilePath = filePath;
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_currentFileName));

    m_fileLoader->startParse(filePath);
}

void MainWindow::onProgressUpdated(int percentage) {
    m_progressBar->setValue(percentage);
}

void MainWindow::onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result) {
    m_progressBar->hide();
    m_arenaResult = result;
    m_currentRoot.reset();  // Clear legacy root when using arena path

    invalidateActiveSearch();
    m_treeModel->setArenaRoot(result);
    m_filterProxy->clearFilter();

    // Show tree, hide welcome
    m_welcomeLabel->hide();
    m_treeView->show();
    m_noResultsLabel->hide();

    // Update status bar
    int nodeCount = result && result->root ? countArenaNodes(*result->root) : 0;
    updateStatusBar(m_currentFileName, nodeCount);

    // Clear detail panel and search
    m_detailPanel->clear();
    m_searchBar->clear();
    m_searchErrorLabel->hide();

    // Clear modified flag on file open
    setModified(false);

    // Record file in recent files list
    if (!m_currentFilePath.isEmpty()) {
        m_recentFilesManager->fileOpened(m_currentFilePath);
    }
}

void MainWindow::onParseError(QString errorMessage) {
    m_progressBar->hide();
    m_statusLabel->setText(tr("Parse failed"));

    QMessageBox::critical(this, tr("Parse Error"),
                          tr("Failed to parse %1:\n\n%2")
                              .arg(m_currentFileName, errorMessage));
}

void MainWindow::onRecentFileSelected(const QString& filePath) {
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, tr("File Not Found"),
            tr("The file \"%1\" no longer exists and will be removed from the recent files list.")
                .arg(filePath));
        m_recentFilesManager->removeFile(filePath);
        return;
    }

    if (!confirmDiscardChanges()) {
        return;
    }

    m_isUnionMode = false;
    m_currentFileName = QFileInfo(filePath).fileName();
    m_currentFilePath = filePath;
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_statusLabel->setText(tr("Parsing %1...").arg(m_currentFileName));
    m_fileLoader->startParse(filePath);
}

// --- Task 16.3: Search bar wiring ---

void MainWindow::onSearchTextChanged(const QString& text) {
    m_searchErrorLabel->hide();
    m_noResultsLabel->hide();

    if (text.isEmpty()) {
        // Clear filter immediately — no debounce needed. Also invalidate any
        // in-flight search so its result cannot re-apply the filter afterwards.
        invalidateActiveSearch();
        m_filterProxy->clearFilter();
        m_treeView->show();
        return;
    }

    if (!m_currentRoot && !m_arenaResult) {
        return;
    }

    // Restart debounce timer — coalesces rapid keystrokes
    m_debounceTimer->start();
}

void MainWindow::invalidateActiveSearch() {
    // Any in-flight worker result now fails the generation check in
    // onSearchComplete, and no debounced search fires against stale UI state.
    ++m_searchGeneration;
    m_debounceTimer->stop();
}

void MainWindow::executeSearch() {
    QString text = m_searchBar->text();
    if (text.isEmpty() || (!m_currentRoot && !m_arenaResult)) {
        return;
    }

    // Increment generation counter to track this search request
    ++m_searchGeneration;

    // Build SearchQuery from current UI state
    jsontitan::core::SearchQuery query;
    query.caseSensitive = m_caseSensitiveToggle->isChecked();

    if (m_regexToggle->isChecked()) {
        // Regex toggle is ON: treat entire text as regex pattern
        query.pattern = text.toStdString();
        query.mode = jsontitan::core::SearchMode::Regex;
    } else if (text.startsWith('/') && text.endsWith('/') && text.size() > 2) {
        // Regex toggle is OFF but /pattern/ convention used: fallback for discoverability
        query.pattern = text.mid(1, text.size() - 2).toStdString();
        query.mode = jsontitan::core::SearchMode::Regex;
    } else {
        // Default: substring search
        query.pattern = text.toStdString();
        query.mode = jsontitan::core::SearchMode::Substring;
    }

    // Dispatch search to background worker — use arena path when available
    if (m_arenaResult) {
        QMetaObject::invokeMethod(m_searchWorker, "executeArenaSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<jsontitan::core::ArenaParseResult>, m_arenaResult),
                                  Q_ARG(uint64_t, m_searchGeneration));
    } else {
        QMetaObject::invokeMethod(m_searchWorker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, m_currentRoot),
                                  Q_ARG(uint64_t, m_searchGeneration));
    }
}

void MainWindow::onSearchComplete(jsontitan::core::FilterResult result, uint64_t generation) {
    // Discard stale results from previous searches
    if (generation != m_searchGeneration) {
        return;
    }

    if (result.error) {
        m_searchErrorLabel->setText(
            QString::fromStdString(result.error->description));
        m_searchErrorLabel->show();
        return;
    }

    if (result.matches.empty()) {
        m_filterProxy->applyFilter(result);
        m_noResultsLabel->show();
        m_treeView->hide();
    } else {
        m_noResultsLabel->hide();
        m_treeView->show();
        m_filterProxy->applyFilter(result);
    }
}

// --- Task 16.4: Multi-file union ---

void MainWindow::onUnionFiles() {
    if (!confirmDiscardChanges()) {
        return;
    }

    QStringList filePaths = QFileDialog::getOpenFileNames(
        this, tr("Select JSON Files to Union"), QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePaths.isEmpty()) {
        return;
    }

    // Parse each file synchronously for union (they should be small enough)
    // For large files, a more sophisticated approach would be needed.
    std::vector<jsontitan::core::FileEntry> entries;

    for (const auto& path : filePaths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("File Error"),
                                  tr("Cannot open file: %1").arg(path));
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        // Parse using the core parser
        auto state = jsontitan::core::makeParserState();
        auto chunk = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data.constData()),
            static_cast<std::size_t>(data.size()));

        auto chunkResult = jsontitan::core::parseChunk(*state, chunk);
        if (chunkResult.error) {
            QMessageBox::critical(this, tr("Parse Error"),
                                  tr("Failed to parse %1:\n\n%2")
                                      .arg(QFileInfo(path).fileName(),
                                           QString::fromStdString(chunkResult.error->description)));
            return;
        }

        auto parseResult = jsontitan::core::finalizeParse(*chunkResult.nextState);
        if (parseResult.error) {
            QMessageBox::critical(this, tr("Parse Error"),
                                  tr("Failed to parse %1:\n\n%2")
                                      .arg(QFileInfo(path).fileName(),
                                           QString::fromStdString(parseResult.error->description)));
            return;
        }

        jsontitan::core::FileEntry entry;
        entry.filename = QFileInfo(path).fileName().toStdString();
        entry.root = parseResult.root;
        entries.push_back(std::move(entry));
    }

    // Union the trees
    auto unionRoot = jsontitan::core::unionTrees(entries);

    m_isUnionMode = true;
    m_currentRoot = unionRoot;
    m_arenaResult.reset();  // Search must target the union, not a previously opened file
    m_currentFileName = tr("Union (%1 files)").arg(filePaths.size());

    invalidateActiveSearch();
    m_treeModel->setRootNode(unionRoot);
    m_filterProxy->clearFilter();

    m_welcomeLabel->hide();
    m_treeView->show();
    m_noResultsLabel->hide();

    int nodeCount = unionRoot ? countNodes(*unionRoot) : 0;
    updateStatusBar(m_currentFileName, nodeCount);

    m_detailPanel->clear();
    m_searchBar->clear();
    m_searchErrorLabel->hide();
}

void MainWindow::onRemoveFromUnion() {
    if (!m_isUnionMode || !m_currentRoot) {
        return;
    }

    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return;
    }

    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return;
    }

    // Only allow removal of top-level children (direct children of union root)
    if (sourceIndex.parent().isValid()) {
        QMessageBox::information(this, tr("Remove from Union"),
                                 tr("Please select a top-level file node to remove."));
        return;
    }

    // Get the key of the selected node
    const auto* nodePtr = m_treeModel->jsonNodeForIndex(sourceIndex);
    if (!nodePtr) {
        return;
    }

    std::string filenameKey = nodePtr->key;

    auto newRoot = jsontitan::core::removeFromUnion(*m_currentRoot, filenameKey);
    m_currentRoot = newRoot;
    invalidateActiveSearch();
    m_treeModel->setRootNode(newRoot);
    m_filterProxy->clearFilter();

    int nodeCount = newRoot ? countNodes(*newRoot) : 0;
    updateStatusBar(m_currentFileName, nodeCount);

    m_detailPanel->clear();
}

// --- Task 16.5: Export actions and detail panel ---

void MainWindow::onExportCsv() {
    // Check for arena-backed case first
    QModelIndex proxyIndex = m_treeView->currentIndex();
    QModelIndex sourceIndex = proxyIndex.isValid()
        ? m_filterProxy->mapToSource(proxyIndex)
        : QModelIndex();

    const jsontitan::core::ArenaJsonNode* arenaNode = nullptr;
    if (sourceIndex.isValid()) {
        arenaNode = m_treeModel->arenaNodeForIndex(sourceIndex);
    } else if (m_arenaResult && m_arenaResult->root) {
        arenaNode = m_arenaResult->root;
    }

    if (arenaNode) {
        // Arena-backed path: convert only the selected subtree on-demand
        QString filePath = QFileDialog::getSaveFileName(
            this, tr("Export CSV"), QString(),
            tr("CSV Files (*.csv);;All Files (*)"));

        if (filePath.isEmpty()) {
            return;
        }

        QString error = ExportHandler::exportCsvToFile(*arenaNode, filePath);
        if (!error.isEmpty()) {
            QMessageBox::critical(this, tr("Export Error"), error);
        } else {
            m_statusLabel->setText(tr("Exported CSV to %1").arg(QFileInfo(filePath).fileName()));
        }
        return;
    }

    // Legacy JsonNode path
    auto node = getSelectedNode();
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

    QString error = ExportHandler::exportCsvToFile(*node, filePath);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), error);
    } else {
        m_statusLabel->setText(tr("Exported CSV to %1").arg(QFileInfo(filePath).fileName()));
    }
}

void MainWindow::onExportXml() {
    // Check for arena-backed case first
    QModelIndex proxyIndex = m_treeView->currentIndex();
    QModelIndex sourceIndex = proxyIndex.isValid()
        ? m_filterProxy->mapToSource(proxyIndex)
        : QModelIndex();

    const jsontitan::core::ArenaJsonNode* arenaNode = nullptr;
    if (sourceIndex.isValid()) {
        arenaNode = m_treeModel->arenaNodeForIndex(sourceIndex);
    } else if (m_arenaResult && m_arenaResult->root) {
        arenaNode = m_arenaResult->root;
    }

    if (arenaNode) {
        // Arena-backed path: convert only the selected subtree on-demand
        QString filePath = QFileDialog::getSaveFileName(
            this, tr("Export XML"), QString(),
            tr("XML Files (*.xml);;All Files (*)"));

        if (filePath.isEmpty()) {
            return;
        }

        QString error = ExportHandler::exportXmlToFile(*arenaNode, filePath);
        if (!error.isEmpty()) {
            QMessageBox::critical(this, tr("Export Error"), error);
        } else {
            m_statusLabel->setText(tr("Exported XML to %1").arg(QFileInfo(filePath).fileName()));
        }
        return;
    }

    // Legacy JsonNode path
    auto node = getSelectedNode();
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

    QString error = ExportHandler::exportXmlToFile(*node, filePath);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), error);
    } else {
        m_statusLabel->setText(tr("Exported XML to %1").arg(QFileInfo(filePath).fileName()));
    }
}

void MainWindow::onTreeSelectionChanged() {
    auto node = getSelectedNode();
    if (node) {
        // Legacy path (unchanged)
        jsontitan::core::PrettyPrintOptions opts;
        opts.maxOutputSize = 65536;  // 64 KB limit

        auto tokenResult = jsontitan::core::emitTokens(*node, opts);
        jsontitan::shell::renderHighlighted(m_detailPanel, tokenResult, m_syntaxTheme);
        return;
    }

    // Arena fallback path
    if (m_arenaResult) {
        QModelIndex proxyIndex = m_treeView->currentIndex();
        if (proxyIndex.isValid()) {
            QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
            const jsontitan::core::ArenaJsonNode* arenaNode =
                m_treeModel->arenaNodeForIndex(sourceIndex);
            if (arenaNode) {
                auto jsonNode = arenaNode->toJsonNode();
                jsontitan::core::PrettyPrintOptions opts;
                opts.maxOutputSize = 65536;  // 64 KB limit

                auto tokenResult = jsontitan::core::emitTokens(*jsonNode, opts);
                jsontitan::shell::renderHighlighted(m_detailPanel, tokenResult, m_syntaxTheme);
                return;
            }
        }
    }

    // No node resolved from either path
    m_detailPanel->clear();
}

// --- Helper: Convert arena tree to editable JsonNode tree ---

bool MainWindow::ensureEditableRoot() {
    if (m_currentRoot) {
        return true;  // Already have an editable root
    }

    if (!m_arenaResult || !m_arenaResult->root) {
        return false;  // Nothing to convert
    }

    // Convert the arena tree to a JsonNode tree (deep copy)
    // Do NOT reset the tree model here — callers that modify the tree
    // will call setRootNode() themselves with the new tree.
    m_currentRoot = m_arenaResult->root->toJsonNode();
    m_arenaResult.reset();

    return true;
}

// --- Task 7.1: Modified flag and title management ---

void MainWindow::setModified(bool modified) {
    m_modified = modified;
    QString title = QStringLiteral("JSONTitan");
    if (!m_currentFileName.isEmpty()) {
        title = m_currentFileName + QStringLiteral(" — JSONTitan");
    }
    if (m_modified) {
        title = QStringLiteral("*") + title;
    }
    setWindowTitle(title);
}

// --- Task 6.2: Delete node implementation ---

void MainWindow::onDeleteNode() {
    // Guard: no-op if nothing loaded at all
    if (!m_currentRoot && !m_arenaResult) {
        return;
    }

    // Guard: no-op if no selection
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return;
    }

    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return;
    }

    // Guard: no-op if root is selected (no parent in source model)
    if (!sourceIndex.parent().isValid()) {
        return;
    }

    // Compute the NodePath by walking up the QModelIndex parent chain.
    // This works regardless of whether the model is arena-backed or JsonNode-backed.
    jsontitan::core::NodePath path;
    QModelIndex walkIndex = sourceIndex;
    while (walkIndex.isValid() && walkIndex.parent().isValid()) {
        QModelIndex parentIndex = walkIndex.parent();
        // Determine if the parent is an Object or Array
        jsontitan::core::NodeType parentType = jsontitan::core::NodeType::Object;
        if (auto* jn = m_treeModel->jsonNodeForIndex(parentIndex)) {
            parentType = jn->type;
        } else if (auto* an = m_treeModel->arenaNodeForIndex(parentIndex)) {
            parentType = an->type;
        }

        if (parentType == jsontitan::core::NodeType::Array) {
            path.insert(path.begin(), static_cast<std::size_t>(walkIndex.row()));
        } else {
            // Object: get the key from the child node
            std::string key;
            if (auto* jn = m_treeModel->jsonNodeForIndex(walkIndex)) {
                key = jn->key;
            } else if (auto* an = m_treeModel->arenaNodeForIndex(walkIndex)) {
                key = std::string(an->keyView());
            }
            path.insert(path.begin(), key);
        }
        walkIndex = parentIndex;
    }

    if (path.empty()) {
        return;
    }

    // Get child count for confirmation dialog (before conversion)
    std::size_t directChildCount = 0;
    std::size_t descendantCount = 0;
    if (auto* jn = m_treeModel->jsonNodeForIndex(sourceIndex)) {
        directChildCount = jn->children.size();
        if (directChildCount > 10) {
            descendantCount = jsontitan::core::countDescendants(*jn);
        }
    } else if (auto* an = m_treeModel->arenaNodeForIndex(sourceIndex)) {
        directChildCount = an->childCount;
        if (directChildCount > 10) {
            // Convert to JsonNode just to count (or count arena nodes directly)
            auto tempNode = an->toJsonNode();
            descendantCount = jsontitan::core::countDescendants(*tempNode);
        }
    }

    // Confirmation dialog if node has >10 direct children
    if (directChildCount > 10) {
        auto reply = QMessageBox::question(
            this, tr("Confirm Deletion"),
            tr("This node has %1 descendants. Are you sure you want to delete it?")
                .arg(descendantCount),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    // Remember the current position for selection restoration
    int deletedRow = sourceIndex.row();
    QModelIndex parentSourceIndex = sourceIndex.parent();

    // Convert arena tree to editable JsonNode tree if needed
    if (!ensureEditableRoot()) {
        return;
    }

    // Perform the deletion
    auto newTree = jsontitan::core::deleteNode(m_currentRoot, path);
    if (!newTree) {
        return;  // Shouldn't happen since we guard against root deletion
    }
    m_currentRoot = newTree;
    invalidateActiveSearch();
    m_treeModel->setRootNode(newTree);
    m_filterProxy->clearFilter();

    // Set modified flag
    setModified(true);

    // Restore selection: try next sibling, then previous sibling, then parent
    // After model reset, we need to re-resolve the parent index
    // The parent path is everything except the last segment
    QModelIndex newParentIndex;  // invalid = root
    // Walk down from root to find the parent
    QModelIndex current;  // starts as invalid (root)
    bool walkComplete = true;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        // The model was just reset, so nothing is fetched yet: rows must be
        // fetched BEFORE the by-key scan below, or rowCount is 0, the key is
        // never found, and the walk silently stops at the wrong level.
        while (m_treeModel->canFetchMore(current)) {
            m_treeModel->fetchMore(current);
        }

        int row = -1;
        if (auto* idx = std::get_if<std::size_t>(&path[i])) {
            row = static_cast<int>(*idx);
        } else {
            // Find the row by key
            auto* keyStr = std::get_if<std::string>(&path[i]);
            int rowCount = m_treeModel->rowCount(current);
            for (int r = 0; r < rowCount; ++r) {
                QModelIndex childIdx = m_treeModel->index(r, 0, current);
                if (auto* jn = m_treeModel->jsonNodeForIndex(childIdx)) {
                    if (jn->key == *keyStr) {
                        row = r;
                        break;
                    }
                }
            }
        }
        if (row < 0) {
            walkComplete = false;
            break;
        }
        current = m_treeModel->index(row, 0, current);
        if (!current.isValid()) {
            walkComplete = false;
            break;
        }
    }
    if (!walkComplete) {
        // The parent could not be re-resolved; selecting deletedRow under
        // the wrong (shallower) parent would highlight an unrelated node.
        return;
    }
    newParentIndex = current;

    // Ensure parent's children are fetched
    while (m_treeModel->canFetchMore(newParentIndex)) {
        m_treeModel->fetchMore(newParentIndex);
    }

    int parentRowCount = m_treeModel->rowCount(newParentIndex);
    QModelIndex newSourceIndex;
    if (deletedRow < parentRowCount) {
        newSourceIndex = m_treeModel->index(deletedRow, 0, newParentIndex);
    } else if (deletedRow > 0) {
        newSourceIndex = m_treeModel->index(deletedRow - 1, 0, newParentIndex);
    } else {
        newSourceIndex = newParentIndex;
    }

    if (newSourceIndex.isValid()) {
        QModelIndex newProxyIndex = m_filterProxy->mapFromSource(newSourceIndex);
        if (newProxyIndex.isValid()) {
            m_treeView->setCurrentIndex(newProxyIndex);
        }
    }
}

// --- Task 6.3: Key press event override ---

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        onDeleteNode();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// --- Task 7.2: Close event override ---

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}

// Returns true when it is safe to discard the current document: either there
// are no unsaved changes, or the user chose Save (and it succeeded) or
// Discard. Returns false when the user cancelled.
bool MainWindow::confirmDiscardChanges() {
    if (!m_modified) {
        return true;
    }

    auto reply = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (reply == QMessageBox::Save) {
        onSave();
        // If still modified after save attempt (e.g., user cancelled the
        // save-as dialog or the write failed), don't discard.
        return !m_modified;
    }
    return reply == QMessageBox::Discard;
}

// --- Task 8.2: Save implementation ---

void MainWindow::onSave() {
    if (!m_currentRoot && !m_arenaResult) {
        return;
    }

    // Convert arena tree to editable JsonNode tree if needed
    if (!ensureEditableRoot()) {
        return;
    }

    // If in union mode or no current file path, delegate to Save As
    if (m_isUnionMode || m_currentFilePath.isEmpty()) {
        onSaveAs();
        return;
    }

    QString error = SaveHandler::saveToFile(*m_currentRoot, m_currentFilePath);
    if (error.isEmpty()) {
        setModified(false);
    } else {
        QMessageBox::critical(this, tr("Save Error"),
            tr("Failed to save to %1:\n\n%2")
                .arg(m_currentFilePath, error));
    }
}

// --- Task 8.3: Save As implementation ---

void MainWindow::onSaveAs() {
    if (!m_currentRoot && !m_arenaResult) {
        return;
    }

    // Convert arena tree to editable JsonNode tree if needed
    if (!ensureEditableRoot()) {
        return;
    }

    QString chosenPath = QFileDialog::getSaveFileName(
        this, tr("Save As"), QString(),
        tr("JSON Files (*.json)"));

    if (chosenPath.isEmpty()) {
        return;
    }

    // If file exists, prompt for overwrite confirmation
    if (QFile::exists(chosenPath)) {
        auto reply = QMessageBox::question(
            this, tr("Overwrite File"),
            tr("The file \"%1\" already exists.\nDo you want to overwrite it?")
                .arg(QFileInfo(chosenPath).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    QString error = SaveHandler::saveToFile(*m_currentRoot, chosenPath);
    if (error.isEmpty()) {
        m_currentFilePath = chosenPath;
        m_currentFileName = QFileInfo(chosenPath).fileName();
        setModified(false);
    } else {
        QMessageBox::critical(this, tr("Save Error"),
            tr("Failed to save to %1:\n\n%2")
                .arg(chosenPath, error));
    }
}
