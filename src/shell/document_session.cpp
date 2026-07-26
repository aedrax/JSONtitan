#include "shell/document_session.h"

#include "core/deletion_engine.h"

DocumentSession::DocumentSession(TreeModel* model, QObject* parent)
    : QObject(parent), m_model(model) {}

void DocumentSession::setFileIdentity(const QString& filePath,
                                      const QString& fileName,
                                      bool unionMode) {
    m_filePath = filePath;
    m_fileName = fileName;
    m_isUnionMode = unionMode;
}

void DocumentSession::setJsonRoot(
    std::shared_ptr<const jsontitan::core::JsonNode> root,
    const QString& filePath, const QString& fileName, bool unionMode) {
    m_currentRoot = std::move(root);
    m_arenaResult.reset();  // Search must target the new tree, not a previously opened file
    m_filePath = filePath;
    m_fileName = fileName;
    m_isUnionMode = unionMode;

    m_model->setRootNode(m_currentRoot);
    emit documentReplaced();

    // A brand-new document: the previous document's history is meaningless.
    clearUndoStack();
}

void DocumentSession::setArenaRoot(
    std::shared_ptr<jsontitan::core::ArenaParseResult> result,
    const QString& filePath, const QString& fileName) {
    m_arenaResult = std::move(result);
    m_currentRoot.reset();  // Clear legacy root when using arena path
    m_filePath = filePath;
    m_fileName = fileName;

    m_model->setArenaRoot(m_arenaResult);
    emit documentReplaced();

    // A brand-new document: the previous document's history is meaningless.
    clearUndoStack();
}

void DocumentSession::replaceJsonRoot(
    std::shared_ptr<const jsontitan::core::JsonNode> root) {
    m_currentRoot = std::move(root);
    m_arenaResult.reset();  // invariant: only one live backing

    m_model->setRootNode(m_currentRoot);
    emit documentReplaced();
}

bool DocumentSession::ensureEditable() {
    if (m_currentRoot) {
        return true;  // Already have an editable root
    }

    if (!m_arenaResult || !m_arenaResult->root) {
        return false;  // Nothing to convert
    }

    // Convert the arena tree to a JsonNode tree (deep copy)
    m_currentRoot = m_arenaResult->root->toJsonNode();
    m_arenaResult.reset();

    // Audit D-10: switch the model to the new backing in the same operation.
    // Without this, the model keeps dangling arena pointers between the
    // conversion and the caller's own setRootNode(). The deletion path calls
    // setRootNode() again with the post-delete tree — resetting the model
    // twice is accepted here (correctness over elegance).
    m_model->setRootNode(m_currentRoot);

    return true;
}

std::optional<jsontitan::core::NodeView> DocumentSession::rootView() const {
    if (m_currentRoot) {
        return jsontitan::core::NodeView(*m_currentRoot);
    }
    if (m_arenaResult && m_arenaResult->root) {
        return jsontitan::core::NodeView(*m_arenaResult->root);
    }
    return std::nullopt;
}

std::size_t DocumentSession::nodeCount() const {
    if (m_arenaResult && m_arenaResult->root) {
        // The parse pipeline already counted the nodes — no walk needed.
        return m_arenaResult->nodeCount;
    }
    if (m_currentRoot) {
        return 1 + jsontitan::core::countDescendants(*m_currentRoot);
    }
    return 0;
}

void DocumentSession::setModified(bool modified) {
    m_modified = modified;
    emit modifiedChanged(modified);
}

// --- Undo/redo bookkeeping -------------------------------------------------

void DocumentSession::pushUndo(jsontitan::core::UndoEntry entry) {
    m_undoStack.push(std::move(entry));
    emitUndoAvailability();
}

std::optional<jsontitan::core::UndoEntry>
DocumentSession::undo(jsontitan::core::NodePath currentSelection) {
    // Undo entries only exist after a mutation, and every mutation installs
    // a JsonNode root — an arena-backed document therefore has an empty
    // stack. The guard is defensive.
    if (!m_currentRoot) {
        return std::nullopt;
    }
    auto restored = m_undoStack.undo(
        jsontitan::core::UndoEntry{m_currentRoot, std::move(currentSelection)});
    emitUndoAvailability();
    return restored;
}

std::optional<jsontitan::core::UndoEntry>
DocumentSession::redo(jsontitan::core::NodePath currentSelection) {
    if (!m_currentRoot) {
        return std::nullopt;
    }
    auto restored = m_undoStack.redo(
        jsontitan::core::UndoEntry{m_currentRoot, std::move(currentSelection)});
    emitUndoAvailability();
    return restored;
}

void DocumentSession::clearUndoStack() {
    m_undoStack.clear();
    emitUndoAvailability();
}

void DocumentSession::emitUndoAvailability() {
    emit undoAvailabilityChanged(m_undoStack.canUndo(), m_undoStack.canRedo());
}
