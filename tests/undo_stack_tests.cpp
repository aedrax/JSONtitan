// ---------------------------------------------------------------------------
// UndoStack Tests
// Phase 5a, Commit C6: bounded undo/redo stack over immutable tree roots
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "core/json_node.h"
#include "core/undo_stack.h"

using namespace jsontitan::core;

namespace {

auto makeLeaf(const std::string& value) -> std::shared_ptr<const JsonNode> {
    return JsonNode::makeString("k", value);
}

auto entry(const std::shared_ptr<const JsonNode>& root,
           NodePath selection = {}) -> UndoEntry {
    return UndoEntry{root, std::move(selection)};
}

}  // namespace

TEST(UndoStack, PushUndoRedoRoundTrip) {
    UndoStack stack;
    auto v1 = makeLeaf("v1");
    auto v2 = makeLeaf("v2");
    NodePath sel = {std::string("a"), std::size_t{2}};

    // Document goes v1 -> v2; pre-mutation state (v1) is pushed.
    stack.push(entry(v1, sel));

    // Undo: current (v2) goes to redo, v1 comes back with its selection.
    auto restored = stack.undo(entry(v2));
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->root.get(), v1.get());
    EXPECT_EQ(restored->selection, sel);

    // Redo: v1 goes back to undo, v2 returns.
    auto redone = stack.redo(entry(v1, sel));
    ASSERT_TRUE(redone.has_value());
    EXPECT_EQ(redone->root.get(), v2.get());

    // And undo works again after the redo.
    EXPECT_TRUE(stack.canUndo());
    auto restored2 = stack.undo(entry(v2));
    ASSERT_TRUE(restored2.has_value());
    EXPECT_EQ(restored2->root.get(), v1.get());
    EXPECT_EQ(restored2->selection, sel);
}

TEST(UndoStack, RedoClearedOnPush) {
    UndoStack stack;
    auto v1 = makeLeaf("v1");
    auto v2 = makeLeaf("v2");
    auto v3 = makeLeaf("v3");

    stack.push(entry(v1));
    auto restored = stack.undo(entry(v2));
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(stack.canRedo());

    // A new edit after undo forks the timeline: redo history is dropped.
    stack.push(entry(v3));
    EXPECT_FALSE(stack.canRedo());
    EXPECT_FALSE(stack.redo(entry(v3)).has_value());
    EXPECT_TRUE(stack.canUndo());
}

TEST(UndoStack, CapacityEvictionDropsOldest) {
    UndoStack stack(3);
    auto v1 = makeLeaf("v1");
    auto v2 = makeLeaf("v2");
    auto v3 = makeLeaf("v3");
    auto v4 = makeLeaf("v4");
    auto live = makeLeaf("live");

    stack.push(entry(v1));
    stack.push(entry(v2));
    stack.push(entry(v3));
    stack.push(entry(v4));  // capacity 3: v1 is evicted

    // Unwinding yields v4, v3, v2 — then nothing (v1 gone).
    auto a = stack.undo(entry(live));
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->root.get(), v4.get());
    auto b = stack.undo(*a);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->root.get(), v3.get());
    auto c = stack.undo(*b);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->root.get(), v2.get());
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.undo(*c).has_value());
}

TEST(UndoStack, CanUndoCanRedoTransitions) {
    UndoStack stack;
    auto v1 = makeLeaf("v1");
    auto v2 = makeLeaf("v2");

    // Empty: neither direction available; ops fail without mutating state.
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_FALSE(stack.undo(entry(v1)).has_value());
    EXPECT_FALSE(stack.redo(entry(v1)).has_value());
    EXPECT_FALSE(stack.canRedo());  // failed undo must not push to redo

    stack.push(entry(v1));
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());

    auto restored = stack.undo(entry(v2));
    ASSERT_TRUE(restored.has_value());
    EXPECT_FALSE(stack.canUndo());
    EXPECT_TRUE(stack.canRedo());

    auto redone = stack.redo(*restored);
    ASSERT_TRUE(redone.has_value());
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, ClearDropsAllHistory) {
    UndoStack stack;
    auto v1 = makeLeaf("v1");
    auto v2 = makeLeaf("v2");
    auto v3 = makeLeaf("v3");

    stack.push(entry(v1));
    stack.push(entry(v2));
    auto restored = stack.undo(entry(v3));
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(stack.canUndo());
    EXPECT_TRUE(stack.canRedo());

    stack.clear();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_FALSE(stack.undo(entry(v3)).has_value());
    EXPECT_FALSE(stack.redo(entry(v3)).has_value());
}
