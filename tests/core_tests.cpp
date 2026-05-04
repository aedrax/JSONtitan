#include <gtest/gtest.h>
#include <rapidcheck.h>

#include "core/json_node.h"

using namespace jsontitan::core;

// Trivial test to verify the test infrastructure compiles and runs
TEST(CoreSetup, JsonNodeFactoryCreatesObject) {
    auto node = JsonNode::makeObject("root", {});
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Object);
    EXPECT_EQ(node->key, "root");
    EXPECT_TRUE(node->children.empty());
}

TEST(CoreSetup, JsonNodeFactoryCreatesString) {
    auto node = JsonNode::makeString("name", "hello");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::String);
    EXPECT_EQ(node->key, "name");
    EXPECT_EQ(node->value, "hello");
}

TEST(CoreSetup, RapidCheckIntegration) {
    // Verify RapidCheck links and runs correctly
    rc::check("trivial property", [](int x) {
        RC_ASSERT(x + 0 == x);
    });
}
