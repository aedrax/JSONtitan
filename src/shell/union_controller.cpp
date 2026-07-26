#include "shell/union_controller.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/deletion_engine.h"
#include "core/parser.h"
#include "core/union_engine.h"

UnionController::UnionController(QWidget* dialogParent, Ui ui,
                                 TreeModel* treeModel,
                                 FilterProxyModel* filterProxy,
                                 DocumentSession* session,
                                 SearchController* searchController,
                                 EditController* editController,
                                 RecentFilesManager* recentFilesManager,
                                 QObject* parent)
    : QObject(parent),
      m_dialogParent(dialogParent),
      m_ui(ui),
      m_treeModel(treeModel),
      m_filterProxy(filterProxy),
      m_session(session),
      m_searchController(searchController),
      m_editController(editController),
      m_recentFilesManager(recentFilesManager) {}

// --- Task 16.4: Multi-file union ---

void UnionController::loadUnionSynchronously(const QStringList& filePaths,
                                             bool recordInRecentFiles) {
    // Parse each file synchronously for union (they should be small enough)
    // For large files, a more sophisticated approach would be needed.
    std::vector<jsontitan::core::FileEntry> entries;

    for (const auto& path : filePaths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(m_dialogParent, tr("File Error"),
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
            QMessageBox::critical(m_dialogParent, tr("Parse Error"),
                                  tr("Failed to parse %1:\n\n%2")
                                      .arg(QFileInfo(path).fileName(),
                                           QString::fromStdString(chunkResult.error->description)));
            return;
        }

        auto parseResult = jsontitan::core::finalizeParse(*chunkResult.nextState);
        if (parseResult.error) {
            QMessageBox::critical(m_dialogParent, tr("Parse Error"),
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

    m_searchController->invalidate();
    // File path deliberately left as-is: union mode never reads it
    // (Save redirects to Save As), matching pre-extraction behavior.
    m_session->setJsonRoot(unionRoot, m_session->filePath(),
                           tr("Union (%1 files)").arg(filePaths.size()), true);
    m_filterProxy->clearFilter();

    m_ui.welcomeLabel->hide();
    m_ui.tree->show();
    m_ui.noResultsLabel->hide();

    int nodeCount = unionRoot
        ? static_cast<int>(1 + jsontitan::core::countDescendants(*unionRoot))
        : 0;
    emit statusUpdated(m_session->fileName(), nodeCount);

    m_ui.detailPanel->clear();
    m_ui.searchBar->clear();
    m_ui.searchErrorLabel->hide();

    // Record all files in recent files list (CLI entry point only)
    if (recordInRecentFiles) {
        for (const auto& path : filePaths) {
            m_recentFilesManager->fileOpened(path);
        }
    }
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

    loadUnionSynchronously(filePaths, /*recordInRecentFiles=*/false);
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
    m_searchController->invalidate();
    m_session->replaceJsonRoot(newRoot);
    m_filterProxy->clearFilter();

    int nodeCount = newRoot
        ? static_cast<int>(1 + jsontitan::core::countDescendants(*newRoot))
        : 0;
    emit statusUpdated(m_session->fileName(), nodeCount);

    m_ui.detailPanel->clear();
}
