#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "core/deletion_engine.h"  // NodePath
#include "core/json_node.h"

namespace jsontitan::core {

// One undoable document state: the root BEFORE a mutation, plus the path of
// the node to reselect when this state is restored.
struct UndoEntry {
    std::shared_ptr<const JsonNode> root;
    NodePath selection = {};
};

// Bounded undo/redo stack over immutable tree roots. Structural sharing
// keeps most of each entry shared with the live document; what an entry
// holds alone is the copied child-pointer arrays along the edited path,
// which for a direct child of a 10M-element array is a 160 MB array per
// entry. So the history is bounded by entry count and by those bytes; the
// most recent entry is always kept.
class UndoStack {
public:
    static constexpr std::size_t kDefaultByteBudget = 256 * 1024 * 1024;

    explicit UndoStack(std::size_t capacity = 50,
                       std::size_t byteBudget = kDefaultByteBudget)
        : m_capacity(capacity), m_byteBudget(byteBudget) {}

    // Record the pre-mutation state. Clears the redo history (a new edit
    // after undo forks the timeline) and evicts the oldest entries past
    // the count or byte budget.
    void push(UndoEntry entry) {
        m_redo.clear();
        pushUndo(std::move(entry));
    }

    // Exchange `current` (the live document state) for the most recent undo
    // entry. Returns std::nullopt when there is nothing to undo.
    auto undo(UndoEntry current) -> std::optional<UndoEntry> {
        if (m_undo.empty()) {
            return std::nullopt;
        }
        UndoEntry restored = std::move(m_undo.back().entry);
        m_undoBytes -= m_undo.back().bytes;
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
        pushUndo(std::move(current));
        return restored;
    }

    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }

    // Drop all history (e.g. when a new file is opened).
    void clear() {
        m_undo.clear();
        m_redo.clear();
        m_undoBytes = 0;
    }

    // Bytes an entry holds alone: the child-pointer arrays of the nodes on
    // its path (the ones its mutation copied), measured in its own root.
    [[nodiscard]] static auto estimatedBytes(const UndoEntry& entry) -> std::size_t {
        std::size_t bytes = 0;
        const JsonNode* node = entry.root.get();
        for (const auto& segment : entry.selection) {
            if (!node) {
                break;
            }
            bytes += node->children.size() * sizeof(node->children[0]);
            const JsonNode* next = nullptr;
            if (const auto* key = std::get_if<std::string>(&segment)) {
                for (const auto& child : node->children) {
                    if (child->key == *key) {
                        next = child.get();
                        break;
                    }
                }
            } else if (const auto index = std::get<std::size_t>(segment);
                       index < node->children.size()) {
                next = node->children[index].get();
            }
            node = next;
        }
        if (node) {
            bytes += node->children.size() * sizeof(node->children[0]);
        }
        return bytes;
    }

private:
    struct Stored {
        UndoEntry entry;
        std::size_t bytes;
    };

    void pushUndo(UndoEntry entry) {
        const std::size_t bytes = estimatedBytes(entry);
        m_undo.push_back(Stored{std::move(entry), bytes});
        m_undoBytes += bytes;
        while (m_undo.size() > 1 &&
               (m_undo.size() > m_capacity || m_undoBytes > m_byteBudget)) {
            m_undoBytes -= m_undo.front().bytes;
            m_undo.pop_front();
        }
    }

    std::size_t m_capacity;
    std::size_t m_byteBudget;
    std::deque<Stored> m_undo;
    std::size_t m_undoBytes = 0;
    std::deque<UndoEntry> m_redo;
};

} // namespace jsontitan::core
