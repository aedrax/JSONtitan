#include <gtest/gtest.h>
#include <rapidcheck.h>

#include "core/json_node.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 2.2: Unit tests for JsonNode factory functions
// Requirements: 1.2, 8.1
// ---------------------------------------------------------------------------

// === makeObject ============================================================

TEST(JsonNodeFactory, MakeObjectProducesCorrectType) {
    auto node = JsonNode::makeObject("root", {});
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Object);
}

TEST(JsonNodeFactory, MakeObjectStoresKey) {
    auto node = JsonNode::makeObject("myObj", {});
    EXPECT_EQ(node->key, "myObj");
}

TEST(JsonNodeFactory, MakeObjectHasEmptyValue) {
    auto node = JsonNode::makeObject("obj", {});
    EXPECT_TRUE(node->value.empty());
}

TEST(JsonNodeFactory, MakeObjectStoresChildren) {
    auto child1 = JsonNode::makeString("a", "hello");
    auto child2 = JsonNode::makeNumber("b", "42");
    auto node = JsonNode::makeObject("parent", {child1, child2});
    ASSERT_EQ(node->children.size(), 2u);
    EXPECT_EQ(node->children[0]->key, "a");
    EXPECT_EQ(node->children[1]->key, "b");
}

TEST(JsonNodeFactory, MakeObjectWithEmptyChildren) {
    auto node = JsonNode::makeObject("empty", {});
    EXPECT_TRUE(node->children.empty());
}

TEST(JsonNodeFactory, MakeObjectWithEmptyKey) {
    auto node = JsonNode::makeObject("", {});
    EXPECT_TRUE(node->key.empty());
    EXPECT_EQ(node->type, NodeType::Object);
}

// === makeArray =============================================================

TEST(JsonNodeFactory, MakeArrayProducesCorrectType) {
    auto node = JsonNode::makeArray("items", {});
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Array);
}

TEST(JsonNodeFactory, MakeArrayStoresKey) {
    auto node = JsonNode::makeArray("list", {});
    EXPECT_EQ(node->key, "list");
}

TEST(JsonNodeFactory, MakeArrayHasEmptyValue) {
    auto node = JsonNode::makeArray("arr", {});
    EXPECT_TRUE(node->value.empty());
}

TEST(JsonNodeFactory, MakeArrayStoresChildren) {
    auto child1 = JsonNode::makeNumber("", "1");
    auto child2 = JsonNode::makeNumber("", "2");
    auto child3 = JsonNode::makeNumber("", "3");
    auto node = JsonNode::makeArray("nums", {child1, child2, child3});
    ASSERT_EQ(node->children.size(), 3u);
    EXPECT_EQ(node->children[0]->value, "1");
    EXPECT_EQ(node->children[1]->value, "2");
    EXPECT_EQ(node->children[2]->value, "3");
}

TEST(JsonNodeFactory, MakeArrayWithEmptyChildren) {
    auto node = JsonNode::makeArray("empty", {});
    EXPECT_TRUE(node->children.empty());
}

// === makeString ============================================================

TEST(JsonNodeFactory, MakeStringProducesCorrectType) {
    auto node = JsonNode::makeString("name", "Alice");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::String);
}

TEST(JsonNodeFactory, MakeStringStoresKeyAndValue) {
    auto node = JsonNode::makeString("greeting", "hello world");
    EXPECT_EQ(node->key, "greeting");
    EXPECT_EQ(node->value, "hello world");
}

TEST(JsonNodeFactory, MakeStringHasEmptyChildren) {
    auto node = JsonNode::makeString("k", "v");
    EXPECT_TRUE(node->children.empty());
}

TEST(JsonNodeFactory, MakeStringPreservesUnicode) {
    auto node = JsonNode::makeString("emoji", "Hello 🌍 世界");
    EXPECT_EQ(node->value, "Hello 🌍 世界");
}

TEST(JsonNodeFactory, MakeStringPreservesEmptyValue) {
    auto node = JsonNode::makeString("empty", "");
    EXPECT_TRUE(node->value.empty());
    EXPECT_EQ(node->type, NodeType::String);
}

TEST(JsonNodeFactory, MakeStringPreservesSpecialCharacters) {
    auto node = JsonNode::makeString("special", "line1\nline2\ttab\"quote\\backslash");
    EXPECT_EQ(node->value, "line1\nline2\ttab\"quote\\backslash");
}

// === makeNumber ============================================================

TEST(JsonNodeFactory, MakeNumberProducesCorrectType) {
    auto node = JsonNode::makeNumber("count", "42");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
}

TEST(JsonNodeFactory, MakeNumberStoresKeyAndValue) {
    auto node = JsonNode::makeNumber("pi", "3.14159265358979323846");
    EXPECT_EQ(node->key, "pi");
    EXPECT_EQ(node->value, "3.14159265358979323846");
}

TEST(JsonNodeFactory, MakeNumberHasEmptyChildren) {
    auto node = JsonNode::makeNumber("n", "0");
    EXPECT_TRUE(node->children.empty());
}

TEST(JsonNodeFactory, MakeNumberPreservesHighPrecision) {
    // Requirement 2.4: preserve numeric precision
    const std::string highPrecision = "1.7976931348623157e+308";
    auto node = JsonNode::makeNumber("max", highPrecision);
    EXPECT_EQ(node->value, highPrecision);
}

TEST(JsonNodeFactory, MakeNumberPreservesNegativeValues) {
    auto node = JsonNode::makeNumber("neg", "-999.123");
    EXPECT_EQ(node->value, "-999.123");
}

TEST(JsonNodeFactory, MakeNumberPreservesScientificNotation) {
    auto node = JsonNode::makeNumber("sci", "6.022e23");
    EXPECT_EQ(node->value, "6.022e23");
}

TEST(JsonNodeFactory, MakeNumberPreservesZero) {
    auto node = JsonNode::makeNumber("zero", "0");
    EXPECT_EQ(node->value, "0");
}

// === makeBool ==============================================================

TEST(JsonNodeFactory, MakeBoolTrueProducesCorrectType) {
    auto node = JsonNode::makeBool("flag", true);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Boolean);
}

TEST(JsonNodeFactory, MakeBoolTrueStoresValueAsString) {
    auto node = JsonNode::makeBool("active", true);
    EXPECT_EQ(node->value, "true");
}

TEST(JsonNodeFactory, MakeBoolFalseStoresValueAsString) {
    auto node = JsonNode::makeBool("active", false);
    EXPECT_EQ(node->value, "false");
}

TEST(JsonNodeFactory, MakeBoolStoresKey) {
    auto node = JsonNode::makeBool("enabled", true);
    EXPECT_EQ(node->key, "enabled");
}

TEST(JsonNodeFactory, MakeBoolHasEmptyChildren) {
    auto node = JsonNode::makeBool("b", true);
    EXPECT_TRUE(node->children.empty());
}

// === makeNull ==============================================================

TEST(JsonNodeFactory, MakeNullProducesCorrectType) {
    auto node = JsonNode::makeNull("nothing");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Null);
}

TEST(JsonNodeFactory, MakeNullStoresKey) {
    auto node = JsonNode::makeNull("absent");
    EXPECT_EQ(node->key, "absent");
}

TEST(JsonNodeFactory, MakeNullStoresNullValue) {
    auto node = JsonNode::makeNull("n");
    EXPECT_EQ(node->value, "null");
}

TEST(JsonNodeFactory, MakeNullHasEmptyChildren) {
    auto node = JsonNode::makeNull("n");
    EXPECT_TRUE(node->children.empty());
}

// === Immutability ==========================================================

TEST(JsonNodeImmutability, FactoryReturnsSharedPtrToConst) {
    auto node = JsonNode::makeString("k", "v");
    // The type is shared_ptr<const JsonNode> — the pointed-to object is const.
    // This is a compile-time guarantee. If this test compiles, the type is correct.
    static_assert(std::is_const_v<std::remove_pointer_t<decltype(node.get())>>,
                  "JsonNode factory must return shared_ptr<const JsonNode>");
}

TEST(JsonNodeImmutability, ChildrenAreConstNodes) {
    auto child = JsonNode::makeString("c", "val");
    auto parent = JsonNode::makeObject("p", {child});
    // Each child is also shared_ptr<const JsonNode>
    static_assert(
        std::is_const_v<std::remove_pointer_t<decltype(parent->children[0].get())>>,
        "Children must be shared_ptr<const JsonNode>");
}

TEST(JsonNodeImmutability, SharedOwnershipPreservesNode) {
    auto child = JsonNode::makeString("c", "original");
    auto parent = JsonNode::makeObject("p", {child});

    // Both child and parent->children[0] point to the same node
    EXPECT_EQ(child.get(), parent->children[0].get());
    EXPECT_EQ(child->value, "original");
    EXPECT_EQ(parent->children[0]->value, "original");
}

// === Nested tree structure =================================================

TEST(JsonNodeTree, DeeplyNestedStructure) {
    auto leaf = JsonNode::makeString("leaf", "deep");
    auto level2 = JsonNode::makeObject("l2", {leaf});
    auto level1 = JsonNode::makeArray("l1", {level2});
    auto root = JsonNode::makeObject("root", {level1});

    ASSERT_EQ(root->children.size(), 1u);
    ASSERT_EQ(root->children[0]->type, NodeType::Array);
    ASSERT_EQ(root->children[0]->children.size(), 1u);
    ASSERT_EQ(root->children[0]->children[0]->type, NodeType::Object);
    ASSERT_EQ(root->children[0]->children[0]->children.size(), 1u);
    EXPECT_EQ(root->children[0]->children[0]->children[0]->value, "deep");
}

TEST(JsonNodeTree, MixedTypeChildren) {
    auto strNode = JsonNode::makeString("s", "text");
    auto numNode = JsonNode::makeNumber("n", "123");
    auto boolNode = JsonNode::makeBool("b", true);
    auto nullNode = JsonNode::makeNull("x");
    auto arrNode = JsonNode::makeArray("a", {});
    auto objNode = JsonNode::makeObject("o", {});

    auto root = JsonNode::makeObject("root",
        {strNode, numNode, boolNode, nullNode, arrNode, objNode});

    ASSERT_EQ(root->children.size(), 6u);
    EXPECT_EQ(root->children[0]->type, NodeType::String);
    EXPECT_EQ(root->children[1]->type, NodeType::Number);
    EXPECT_EQ(root->children[2]->type, NodeType::Boolean);
    EXPECT_EQ(root->children[3]->type, NodeType::Null);
    EXPECT_EQ(root->children[4]->type, NodeType::Array);
    EXPECT_EQ(root->children[5]->type, NodeType::Object);
}

// === RapidCheck integration (from task 1) ==================================

TEST(CoreSetup, RapidCheckIntegration) {
    rc::check("trivial property", [](int x) {
        RC_ASSERT(x + 0 == x);
    });
}
