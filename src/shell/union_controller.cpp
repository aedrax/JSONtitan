#include "shell/union_controller.h"

#include <QFileDialog>
#include <QMessageBox>

#include <string>

#include "core/deletion_engine.h"
#include "core/union_engine.h"

UnionController::UnionController(QWidget* dialogParent, Ui ui,
                                 TreeModel* treeModel,
                                 FilterProxyModel* filterProxy,
                                 DocumentSession* session,
                                 SearchController* searchController,
                                 EditController* editController,
                                 RecentFilesManager* recentFilesManager,
                                 FileLoader* fileLoader,
                                 QObject* parent)
    : QObject(parent),
      m_dialogParent(dialogParent),
      m_ui(ui),
      m_treeModel(treeModel),
      m_filterProxy(filterProxy),
      m_session(session),
      m_searchController(searchController),
      m_editController(editController),
      m_recentFilesManager(recentFilesManager),
      m_fileLoader(fileLoader) {
    connect(m_fileLoader, &FileLoader::unionParseComplete,
            this, &UnionController::onUnionParseComplete);
    connect(m_fileLoader, &FileLoader::unionParseError,
            this, &UnionController::onUnionParseError);
}

// --- Task 16.4 / Phase 5a C8: multi-file union on the worker thread ---

void UnionController::loadUnion(const QStringList& filePaths,
                                bool recordInRecentFiles) {
    if (filePaths.isEmpty()) {
        return;
    }

    // startUnionParse supersedes any in-flight parse (single-file or union);
    // cancelParse first makes the intent explicit and drops stale results
    // even if the worker is between cancellation checks.
    m_fileLoader->cancelParse();

    m_recordInRecentFiles = recordInRecentFiles;

    emit loadStarted();
    m_fileLoader->startUnionParse(filePaths);
}

void UnionController::onUnionParseComplete(
    std::shared_ptr<const jsontitan::core::JsonNode> root,
    const QStringList& filePaths) {
    emit loadFinished();

    m_searchController->invalidate();
    // File path deliberately left as-is: union mode never reads it
    // (Save redirects to Save As), matching pre-extraction behavior.
    // setJsonRoot also clears the undo history (new-document install).
    m_session->setJsonRoot(root, m_session->filePath(),
                           tr("Union (%1 files)").arg(filePaths.size()), true);
    m_filterProxy->clearFilter();

    m_ui.welcomeLabel->hide();
    m_ui.tree->show();
    m_ui.noResultsLabel->hide();

    int nodeCount = root
        ? static_cast<int>(1 + jsontitan::core::countDescendants(*root))
        : 0;
    emit statusUpdated(m_session->fileName(), nodeCount);

    m_ui.detailPanel->clear();
    m_ui.searchBar->clear();
    m_ui.searchErrorLabel->hide();

    // Record all files in recent files list (CLI/drop entry points only)
    if (m_recordInRecentFiles) {
        for (const auto& path : filePaths) {
            m_recentFilesManager->fileOpened(path);
        }
    }
}

void UnionController::onUnionParseError(const QString& fileName,
                                        const QString& errorMessage) {
    emit loadFinished();
    QMessageBox::critical(m_dialogParent, tr("Parse Error"),
                          tr("Failed to parse %1:\n\n%2")
                              .arg(fileName, errorMessage));
}

void UnionController::unionFiles() {
    if (!m_editController->confirmDiscardChanges()) {
        return;
    }

    QStringList filePaths = QFileDialog::getOpenFileNames(
        m_dialogParent, tr("Select JSON Files to Union"), QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePaths.isEmpty()) {
        return;
    }

    loadUnion(filePaths, /*recordInRecentFiles=*/false);
}

void UnionController::removeFromUnion() {
    if (!m_session->isUnionMode() || !m_session->currentRoot()) {
        return;
    }

    QModelIndex proxyIndex = m_ui.tree->currentIndex();
    if (!proxyIndex.isValid()) {
        return;
    }

    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return;
    }

    // Only allow removal of top-level children (direct children of union root)
    if (sourceIndex.parent().isValid()) {
        QMessageBox::information(m_dialogParent, tr("Remove from Union"),
                                 tr("Please select a top-level file node to remove."));
        return;
    }

    // Get the key of the selected node
    const auto* nodePtr = m_treeModel->jsonNodeForIndex(sourceIndex);
    if (!nodePtr) {
        return;
    }

    std::string filenameKey = nodePtr->key;

    auto newRoot = jsontitan::core::removeFromUnion(*m_session->currentRoot(), filenameKey);
    // Undo bookkeeping: pre-mutation union root + the removed file's path so
    // undo restores and reselects it. (Union trees are always JsonNode-backed.)
    m_session->pushUndo(jsontitan::core::UndoEntry{
        m_session->currentRoot(),
        jsontitan::core::NodePath{filenameKey}});
    m_searchController->invalidate();
    m_session->replaceJsonRoot(newRoot);
    m_session->setModified(true);
    m_filterProxy->clearFilter();

    int nodeCount = newRoot
        ? static_cast<int>(1 + jsontitan::core::countDescendants(*newRoot))
        : 0;
    emit statusUpdated(m_session->fileName(), nodeCount);

    m_ui.detailPanel->clear();
}
