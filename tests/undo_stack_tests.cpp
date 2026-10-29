// ---------------------------------------------------------------------------
// UndoStack Tests
// Phase 5a, Commit C6: bounded undo/redo stack over immutable tree roots
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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

// Regression: entries were bounded by count only, and then by the child
// arrays along the selection path, which Replace All (no selection) and
// multi-delete did not cover. The cost is what each entry's root holds that
// the next state does not share.
TEST(UndoStack, ByteBudgetCountsWhatEachEntryHoldsAlone) {
    // {"data": [1000 strings]}, every string replaced each step (Replace All).
    auto version = [](int step) {
        std::vector<std::shared_ptr<const JsonNode>> strings(
            1000, makeLeaf("v" + std::to_string(step)));
        return JsonNode::makeObject("", {JsonNode::makeArray("data", std::move(strings))});
    };
    const std::size_t perStep = UndoStack::unsharedBytes(*version(0), *version(1));
    EXPECT_EQ(perStep, 1001 * sizeof(std::shared_ptr<const JsonNode>));

    // Room for two full entries; the newest (cost not yet known) is extra.
    UndoStack stack(50, 2 * perStep);
    for (int step = 0; step < 6; ++step) {
        stack.push(entry(version(step)));
    }
    int undos = 0;
    while (stack.undo(entry(version(99)))) {
        ++undos;
    }
    EXPECT_EQ(undos, 3);

    // One entry over the whole budget still stays undoable.
    UndoStack tiny(50, 1);
    tiny.push(entry(version(0)));
    tiny.push(entry(version(1)));
    EXPECT_TRUE(tiny.canUndo());
}

// An unchanged subtree is shared, not counted.
TEST(UndoStack, UnsharedBytesSkipsSharedSubtrees) {
    auto shared = JsonNode::makeArray("big", std::vector<std::shared_ptr<const JsonNode>>(
                                                 500, makeLeaf("x")));
    auto before = JsonNode::makeObject("", {shared, makeLeaf("old")});
    auto after = JsonNode::makeObject("", {shared, makeLeaf("new")});
    EXPECT_EQ(UndoStack::unsharedBytes(*before, *after),
              2 * sizeof(std::shared_ptr<const JsonNode>));
}

// Regression: children were paired by position, so after a move or sort
// each was compared with an unrelated sibling and its whole subtree counted,
// although it is still shared. One Move Up in a large array could then
// exceed the budget and evict the whole history.
TEST(UndoStack, UnsharedBytesSkipsMovedChildren) {
    std::vector<std::shared_ptr<const JsonNode>> members;
    for (int i = 0; i < 100; ++i) {
        members.push_back(JsonNode::makeArray(
            "m" + std::to_string(i),
            std::vector<std::shared_ptr<const JsonNode>>(50, makeLeaf("x"))));
    }
    auto before = JsonNode::makeObject("", members);
    const std::size_t oneArray = 100 * sizeof(std::shared_ptr<const JsonNode>);

    auto swapped = members;
    std::swap(swapped[3], swapped[4]);
    EXPECT_EQ(UndoStack::unsharedBytes(*before, *JsonNode::makeObject("", swapped)), oneArray);

    auto reversed = members;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_EQ(UndoStack::unsharedBytes(*before, *JsonNode::makeObject("", reversed)), oneArray);
}
