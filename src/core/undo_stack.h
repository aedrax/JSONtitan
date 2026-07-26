#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <utility>

#include "core/deletion_engine.h"  // NodePath
#include "core/json_node.h"

namespace jsontitan::core {

// One undoable document state: the root BEFORE a mutation, plus the path of
// the node to reselect when this state is restored.
struct UndoEntry {
    std::shared_ptr<const JsonNode> root;
    NodePath selection = {};
};

// Bounded undo/redo stack over immutable tree roots. Structural sharing makes
// each entry O(depth) new nodes, so a few dozen entries are cheap even for
// large documents.
class UndoStack {
public:
    explicit UndoStack(std::size_t capacity = 50) : m_capacity(capacity) {}

    // Record the pre-mutation state. Clears the redo history (a new edit
    // after undo forks the timeline) and evicts the oldest entry past
    // capacity.
    void push(UndoEntry entry) {
        m_redo.clear();
        m_undo.push_back(std::move(entry));
        if (m_undo.size() > m_capacity) {
            m_undo.pop_front();
        }
    }

    // Exchange `current` (the live document state) for the most recent undo
    // entry. Returns std::nullopt when there is nothing to undo.
    auto undo(UndoEntry current) -> std::optional<UndoEntry> {
        if (m_undo.empty()) {
            return std::nullopt;
        }
        UndoEntry restored = std::move(m_undo.back());
        m_undo.pop_back();
        m_redo.push_back(std::move(current));
        return restored;
    }

    // Exchange `current` for the most recently undone entry.
    auto redo(UndoEntry current) -> std::optional<UndoEntry> {
        if (m_redo.empty()) {
            return std::nullopt;
        }
        UndoEntry restored = std::move(m_redo.back());
        m_redo.pop_back();
        m_undo.push_back(std::move(current));
        return restored;
    }

    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }

    // Drop all history (e.g. when a new file is opened).
    void clear() {
        m_undo.clear();
        m_redo.clear();
    }

private:
    std::size_t m_capacity;
    std::deque<UndoEntry> m_undo;
    std::deque<UndoEntry> m_redo;
};

} // namespace jsontitan::core
