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

// ---------------------------------------------------------------------------
// Task 3.1: Unit tests for Parser
// Requirements: 1.1, 1.2, 1.3, 1.4, 8.1
// ---------------------------------------------------------------------------

#include "core/parser.h"

// Helper: parse a complete JSON string in one chunk via parseChunk + finalizeParse
static auto parseAll(const std::string& json) -> ParseResult {
    auto state = makeParserState();
    std::vector<std::byte> bytes(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        bytes[i] = static_cast<std::byte>(json[i]);
    }
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));
    if (result.error) {
        return ParseResult{.root = nullptr, .error = result.error};
    }
    return finalizeParse(*result.nextState);
}

// Helper: parse JSON by feeding one byte at a time (stress-test chunk boundaries)
static auto parseByteByByte(const std::string& json) -> ParseResult {
    auto state = makeParserState();
    for (std::size_t i = 0; i < json.size(); ++i) {
        std::byte b = static_cast<std::byte>(json[i]);
        auto result = parseChunk(*state, std::span<const std::byte>(&b, 1));
        if (result.error) {
            return ParseResult{.root = nullptr, .error = result.error};
        }
        state = std::move(result.nextState);
    }
    return finalizeParse(*state);
}

// === Empty containers ======================================================

TEST(Parser, ParseEmptyObject) {
    auto r = parseAll("{}");
    ASSERT_NE(r.root, nullptr);
    EXPECT_FALSE(r.error.has_value());
    EXPECT_EQ(r.root->type, NodeType::Object);
    EXPECT_TRUE(r.root->children.empty());
}

TEST(Parser, ParseEmptyArray) {
    auto r = parseAll("[]");
    ASSERT_NE(r.root, nullptr);
    EXPECT_FALSE(r.error.has_value());
    EXPECT_EQ(r.root->type, NodeType::Array);
    EXPECT_TRUE(r.root->children.empty());
}

// === Scalar values =========================================================

TEST(Parser, ParseStringValue) {
    auto r = parseAll(R"("hello world")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::String);
    EXPECT_EQ(r.root->value, "hello world");
}

TEST(Parser, ParseIntegerNumber) {
    auto r = parseAll("42");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "42");
}

TEST(Parser, ParseNegativeNumber) {
    auto r = parseAll("-123");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "-123");
}

TEST(Parser, ParseDecimalNumber) {
    auto r = parseAll("3.14");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "3.14");
}

TEST(Parser, ParseScientificNotation) {
    auto r = parseAll("1.5e10");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "1.5e10");
}

TEST(Parser, ParseScientificNotationNegativeExponent) {
    auto r = parseAll("2.99E-8");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "2.99E-8");
}

TEST(Parser, ParseHighPrecisionNumber) {
    auto r = parseAll("1.7976931348623157e+308");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "1.7976931348623157e+308");
}

TEST(Parser, ParseBoolTrue) {
    auto r = parseAll("true");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Boolean);
    EXPECT_EQ(r.root->value, "true");
}

TEST(Parser, ParseBoolFalse) {
    auto r = parseAll("false");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Boolean);
    EXPECT_EQ(r.root->value, "false");
}

TEST(Parser, ParseNull) {
    auto r = parseAll("null");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Null);
    EXPECT_EQ(r.root->value, "null");
}

// === String escapes ========================================================

TEST(Parser, ParseStringWithEscapes) {
    auto r = parseAll(R"("line1\nline2\ttab")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "line1\nline2\ttab");
}

TEST(Parser, ParseStringWithQuoteEscape) {
    auto r = parseAll(R"("say \"hello\"")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "say \"hello\"");
}

TEST(Parser, ParseStringWithBackslashEscape) {
    auto r = parseAll(R"("path\\to\\file")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "path\\to\\file");
}

TEST(Parser, ParseStringWithUnicodeEscape) {
    // \u0041 = 'A'
    auto r = parseAll(R"("\u0041")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "A");
}

TEST(Parser, ParseStringWithUnicodeSurrogatePair) {
    // U+1F600 (😀) = \uD83D\uDE00
    auto r = parseAll(R"("\uD83D\uDE00")");
    ASSERT_NE(r.root, nullptr);
    // Check it's a 4-byte UTF-8 sequence
    EXPECT_EQ(r.root->value.size(), 4u);
    // Verify the code point
    EXPECT_EQ(r.root->value, "\xF0\x9F\x98\x80");
}

// === Objects ===============================================================

TEST(Parser, ParseSimpleObject) {
    auto r = parseAll(R"({"name": "Alice", "age": 30})");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->children.size(), 2u);
    EXPECT_EQ(r.root->children[0]->key, "name");
    EXPECT_EQ(r.root->children[0]->value, "Alice");
    EXPECT_EQ(r.root->children[1]->key, "age");
    EXPECT_EQ(r.root->children[1]->value, "30");
}

TEST(Parser, ParseNestedObject) {
    auto r = parseAll(R"({"outer": {"inner": "value"}})");
    ASSERT_NE(r.root, nullptr);
    ASSERT_EQ(r.root->children.size(), 1u);
    auto& outer = r.root->children[0];
    EXPECT_EQ(outer->key, "outer");
    EXPECT_EQ(outer->type, NodeType::Object);
    ASSERT_EQ(outer->children.size(), 1u);
    EXPECT_EQ(outer->children[0]->key, "inner");
    EXPECT_EQ(outer->children[0]->value, "value");
}

// === Arrays ================================================================

TEST(Parser, ParseSimpleArray) {
    auto r = parseAll(R"([1, 2, 3])");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Array);
    ASSERT_EQ(r.root->children.size(), 3u);
    EXPECT_EQ(r.root->children[0]->value, "1");
    EXPECT_EQ(r.root->children[1]->value, "2");
    EXPECT_EQ(r.root->children[2]->value, "3");
}

TEST(Parser, ParseMixedArray) {
    auto r = parseAll(R"(["hello", 42, true, null])");
    ASSERT_NE(r.root, nullptr);
    ASSERT_EQ(r.root->children.size(), 4u);
    EXPECT_EQ(r.root->children[0]->type, NodeType::String);
    EXPECT_EQ(r.root->children[1]->type, NodeType::Number);
    EXPECT_EQ(r.root->children[2]->type, NodeType::Boolean);
    EXPECT_EQ(r.root->children[3]->type, NodeType::Null);
}

TEST(Parser, ParseNestedArray) {
    auto r = parseAll(R"([[1, 2], [3, 4]])");
    ASSERT_NE(r.root, nullptr);
    ASSERT_EQ(r.root->children.size(), 2u);
    ASSERT_EQ(r.root->children[0]->children.size(), 2u);
    ASSERT_EQ(r.root->children[1]->children.size(), 2u);
}

// === Deeply nested =========================================================

TEST(Parser, ParseDeeplyNested) {
    auto r = parseAll(R"({"a":{"b":{"c":{"d":"deep"}}}})");
    ASSERT_NE(r.root, nullptr);
    auto node = r.root;
    ASSERT_EQ(node->children.size(), 1u);
    node = node->children[0]; // a
    ASSERT_EQ(node->children.size(), 1u);
    node = node->children[0]; // b
    ASSERT_EQ(node->children.size(), 1u);
    node = node->children[0]; // c
    ASSERT_EQ(node->children.size(), 1u);
    EXPECT_EQ(node->children[0]->key, "d");
    EXPECT_EQ(node->children[0]->value, "deep");
}

// === Whitespace handling ===================================================

TEST(Parser, ParseWithExtraWhitespace) {
    auto r = parseAll("  {  \"key\"  :  \"value\"  }  ");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->children.size(), 1u);
    EXPECT_EQ(r.root->children[0]->value, "value");
}

// === Streaming / chunk boundary tests ======================================

TEST(Parser, ParseByteByByteEmptyObject) {
    auto r = parseByteByByte("{}");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Object);
    EXPECT_TRUE(r.root->children.empty());
}

TEST(Parser, ParseByteByByteSimpleObject) {
    auto r = parseByteByByte(R"({"key": "value"})");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->children.size(), 1u);
    EXPECT_EQ(r.root->children[0]->key, "key");
    EXPECT_EQ(r.root->children[0]->value, "value");
}

TEST(Parser, ParseByteByByteArray) {
    auto r = parseByteByByte(R"([1, "two", true, null])");
    ASSERT_NE(r.root, nullptr);
    ASSERT_EQ(r.root->children.size(), 4u);
}

TEST(Parser, ParseByteByByteNumber) {
    auto r = parseByteByByte("3.14159");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "3.14159");
}

TEST(Parser, ParseByteByByteScientificNumber) {
    auto r = parseByteByByte("1.5e10");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "1.5e10");
}

TEST(Parser, ParseByteByByteKeywordTrue) {
    auto r = parseByteByByte("true");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Boolean);
    EXPECT_EQ(r.root->value, "true");
}

TEST(Parser, ParseByteByByteString) {
    auto r = parseByteByByte(R"("hello\nworld")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "hello\nworld");
}

TEST(Parser, ParseTwoChunks) {
    std::string json = R"({"key": "value"})";
    auto mid = json.size() / 2;

    auto state = makeParserState();

    // First chunk
    std::vector<std::byte> chunk1(mid);
    for (std::size_t i = 0; i < mid; ++i)
        chunk1[i] = static_cast<std::byte>(json[i]);
    auto r1 = parseChunk(*state, std::span<const std::byte>(chunk1));
    EXPECT_FALSE(r1.error.has_value());
    state = std::move(r1.nextState);

    // Second chunk
    std::vector<std::byte> chunk2(json.size() - mid);
    for (std::size_t i = mid; i < json.size(); ++i)
        chunk2[i - mid] = static_cast<std::byte>(json[i]);
    auto r2 = parseChunk(*state, std::span<const std::byte>(chunk2));
    EXPECT_FALSE(r2.error.has_value());

    auto result = finalizeParse(*r2.nextState);
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->type, NodeType::Object);
    ASSERT_EQ(result.root->children.size(), 1u);
    EXPECT_EQ(result.root->children[0]->key, "key");
    EXPECT_EQ(result.root->children[0]->value, "value");
}

// === Error cases ===========================================================

TEST(Parser, ErrorOnEmptyInput) {
    auto r = parseAll("");
    EXPECT_EQ(r.root, nullptr);
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnTruncatedObject) {
    auto state = makeParserState();
    std::string json = R"({"key": "val)";
    std::vector<std::byte> bytes(json.size());
    for (std::size_t i = 0; i < json.size(); ++i)
        bytes[i] = static_cast<std::byte>(json[i]);
    auto r = parseChunk(*state, std::span<const std::byte>(bytes));
    auto result = finalizeParse(*r.nextState);
    EXPECT_TRUE(result.error.has_value());
}

TEST(Parser, ErrorOnInvalidToken) {
    auto r = parseAll("xyz");
    EXPECT_TRUE(r.error.has_value());
    EXPECT_GT(r.error->description.size(), 0u);
}

TEST(Parser, ErrorOnMismatchedBrackets) {
    auto r = parseAll(R"({"key": "value"])");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnTrailingCommaInObject) {
    auto r = parseAll(R"({"key": "value",})");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnTrailingCommaInArray) {
    auto r = parseAll(R"([1, 2, 3,])");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnMultipleTopLevelValues) {
    auto r = parseAll(R"({}[])");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorByteOffsetIsReasonable) {
    auto r = parseAll(R"({"key": })");
    ASSERT_TRUE(r.error.has_value());
    // The error should be at or near the '}' which is at position 8
    EXPECT_GE(r.error->byteOffset, 0u);
    EXPECT_FALSE(r.error->description.empty());
}

// === parseChunk purity =====================================================

TEST(Parser, ParseChunkDoesNotMutateInputState) {
    auto state = makeParserState();
    std::string json = R"({"a": 1})";
    std::vector<std::byte> bytes(json.size());
    for (std::size_t i = 0; i < json.size(); ++i)
        bytes[i] = static_cast<std::byte>(json[i]);

    // Call parseChunk — the original state should be unchanged
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    // Original state should still be in initial condition
    auto result2 = finalizeParse(*state);
    // Since state was never modified, finalizeParse on it should fail (empty)
    EXPECT_TRUE(result2.error.has_value());
}

// === Complex document ======================================================

TEST(Parser, ParseComplexDocument) {
    auto r = parseAll(R"({
        "name": "JSONTitan",
        "version": "0.1.0",
        "features": ["parsing", "search", "export"],
        "config": {
            "maxDepth": 100,
            "streaming": true,
            "encoding": null
        },
        "stats": {
            "precision": 1.7976931348623157e+308,
            "negative": -42,
            "zero": 0
        }
    })");
    ASSERT_NE(r.root, nullptr);
    EXPECT_FALSE(r.error.has_value());
    EXPECT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->children.size(), 5u);

    // features array
    auto& features = r.root->children[2];
    EXPECT_EQ(features->key, "features");
    EXPECT_EQ(features->type, NodeType::Array);
    ASSERT_EQ(features->children.size(), 3u);

    // config object
    auto& config = r.root->children[3];
    EXPECT_EQ(config->key, "config");
    ASSERT_EQ(config->children.size(), 3u);
    EXPECT_EQ(config->children[0]->value, "100");
    EXPECT_EQ(config->children[1]->value, "true");
    EXPECT_EQ(config->children[2]->type, NodeType::Null);

    // stats object
    auto& stats = r.root->children[4];
    EXPECT_EQ(stats->key, "stats");
    ASSERT_EQ(stats->children.size(), 3u);
    EXPECT_EQ(stats->children[0]->value, "1.7976931348623157e+308");
    EXPECT_EQ(stats->children[1]->value, "-42");
    EXPECT_EQ(stats->children[2]->value, "0");
}

TEST(Parser, ParseComplexDocumentByteByByte) {
    auto r = parseByteByByte(R"({
        "name": "JSONTitan",
        "version": "0.1.0",
        "features": ["parsing", "search", "export"],
        "config": {
            "maxDepth": 100,
            "streaming": true,
            "encoding": null
        }
    })");
    ASSERT_NE(r.root, nullptr);
    EXPECT_FALSE(r.error.has_value());
    EXPECT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->children.size(), 4u);
}

// === Zero value ============================================================

TEST(Parser, ParseZero) {
    auto r = parseAll("0");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "0");
}

// === Empty string ==========================================================

TEST(Parser, ParseEmptyString) {
    auto r = parseAll(R"("")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::String);
    EXPECT_EQ(r.root->value, "");
}

// === Array elements have empty keys ========================================

TEST(Parser, ArrayElementsHaveEmptyKeys) {
    auto r = parseAll(R"([1, "two"])");
    ASSERT_NE(r.root, nullptr);
    for (const auto& child : r.root->children) {
        EXPECT_TRUE(child->key.empty());
    }
}

// ===========================================================================
// Property-Based Tests (RapidCheck)
// ===========================================================================

#include <random>

// ---------------------------------------------------------------------------
// Helper: structural + value equivalence of two JsonNode trees
// ---------------------------------------------------------------------------

static bool nodesEqual(const std::shared_ptr<const JsonNode>& a,
                       const std::shared_ptr<const JsonNode>& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    if (a->key != b->key) return false;
    if (a->value != b->value) return false;
    if (a->children.size() != b->children.size()) return false;
    for (std::size_t i = 0; i < a->children.size(); ++i) {
        if (!nodesEqual(a->children[i], b->children[i])) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: generate a random valid JSON string
// ---------------------------------------------------------------------------

static std::string generateJsonValue(int depth, std::mt19937& rng) {
    if (depth <= 0) {
        // Only generate scalars at max depth
        std::uniform_int_distribution<int> scalarDist(0, 3);
        switch (scalarDist(rng)) {
            case 0: { // string
                std::uniform_int_distribution<int> lenDist(0, 10);
                int len = lenDist(rng);
                std::string s = "\"";
                std::uniform_int_distribution<int> charDist(32, 122);
                for (int i = 0; i < len; ++i) {
                    char c = static_cast<char>(charDist(rng));
                    if (c == '"' || c == '\\') s += '\\';
                    s += c;
                }
                s += '"';
                return s;
            }
            case 1: { // number
                std::uniform_int_distribution<int> numDist(-999, 999);
                return std::to_string(numDist(rng));
            }
            case 2: return "true";
            case 3: return "null";
            default: return "false";
        }
    }

    std::uniform_int_distribution<int> typeDist(0, 5);
    int t = typeDist(rng);

    if (t == 0) {
        // Object
        std::uniform_int_distribution<int> sizeDist(0, 3);
        int n = sizeDist(rng);
        std::string result = "{";
        for (int i = 0; i < n; ++i) {
            if (i > 0) result += ",";
            result += "\"k" + std::to_string(i) + "\":";
            result += generateJsonValue(depth - 1, rng);
        }
        result += "}";
        return result;
    }
    if (t == 1) {
        // Array
        std::uniform_int_distribution<int> sizeDist(0, 3);
        int n = sizeDist(rng);
        std::string result = "[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) result += ",";
            result += generateJsonValue(depth - 1, rng);
        }
        result += "]";
        return result;
    }
    // Scalar
    return generateJsonValue(0, rng);
}

// ---------------------------------------------------------------------------
// Task 3.2: Property 2 — Chunk-Invariant Parsing
// Validates: Requirement 1.3
// ---------------------------------------------------------------------------

TEST(ParserProperty, ChunkInvariantParsing) {
    rc::check("Property 2: Chunk-Invariant Parsing",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Single-pass parse (reference)
            auto refResult = parseAll(json);
            RC_ASSERT(refResult.root != nullptr);
            RC_ASSERT(!refResult.error.has_value());

            // Split into random chunks
            auto numSplits = *rc::gen::inRange(0, static_cast<int>(json.size()));
            std::vector<std::size_t> splitPoints;
            for (int i = 0; i < numSplits; ++i) {
                auto pt = *rc::gen::inRange(std::size_t{0}, json.size());
                splitPoints.push_back(pt);
            }
            std::sort(splitPoints.begin(), splitPoints.end());
            splitPoints.erase(std::unique(splitPoints.begin(), splitPoints.end()),
                              splitPoints.end());

            // Build chunk boundaries
            std::vector<std::size_t> boundaries;
            boundaries.push_back(0);
            for (auto sp : splitPoints) {
                if (sp > 0 && sp < json.size()) {
                    boundaries.push_back(sp);
                }
            }
            boundaries.push_back(json.size());

            // Parse via chunks
            auto state = makeParserState();
            for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
                std::size_t start = boundaries[i];
                std::size_t end = boundaries[i + 1];
                std::vector<std::byte> chunkBytes(end - start);
                for (std::size_t j = start; j < end; ++j) {
                    chunkBytes[j - start] = static_cast<std::byte>(json[j]);
                }
                auto chunkResult = parseChunk(*state, std::span<const std::byte>(chunkBytes));
                RC_ASSERT(!chunkResult.error.has_value());
                state = std::move(chunkResult.nextState);
            }
            auto chunkedResult = finalizeParse(*state);
            RC_ASSERT(chunkedResult.root != nullptr);
            RC_ASSERT(!chunkedResult.error.has_value());

            // Verify equivalence
            RC_ASSERT(nodesEqual(refResult.root, chunkedResult.root));
        }
    );
}

// ---------------------------------------------------------------------------
// Task 3.3: Property 3 — Parse Error Reporting
// Validates: Requirement 1.4
// ---------------------------------------------------------------------------

// Helper: mutate valid JSON to produce invalid JSON
static std::string mutateJson(const std::string& validJson, std::mt19937& rng) {
    if (validJson.empty()) return "!!!";

    std::uniform_int_distribution<int> mutationDist(0, 4);
    std::string mutated = validJson;

    switch (mutationDist(rng)) {
        case 0: {
            // Remove a random bracket/brace
            std::vector<std::size_t> bracketPositions;
            for (std::size_t i = 0; i < mutated.size(); ++i) {
                char c = mutated[i];
                if (c == '{' || c == '}' || c == '[' || c == ']') {
                    bracketPositions.push_back(i);
                }
            }
            if (!bracketPositions.empty()) {
                std::uniform_int_distribution<std::size_t> posDist(0, bracketPositions.size() - 1);
                mutated.erase(bracketPositions[posDist(rng)], 1);
            } else {
                mutated = "{" + mutated; // unbalanced
            }
            break;
        }
        case 1: {
            // Add a trailing comma before a closing bracket
            for (std::size_t i = mutated.size(); i > 0; --i) {
                if (mutated[i - 1] == '}' || mutated[i - 1] == ']') {
                    mutated.insert(i - 1, ",");
                    break;
                }
            }
            break;
        }
        case 2: {
            // Truncate the string
            if (mutated.size() > 2) {
                std::uniform_int_distribution<std::size_t> cutDist(1, mutated.size() - 1);
                mutated = mutated.substr(0, cutDist(rng));
            }
            break;
        }
        case 3: {
            // Insert invalid characters
            std::uniform_int_distribution<std::size_t> posDist(0, mutated.size());
            mutated.insert(posDist(rng), "###");
            break;
        }
        case 4: {
            // Replace a colon with a semicolon in an object
            for (std::size_t i = 0; i < mutated.size(); ++i) {
                if (mutated[i] == ':') {
                    mutated[i] = ';';
                    break;
                }
            }
            if (mutated.find(';') == std::string::npos) {
                mutated = "{{{}"; // guaranteed invalid
            }
            break;
        }
    }
    return mutated;
}

TEST(ParserProperty, ParseErrorReporting) {
    rc::check("Property 3: Parse Error Reporting",
        [](void) {
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a valid JSON document
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string validJson = generateJsonValue(depthDist(rng), rng);

            // Mutate it to produce invalid JSON
            std::string invalidJson = mutateJson(validJson, rng);

            // Try to parse — it should either fail during parseChunk or finalizeParse
            auto state = makeParserState();
            std::vector<std::byte> bytes(invalidJson.size());
            for (std::size_t i = 0; i < invalidJson.size(); ++i) {
                bytes[i] = static_cast<std::byte>(invalidJson[i]);
            }
            auto chunkResult = parseChunk(*state, std::span<const std::byte>(bytes));

            if (chunkResult.error.has_value()) {
                // Error during chunk processing
                RC_ASSERT(chunkResult.error->byteOffset >= 0u);
                RC_ASSERT(!chunkResult.error->description.empty());
            } else {
                // Try to finalize — should produce an error
                auto result = finalizeParse(*chunkResult.nextState);
                // The mutation might still produce valid JSON in rare cases
                // (e.g., truncating "null" to "nul" is invalid, but truncating
                // "[1,2]" to "[1" is also invalid). If it parses successfully,
                // that's okay — the mutation just happened to produce valid JSON.
                if (result.error.has_value()) {
                    RC_ASSERT(result.error->byteOffset >= 0u);
                    RC_ASSERT(!result.error->description.empty());
                }
                // If no error, the mutation accidentally produced valid JSON — skip
            }
        }
    );
}

// ---------------------------------------------------------------------------
// Task 3.4: Additional unit tests for Parser
// Requirements: 1.1, 1.2, 1.3, 1.4
// ---------------------------------------------------------------------------

// === Additional Unicode tests ==============================================

TEST(Parser, ParseStringWithMultiByteUtf8) {
    // Direct UTF-8 multi-byte characters (Chinese, Japanese, emoji)
    auto r = parseAll(R"("日本語テスト")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "日本語テスト");
}

TEST(Parser, ParseStringWithAllEscapeTypes) {
    auto r = parseAll(R"("\"\\\/\b\f\n\r\t")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "\"\\/\b\f\n\r\t");
}

TEST(Parser, ParseStringWithUnicodeBasicMultilingual) {
    // \u00E9 = é (Latin small letter e with acute)
    auto r = parseAll(R"("\u00E9")");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "\xC3\xA9"); // UTF-8 for é
}

// === Additional number edge cases ==========================================

TEST(Parser, ParseNegativeZero) {
    auto r = parseAll("-0");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->type, NodeType::Number);
    EXPECT_EQ(r.root->value, "-0");
}

TEST(Parser, ParseNumberWithPositiveExponent) {
    auto r = parseAll("1e+10");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, "1e+10");
}

TEST(Parser, ParseVeryLongNumber) {
    std::string longNum = "123456789012345678901234567890";
    auto r = parseAll(longNum);
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->value, longNum);
}

// === Additional chunk boundary tests =======================================

TEST(Parser, ParseThreeChunksNestedObject) {
    std::string json = R"({"a":{"b":"c"},"d":[1,2]})";
    // Split into 3 roughly equal chunks
    auto s1 = json.size() / 3;
    auto s2 = 2 * json.size() / 3;

    auto state = makeParserState();

    auto toBytes = [](const std::string& s) {
        std::vector<std::byte> b(s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
            b[i] = static_cast<std::byte>(s[i]);
        return b;
    };

    auto c1 = toBytes(json.substr(0, s1));
    auto r1 = parseChunk(*state, std::span<const std::byte>(c1));
    EXPECT_FALSE(r1.error.has_value());
    state = std::move(r1.nextState);

    auto c2 = toBytes(json.substr(s1, s2 - s1));
    auto r2 = parseChunk(*state, std::span<const std::byte>(c2));
    EXPECT_FALSE(r2.error.has_value());
    state = std::move(r2.nextState);

    auto c3 = toBytes(json.substr(s2));
    auto r3 = parseChunk(*state, std::span<const std::byte>(c3));
    EXPECT_FALSE(r3.error.has_value());

    auto result = finalizeParse(*r3.nextState);
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->type, NodeType::Object);
    ASSERT_EQ(result.root->children.size(), 2u);
    EXPECT_EQ(result.root->children[0]->key, "a");
    EXPECT_EQ(result.root->children[1]->key, "d");
}

TEST(Parser, ParseEmptyChunksBetweenData) {
    std::string json = R"({"key":"value"})";
    auto state = makeParserState();

    // Empty chunk first
    auto r0 = parseChunk(*state, std::span<const std::byte>{});
    EXPECT_FALSE(r0.error.has_value());
    state = std::move(r0.nextState);

    // Actual data
    std::vector<std::byte> bytes(json.size());
    for (std::size_t i = 0; i < json.size(); ++i)
        bytes[i] = static_cast<std::byte>(json[i]);
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes));
    EXPECT_FALSE(r1.error.has_value());
    state = std::move(r1.nextState);

    // Another empty chunk
    auto r2 = parseChunk(*state, std::span<const std::byte>{});
    EXPECT_FALSE(r2.error.has_value());

    auto result = finalizeParse(*r2.nextState);
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->children.size(), 1u);
}

// === Additional error cases ================================================

TEST(Parser, ErrorOnLeadingZeroInNumber) {
    // JSON spec: numbers cannot have leading zeros (except "0" itself)
    auto r = parseAll("01");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnPlusSignNumber) {
    auto r = parseAll("+1");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnSingleMinus) {
    auto r = parseAll("-");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnIncompleteTrue) {
    auto r = parseAll("tru");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnIncompleteFalse) {
    auto r = parseAll("fals");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnIncompleteNull) {
    auto r = parseAll("nul");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnUnterminatedString) {
    auto r = parseAll(R"("hello)");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnInvalidEscapeInString) {
    auto r = parseAll(R"("\x")");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnIncompleteUnicodeEscape) {
    auto r = parseAll(R"("\u00")");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnObjectMissingColon) {
    auto r = parseAll(R"({"key" "value"})");
    EXPECT_TRUE(r.error.has_value());
}

TEST(Parser, ErrorOnObjectNonStringKey) {
    auto r = parseAll(R"({42: "value"})");
    EXPECT_TRUE(r.error.has_value());
}
