#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "core/undo_stack.h"
#include "shell/tree_model.h"

// Holds the state of the currently loaded document — either tree backing
// (JsonNode or arena), file identity, union mode, and the modified flag —
// and keeps the TreeModel in sync whenever the backing is replaced.
// Owned by MainWindow via Qt parent ownership.
class DocumentSession : public QObject {
    Q_OBJECT
public:
    explicit DocumentSession(TreeModel* model, QObject* parent = nullptr);

    // Sets the file identity without touching the tree backing. Used at
    // parse start so progress/error reporting can name the file before the
    // (asynchronous) parse result lands.
    void setFileIdentity(const QString& filePath, const QString& fileName,
                         bool unionMode);

    // Installs a JsonNode-backed document: resets the arena backing, updates
    // the file identity, updates the model, and emits documentReplaced().
    void setJsonRoot(std::shared_ptr<const jsontitan::core::JsonNode> root,
                     const QString& filePath, const QString& fileName,
                     bool unionMode);

    // Installs an arena-backed document: resets the JsonNode backing,
    // updates the file identity, updates the model, and emits
    // documentReplaced(). Union mode is left unchanged (matching the
    // pre-extraction parse-completion handler).
    void setArenaRoot(std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                      const QString& filePath, const QString& fileName);

    // Installs a new JsonNode tree while keeping the file identity — used
    // for deletion's new-tree install (and union removal). Does not change
    // the modified flag; the caller decides.
    void replaceJsonRoot(std::shared_ptr<const jsontitan::core::JsonNode> root);

    // Converts an arena-backed document into an editable JsonNode tree
    // (deep copy), switching the model backing in the same operation and
    // releasing the arena. Returns false when nothing is loaded.
    bool ensureEditable();

    // View of the live document root (JsonNode root or the arena root).
    std::optional<jsontitan::core::NodeView> rootView() const;

    bool modified() const { return m_modified; }
    void setModified(bool modified);

    const QString& filePath() const { return m_filePath; }
    const QString& fileName() const { return m_fileName; }
    bool isUnionMode() const { return m_isUnionMode; }

    std::shared_ptr<const jsontitan::core::JsonNode> currentRoot() const {
        return m_currentRoot;
    }
    std::shared_ptr<jsontitan::core::ArenaParseResult> arenaResult() const {
        return m_arenaResult;
    }

    // --- Undo/redo bookkeeping (shared by edit, delete, union-remove) -----
    // The session owns the UndoStack so every mutation flow pushes into the
    // same history. Entries hold the PRE-mutation root plus the selection to
    // restore. The stack is cleared whenever a new document is installed
    // (setJsonRoot / setArenaRoot) but NOT by replaceJsonRoot, which is the
    // in-document mutation install path.

    // Record the pre-mutation state.
    void pushUndo(jsontitan::core::UndoEntry entry);

    // Exchange the live {root, selection} for the most recent undo/redo
    // entry. Returns the state to install (via the caller's install path) or
    // std::nullopt when there is nothing to undo/redo or the document is not
    // JsonNode-backed. Does not install anything itself.
    std::optional<jsontitan::core::UndoEntry>
    undo(jsontitan::core::NodePath currentSelection);
    std::optional<jsontitan::core::UndoEntry>
    redo(jsontitan::core::NodePath currentSelection);

    bool canUndo() const { return m_undoStack.canUndo(); }
    bool canRedo() const { return m_undoStack.canRedo(); }

signals:
    // Emitted after the model has been switched to a new backing.
    void documentReplaced();
    void modifiedChanged(bool modified);
    // Emitted whenever the undo/redo availability may have changed.
    void undoAvailabilityChanged(bool canUndo, bool canRedo);

private:
    TreeModel* m_model = nullptr;

    std::shared_ptr<const jsontitan::core::JsonNode> m_currentRoot;
    std::shared_ptr<jsontitan::core::ArenaParseResult> m_arenaResult;
    QString m_filePath;
    QString m_fileName;
    bool m_isUnionMode = false;
    bool m_modified = false;

    jsontitan::core::UndoStack m_undoStack;

    void clearUndoStack();
    void emitUndoAvailability();
};
