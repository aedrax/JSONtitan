#include "shell/main_window.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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
#include <string>
#include <utility>
#include <variant>

#include "core/jsonpath.h"
#include "core/parse_orchestrator.h"
#include "shell/clipboard_utils.h"
#include "shell/drop_validator.h"
#include "shell/model_paths.h"
#include "shell/save_handler.h"
#include "shell/wait_cursor.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("JSONTitan");
    resize(1200, 800);
    setAcceptDrops(true);

    // Theme first — before any widget is created — so the initial widget
    // polish already happens with the persisted stylesheet applied.
    m_themeManager = new ThemeManager(this);
    m_themeManager->apply(m_themeManager->current());

    m_treeModel = new TreeModel(this);
    m_filterProxy = new FilterProxyModel(this);
    m_filterProxy->setSourceModel(m_treeModel);
    m_fileLoader = new FileLoader(this);

    // Match-row highlight tracks the theme's accent (translucent so
    // selection and hover still read clearly on either palette).
    const auto matchHighlightFor = [](ThemeManager::Theme theme) {
        return theme == ThemeManager::Theme::Light
                   ? QColor(0x1e, 0x66, 0xf5, 50)   // Latte blue
                   : QColor(0x89, 0xb4, 0xfa, 60);  // Mocha blue
    };
    m_filterProxy->setMatchHighlightColor(
        matchHighlightFor(m_themeManager->current()));
    connect(m_themeManager, &ThemeManager::themeChanged, this,
            [this, matchHighlightFor](ThemeManager::Theme theme) {
                m_filterProxy->setMatchHighlightColor(matchHighlightFor(theme));
            });

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
                             m_matchCountLabel, m_jsonPathToggle},
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
    connect(m_undoAction, &QAction::triggered,
            m_editController, &EditController::undo);
    connect(m_redoAction, &QAction::triggered,
            m_editController, &EditController::redo);
    connect(m_session, &DocumentSession::undoAvailabilityChanged, this,
            [this](bool canUndo, bool canRedo) {
                m_undoAction->setEnabled(canUndo);
                m_redoAction->setEnabled(canRedo);
            });
    // Inline value editing: the model delegates edit commits to the
    // controller, which installs the edited tree via a queued model reset.
    m_treeModel->setEditCommitHandler(
        [this](const QModelIndex& sourceIndex, const QString& newText) {
            return m_editController->applyEdit(sourceIndex, newText);
        });
    connect(m_renameKeyAction, &QAction::triggered,
            m_editController, &EditController::renameSelectedKey);
    connect(m_addChildAction, &QAction::triggered,
            m_editController, &EditController::addChild);
    connect(m_insertAfterAction, &QAction::triggered,
            m_editController, &EditController::insertAfter);
    connect(m_editAsJsonAction, &QAction::triggered,
            m_editController, &EditController::editAsJson);
    connect(m_duplicateAction, &QAction::triggered,
            m_editController, &EditController::duplicateSelected);
    connect(m_pasteAction, &QAction::triggered,
            m_editController, &EditController::paste);
    connect(m_pasteAsChildAction, &QAction::triggered,
            m_editController, &EditController::pasteAsChild);
    connect(m_moveUpAction, &QAction::triggered,
            m_editController, &EditController::moveSelectedUp);
    connect(m_moveDownAction, &QAction::triggered,
            m_editController, &EditController::moveSelectedDown);
    connect(m_sortKeysAction, &QAction::triggered,
            m_editController, &EditController::sortSelectedKeys);
    connect(m_findReplaceAction, &QAction::triggered, this, [this]() {
        // Start from the search bar's text unless it holds a JSONPath query.
        m_editController->findAndReplace(
            m_jsonPathToggle->isChecked() ? QString() : m_searchBar->text());
    });
    // Keep the Edit actions' enabled state in sync with the tree selection
    // and the document.
    connect(m_treeView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this]() { updateEditActions(); });
    connect(m_treeView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this]() { updateEditActions(); });
    connect(m_session, &DocumentSession::documentReplaced,
            this, &MainWindow::updateEditActions);

    // Multi-file union flows
    m_unionController = new UnionController(
        this,
        UnionController::Ui{m_treeView, m_welcomeLabel, m_noResultsLabel,
                            m_searchErrorLabel, m_searchBar, m_detailPanel},
        m_treeModel, m_filterProxy, m_session, m_searchController,
        m_editController, m_recentFilesManager, m_fileLoader, this);
    connect(m_unionAction, &QAction::triggered,
            m_unionController, &UnionController::unionFiles);
    connect(m_unionController, &UnionController::statusUpdated,
            this, &MainWindow::updateStatusBar);
    // Union loads run on the worker thread now: reuse the single-file
    // load-progress UI (progress bar + cancel button).
    connect(m_unionController, &UnionController::loadStarted,
            this, [this]() {
                m_pendingLoadPath.clear();  // supersedes a single-file load
                showLoadProgress();
                m_statusLabel->setText(tr("Parsing union..."));
            });
    connect(m_unionController, &UnionController::loadFinished,
            this, &MainWindow::hideLoadProgress);

    // External file-change watching: debounced (editors fire bursts of
    // events per save), paused around our own saves, single-file mode only.
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onWatchedFileChanged);
    m_fileChangeDebounce = new QTimer(this);
    m_fileChangeDebounce->setSingleShot(true);
    m_fileChangeDebounce->setInterval(500);
    connect(m_fileChangeDebounce, &QTimer::timeout, this, [this]() {
        if (!m_suppressWatchNotifications) {
            showFileChangedBar();
        }
    });
    connect(m_editController, &EditController::aboutToSave, this, [this]() {
        // QSaveFile's commit renames over the watched file; without the
        // pause our own save would surface as an external change (and the
        // rename drops the path from the watcher anyway).
        m_suppressWatchNotifications = true;
        m_fileChangeDebounce->stop();
        const QStringList watched = m_fileWatcher->files();
        if (!watched.isEmpty()) {
            m_fileWatcher->removePaths(watched);
        }
    });
    connect(m_editController, &EditController::saved, this, [this]() {
        m_suppressWatchNotifications = false;
        // Re-watch the file the session points at NOW — Save As may have
        // changed the identity.
        startWatchingCurrentFile();
        // A successful Save As makes a new file the current one.
        if (!m_session->modified()) {
            rememberCurrentFile();
        }
        // A save to the file still loading makes that load's result older
        // than what is on screen (and now on disk): drop the load.
        if (!m_pendingLoadPath.isEmpty() && !m_session->modified() &&
            m_session->filePath() == m_pendingLoadPath) {
            m_fileLoader->cancelParse();
            m_pendingLoadPath.clear();
            hideLoadProgress();
            m_statusLabel->setText(tr("Saved; reload skipped"));
        }
    });
    // The union controller emits statusUpdated whenever it settles the
    // document (union installed, load discarded, file removed): watch
    // whatever is on screen now. Unions have no single file to watch.
    connect(m_unionController, &UnionController::statusUpdated,
            this, &MainWindow::startWatchingCurrentFile);

    // Detail panel rendering for the current tree selection
    m_detailPresenter = new DetailPanelPresenter(
        m_detailPanel, m_breadcrumbLabel, m_treeView, m_filterProxy,
        m_treeModel, m_session, this);
    // Syntax palette follows the theme; a change re-renders the current
    // selection with the new colors.
    m_detailPresenter->setSyntaxTheme(m_themeManager->syntaxTheme());
    connect(m_themeManager, &ThemeManager::themeChanged, this, [this]() {
        m_detailPresenter->setSyntaxTheme(m_themeManager->syntaxTheme());
    });

    showWelcomeMessage();
    updateEditActions();

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
    // Column widths: the header sections exist already (the model reports
    // its columns even while empty), so restoring here is safe.
    const QByteArray headerState =
        settings.value(QStringLiteral("window/treeHeaderState")).toByteArray();
    if (!headerState.isEmpty()) {
        m_treeView->header()->restoreState(headerState);
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
        beginSingleFileLoad(QString::fromStdString(filePaths[0]));
    } else {
        // Multiple files: use the same union logic as File > Union Files
        QStringList qPaths;
        for (const auto& path : filePaths) {
            qPaths.append(QString::fromStdString(path));
        }
        m_unionController->loadUnion(qPaths, /*recordInRecentFiles=*/true);
    }
}

void MainWindow::openFileFromSystem(const QString& filePath) {
    raise();
    activateWindow();
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }
    openFromCliArgs({filePath.toStdString()});
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_newAction = fileMenu->addAction(tr("&New"));
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setStatusTip(tr("Start a new, empty JSON document"));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNewDocument);

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

    m_reloadAction = fileMenu->addAction(tr("Re&load from Disk"));
    m_reloadAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_reloadAction->setStatusTip(tr("Reload the current file from disk"));
    // Enabled once a single file is loaded (startWatchingCurrentFile).
    m_reloadAction->setEnabled(false);
    connect(m_reloadAction, &QAction::triggered, this, &MainWindow::onReload);

    // Output style for Save / Save As (persisted by SaveHandler). JSON Lines
    // files are always written one compact record per line.
    auto* saveFormatMenu = fileMenu->addMenu(tr("Save &Format"));
    saveFormatMenu->menuAction()->setStatusTip(
        tr("Choose how Save writes JSON: indented or minified"));
    auto* saveFormatGroup = new QActionGroup(this);
    const SaveHandler::Style currentStyle = SaveHandler::savedStyle();
    const std::pair<SaveHandler::Style, QString> styles[] = {
        {SaveHandler::Style::Indent2, tr("Indent with &2 Spaces")},
        {SaveHandler::Style::Indent4, tr("Indent with &4 Spaces")},
        {SaveHandler::Style::Compact, tr("&Minified")},
    };
    for (const auto& [style, label] : styles) {
        auto* action = saveFormatMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(style == currentStyle);
        saveFormatGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [style]() { SaveHandler::setSavedStyle(style); });
    }

    fileMenu->addSeparator();

    m_exportJsonAction = fileMenu->addAction(tr("Export &JSON..."));
    m_exportJsonAction->setStatusTip(
        tr("Export the selection (or the whole document) as JSON"));
    connect(m_exportJsonAction, &QAction::triggered, this, &MainWindow::onExportJson);

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

    // Undo/Redo enabled-state tracks DocumentSession::undoAvailabilityChanged;
    // the triggers are wired in the constructor once EditController exists.
    m_undoAction = editMenu->addAction(tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setStatusTip(tr("Undo the last document change"));
    m_undoAction->setEnabled(false);

    m_redoAction = editMenu->addAction(tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setStatusTip(tr("Redo the last undone change"));
    m_redoAction->setEnabled(false);

    editMenu->addSeparator();

    // Structural edits. Triggers are wired in the constructor once
    // EditController exists; enabled state follows updateEditActions().
    const auto addEditAction = [editMenu](const QString& text,
                                          const QKeySequence& shortcut,
                                          const QString& tip) {
        auto* action = editMenu->addAction(text);
        action->setShortcut(shortcut);
        action->setStatusTip(tip);
        action->setEnabled(false);
        return action;
    };
    m_addChildAction = addEditAction(
        tr("Add &Child..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
        tr("Add a member or element to the selected object or array"));
    m_insertAfterAction = addEditAction(
        tr("&Insert After..."), QKeySequence(Qt::CTRL | Qt::Key_I),
        tr("Insert a new sibling after the selected node"));
    m_editAsJsonAction = addEditAction(
        tr("Edit as &JSON..."), QKeySequence(Qt::CTRL | Qt::Key_E),
        tr("Edit the selected node (or the whole document) as JSON text"));
    m_duplicateAction = addEditAction(
        tr("Dupli&cate"), QKeySequence(Qt::CTRL | Qt::Key_D),
        tr("Insert a copy of the selected node after it"));

    editMenu->addSeparator();

    // Paste shortcuts only fire while the tree has focus, so Ctrl+V keeps
    // pasting text in the search bar (the actions are also added to the
    // tree in setupCentralWidget).
    m_pasteAction = addEditAction(
        tr("&Paste"), QKeySequence::Paste,
        tr("Insert the clipboard's JSON after the selected node"));
    m_pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_pasteAsChildAction = addEditAction(
        tr("Paste as C&hild"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
        tr("Insert the clipboard's JSON into the selected object or array"));
    m_pasteAsChildAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);

    editMenu->addSeparator();

    m_moveUpAction = addEditAction(
        tr("Move &Up"), QKeySequence(Qt::ALT | Qt::Key_Up),
        tr("Move the selected node before its previous sibling"));
    m_moveDownAction = addEditAction(
        tr("Move Do&wn"), QKeySequence(Qt::ALT | Qt::Key_Down),
        tr("Move the selected node after its next sibling"));
    m_sortKeysAction = addEditAction(
        tr("&Sort Keys"), QKeySequence(),
        tr("Sort object keys alphabetically in the selected node (or the "
           "whole document)"));

    editMenu->addSeparator();

    // Ctrl+H where the platform has a Replace key (Windows, Linux); macOS
    // has none (Cmd+H hides the app), so Cmd+Option+F like most Mac editors.
    QKeySequence replaceKeys(QKeySequence::Replace);
    if (replaceKeys.isEmpty()) {
        replaceKeys = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F);
    }
    m_findReplaceAction = addEditAction(
        tr("&Find and Replace..."), replaceKeys,
        tr("Replace text in string values and keys across the document"));

    editMenu->addSeparator();

    m_renameKeyAction = addEditAction(tr("Rena&me Key..."), QKeySequence(),
                                      tr("Rename the selected object member"));

    m_deleteAction = editMenu->addAction(tr("&Delete"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setStatusTip(tr("Delete the selected node(s)"));
    // Disabled until the tree has a valid current index; the trigger and the
    // enabled-state sync are wired in the constructor once EditController
    // and the tree's selection model exist.
    m_deleteAction->setEnabled(false);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    m_goToPathAction = viewMenu->addAction(tr("&Go to Path..."));
    m_goToPathAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    m_goToPathAction->setStatusTip(
        tr("Jump to the node at a JSONPath such as $.store.book[0]"));
    connect(m_goToPathAction, &QAction::triggered,
            this, &MainWindow::onGoToPath);

    viewMenu->addSeparator();

    m_expandAllAction = viewMenu->addAction(tr("&Expand All"));
    m_expandAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    m_expandAllAction->setStatusTip(tr("Expand every node in the tree"));
    connect(m_expandAllAction, &QAction::triggered,
            this, &MainWindow::onExpandAll);

    m_collapseAllAction = viewMenu->addAction(tr("&Collapse All"));
    m_collapseAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    m_collapseAllAction->setStatusTip(tr("Collapse every node in the tree"));
    connect(m_collapseAllAction, &QAction::triggered,
            this, &MainWindow::onCollapseAll);

    viewMenu->addSeparator();

    for (int level = 1; level <= 3; ++level) {
        auto* levelAction =
            viewMenu->addAction(tr("Expand to Level %1").arg(level));
        levelAction->setStatusTip(
            tr("Expand the tree %1 level(s) deep").arg(level));
        connect(levelAction, &QAction::triggered, this,
                [this, level]() { expandToLevel(level); });
    }

    viewMenu->addSeparator();

    // Theme toggle: checked = light theme. ThemeManager persists the choice
    // (QSettings "view/theme") on every apply().
    m_lightThemeAction = viewMenu->addAction(tr("&Light Theme"));
    m_lightThemeAction->setCheckable(true);
    m_lightThemeAction->setChecked(m_themeManager->current() ==
                                   ThemeManager::Theme::Light);
    m_lightThemeAction->setStatusTip(
        tr("Switch between the dark and light theme"));
    connect(m_lightThemeAction, &QAction::toggled, this, [this](bool checked) {
        m_themeManager->apply(checked ? ThemeManager::Theme::Light
                                      : ThemeManager::Theme::Dark);
    });
    // Keep the checkmark in sync when the theme changes through other paths
    // (setChecked with an unchanged value emits no toggled signal, so this
    // cannot loop).
    connect(m_themeManager, &ThemeManager::themeChanged, this,
            [this](ThemeManager::Theme theme) {
                m_lightThemeAction->setChecked(theme ==
                                               ThemeManager::Theme::Light);
            });

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
           "background parsing, search and JSONPath queries, structural "
           "editing with undo, multi-file union, and JSON/CSV/XML "
           "export.</p>"
           "<p>Built with Qt %2.</p>")
            .arg(QStringLiteral("0.1.0"),
                 QString::fromLatin1(qVersion())));
}

void MainWindow::onShowKeyboardShortcuts() {
    const QString rows =
        tr("<tr><td><b>Ctrl+N</b></td><td>New document</td></tr>"
           "<tr><td><b>Ctrl+O</b></td><td>Open file</td></tr>"
           "<tr><td><b>Ctrl+S</b></td><td>Save</td></tr>"
           "<tr><td><b>Ctrl+Shift+S</b></td><td>Save As</td></tr>"
           "<tr><td><b>F5</b></td><td>Reload file from disk</td></tr>"
           "<tr><td><b>Ctrl+F</b></td><td>Focus search bar</td></tr>"
           "<tr><td><b>Ctrl+G</b></td><td>Go to path</td></tr>"
           "<tr><td><b>Return</b></td><td>Next match (in search bar)</td></tr>"
           "<tr><td><b>%1</b></td><td>Find and replace</td></tr>"
           "<tr><td><b>F3</b></td><td>Next match</td></tr>"
           "<tr><td><b>Shift+F3</b></td><td>Previous match</td></tr>"
           "<tr><td><b>Ctrl+C</b></td><td>Copy selected value (tree)</td></tr>"
           "<tr><td><b>Ctrl+Shift+E</b></td><td>Expand all</td></tr>"
           "<tr><td><b>Ctrl+Shift+C</b></td><td>Collapse all</td></tr>"
           "<tr><td><b>F2</b></td><td>Edit value inline</td></tr>"
           "<tr><td><b>Ctrl+E</b></td><td>Edit as JSON</td></tr>"
           "<tr><td><b>Ctrl+Shift+I</b></td><td>Add child</td></tr>"
           "<tr><td><b>Ctrl+I</b></td><td>Insert after</td></tr>"
           "<tr><td><b>Ctrl+D</b></td><td>Duplicate</td></tr>"
           "<tr><td><b>Ctrl+V</b></td><td>Paste after (tree)</td></tr>"
           "<tr><td><b>Ctrl+Shift+V</b></td><td>Paste as child (tree)</td></tr>"
           "<tr><td><b>Alt+Up / Alt+Down</b></td><td>Move node up / down</td></tr>"
           "<tr><td><b>Ctrl+Z / Ctrl+Shift+Z</b></td><td>Undo / redo</td></tr>"
           "<tr><td><b>Del</b></td><td>Delete selected node(s)</td></tr>"
           "<tr><td><b>Esc</b></td><td>Cancel load in progress</td></tr>"
           "<tr><td><b>Ctrl+Q</b></td><td>Exit</td></tr>")
            .arg(m_findReplaceAction->shortcut().toString(QKeySequence::NativeText));
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

    m_jsonPathToggle = new QToolButton(centralWidget);
    m_jsonPathToggle->setObjectName("jsonPathToggle");
    m_jsonPathToggle->setText(tr("$"));
    m_jsonPathToggle->setCheckable(true);
    m_jsonPathToggle->setChecked(false);
    m_jsonPathToggle->setToolTip(
        tr("JSONPath query, e.g. $.store.book[*].author or "
           "$..book[?(@.price < 10)].title"));
    searchLayout->addWidget(m_jsonPathToggle);

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

    // Non-modal "file changed on disk" bar above the tree (hidden until the
    // watcher reports an external modification).
    m_fileChangedBar = new QWidget(treeContainer);
    m_fileChangedBar->setObjectName("fileChangedBar");
    auto* fileChangedLayout = new QHBoxLayout(m_fileChangedBar);
    fileChangedLayout->setContentsMargins(6, 4, 6, 4);
    fileChangedLayout->setSpacing(6);
    m_fileChangedLabel = new QLabel(m_fileChangedBar);
    m_fileChangedLabel->setObjectName("fileChangedLabel");
    fileChangedLayout->addWidget(m_fileChangedLabel, 1);
    auto* fileChangedReload = new QPushButton(tr("Reload"), m_fileChangedBar);
    fileChangedReload->setObjectName("fileChangedReloadButton");
    connect(fileChangedReload, &QPushButton::clicked,
            this, &MainWindow::onReload);
    fileChangedLayout->addWidget(fileChangedReload);
    auto* fileChangedDismiss = new QPushButton(tr("Dismiss"), m_fileChangedBar);
    fileChangedDismiss->setObjectName("fileChangedDismissButton");
    connect(fileChangedDismiss, &QPushButton::clicked,
            this, &MainWindow::hideFileChangedBar);
    fileChangedLayout->addWidget(fileChangedDismiss);
    m_fileChangedBar->hide();
    treeLayout->addWidget(m_fileChangedBar);

    m_treeView = new QTreeView(treeContainer);
    m_treeView->setModel(m_filterProxy);
    // Headers are visible now that the model has Type/Size columns.
    m_treeView->setHeaderHidden(false);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Shift/Ctrl-click selects several rows (Delete removes them all); every
    // other action follows the current row.
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Tree-scoped shortcuts (see setupMenuBar).
    m_treeView->addAction(m_pasteAction);
    m_treeView->addAction(m_pasteAsChildAction);
    m_treeView->setAlternatingRowColors(true);
    // Inline editing of scalar rows: F2 or double-click opens the editor
    // (which shows the raw EditRole text, not the display string).
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                QAbstractItemView::DoubleClicked);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* menu = new QMenu(m_treeView);
        // popup() menus are not deleted on dismissal; without this attribute
        // every right-click would leak a QMenu (and its actions) until exit.
        menu->setAttribute(Qt::WA_DeleteOnClose);

        // Clipboard actions act on the CURRENT selection (same convention as
        // Delete below); enablement follows the row under the cursor.
        QModelIndex clickedIndex = m_treeView->indexAt(pos);
        auto* copyValueAction = menu->addAction(tr("Copy Value"));
        copyValueAction->setShortcut(QKeySequence::Copy);
        copyValueAction->setEnabled(clickedIndex.isValid());
        connect(copyValueAction, &QAction::triggered,
                this, &MainWindow::onCopyValue);

        auto* copyKeyAction = menu->addAction(tr("Copy Key"));
        // Only object members carry keys; array elements and the root don't.
        copyKeyAction->setEnabled(
            jsontitan::shell::keyClipboardText(*m_treeModel,
                                               m_filterProxy->mapToSource(clickedIndex))
                .has_value());
        connect(copyKeyAction, &QAction::triggered,
                this, &MainWindow::onCopyKey);

        auto* copyPathAction = menu->addAction(tr("Copy Path"));
        copyPathAction->setEnabled(clickedIndex.isValid());
        connect(copyPathAction, &QAction::triggered,
                this, &MainWindow::onCopyPath);

        menu->addSeparator();
        auto* exportJsonAction = menu->addAction(tr("Export as JSON..."));
        connect(exportJsonAction, &QAction::triggered, this, &MainWindow::onExportJson);
        auto* exportCsvAction = menu->addAction(tr("Export as CSV..."));
        connect(exportCsvAction, &QAction::triggered, this, &MainWindow::onExportCsv);
        auto* exportXmlAction = menu->addAction(tr("Export as XML..."));
        connect(exportXmlAction, &QAction::triggered, this, &MainWindow::onExportXml);

        // Structural edits share the Edit menu's actions, whose enabled
        // state already follows the current row (a right-click makes the
        // clicked row current).
        menu->addSeparator();
        menu->addAction(m_addChildAction);
        menu->addAction(m_insertAfterAction);
        menu->addAction(m_editAsJsonAction);
        menu->addAction(m_duplicateAction);
        menu->addAction(m_pasteAction);
        menu->addAction(m_pasteAsChildAction);

        menu->addSeparator();
        menu->addAction(m_moveUpAction);
        menu->addAction(m_moveDownAction);
        menu->addAction(m_sortKeysAction);

        menu->addSeparator();
        menu->addAction(m_renameKeyAction);
        menu->addAction(m_deleteAction);

        if (m_session->isUnionMode()) {
            menu->addSeparator();
            auto* removeAction = menu->addAction(tr("Remove from Union"));
            connect(removeAction, &QAction::triggered,
                    m_unionController, &UnionController::removeFromUnion);
        }
        menu->popup(m_treeView->viewport()->mapToGlobal(pos));
    });
    treeLayout->addWidget(m_treeView);

    // Ctrl+C over the tree copies the selected node's value. Scoped to the
    // tree (WidgetWithChildrenShortcut) so the detail panel's own copy and
    // the search bar's copy keep working when they have focus.
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, m_treeView);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated,
            this, &MainWindow::onCopyValue);

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
                               "Open a JSON file using File > Open,\n"
                               "combine multiple files with File > Union Files,\n"
                               "or start from scratch with File > New"));
    treeLayout->addWidget(m_welcomeLabel);

    splitter->addWidget(treeContainer);

    // Detail pane: breadcrumb path bar above the detail panel
    auto* detailContainer = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detailContainer);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(2);

    m_breadcrumbLabel = new QLabel(detailContainer);
    m_breadcrumbLabel->setObjectName("breadcrumbLabel");
    m_breadcrumbLabel->setTextFormat(Qt::RichText);
    m_breadcrumbLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                               Qt::LinksAccessibleByKeyboard);
    detailLayout->addWidget(m_breadcrumbLabel);

    m_detailPanel = new QTextEdit(detailContainer);
    m_detailPanel->setObjectName("detailPanel");
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setPlaceholderText(tr("Select a node to view its full value"));
    detailLayout->addWidget(m_detailPanel, 1);

    splitter->addWidget(detailContainer);
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
    m_pendingLoadPath.clear();
    hideLoadProgress();
    m_statusLabel->setText(tr("Load cancelled"));
}

void MainWindow::beginSingleFileLoad(const QString& filePath) {
    m_pendingLoadPath = filePath;
    m_session->beginLoad();
    showLoadProgress();
    m_statusLabel->setText(tr("Parsing %1...").arg(QFileInfo(filePath).fileName()));
    m_fileLoader->startParse(filePath);
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
        m_unionController->loadUnion(result.filePaths,
                                     /*recordInRecentFiles=*/true);
        return;
    }

    beginSingleFileLoad(result.filePaths.first());
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

void MainWindow::onNewDocument() {
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }
    // Drop any in-flight load so its result cannot replace the new document.
    m_fileLoader->cancelParse();
    m_pendingLoadPath.clear();
    hideLoadProgress();
    stopWatchingFile();

    m_searchBar->clear();
    m_searchErrorLabel->hide();
    m_detailPanel->clear();

    // An empty object; no file path, so the first Save asks where to write.
    m_session->setJsonRoot(jsontitan::core::JsonNode::makeObject({}, {}),
                           QString(), tr("Untitled"), false);
    m_filterProxy->clearFilter();
    m_session->setModified(false);

    m_welcomeLabel->hide();
    m_noResultsLabel->hide();
    m_treeView->show();
    m_treeView->setFocus();
    updateStatusBar(m_session->fileName(), 1);
}

void MainWindow::updateEditActions() {
    if (!m_session || !m_detailPresenter) {
        return;  // still constructing
    }
    using jsontitan::core::NodeType;
    const auto root = m_session->rootView();
    // Clicking blank space clears the selection but keeps a current index;
    // only a current row that is still selected counts.
    const QModelIndex sourceIndex =
        jsontitan::shell::selectedSourceIndex(*m_treeView, *m_filterProxy);
    const bool hasRow = sourceIndex.isValid();

    // The node actions apply to: the current row, else the document root.
    const auto target = hasRow ? m_detailPresenter->selectedNodeView() : root;
    const bool targetIsContainer =
        target && (target->type() == NodeType::Object ||
                   target->type() == NodeType::Array);

    bool canRename = false;
    bool canMoveDown = false;
    if (hasRow) {
        // Object members carry keys (a later duplicate key's path ends in
        // its position, so the parent decides).
        const QModelIndex parent = sourceIndex.parent();
        const auto* jsonParent = m_treeModel->jsonNodeForIndex(parent);
        const auto* arenaParent = m_treeModel->arenaNodeForIndex(parent);
        canRename = (jsonParent && jsonParent->type == NodeType::Object) ||
                    (arenaParent && arenaParent->type == NodeType::Object);
        canMoveDown = sourceIndex.row() + 1 < m_treeModel->rowCount(parent) ||
                      m_treeModel->canFetchMore(parent);
    }

    m_addChildAction->setEnabled(targetIsContainer);
    m_insertAfterAction->setEnabled(hasRow);
    m_editAsJsonAction->setEnabled(root.has_value());
    m_duplicateAction->setEnabled(hasRow);
    m_pasteAction->setEnabled(hasRow || targetIsContainer);
    m_pasteAsChildAction->setEnabled(hasRow && targetIsContainer);
    m_moveUpAction->setEnabled(hasRow && sourceIndex.row() > 0);
    m_moveDownAction->setEnabled(canMoveDown);
    m_sortKeysAction->setEnabled(targetIsContainer);
    m_findReplaceAction->setEnabled(root.has_value());
    m_renameKeyAction->setEnabled(canRename);
    m_deleteAction->setEnabled(hasRow);
}

// --- Task 16.2: File Open and background parsing ---

void MainWindow::onOpenFile() {
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open JSON File"), QString(),
        tr("JSON Files (*.json *.jsonc *.jsonl *.ndjson);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    beginSingleFileLoad(filePath);
}

void MainWindow::onProgressUpdated(int percentage) {
    m_progressBar->setValue(percentage);
}

void MainWindow::onArenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result) {
    // Taken now: the prompt below can save, and a save consults it.
    const QString loadPath = std::exchange(m_pendingLoadPath, QString());
    hideLoadProgress();

    // The document stays editable while a load runs; installing would
    // replace those edits without the prompt the load started with.
    if (m_session->editedSinceLoadStart()) {
        if (!m_editController->confirmDiscardChanges()) {
            m_statusLabel->setText(tr("Load discarded"));
            return;
        }
        // A save during the load (or from the prompt just now) wrote the
        // document on screen to the very file being loaded. The result was
        // read before that save, so it is older than what is on screen.
        if (!m_session->modified() && loadPath == m_session->filePath()) {
            m_statusLabel->setText(tr("Saved; reload skipped"));
            return;
        }
    }

    m_searchController->invalidate();
    m_session->setArenaRoot(result, loadPath, QFileInfo(loadPath).fileName());
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

    // Watch the freshly loaded file for external changes (also clears any
    // pending "file changed" bar from the previous document).
    startWatchingCurrentFile();

    rememberCurrentFile();
}

void MainWindow::rememberCurrentFile() {
    // Unions have no single file to reopen.
    if (m_session->isUnionMode() || m_session->filePath().isEmpty()) {
        return;
    }
    m_recentFilesManager->fileOpened(m_session->filePath());
    // Session restore reopens it on the next launch.
    QSettings().setValue(QStringLiteral("session/lastFilePath"),
                         m_session->filePath());
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

    beginSingleFileLoad(lastFile);
}

void MainWindow::onParseError(QString errorMessage) {
    hideLoadProgress();
    m_statusLabel->setText(tr("Parse failed"));

    QMessageBox::critical(this, tr("Parse Error"),
                          tr("Failed to parse %1:\n\n%2")
                              .arg(QFileInfo(std::exchange(m_pendingLoadPath, QString()))
                                       .fileName(),
                                   errorMessage));
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

    beginSingleFileLoad(filePath);
}

// --- Task 16.5: Export actions and detail panel ---

void MainWindow::onExportJson() {
    exportSelection(tr("JSON"), tr("JSON Files (*.json);;All Files (*)"),
                    [](jsontitan::core::NodeView node, const QString& path) {
                        return ExportHandler::exportJsonToFile(node, path);
                    });
}

void MainWindow::onExportCsv() {
    exportSelection(tr("CSV"), tr("CSV Files (*.csv);;All Files (*)"),
                    [](jsontitan::core::NodeView node, const QString& path) {
                        return ExportHandler::exportCsvToFile(node, path);
                    });
}

void MainWindow::onExportXml() {
    exportSelection(tr("XML"), tr("XML Files (*.xml);;All Files (*)"),
                    [](jsontitan::core::NodeView node, const QString& path) {
                        return ExportHandler::exportXmlToFile(node, path);
                    });
}

void MainWindow::exportSelection(
    const QString& formatName, const QString& fileFilter,
    const std::function<QString(jsontitan::core::NodeView, const QString&)>& write) {
    // Export the selection if one resolves, otherwise the live root —
    // uniformly over both backings via NodeView (no deep copies).
    auto node = m_detailPresenter->selectedNodeView();
    if (!node) {
        node = m_session->rootView();
    }
    const QString title = tr("Export %1").arg(formatName);
    if (!node) {
        QMessageBox::information(this, title,
                                 tr("No data to export. Please open a file first."));
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(this, title, QString(), fileFilter);
    if (filePath.isEmpty()) {
        return;
    }

    jsontitan::shell::WaitCursorGuard waitCursor;
    const QString error = write(*node, filePath);
    waitCursor.restore();
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), error);
    } else {
        m_statusLabel->setText(
            tr("Exported %1 to %2").arg(formatName, QFileInfo(filePath).fileName()));
    }
}

// --- Phase 5b commit 1: clipboard actions ---

void MainWindow::onCopyValue() {
    auto node = m_detailPresenter->selectedNodeView();
    if (!node) {
        return;
    }
    const auto text = jsontitan::shell::valueClipboardText(*node);
    if (!text) {
        QMessageBox::information(
            this, tr("Copy Value"),
            tr("This value is larger than %1 MB as text, which is too big for "
               "the clipboard. Export it instead.")
                .arg(jsontitan::shell::kMaxClipboardBytes / (1024 * 1024)));
        return;
    }
    QGuiApplication::clipboard()->setText(*text);
    statusBar()->showMessage(tr("Value copied"), 2000);
}

void MainWindow::onCopyKey() {
    auto key = jsontitan::shell::keyClipboardText(
        *m_treeModel, jsontitan::shell::selectedSourceIndex(*m_treeView, *m_filterProxy));
    if (!key) {
        return;
    }
    QGuiApplication::clipboard()->setText(*key);
    statusBar()->showMessage(tr("Key copied"), 2000);
}

void MainWindow::onCopyPath() {
    if (!jsontitan::shell::selectedSourceIndex(*m_treeView, *m_filterProxy).isValid()) {
        return;
    }
    QGuiApplication::clipboard()->setText(
        jsontitan::shell::jsonPathText(m_detailPresenter->currentSelectionPath()));
    statusBar()->showMessage(tr("Path copied"), 2000);
}

bool MainWindow::goToPath(const QString& expression, QString* errorMessage) {
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!m_session->rootView()) {
        return fail(tr("No document is open."));
    }

    auto path = jsontitan::core::parseDefinitePath(expression.toStdString());
    if (!path) {
        return fail(tr("Position %1: %2")
                        .arg(path.error().position)
                        .arg(QString::fromStdString(path.error().description)));
    }
    if (path->empty()) {
        // The root is not a row; "$" just goes back to the top.
        m_treeView->setCurrentIndex(QModelIndex());
        m_treeView->scrollToTop();
        return true;
    }

    QModelIndex sourceIndex =
        jsontitan::shell::indexForPath(*m_treeModel, *path);
    if (!sourceIndex.isValid()) {
        return fail(tr("There is no node at %1.").arg(expression.trimmed()));
    }
    QModelIndex proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) {
        // Hidden by the active search filter; clearing the search bar drops
        // the filter synchronously.
        m_searchBar->clear();
        proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid()) {
            return fail(tr("There is no node at %1.").arg(expression.trimmed()));
        }
    }
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
    m_treeView->setFocus();
    return true;
}

void MainWindow::onGoToPath() {
    if (!m_session->rootView()) {
        return;
    }
    // Prefill with the current selection's path: editing a suffix of where
    // you are is the common case.
    QString text = jsontitan::shell::jsonPathText(m_detailPresenter->currentSelectionPath());
    while (true) {
        bool ok = false;
        text = QInputDialog::getText(this, tr("Go to Path"),
                                     tr("JSONPath (e.g. $.store.book[0]):"),
                                     QLineEdit::Normal, text, &ok);
        if (!ok) {
            return;
        }
        QString error;
        if (goToPath(text, &error)) {
            return;
        }
        QMessageBox::warning(this, tr("Go to Path"), error);
    }
}

// --- Phase 5b commit 3: expand controls ---

void MainWindow::fetchAllRows(const QModelIndex& sourceParent) {
    while (m_treeModel->canFetchMore(sourceParent)) {
        m_treeModel->fetchMore(sourceParent);
    }
    const int rows = m_treeModel->rowCount(sourceParent);
    for (int r = 0; r < rows; ++r) {
        fetchAllRows(m_treeModel->index(r, 0, sourceParent));
    }
}

void MainWindow::onExpandAll() {
    if (!m_session->rootView()) {
        return;
    }
    // Expanding a huge document forces a full fetch of every lazily-loaded
    // row — that can take a while, so ask first.
    constexpr std::size_t kExpandAllConfirmThreshold = 200000;
    if (m_session->nodeCount() > kExpandAllConfirmThreshold) {
        auto reply = QMessageBox::question(
            this, tr("Expand All"),
            tr("This document has %1 nodes; expanding all of them may take "
               "a while. Continue?")
                .arg(m_session->nodeCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    jsontitan::shell::WaitCursorGuard waitCursor;
    // expandAll only expands rows that exist; the lazily-fetched model must
    // be fully materialized first.
    fetchAllRows(QModelIndex());
    m_treeView->expandAll();
}

void MainWindow::onCollapseAll() {
    m_treeView->collapseAll();
}

void MainWindow::expandToLevel(int level) {
    if (level < 1) {
        return;
    }
    // Note the fetchMore interplay: expandToDepth only expands rows that
    // have been fetched, but expanding a row makes the view fetch its
    // children, so repeated use (or scrolling) converges naturally. This is
    // deliberate — expand-to-level must stay cheap on huge documents.
    m_treeView->expandToDepth(level - 1);
}

// --- Phase 5b commit 5: external file-change watching and reload ---

void MainWindow::startWatchingCurrentFile() {
    m_fileChangeDebounce->stop();
    hideFileChangedBar();
    const QStringList watched = m_fileWatcher->files();
    if (!watched.isEmpty()) {
        m_fileWatcher->removePaths(watched);
    }

    const bool singleFileLoaded =
        !m_session->isUnionMode() && !m_session->filePath().isEmpty() &&
        m_session->rootView().has_value();
    m_reloadAction->setEnabled(singleFileLoaded);
    if (singleFileLoaded && QFile::exists(m_session->filePath())) {
        m_fileWatcher->addPath(m_session->filePath());
    }
}

void MainWindow::stopWatchingFile() {
    m_fileChangeDebounce->stop();
    hideFileChangedBar();
    const QStringList watched = m_fileWatcher->files();
    if (!watched.isEmpty()) {
        m_fileWatcher->removePaths(watched);
    }
    m_reloadAction->setEnabled(false);
}

void MainWindow::onWatchedFileChanged(const QString& path) {
    // Editors typically save by renaming a temp file over the original,
    // which drops the (now-replaced) path from the watcher — re-add it as
    // soon as the new file exists so subsequent changes are still seen.
    if (!m_fileWatcher->files().contains(path) && QFile::exists(path)) {
        m_fileWatcher->addPath(path);
    }
    if (m_suppressWatchNotifications) {
        return;
    }
    // Restart on every event: coalesces the bursts editors fire per save.
    m_fileChangeDebounce->start();
}

void MainWindow::showFileChangedBar() {
    m_fileChangedLabel->setText(
        tr("%1 changed on disk").arg(m_session->fileName()));
    m_fileChangedBar->show();
}

void MainWindow::hideFileChangedBar() {
    m_fileChangedBar->hide();
}

void MainWindow::onReload() {
    if (m_session->isUnionMode() || m_session->filePath().isEmpty()) {
        return;
    }
    const QString filePath = m_session->filePath();
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, tr("Reload"),
                             tr("The file \"%1\" no longer exists.")
                                 .arg(filePath));
        return;
    }
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }
    hideFileChangedBar();
    beginSingleFileLoad(filePath);
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
        settings.setValue(QStringLiteral("window/treeHeaderState"),
                          m_treeView->header()->saveState());
        event->accept();
    } else {
        event->ignore();
    }
}
