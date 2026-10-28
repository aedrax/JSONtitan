#pragma once

#include <QObject>
#include <QString>
#include <QTreeView>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "core/edit_engine.h"
#include "core/replace_engine.h"
#include "shell/document_session.h"
#include "shell/filter_proxy_model.h"
#include "shell/search_controller.h"
#include "shell/tree_model.h"

// Owns the document-mutation flows: inline value editing, key rename,
// undo/redo, node deletion (confirmation dialog + selection restore via
// model_paths), Save / Save As, and the unsaved-changes prompt. Owned by
// MainWindow via Qt parent ownership.
class EditController : public QObject {
    Q_OBJECT
public:
    EditController(QWidget* dialogParent, QTreeView* treeView,
                   TreeModel* treeModel, FilterProxyModel* filterProxy,
                   DocumentSession* session,
                   SearchController* searchController,
                   QObject* parent = nullptr);

    // Returns true when it is safe to discard the current document: either
    // there are no unsaved changes, or the user chose Save (and it
    // succeeded) or Discard. Returns false when the user cancelled.
    bool confirmDiscardChanges();

    // Inline-edit commit (TreeModel's EditCommitHandler): classifies the
    // raw editor text, computes the edited tree synchronously, and installs
    // it via a QUEUED model reset — setData runs inside the item delegate's
    // commit, so the model must not be reset synchronously here. Returns
    // whether the commit was accepted.
    bool applyEdit(const QModelIndex& sourceIndex, const QString& newText);

    // Testable core of "Rename Key…": renames the object member at `path`.
    // Returns the engine error on failure (e.g. DuplicateKey), std::nullopt
    // on success or no-op. Installation is queued like applyEdit.
    std::optional<jsontitan::core::EditError>
    applyRename(const jsontitan::core::NodePath& path, const QString& newKey);

    // Testable cores of the structural edits. Each computes the new tree
    // from the editable snapshot, queues its install with one undo entry,
    // and selects the affected node afterwards. They return the engine
    // error on failure and std::nullopt on success or no-op.

    // Insert `value` into the container at `parentPath` at `position`
    // (SIZE_MAX appends); `key` names object members.
    std::optional<jsontitan::core::EditError>
    applyInsert(const jsontitan::core::NodePath& parentPath,
                std::size_t position, const QString& key,
                std::shared_ptr<const jsontitan::core::JsonNode> value);
    // Replace the node at `path` (the whole document when empty).
    std::optional<jsontitan::core::EditError>
    applyReplace(const jsontitan::core::NodePath& path,
                 std::shared_ptr<const jsontitan::core::JsonNode> value);
    // Move the node at `path` by `delta` positions among its siblings.
    std::optional<jsontitan::core::EditError>
    applyMove(const jsontitan::core::NodePath& path, int delta);
    // Insert a copy right after the node (objects: key "<key> copy").
    std::optional<jsontitan::core::EditError>
    applyDuplicate(const jsontitan::core::NodePath& path);
    // Sort object keys throughout the subtree at `path`.
    std::optional<jsontitan::core::EditError>
    applySortKeys(const jsontitan::core::NodePath& path);
    // Delete every node in `paths` as one undoable step.
    void applyDeletePaths(std::vector<jsontitan::core::NodePath> paths);
    // Find & Replace over the whole document as one undoable step. The
    // returned result carries the counts and any error; its root is the
    // pre-replace root when nothing changed.
    jsontitan::core::ReplaceResult
    applyReplaceAll(const jsontitan::core::ReplaceOptions& options);

signals:
    // Emitted immediately before / after our own write of the document to
    // disk (Save and Save As), so the owner can pause file-change watching
    // around it — QSaveFile's commit renames over the watched file, which
    // would otherwise look like an external modification.
    void aboutToSave();
    void saved();

public slots:
    void deleteSelectedNode();
    void save();
    void saveAs();
    // Interactive "Rename Key…": prompts for the new key (prefilled with the
    // current one) and surfaces DuplicateKey as a warning dialog.
    void renameSelectedKey();
    void undo();
    void redo();

    // Interactive structural edits (Edit menu / tree context menu). Targets
    // follow the tree's current row; with no row selected, "the node" is
    // the document root.
    void addChild();        // into the selected container
    void insertAfter();     // sibling after the selected node
    void editAsJson();      // replace the selected subtree with edited JSON
    void paste();           // clipboard value after the selection
    void pasteAsChild();    // clipboard value into the selected container
    void duplicateSelected();
    void moveSelectedUp();
    void moveSelectedDown();
    void sortSelectedKeys();
    // Opens the Find and Replace dialog, prefilled with `initialPattern`.
    void findAndReplace(const QString& initialPattern);

private:
    // Pre-mutation root as a JsonNode tree: the live JsonNode root, or a
    // conversion of the arena backing (the arena itself is untouched; the
    // queued install replaces the backing). Nullptr when nothing is loaded.
    std::shared_ptr<const jsontitan::core::JsonNode> editableRootSnapshot() const;

    // NodePath of the tree view's current selection ({} when none).
    jsontitan::core::NodePath currentSelectionPath() const;

    // NodePaths of every selected row (multi-selection), in view order.
    std::vector<jsontitan::core::NodePath> selectedPaths() const;

    // Shared tail of the insert flows: prompt for a key when the parent is
    // an object (prefilled with `suggestedKey`), then applyInsert. Shows
    // engine errors as a warning.
    void insertWithKeyPrompt(const jsontitan::core::NodePath& parentPath,
                             std::size_t position,
                             std::shared_ptr<const jsontitan::core::JsonNode> value,
                             const QString& suggestedKey);
    // View of the node at `path` over either backing, without converting
    // anything (the root for an empty path); std::nullopt when absent.
    std::optional<jsontitan::core::NodeView>
    viewAt(const jsontitan::core::NodePath& path) const;
    // Clipboard text parsed as a value, or nullptr after warning the user.
    std::shared_ptr<const jsontitan::core::JsonNode> clipboardValue();
    void warnIfError(const QString& title,
                     const std::optional<jsontitan::core::EditError>& error);

    // Shared mutation install: invalidate search, replace the root, clear
    // the filter, mark modified, restore `selection`.
    void installRoot(const std::shared_ptr<const jsontitan::core::JsonNode>& root,
                     const jsontitan::core::NodePath& selection);

    // Queue installRoot (plus an optional pushUndo of `undoEntry`) onto the
    // event loop — the reentrancy-safe install path for delegate commits and
    // undo/redo.
    void queueInstall(std::shared_ptr<const jsontitan::core::JsonNode> root,
                      jsontitan::core::NodePath selection,
                      std::optional<jsontitan::core::UndoEntry> undoEntry);

    QWidget* m_dialogParent = nullptr;
    QTreeView* m_treeView = nullptr;
    TreeModel* m_treeModel = nullptr;
    FilterProxyModel* m_filterProxy = nullptr;
    DocumentSession* m_session = nullptr;
    SearchController* m_searchController = nullptr;
};
