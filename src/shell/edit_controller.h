#pragma once

#include <QObject>
#include <QString>
#include <QTreeView>
#include <QWidget>

#include <memory>
#include <optional>

#include "core/edit_engine.h"
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

private:
    // Pre-mutation root as a JsonNode tree: the live JsonNode root, or a
    // conversion of the arena backing (the arena itself is untouched; the
    // queued install replaces the backing). Nullptr when nothing is loaded.
    std::shared_ptr<const jsontitan::core::JsonNode> editableRootSnapshot() const;

    // NodePath of the tree view's current selection ({} when none).
    jsontitan::core::NodePath currentSelectionPath() const;

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
