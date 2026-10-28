#include "shell/edit_controller.h"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "core/deletion_engine.h"
#include "core/edit_engine.h"
#include "core/stream_export.h"
#include "shell/model_paths.h"
#include "shell/node_input_dialog.h"
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

using jsontitan::core::NodePath;
using jsontitan::core::NodeType;
using jsontitan::core::NodeView;

bool isContainer(NodeType type) {
    return type == NodeType::Object || type == NodeType::Array;
}

// Position of the child `segment` names within `parent` (index segments are
// positions already; key segments are looked up). SIZE_MAX when absent.
std::size_t positionOf(const jsontitan::core::JsonNode& parent,
                       const jsontitan::core::PathSegment& segment) {
    if (const auto* index = std::get_if<std::size_t>(&segment)) {
        return *index;
    }
    const auto& key = std::get<std::string>(segment);
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (parent.children[i]->key == key) {
            return i;
        }
    }
    return SIZE_MAX;
}

std::size_t positionOf(NodeView parent, const jsontitan::core::PathSegment& segment) {
    if (const auto* index = std::get_if<std::size_t>(&segment)) {
        return *index;
    }
    const auto& key = std::get<std::string>(segment);
    for (std::size_t i = 0; i < parent.childCount(); ++i) {
        if (parent.child(i).key() == key) {
            return i;
        }
    }
    return SIZE_MAX;
}

// Path of the child of `parentPath` at `position` / named `key`.
NodePath childPath(NodePath parentPath, NodeType parentType, std::size_t position,
                   const std::string& key) {
    if (parentType == NodeType::Object) {
        parentPath.emplace_back(key);
    } else {
        parentPath.emplace_back(position);
    }
    return parentPath;
}

QSet<QString> memberKeys(NodeView object) {
    QSet<QString> keys;
    if (object.type() != NodeType::Object) {
        return keys;
    }
    keys.reserve(static_cast<qsizetype>(object.childCount()));
    for (std::size_t i = 0; i < object.childCount(); ++i) {
        keys.insert(QString::fromUtf8(object.child(i).key().data(),
                                      static_cast<qsizetype>(object.child(i).key().size())));
    }
    return keys;
}

QString uniqueKey(const QSet<QString>& taken, const QString& base) {
    QString candidate = base;
    for (int suffix = 2; taken.contains(candidate); ++suffix) {
        candidate = QStringLiteral("%1 %2").arg(base).arg(suffix);
    }
    return candidate;
}

// Larger subtrees are refused by "Edit as JSON": a text editor is the wrong
// tool for them (export, edit elsewhere, and reopen instead).
constexpr std::size_t kMaxEditAsJsonBytes = 16 * 1024 * 1024;

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

    // Several rows selected: one confirmation, one undoable step.
    if (auto paths = selectedPaths(); paths.size() > 1) {
        auto reply = QMessageBox::question(
            m_dialogParent, tr("Confirm Deletion"),
            tr("Delete the %1 selected nodes?").arg(paths.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            applyDeletePaths(std::move(paths));
        }
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

std::vector<NodePath> EditController::selectedPaths() const {
    std::vector<NodePath> paths;
    if (!m_treeView->selectionModel()) {
        return paths;
    }
    for (const QModelIndex& proxyIndex :
         m_treeView->selectionModel()->selectedRows()) {
        QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
        if (sourceIndex.isValid()) {
            paths.push_back(
                jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex));
        }
    }
    return paths;
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

// --- Structural edits: insert / replace / move / duplicate / sort ---

std::optional<jsontitan::core::EditError>
EditController::applyInsert(const NodePath& parentPath, std::size_t position,
                            const QString& key,
                            std::shared_ptr<const jsontitan::core::JsonNode> value) {
    auto preRoot = editableRootSnapshot();
    const auto parent = jsontitan::core::findNode(preRoot, parentPath);
    if (!parent) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "No document"};
    }
    position = std::min(position, parent->children.size());
    const std::string keyText = key.toStdString();

    auto result = jsontitan::core::insertChild(preRoot, parentPath, position,
                                               keyText, std::move(value));
    if (!result) {
        return result.error();
    }
    queueInstall(*result, childPath(parentPath, parent->type, position, keyText),
                 jsontitan::core::UndoEntry{std::move(preRoot), currentSelectionPath()});
    return std::nullopt;
}

std::optional<jsontitan::core::EditError>
EditController::applyReplace(const NodePath& path,
                             std::shared_ptr<const jsontitan::core::JsonNode> value) {
    auto preRoot = editableRootSnapshot();
    if (!preRoot) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "No document"};
    }
    auto result = jsontitan::core::replaceNode(preRoot, path, std::move(value));
    if (!result) {
        return result.error();
    }
    queueInstall(*result, path, jsontitan::core::UndoEntry{std::move(preRoot), path});
    return std::nullopt;
}

std::optional<jsontitan::core::EditError>
EditController::applyMove(const NodePath& path, int delta) {
    if (path.empty()) {
        return std::nullopt;
    }
    auto preRoot = editableRootSnapshot();
    const NodePath parentPath(path.begin(), path.end() - 1);
    const auto parent = jsontitan::core::findNode(preRoot, parentPath);
    if (!parent) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "No document"};
    }
    const std::size_t from = positionOf(*parent, path.back());
    const auto to = static_cast<long long>(from) + delta;
    if (from == SIZE_MAX || to < 0 ||
        to >= static_cast<long long>(parent->children.size())) {
        return std::nullopt;  // already first/last: nothing to do
    }

    auto result = jsontitan::core::moveChild(preRoot, path, static_cast<std::size_t>(to));
    if (!result) {
        return result.error();
    }
    NodePath selection = path;
    if (parent->type == NodeType::Array) {
        selection.back() = static_cast<std::size_t>(to);
    }
    queueInstall(*result, std::move(selection),
                 jsontitan::core::UndoEntry{std::move(preRoot), path});
    return std::nullopt;
}

std::optional<jsontitan::core::EditError>
EditController::applyDuplicate(const NodePath& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    auto preRoot = editableRootSnapshot();
    const NodePath parentPath(path.begin(), path.end() - 1);
    const auto parent = jsontitan::core::findNode(preRoot, parentPath);
    const auto target = jsontitan::core::findNode(preRoot, path);
    if (!parent || !target) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "Path does not resolve to a node"};
    }
    const std::size_t position = positionOf(*parent, path.back()) + 1;
    const std::string key = parent->type == NodeType::Object
                                ? jsontitan::core::uniqueChildKey(*parent, target->key + " copy")
                                : std::string();

    auto result = jsontitan::core::insertChild(preRoot, parentPath, position, key, target);
    if (!result) {
        return result.error();
    }
    queueInstall(*result, childPath(parentPath, parent->type, position, key),
                 jsontitan::core::UndoEntry{std::move(preRoot), path});
    return std::nullopt;
}

std::optional<jsontitan::core::EditError>
EditController::applySortKeys(const NodePath& path) {
    auto preRoot = editableRootSnapshot();
    if (!preRoot) {
        return jsontitan::core::EditError{
            jsontitan::core::EditErrorCode::InvalidPath, "No document"};
    }
    auto result = jsontitan::core::sortKeys(preRoot, path);
    if (!result) {
        return result.error();
    }
    if (result->get() == preRoot.get()) {
        return std::nullopt;  // already sorted: no undo entry
    }
    queueInstall(*result, path, jsontitan::core::UndoEntry{std::move(preRoot), path});
    return std::nullopt;
}

void EditController::applyDeletePaths(std::vector<NodePath> paths) {
    // The root is never a row, but guard anyway: deleting it is not an edit.
    std::erase_if(paths, [](const NodePath& path) { return path.empty(); });
    if (paths.empty()) {
        return;
    }
    auto preRoot = editableRootSnapshot();
    if (!preRoot) {
        return;
    }
    const NodePath first = paths.front();
    auto newRoot = jsontitan::core::deleteNodes(preRoot, std::move(paths));
    if (!newRoot || newRoot.get() == preRoot.get()) {
        return;
    }
    // Afterwards select the first deleted node's parent; undo reselects the
    // first deleted node itself.
    queueInstall(std::move(newRoot), NodePath(first.begin(), first.end() - 1),
                 jsontitan::core::UndoEntry{std::move(preRoot), first});
}

void EditController::warnIfError(const QString& title,
                                 const std::optional<jsontitan::core::EditError>& error) {
    if (error) {
        QMessageBox::warning(m_dialogParent, title,
                             QString::fromStdString(error->description));
    }
}

std::optional<NodeView> EditController::viewAt(const NodePath& path) const {
    if (path.empty()) {
        return m_session->rootView();
    }
    QModelIndex sourceIndex = jsontitan::shell::indexForPath(*m_treeModel, path);
    if (!sourceIndex.isValid()) {
        return std::nullopt;
    }
    if (const auto* node = m_treeModel->jsonNodeForIndex(sourceIndex)) {
        return NodeView(*node);
    }
    if (const auto* node = m_treeModel->arenaNodeForIndex(sourceIndex)) {
        return NodeView(*node);
    }
    return std::nullopt;
}

void EditController::addChild() {
    const NodePath target = currentSelectionPath();
    const auto view = viewAt(target);
    if (!view || !isContainer(view->type())) {
        return;
    }
    const bool isObject = view->type() == NodeType::Object;
    const QSet<QString> taken = memberKeys(*view);
    NodeInputDialog dialog({.title = tr("Add Child"),
                            .showKey = isObject,
                            .key = uniqueKey(taken, QStringLiteral("key")),
                            .takenKeys = taken,
                            .value = QString()},
                           m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    warnIfError(tr("Add Child"),
                applyInsert(target, SIZE_MAX, dialog.key(), dialog.value()));
}

void EditController::insertAfter() {
    const NodePath selection = currentSelectionPath();
    if (selection.empty()) {
        return;
    }
    const NodePath parentPath(selection.begin(), selection.end() - 1);
    const auto parent = viewAt(parentPath);
    if (!parent) {
        return;
    }
    const bool isObject = parent->type() == NodeType::Object;
    const QSet<QString> taken = memberKeys(*parent);
    NodeInputDialog dialog({.title = tr("Insert After"),
                            .showKey = isObject,
                            .key = uniqueKey(taken, QStringLiteral("key")),
                            .takenKeys = taken,
                            .value = QString()},
                           m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    warnIfError(tr("Insert After"),
                applyInsert(parentPath, positionOf(*parent, selection.back()) + 1,
                            dialog.key(), dialog.value()));
}

void EditController::editAsJson() {
    const NodePath path = currentSelectionPath();
    const auto view = viewAt(path);
    if (!view) {
        return;
    }

    std::string text;
    bool tooLarge = false;
    jsontitan::core::ByteSink sink = [&text, &tooLarge](std::string_view chunk) {
        if (text.size() + chunk.size() > kMaxEditAsJsonBytes) {
            tooLarge = true;
            return false;
        }
        text.append(chunk);
        return true;
    };
    {
        jsontitan::shell::WaitCursorGuard waitCursor;
        jsontitan::core::exportJsonStream(
            *view, sink,
            {.mode = jsontitan::core::IndentMode::PrettyPrint,
             .indentWidth = 2,
             .trailingNewline = false});
    }
    if (tooLarge) {
        QMessageBox::information(
            m_dialogParent, tr("Edit as JSON"),
            tr("This node is larger than %1 MB as text, which is too big to "
               "edit here. Export it, edit it elsewhere, and reopen it instead.")
                .arg(kMaxEditAsJsonBytes / (1024 * 1024)));
        return;
    }

    NodeInputDialog dialog({.title = path.empty() ? tr("Edit Document as JSON")
                                                  : tr("Edit as JSON"),
                            .showKey = false,
                            .key = QString(),
                            .takenKeys = {},
                            .value = QString::fromStdString(text)},
                           m_dialogParent);
    dialog.resize(720, 560);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    warnIfError(tr("Edit as JSON"), applyReplace(path, dialog.value()));
}

std::shared_ptr<const jsontitan::core::JsonNode> EditController::clipboardValue() {
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(m_dialogParent, tr("Paste"),
                                 tr("The clipboard has no text to paste."));
        return nullptr;
    }
    auto parsed = jsontitan::core::parseValueInput(text.toStdString());
    if (!parsed) {
        QMessageBox::warning(m_dialogParent, tr("Paste"),
                             QString::fromStdString(parsed.error().description));
        return nullptr;
    }
    return *parsed;
}

void EditController::insertWithKeyPrompt(
    const NodePath& parentPath, std::size_t position,
    std::shared_ptr<const jsontitan::core::JsonNode> value,
    const QString& suggestedKey) {
    const auto parent = viewAt(parentPath);
    if (!parent || !isContainer(parent->type())) {
        return;
    }
    QString key;
    if (parent->type() == NodeType::Object) {
        const QSet<QString> taken = memberKeys(*parent);
        key = uniqueKey(taken, suggestedKey);
        while (true) {
            bool ok = false;
            key = QInputDialog::getText(m_dialogParent, tr("Paste"),
                                        tr("Key for the pasted value:"),
                                        QLineEdit::Normal, key, &ok);
            if (!ok) {
                return;
            }
            if (!taken.contains(key)) {
                break;
            }
            QMessageBox::warning(m_dialogParent, tr("Paste"),
                                 tr("Key \"%1\" already exists in this object.").arg(key));
        }
    }
    warnIfError(tr("Paste"), applyInsert(parentPath, position, key, std::move(value)));
}

void EditController::paste() {
    const NodePath selection = currentSelectionPath();
    const auto rootView = m_session->rootView();
    if (!rootView) {
        return;
    }
    auto value = clipboardValue();
    if (!value) {
        return;
    }
    if (selection.empty()) {
        // Nothing selected: append to the document root.
        insertWithKeyPrompt({}, SIZE_MAX, std::move(value), QStringLiteral("key"));
        return;
    }
    const NodePath parentPath(selection.begin(), selection.end() - 1);
    const auto parent = viewAt(parentPath);
    if (!parent) {
        return;
    }
    insertWithKeyPrompt(parentPath, positionOf(*parent, selection.back()) + 1,
                        std::move(value), QStringLiteral("key"));
}

void EditController::pasteAsChild() {
    const NodePath target = currentSelectionPath();
    const auto view = viewAt(target);
    if (!view || !isContainer(view->type())) {
        return;
    }
    auto value = clipboardValue();
    if (!value) {
        return;
    }
    insertWithKeyPrompt(target, SIZE_MAX, std::move(value), QStringLiteral("key"));
}

void EditController::duplicateSelected() {
    warnIfError(tr("Duplicate"), applyDuplicate(currentSelectionPath()));
}

void EditController::moveSelectedUp() {
    warnIfError(tr("Move Up"), applyMove(currentSelectionPath(), -1));
}

void EditController::moveSelectedDown() {
    warnIfError(tr("Move Down"), applyMove(currentSelectionPath(), +1));
}

void EditController::sortSelectedKeys() {
    warnIfError(tr("Sort Keys"), applySortKeys(currentSelectionPath()));
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

    // Comments cannot be carried through the tree, so overwriting a JSONC
    // file that had them is lossy: ask first.
    if (m_session->hadComments()) {
        auto reply = QMessageBox::question(
            m_dialogParent, tr("Remove Comments"),
            tr("\"%1\" contains comments, which JSONTitan cannot keep.\n"
               "Saving will remove them. Save anyway?")
                .arg(m_session->fileName()),
            QMessageBox::Save | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (reply != QMessageBox::Save) {
            return;
        }
    }

    jsontitan::shell::WaitCursorGuard waitCursor;
    emit aboutToSave();
    QString error = SaveHandler::saveToFile(*root, m_session->filePath(),
                                            m_session->format(),
                                            SaveHandler::savedStyle());
    waitCursor.restore();
    if (error.isEmpty()) {
        m_session->setFormat(m_session->format(), false);
        m_session->setModified(false);
    }
    emit saved();
    if (!error.isEmpty()) {
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

    using jsontitan::core::JsonFormat;
    const QString jsonFilter = tr("JSON Files (*.json *.jsonc)");
    const QString linesFilter = tr("JSON Lines Files (*.jsonl *.ndjson)");
    QString selectedFilter = m_session->format() == JsonFormat::JsonLines
                                 ? linesFilter
                                 : jsonFilter;
    QString chosenPath = QFileDialog::getSaveFileName(
        m_dialogParent, tr("Save As"), QString(),
        jsonFilter + QStringLiteral(";;") + linesFilter, &selectedFilter);

    if (chosenPath.isEmpty()) {
        return;
    }

    // The extension decides the output format; without a known one, the
    // filter the user picked does.
    const JsonFormat format =
        jsontitan::core::formatForPath(chosenPath.toStdString())
            .value_or(selectedFilter == linesFilter ? JsonFormat::JsonLines
                                                    : JsonFormat::Json);

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
    emit aboutToSave();
    QString error = SaveHandler::saveToFile(*root, chosenPath, format,
                                            SaveHandler::savedStyle());
    waitCursor.restore();
    if (error.isEmpty()) {
        m_session->setFormat(format, false);
        // Union mode is deliberately left unchanged (pre-extraction
        // behavior: Save As never cleared it).
        m_session->setFileIdentity(chosenPath,
                                   QFileInfo(chosenPath).fileName(),
                                   m_session->isUnionMode());
        m_session->setModified(false);
    }
    // Emitted after the identity update so the owner re-watches the file the
    // session now points at.
    emit saved();
    if (!error.isEmpty()) {
        QMessageBox::critical(m_dialogParent, tr("Save Error"),
            tr("Failed to save to %1:\n\n%2")
                .arg(chosenPath, error));
    }
}
