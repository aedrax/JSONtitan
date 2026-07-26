#include "shell/edit_controller.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include "core/deletion_engine.h"
#include "shell/model_paths.h"
#include "shell/save_handler.h"

EditController::EditController(QWidget* dialogParent, QTreeView* treeView,
                               TreeModel* treeModel,
                               FilterProxyModel* filterProxy,
                               DocumentSession* session,
                               SearchController* searchController,
                               QObject* parent)
    : QObject(parent),
      m_dialogParent(dialogParent),
      m_treeView(treeView),
      m_treeModel(treeModel),
      m_filterProxy(filterProxy),
      m_session(session),
      m_searchController(searchController) {}

// --- Task 6.2: Delete node implementation ---

void EditController::deleteSelectedNode() {
    // Guard: no-op if nothing loaded at all
    if (!m_session->currentRoot() && !m_session->arenaResult()) {
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

    // Note: top-level rows are the root's direct children, not the root
    // itself (the root is the model's invalid index and never selectable),
    // so every valid index is deletable. The path.empty() check below is
    // the actual root guard.

    // Compute the NodePath by walking up the QModelIndex parent chain.
    // This works regardless of whether the model is arena-backed or JsonNode-backed.
    jsontitan::core::NodePath path =
        jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex);

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
            // Count directly over the arena backing — no deep copy.
            descendantCount =
                jsontitan::core::countDescendants(jsontitan::core::NodeView(*an));
        }
    }

    // Confirmation dialog if node has >10 direct children
    if (directChildCount > 10) {
        auto reply = QMessageBox::question(
            m_dialogParent, tr("Confirm Deletion"),
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
    if (!m_session->ensureEditable()) {
        return;
    }

    // Perform the deletion
    auto newTree = jsontitan::core::deleteNode(m_session->currentRoot(), path);
    if (!newTree) {
        return;  // Shouldn't happen since we guard against root deletion
    }
    m_searchController->invalidate();
    m_session->replaceJsonRoot(newTree);
    m_filterProxy->clearFilter();

    // Set modified flag
    m_session->setModified(true);

    // Restore selection: try next sibling, then previous sibling, then parent
    // After model reset, we need to re-resolve the parent index
    // The parent path is everything except the last segment
    QModelIndex newParentIndex;  // invalid = root
    if (path.size() > 1) {
        jsontitan::core::NodePath parentPath(path.begin(), path.end() - 1);
        newParentIndex =
            jsontitan::shell::indexForPath(*m_treeModel, parentPath);
        if (!newParentIndex.isValid()) {
            // The parent could not be re-resolved; selecting deletedRow under
            // the wrong (shallower) parent would highlight an unrelated node.
            return;
        }
    }

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

// --- Task 7.2: Unsaved-changes prompt ---

bool EditController::confirmDiscardChanges() {
    if (!m_session->modified()) {
        return true;
    }

    auto reply = QMessageBox::question(
        m_dialogParent, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (reply == QMessageBox::Save) {
        save();
        // If still modified after save attempt (e.g., user cancelled the
        // save-as dialog or the write failed), don't discard.
        return !m_session->modified();
    }
    return reply == QMessageBox::Discard;
}

// --- Task 8.2: Save implementation ---

void EditController::save() {
    // Save streams straight from whichever backing is live — no deep copy of
    // arena-backed trees into JsonNode anymore.
    auto root = m_session->rootView();
    if (!root) {
        return;
    }

    // If in union mode or no current file path, delegate to Save As
    if (m_session->isUnionMode() || m_session->filePath().isEmpty()) {
        saveAs();
        return;
    }

    QString error = SaveHandler::saveToFile(*root, m_session->filePath());
    if (error.isEmpty()) {
        m_session->setModified(false);
    } else {
        QMessageBox::critical(m_dialogParent, tr("Save Error"),
            tr("Failed to save to %1:\n\n%2")
                .arg(m_session->filePath(), error));
    }
}

// --- Task 8.3: Save As implementation ---

void EditController::saveAs() {
    // Save streams straight from whichever backing is live — no deep copy of
    // arena-backed trees into JsonNode anymore.
    auto root = m_session->rootView();
    if (!root) {
        return;
    }

    QString chosenPath = QFileDialog::getSaveFileName(
        m_dialogParent, tr("Save As"), QString(),
        tr("JSON Files (*.json)"));

    if (chosenPath.isEmpty()) {
        return;
    }

    // If file exists, prompt for overwrite confirmation
    if (QFile::exists(chosenPath)) {
        auto reply = QMessageBox::question(
            m_dialogParent, tr("Overwrite File"),
            tr("The file \"%1\" already exists.\nDo you want to overwrite it?")
                .arg(QFileInfo(chosenPath).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    QString error = SaveHandler::saveToFile(*root, chosenPath);
    if (error.isEmpty()) {
        // Union mode is deliberately left unchanged (pre-extraction
        // behavior: Save As never cleared it).
        m_session->setFileIdentity(chosenPath,
                                   QFileInfo(chosenPath).fileName(),
                                   m_session->isUnionMode());
        m_session->setModified(false);
    } else {
        QMessageBox::critical(m_dialogParent, tr("Save Error"),
            tr("Failed to save to %1:\n\n%2")
                .arg(chosenPath, error));
    }
}
