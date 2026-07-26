#include "shell/edit_controller.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>

#include <string>
#include <utility>
#include <variant>

#include "core/deletion_engine.h"
#include "core/edit_engine.h"
#include "shell/model_paths.h"
#include "shell/save_handler.h"
#include "shell/wait_cursor.h"

namespace {

// Resolve `path` over a JsonNode tree; nullptr when it does not resolve.
const jsontitan::core::JsonNode* resolvePath(
    const jsontitan::core::JsonNode* node,
    const jsontitan::core::NodePath& path) {
    using jsontitan::core::NodeType;
    for (const auto& segment : path) {
        if (!node) {
            return nullptr;
        }
        if (const auto* keyPtr = std::get_if<std::string>(&segment)) {
            const jsontitan::core::JsonNode* found = nullptr;
            for (const auto& child : node->children) {
                if (child->key == *keyPtr) {
                    found = child.get();
                    break;
                }
            }
            node = found;
        } else {
            auto idx = std::get<std::size_t>(segment);
            if (node->type != NodeType::Array || idx >= node->children.size()) {
                return nullptr;
            }
            node = node->children[idx].get();
        }
    }
    return node;
}

}  // namespace

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
    // Undo bookkeeping: the pre-mutation root (converted above if the file
    // was arena-backed) plus the deleted node's path, so undo restores and
    // reselects it.
    m_session->pushUndo(
        jsontitan::core::UndoEntry{m_session->currentRoot(), path});
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

// --- Phase 5a: inline value editing, key rename, undo/redo ---

std::shared_ptr<const jsontitan::core::JsonNode>
EditController::editableRootSnapshot() const {
    if (auto root = m_session->currentRoot()) {
        return root;
    }
    // Arena-backed: convert WITHOUT touching the session or the model.
    // applyEdit runs inside the delegate's setData commit, where a
    // synchronous model reset (which ensureEditable would trigger) is
    // forbidden. The queued install replaces the backing instead; the arena
    // is released when the JsonNode root is installed.
    if (auto arena = m_session->arenaResult(); arena && arena->root) {
        return arena->root->toJsonNode();
    }
    return nullptr;
}

jsontitan::core::NodePath EditController::currentSelectionPath() const {
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return {};
    }
    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return {};
    }
    return jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex);
}

void EditController::installRoot(
    const std::shared_ptr<const jsontitan::core::JsonNode>& root,
    const jsontitan::core::NodePath& selection) {
    m_searchController->invalidate();
    m_session->replaceJsonRoot(root);
    m_filterProxy->clearFilter();
    // Content differs from disk after every mutation AND after undo/redo
    // (accepted simplification: undo back to the on-disk state still shows
    // modified).
    m_session->setModified(true);

    if (selection.empty()) {
        return;
    }
    QModelIndex sourceIndex =
        jsontitan::shell::indexForPath(*m_treeModel, selection);
    if (sourceIndex.isValid()) {
        QModelIndex proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
        if (proxyIndex.isValid()) {
            m_treeView->setCurrentIndex(proxyIndex);
        }
    }
}

void EditController::queueInstall(
    std::shared_ptr<const jsontitan::core::JsonNode> root,
    jsontitan::core::NodePath selection,
    std::optional<jsontitan::core::UndoEntry> undoEntry) {
    QMetaObject::invokeMethod(
        this,
        [this, root = std::move(root), selection = std::move(selection),
         undoEntry = std::move(undoEntry)]() {
            if (undoEntry) {
                m_session->pushUndo(*undoEntry);
            }
            installRoot(root, selection);
        },
        Qt::QueuedConnection);
}

bool EditController::applyEdit(const QModelIndex& sourceIndex,
                               const QString& newText) {
    if (!sourceIndex.isValid()) {
        return false;
    }
    auto path = jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex);
    if (path.empty()) {
        return false;
    }

    auto preRoot = editableRootSnapshot();
    if (!preRoot) {
        return false;
    }

    auto value = jsontitan::core::classifyScalarInput(newText.toStdString());

    // No-op commit (delegates commit on focus-out even when unchanged):
    // accept it without mutating or polluting the undo history.
    if (const auto* target = resolvePath(preRoot.get(), path)) {
        const bool sameValue =
            value.type == jsontitan::core::NodeType::Null
                ? true  // Null carries no value text
                : target->value == value.text;
        if (target->type == value.type && sameValue) {
            return true;
        }
    }

    auto result = jsontitan::core::editValue(preRoot, path, value);
    if (!result) {
        return false;
    }

    queueInstall(*result, path,
                 jsontitan::core::UndoEntry{std::move(preRoot), path});
    return true;
}

std::optional<jsontitan::core::EditError>
EditController::applyRename(const jsontitan::core::NodePath& path,
                            const QString& newKey) {
    auto preRoot = editableRootSnapshot();
    if (!preRoot) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "No document"};
    }

    auto result =
        jsontitan::core::renameKey(preRoot, path, newKey.toStdString());
    if (!result) {
        return result.error();
    }
    if (result->get() == preRoot.get()) {
        return std::nullopt;  // same-key rename: no-op, nothing to install
    }

    jsontitan::core::NodePath newSelection = path;
    newSelection.back() = newKey.toStdString();
    queueInstall(*result, std::move(newSelection),
                 jsontitan::core::UndoEntry{std::move(preRoot), path});
    return std::nullopt;
}

void EditController::renameSelectedKey() {
    // Compute the path BEFORE the modal dialog: its nested event loop could
    // deliver a model reset (e.g. a background parse completing) that would
    // invalidate any held QModelIndex.
    auto path = currentSelectionPath();
    if (path.empty() || !std::holds_alternative<std::string>(path.back())) {
        return;  // only object members carry keys
    }
    const QString currentKey =
        QString::fromStdString(std::get<std::string>(path.back()));

    bool ok = false;
    QString newKey = QInputDialog::getText(
        m_dialogParent, tr("Rename Key"), tr("New key name:"),
        QLineEdit::Normal, currentKey, &ok);
    if (!ok) {
        return;
    }

    auto error = applyRename(path, newKey);
    if (error) {
        QMessageBox::warning(m_dialogParent, tr("Rename Key"),
                             QString::fromStdString(error->description));
    }
}

void EditController::undo() {
    auto restored = m_session->undo(currentSelectionPath());
    if (!restored) {
        return;
    }
    queueInstall(restored->root, restored->selection, std::nullopt);
}

void EditController::redo() {
    auto restored = m_session->redo(currentSelectionPath());
    if (!restored) {
        return;
    }
    queueInstall(restored->root, restored->selection, std::nullopt);
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

    jsontitan::shell::WaitCursorGuard waitCursor;
    QString error = SaveHandler::saveToFile(*root, m_session->filePath());
    waitCursor.restore();
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

    jsontitan::shell::WaitCursorGuard waitCursor;
    QString error = SaveHandler::saveToFile(*root, chosenPath);
    waitCursor.restore();
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
