#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

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
// keeps most of each entry shared with the state after it; what an entry
// holds alone is the containers its mutation replaced, whose child-pointer
// arrays can be large (a direct child of a 10M-element array: 160 MB per
// entry). So the history is bounded by entry count and by those bytes; the
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

    // Bytes `before` holds that `after` does not share: the child arrays of
    // the containers replaced between them. Where both sides have as many
    // children, the changed ones are paired by position and descended
    // (an edit in place); a child that only changed position (Move, Sort
    // Keys) is still shared and skipped. Where the count changed (insert,
    // delete) the old array alone is counted. Costs about as much as the
    // mutation itself did.
    [[nodiscard]] static auto unsharedBytes(const JsonNode& before, const JsonNode& after)
        -> std::size_t {
        if (&before == &after) {
            return 0;
        }
        const auto& old = before.children;
        const auto& now = after.children;
        std::size_t bytes = old.size() * sizeof(std::shared_ptr<const JsonNode>);
        if (old.size() != now.size()) {
            return bytes;
        }
        // A child that only moved now sits where a different child was, so
        // it is among the new children at changed positions: the set holds
        // those alone (two for a swap, not every child of a big array).
        std::vector<std::size_t> changed;
        for (std::size_t i = 0; i < old.size(); ++i) {
            if (old[i] != now[i]) {
                changed.push_back(i);
            }
        }
        if (changed.size() > 1) {
            std::unordered_set<const JsonNode*> moved;
            moved.reserve(changed.size());
            for (std::size_t i : changed) {
                moved.insert(now[i].get());
            }
            std::erase_if(changed, [&](std::size_t i) { return moved.contains(old[i].get()); });
        }
        for (std::size_t i : changed) {
            bytes += unsharedBytes(*old[i], *now[i]);
        }
        return bytes;
    }

private:
    struct Stored {
        UndoEntry entry;
        std::size_t bytes;
    };

    void pushUndo(UndoEntry entry) {
        // An entry's cost depends on the state after it, which is known now
        // for the previous entry. The newest entry counts as 0 until the
        // next push; it is always kept anyway.
        if (!m_undo.empty() && m_undo.back().entry.root && entry.root) {
            auto& previous = m_undo.back();
            m_undoBytes -= previous.bytes;
            previous.bytes = unsharedBytes(*previous.entry.root, *entry.root);
            m_undoBytes += previous.bytes;
        }
        m_undo.push_back(Stored{std::move(entry), 0});
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
