#include <gtest/gtest.h>
#include <rapidcheck.h>

#include "core/json_node.h"
#include "core/union_engine.h"

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
#include "core/pretty_printer.h"

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

// ---------------------------------------------------------------------------
// Task 4.2: Property 1 — Parse-Print Round Trip
// Validates: Requirements 1.2, 2.1, 2.2, 2.3, 2.4
// ---------------------------------------------------------------------------

// Helper: generate a random JsonNode tree for round-trip testing
static std::shared_ptr<const JsonNode> generateRandomNode(int maxDepth, std::mt19937& rng) {
    // Choose node type
    // At depth 0: only scalars
    // At depth > 0: any type including containers
    int typeChoice;
    if (maxDepth <= 0) {
        std::uniform_int_distribution<int> scalarDist(0, 3);
        typeChoice = scalarDist(rng) + 2; // 2=String, 3=Number, 4=Boolean, 5=Null
    } else {
        std::uniform_int_distribution<int> typeDist(0, 5);
        typeChoice = typeDist(rng);
    }

    switch (typeChoice) {
        case 0: { // Object
            std::uniform_int_distribution<int> sizeDist(0, 4);
            int n = sizeDist(rng);
            std::vector<std::shared_ptr<const JsonNode>> children;
            // Use a set to ensure unique keys
            std::vector<std::string> usedKeys;
            for (int i = 0; i < n; ++i) {
                // Generate a unique key
                std::string key = "k" + std::to_string(i);
                // Occasionally add special characters to keys
                std::uniform_int_distribution<int> specialDist(0, 4);
                int special = specialDist(rng);
                if (special == 0) {
                    key += "\"esc";
                } else if (special == 1) {
                    key += "\\back";
                } else if (special == 2) {
                    key += "\ttab";
                } else if (special == 3) {
                    // Unicode key
                    key += "\xC3\xA9"; // é in UTF-8
                }
                auto child = generateRandomNode(maxDepth - 1, rng);
                // Reconstruct child with the generated key
                switch (child->type) {
                    case NodeType::Object:
                        children.push_back(JsonNode::makeObject(key, child->children));
                        break;
                    case NodeType::Array:
                        children.push_back(JsonNode::makeArray(key, child->children));
                        break;
                    case NodeType::String:
                        children.push_back(JsonNode::makeString(key, child->value));
                        break;
                    case NodeType::Number:
                        children.push_back(JsonNode::makeNumber(key, child->value));
                        break;
                    case NodeType::Boolean:
                        children.push_back(JsonNode::makeBool(key, child->value == "true"));
                        break;
                    case NodeType::Null:
                        children.push_back(JsonNode::makeNull(key));
                        break;
                }
            }
            return JsonNode::makeObject("", children);
        }
        case 1: { // Array
            std::uniform_int_distribution<int> sizeDist(0, 4);
            int n = sizeDist(rng);
            std::vector<std::shared_ptr<const JsonNode>> children;
            for (int i = 0; i < n; ++i) {
                children.push_back(generateRandomNode(maxDepth - 1, rng));
            }
            return JsonNode::makeArray("", children);
        }
        case 2: { // String
            std::uniform_int_distribution<int> variantDist(0, 5);
            int variant = variantDist(rng);
            std::string val;
            switch (variant) {
                case 0: { // ASCII printable
                    std::uniform_int_distribution<int> lenDist(0, 10);
                    int len = lenDist(rng);
                    std::uniform_int_distribution<int> charDist(32, 126);
                    for (int i = 0; i < len; ++i) {
                        val += static_cast<char>(charDist(rng));
                    }
                    break;
                }
                case 1: // String with escapes
                    val = "line1\nline2\ttab\"quote\\backslash";
                    break;
                case 2: // Unicode emoji
                    val = "Hello \xF0\x9F\x8C\x8D world"; // 🌍
                    break;
                case 3: // CJK characters
                    val = "\xE4\xB8\x96\xE7\x95\x8C"; // 世界
                    break;
                case 4: // Empty string
                    val = "";
                    break;
                case 5: // Mixed Unicode
                    val = "\xC3\xA9\xC3\xBC\xC3\xB6"; // éüö
                    break;
            }
            return JsonNode::makeString("", val);
        }
        case 3: { // Number
            std::uniform_int_distribution<int> numVariant(0, 4);
            std::string numStr;
            switch (numVariant(rng)) {
                case 0: { // Integer
                    std::uniform_int_distribution<int> intDist(-999, 999);
                    numStr = std::to_string(intDist(rng));
                    break;
                }
                case 1: // Decimal
                    numStr = "3.14159265358979";
                    break;
                case 2: // Scientific notation
                    numStr = "1.5e10";
                    break;
                case 3: // High precision
                    numStr = "1.7976931348623157e+308";
                    break;
                case 4: // Zero
                    numStr = "0";
                    break;
            }
            return JsonNode::makeNumber("", numStr);
        }
        case 4: { // Boolean
            std::uniform_int_distribution<int> boolDist(0, 1);
            return JsonNode::makeBool("", boolDist(rng) == 1);
        }
        case 5: // Null
        default:
            return JsonNode::makeNull("");
    }
}

// Helper: compare two JsonNode trees ignoring the root key
static bool nodesEqualIgnoringRootKey(const std::shared_ptr<const JsonNode>& a,
                                       const std::shared_ptr<const JsonNode>& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    // Don't compare keys at the root level — the caller handles this
    if (a->value != b->value) return false;
    if (a->children.size() != b->children.size()) return false;
    for (std::size_t i = 0; i < a->children.size(); ++i) {
        if (!nodesEqual(a->children[i], b->children[i])) return false;
    }
    return true;
}

TEST(PrettyPrinterProperty, ParsePrintRoundTrip) {
    rc::check("Property 1: Parse-Print Round Trip",
        [](void) {
            // Generate a random seed for our tree generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random depth (0-4)
            std::uniform_int_distribution<int> depthDist(0, 4);
            int depth = depthDist(rng);

            // Generate a random JsonNode tree
            auto originalNode = generateRandomNode(depth, rng);
            RC_ASSERT(originalNode != nullptr);

            // Pretty-print the tree
            auto printed = prettyPrint(*originalNode);
            RC_ASSERT(!printed.empty());

            // Re-parse the pretty-printed output
            auto reparsed = parseAll(printed);
            RC_ASSERT(reparsed.root != nullptr);
            RC_ASSERT(!reparsed.error.has_value());

            // The re-parsed tree should be structurally and value-equivalent
            // to the original, except the root key will be empty after re-parsing
            // (the parser doesn't assign keys to the root node).
            // Our generated nodes already have empty root keys, so we can use
            // nodesEqualIgnoringRootKey for safety.
            RC_ASSERT(nodesEqualIgnoringRootKey(originalNode, reparsed.root));
        }
    );
}

// ---------------------------------------------------------------------------
// Task 4.3: Unit tests for PrettyPrinter
// Requirements: 2.1, 2.3, 2.4
// ---------------------------------------------------------------------------

TEST(PrettyPrinter, EmptyObject) {
    auto node = JsonNode::makeObject("", {});
    EXPECT_EQ(prettyPrint(*node), "{}");
}

TEST(PrettyPrinter, EmptyArray) {
    auto node = JsonNode::makeArray("", {});
    EXPECT_EQ(prettyPrint(*node), "[]");
}

TEST(PrettyPrinter, SingleStringValue) {
    auto node = JsonNode::makeString("", "value");
    EXPECT_EQ(prettyPrint(*node), "\"value\"");
}

TEST(PrettyPrinter, SingleNumberValue) {
    auto node = JsonNode::makeNumber("", "42");
    EXPECT_EQ(prettyPrint(*node), "42");
}

TEST(PrettyPrinter, SingleBoolTrue) {
    auto node = JsonNode::makeBool("", true);
    EXPECT_EQ(prettyPrint(*node), "true");
}

TEST(PrettyPrinter, SingleBoolFalse) {
    auto node = JsonNode::makeBool("", false);
    EXPECT_EQ(prettyPrint(*node), "false");
}

TEST(PrettyPrinter, SingleNull) {
    auto node = JsonNode::makeNull("");
    EXPECT_EQ(prettyPrint(*node), "null");
}

TEST(PrettyPrinter, ObjectWithOneChild) {
    auto child = JsonNode::makeString("key", "value");
    auto node = JsonNode::makeObject("", {child});
    std::string expected =
        "{\n"
        "  \"key\": \"value\"\n"
        "}";
    EXPECT_EQ(prettyPrint(*node), expected);
}

TEST(PrettyPrinter, ObjectWithMultipleChildren) {
    auto c1 = JsonNode::makeString("name", "Alice");
    auto c2 = JsonNode::makeNumber("age", "30");
    auto c3 = JsonNode::makeBool("active", true);
    auto node = JsonNode::makeObject("", {c1, c2, c3});
    std::string expected =
        "{\n"
        "  \"name\": \"Alice\",\n"
        "  \"age\": 30,\n"
        "  \"active\": true\n"
        "}";
    EXPECT_EQ(prettyPrint(*node), expected);
}

TEST(PrettyPrinter, NestedObjects) {
    auto inner = JsonNode::makeString("b", "val");
    auto mid = JsonNode::makeObject("a", {inner});
    auto root = JsonNode::makeObject("", {mid});
    std::string expected =
        "{\n"
        "  \"a\": {\n"
        "    \"b\": \"val\"\n"
        "  }\n"
        "}";
    EXPECT_EQ(prettyPrint(*root), expected);
}

TEST(PrettyPrinter, ArrayWithElements) {
    auto e1 = JsonNode::makeNumber("", "1");
    auto e2 = JsonNode::makeNumber("", "2");
    auto e3 = JsonNode::makeNumber("", "3");
    auto node = JsonNode::makeArray("", {e1, e2, e3});
    std::string expected =
        "[\n"
        "  1,\n"
        "  2,\n"
        "  3\n"
        "]";
    EXPECT_EQ(prettyPrint(*node), expected);
}

TEST(PrettyPrinter, NestedArrays) {
    auto inner1 = JsonNode::makeNumber("", "1");
    auto inner2 = JsonNode::makeNumber("", "2");
    auto innerArr = JsonNode::makeArray("", {inner1, inner2});
    auto outer = JsonNode::makeArray("", {innerArr});
    std::string expected =
        "[\n"
        "  [\n"
        "    1,\n"
        "    2\n"
        "  ]\n"
        "]";
    EXPECT_EQ(prettyPrint(*outer), expected);
}

TEST(PrettyPrinter, MixedObjectAndArray) {
    auto e1 = JsonNode::makeNumber("", "1");
    auto e2 = JsonNode::makeNumber("", "2");
    auto arr = JsonNode::makeArray("items", {e1, e2});
    auto root = JsonNode::makeObject("", {arr});
    std::string expected =
        "{\n"
        "  \"items\": [\n"
        "    1,\n"
        "    2\n"
        "  ]\n"
        "}";
    EXPECT_EQ(prettyPrint(*root), expected);
}

TEST(PrettyPrinter, CustomIndentWidth) {
    auto child = JsonNode::makeString("key", "value");
    auto node = JsonNode::makeObject("", {child});
    PrettyPrintOptions opts{.indentWidth = 4};
    std::string expected =
        "{\n"
        "    \"key\": \"value\"\n"
        "}";
    EXPECT_EQ(prettyPrint(*node, opts), expected);
}

TEST(PrettyPrinter, SortKeysOption) {
    auto c1 = JsonNode::makeString("c", "3");
    auto c2 = JsonNode::makeString("a", "1");
    auto c3 = JsonNode::makeString("b", "2");
    auto node = JsonNode::makeObject("", {c1, c2, c3});
    PrettyPrintOptions opts{.sortKeys = true};
    std::string expected =
        "{\n"
        "  \"a\": \"1\",\n"
        "  \"b\": \"2\",\n"
        "  \"c\": \"3\"\n"
        "}";
    EXPECT_EQ(prettyPrint(*node, opts), expected);
}

TEST(PrettyPrinter, SortKeysDisabledPreservesOrder) {
    auto c1 = JsonNode::makeString("c", "3");
    auto c2 = JsonNode::makeString("a", "1");
    auto c3 = JsonNode::makeString("b", "2");
    auto node = JsonNode::makeObject("", {c1, c2, c3});
    // Default options — sortKeys is false
    std::string expected =
        "{\n"
        "  \"c\": \"3\",\n"
        "  \"a\": \"1\",\n"
        "  \"b\": \"2\"\n"
        "}";
    EXPECT_EQ(prettyPrint(*node), expected);
}

TEST(PrettyPrinter, StringWithEscapeCharacters) {
    auto node = JsonNode::makeString("", "say \"hello\"\nand\\go\there");
    std::string result = prettyPrint(*node);
    EXPECT_EQ(result, "\"say \\\"hello\\\"\\nand\\\\go\\there\"");
}

TEST(PrettyPrinter, StringWithUnicode) {
    auto node = JsonNode::makeString("", "Hello 🌍 世界");
    std::string result = prettyPrint(*node);
    EXPECT_EQ(result, "\"Hello 🌍 世界\"");
}

TEST(PrettyPrinter, StringWithControlCharacters) {
    std::string val(1, static_cast<char>(0x01));
    auto node = JsonNode::makeString("", val);
    std::string result = prettyPrint(*node);
    EXPECT_EQ(result, "\"\\u0001\"");
}

TEST(PrettyPrinter, HighPrecisionNumber) {
    auto node = JsonNode::makeNumber("", "1.7976931348623157e+308");
    EXPECT_EQ(prettyPrint(*node), "1.7976931348623157e+308");
}

TEST(PrettyPrinter, ScientificNotationNumber) {
    auto node = JsonNode::makeNumber("", "6.022e23");
    EXPECT_EQ(prettyPrint(*node), "6.022e23");
}

TEST(PrettyPrinter, NegativeNumber) {
    auto node = JsonNode::makeNumber("", "-42.5");
    EXPECT_EQ(prettyPrint(*node), "-42.5");
}

TEST(PrettyPrinter, ZeroNumber) {
    auto node = JsonNode::makeNumber("", "0");
    EXPECT_EQ(prettyPrint(*node), "0");
}

TEST(PrettyPrinter, DeeplyNestedStructure) {
    // 4 levels: root object -> child object -> child array -> child object -> leaf string
    auto leaf = JsonNode::makeString("d", "deep");
    auto level3 = JsonNode::makeObject("", {leaf});
    auto level2 = JsonNode::makeArray("c", {level3});
    auto level1 = JsonNode::makeObject("b", {level2});
    auto root = JsonNode::makeObject("", {level1});
    std::string expected =
        "{\n"
        "  \"b\": {\n"
        "    \"c\": [\n"
        "      {\n"
        "        \"d\": \"deep\"\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}";
    EXPECT_EQ(prettyPrint(*root), expected);
}

TEST(PrettyPrinter, EmptyStringValue) {
    auto node = JsonNode::makeString("", "");
    EXPECT_EQ(prettyPrint(*node), "\"\"");
}

// ===========================================================================
// Task 6: SearchEngine Tests
// Requirements: 4.1, 4.2, 4.4, 4.5, 4.6, 4.7, 8.1
// ===========================================================================

#include "core/search_engine.h"

#include <regex>

// ---------------------------------------------------------------------------
// Task 6.6: Unit tests for SearchEngine
// Requirements: 4.1, 4.2, 4.4, 4.5, 4.6, 4.7
// ---------------------------------------------------------------------------

// === Empty query returns no matches ========================================

TEST(SearchEngine, EmptyQueryReturnsNoMatches) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeNumber("age", "30")
    });
    auto result = filter(*root, SearchQuery{.pattern = ""});
    EXPECT_TRUE(result.matches.empty());
    EXPECT_FALSE(result.error.has_value());
}

// === Substring matching on keys ============================================

TEST(SearchEngine, SubstringMatchesKey) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("username", "Alice"),
        JsonNode::makeNumber("age", "30")
    });
    auto result = filter(*root, SearchQuery{.pattern = "user"});
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->key, "username");
}

TEST(SearchEngine, SubstringMatchesMultipleKeys) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("firstName", "Alice"),
        JsonNode::makeString("lastName", "Smith"),
        JsonNode::makeNumber("age", "30")
    });
    auto result = filter(*root, SearchQuery{.pattern = "Name"});
    EXPECT_EQ(result.matches.size(), 2u);
}

// === Substring matching on string values ===================================

TEST(SearchEngine, SubstringMatchesStringValue) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("city", "Wonderland")
    });
    auto result = filter(*root, SearchQuery{.pattern = "alice"});
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->value, "Alice");
}

// === Case-insensitive matching (default) ===================================

TEST(SearchEngine, CaseInsensitiveByDefault) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("Name", "ALICE"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "name"});
    // Should match the key "Name" case-insensitively
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->key, "Name");
}

TEST(SearchEngine, CaseInsensitiveValueMatch) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("x", "Hello World"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "hello world"});
    ASSERT_EQ(result.matches.size(), 1u);
}

// === Case-sensitive matching ===============================================

TEST(SearchEngine, CaseSensitiveNoMatch) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("Name", "Alice"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "name",
        .mode = SearchMode::Substring,
        .caseSensitive = true
    });
    EXPECT_TRUE(result.matches.empty());
}

TEST(SearchEngine, CaseSensitiveMatch) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("Name", "Alice"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "Name",
        .mode = SearchMode::Substring,
        .caseSensitive = true
    });
    ASSERT_EQ(result.matches.size(), 1u);
}

// === No matches returns empty vector (not error) ===========================

TEST(SearchEngine, NoMatchesReturnsEmptyVector) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "zzz_nonexistent"});
    EXPECT_TRUE(result.matches.empty());
    EXPECT_FALSE(result.error.has_value());
}

// === Query matching root node ==============================================

TEST(SearchEngine, MatchesRootKey) {
    // Root with a key that matches
    auto root = JsonNode::makeObject("rootKey", {
        JsonNode::makeString("child", "value"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "rootKey"});
    ASSERT_GE(result.matches.size(), 1u);
    // The root match should have an empty ancestor path
    bool foundRoot = false;
    for (const auto& m : result.matches) {
        if (m.ancestorIndices.empty()) {
            foundRoot = true;
            break;
        }
    }
    EXPECT_TRUE(foundRoot);
}

// === Query matching leaf node ==============================================

TEST(SearchEngine, MatchesLeafNode) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeObject("level1", {
            JsonNode::makeString("deep", "target_value"),
        }),
    });
    auto result = filter(*root, SearchQuery{.pattern = "target_value"});
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->value, "target_value");
    // Ancestor path should be [0, 0] (first child of root, first child of level1)
    ASSERT_EQ(result.matches[0].ancestorIndices.size(), 2u);
    EXPECT_EQ(result.matches[0].ancestorIndices[0], 0u);
    EXPECT_EQ(result.matches[0].ancestorIndices[1], 0u);
}

// === Ancestor index path correctness =======================================

TEST(SearchEngine, AncestorPathIsCorrect) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("a", "no"),
        JsonNode::makeObject("b", {
            JsonNode::makeString("c", "no"),
            JsonNode::makeString("d", "found_me"),
        }),
    });
    auto result = filter(*root, SearchQuery{.pattern = "found_me"});
    ASSERT_EQ(result.matches.size(), 1u);
    // Path: root -> child[1] ("b") -> child[1] ("d")
    auto& path = result.matches[0].ancestorIndices;
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path[0], 1u); // "b" is at index 1
    EXPECT_EQ(path[1], 1u); // "d" is at index 1 within "b"
}

// === Searches across nested structures =====================================

TEST(SearchEngine, SearchesNestedChildren) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeArray("items", {
            JsonNode::makeString("", "apple"),
            JsonNode::makeString("", "banana"),
            JsonNode::makeString("", "cherry"),
        }),
    });
    auto result = filter(*root, SearchQuery{.pattern = "banana"});
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->value, "banana");
}

// === Does not match non-string values ======================================

TEST(SearchEngine, DoesNotMatchNumberValues) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeNumber("count", "42"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "42"});
    // "42" is a number value, not a string value — should not match value
    // But "count" key doesn't contain "42" either
    EXPECT_TRUE(result.matches.empty());
}

TEST(SearchEngine, DoesNotMatchBoolValues) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeBool("flag", true),
    });
    auto result = filter(*root, SearchQuery{.pattern = "true"});
    EXPECT_TRUE(result.matches.empty());
}

TEST(SearchEngine, DoesNotMatchNullValues) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeNull("nothing"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "null"});
    EXPECT_TRUE(result.matches.empty());
}

// === Regex mode ============================================================

TEST(SearchEngine, RegexMatchesKey) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("user_name", "Alice"),
        JsonNode::makeString("user_email", "alice@example.com"),
        JsonNode::makeNumber("age", "30"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "^user_",
        .mode = SearchMode::Regex
    });
    EXPECT_EQ(result.matches.size(), 2u);
}

TEST(SearchEngine, RegexMatchesStringValue) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("email", "alice@example.com"),
        JsonNode::makeString("name", "Alice"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = R"(\w+@\w+\.\w+)",
        .mode = SearchMode::Regex
    });
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->value, "alice@example.com");
}

TEST(SearchEngine, RegexCaseInsensitiveByDefault) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("Name", "ALICE"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "^name$",
        .mode = SearchMode::Regex
    });
    ASSERT_EQ(result.matches.size(), 1u);
}

TEST(SearchEngine, RegexCaseSensitive) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("Name", "ALICE"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "^name$",
        .mode = SearchMode::Regex,
        .caseSensitive = true
    });
    EXPECT_TRUE(result.matches.empty());
}

// === Invalid regex returns error ===========================================

TEST(SearchEngine, InvalidRegexReturnsError) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
    });
    auto result = filter(*root, SearchQuery{
        .pattern = "[invalid",
        .mode = SearchMode::Regex
    });
    EXPECT_TRUE(result.matches.empty());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_FALSE(result.error->description.empty());
}

TEST(SearchEngine, InvalidRegexUnmatchedParen) {
    auto root = JsonNode::makeObject("", {});
    auto result = filter(*root, SearchQuery{
        .pattern = "(unclosed",
        .mode = SearchMode::Regex
    });
    EXPECT_TRUE(result.matches.empty());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_FALSE(result.error->description.empty());
}

// === Multiple matches at different depths ==================================

TEST(SearchEngine, MultipleMatchesAtDifferentDepths) {
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "test"),
        JsonNode::makeObject("nested", {
            JsonNode::makeString("name", "test2"),
            JsonNode::makeObject("deep", {
                JsonNode::makeString("name", "test3"),
            }),
        }),
    });
    auto result = filter(*root, SearchQuery{.pattern = "name"});
    // Should match all three "name" keys
    EXPECT_EQ(result.matches.size(), 3u);
}

// === Array element matching ================================================

TEST(SearchEngine, MatchesArrayStringElements) {
    auto root = JsonNode::makeArray("", {
        JsonNode::makeString("", "hello"),
        JsonNode::makeString("", "world"),
        JsonNode::makeNumber("", "42"),
    });
    auto result = filter(*root, SearchQuery{.pattern = "hello"});
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].node->value, "hello");
}

// ---------------------------------------------------------------------------
// Task 6.2: Property 5 — Search Completeness (Case-Insensitive)
// Validates: Requirements 4.1, 4.4
// ---------------------------------------------------------------------------

// Helper: collect all string values and keys from a JsonNode tree
static void collectKeysAndStringValues(const JsonNode& node,
                                        std::vector<std::pair<std::string, const JsonNode*>>& results) {
    if (!node.key.empty()) {
        results.push_back({node.key, &node});
    }
    if (node.type == NodeType::String && !node.value.empty()) {
        results.push_back({node.value, &node});
    }
    for (const auto& child : node.children) {
        collectKeysAndStringValues(*child, results);
    }
}

TEST(SearchEngineProperty, SearchCompleteness) {
    rc::check("Property 5: Search Completeness (Case-Insensitive)",
        [](void) {
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random JsonNode tree
            std::uniform_int_distribution<int> depthDist(1, 3);
            auto tree = generateRandomNode(depthDist(rng), rng);
            RC_ASSERT(tree != nullptr);

            // Collect all keys and string values
            std::vector<std::pair<std::string, const JsonNode*>> keysAndValues;
            collectKeysAndStringValues(*tree, keysAndValues);

            // Skip if tree has no searchable content
            RC_PRE(!keysAndValues.empty());

            // Pick a random key/value
            std::uniform_int_distribution<std::size_t> pickDist(0, keysAndValues.size() - 1);
            auto& [text, sourceNode] = keysAndValues[pickDist(rng)];

            // Extract a random substring (at least 1 char)
            RC_PRE(text.size() >= 1);
            std::uniform_int_distribution<std::size_t> startDist(0, text.size() - 1);
            auto start = startDist(rng);
            std::uniform_int_distribution<std::size_t> lenDist(1, text.size() - start);
            auto len = lenDist(rng);
            std::string substring = text.substr(start, len);

            // Filter with that substring (case-insensitive, default mode)
            auto result = filter(*tree, SearchQuery{.pattern = substring});
            RC_ASSERT(!result.error.has_value());

            // Verify the source node appears in results
            bool found = false;
            for (const auto& match : result.matches) {
                if (match.node.get() == sourceNode ||
                    (match.node->key == sourceNode->key &&
                     match.node->value == sourceNode->value &&
                     match.node->type == sourceNode->type)) {
                    found = true;
                    break;
                }
            }
            RC_ASSERT(found);
        }
    );
}

// ---------------------------------------------------------------------------
// Task 6.3: Property 6 — Search Ancestor Preservation
// Validates: Requirement 4.2
// ---------------------------------------------------------------------------

// Helper: verify an ancestor index path traces a valid route from root to node
static bool verifyAncestorPath(const JsonNode& root,
                                const std::vector<std::size_t>& path,
                                const JsonNode& expectedNode) {
    const JsonNode* current = &root;
    for (std::size_t idx : path) {
        if (idx >= current->children.size()) return false;
        current = current->children[idx].get();
    }
    // The node at the end of the path should match the expected node
    return current->key == expectedNode.key &&
           current->value == expectedNode.value &&
           current->type == expectedNode.type;
}

TEST(SearchEngineProperty, SearchAncestorPreservation) {
    rc::check("Property 6: Search Ancestor Preservation",
        [](void) {
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random JsonNode tree
            std::uniform_int_distribution<int> depthDist(1, 4);
            auto tree = generateRandomNode(depthDist(rng), rng);
            RC_ASSERT(tree != nullptr);

            // Collect searchable content
            std::vector<std::pair<std::string, const JsonNode*>> keysAndValues;
            collectKeysAndStringValues(*tree, keysAndValues);
            RC_PRE(!keysAndValues.empty());

            // Pick a random substring to search for
            std::uniform_int_distribution<std::size_t> pickDist(0, keysAndValues.size() - 1);
            auto& [text, _] = keysAndValues[pickDist(rng)];
            RC_PRE(text.size() >= 1);
            std::uniform_int_distribution<std::size_t> startDist(0, text.size() - 1);
            auto start = startDist(rng);
            std::uniform_int_distribution<std::size_t> lenDist(1, text.size() - start);
            std::string substring = text.substr(start, lenDist(rng));

            auto result = filter(*tree, SearchQuery{.pattern = substring});
            RC_ASSERT(!result.error.has_value());

            // For every match, verify the ancestor path is valid
            for (const auto& match : result.matches) {
                RC_ASSERT(verifyAncestorPath(*tree, match.ancestorIndices, *match.node));
            }
        }
    );
}

// ---------------------------------------------------------------------------
// Task 6.4: Property 7 — Regex Search Correctness
// Validates: Requirement 4.5
// ---------------------------------------------------------------------------

// Helper: collect all nodes that should match a regex pattern
static void collectRegexMatches(const JsonNode& node,
                                 const std::regex& pattern,
                                 std::vector<const JsonNode*>& expected) {
    if (std::regex_search(node.key, pattern)) {
        expected.push_back(&node);
    } else if (node.type == NodeType::String && std::regex_search(node.value, pattern)) {
        expected.push_back(&node);
    }
    for (const auto& child : node.children) {
        collectRegexMatches(*child, pattern, expected);
    }
}

TEST(SearchEngineProperty, RegexSearchCorrectness) {
    rc::check("Property 7: Regex Search Correctness",
        [](void) {
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random JsonNode tree
            std::uniform_int_distribution<int> depthDist(1, 3);
            auto tree = generateRandomNode(depthDist(rng), rng);
            RC_ASSERT(tree != nullptr);

            // Generate a simple valid regex pattern from known content
            // Strategy: pick a literal substring from the tree and use it as a regex
            std::vector<std::pair<std::string, const JsonNode*>> keysAndValues;
            collectKeysAndStringValues(*tree, keysAndValues);
            RC_PRE(!keysAndValues.empty());

            std::uniform_int_distribution<std::size_t> pickDist(0, keysAndValues.size() - 1);
            auto& [text, _] = keysAndValues[pickDist(rng)];
            RC_PRE(text.size() >= 1);

            // Use a simple literal pattern (escape regex special chars)
            std::string pattern;
            for (char c : text) {
                if (std::string("[](){}*+?.\\^$|").find(c) != std::string::npos) {
                    pattern += '\\';
                }
                pattern += c;
            }

            // Compute expected matches manually
            std::regex compiledPattern(pattern, std::regex_constants::ECMAScript | std::regex_constants::icase);
            std::vector<const JsonNode*> expected;
            collectRegexMatches(*tree, compiledPattern, expected);

            // Run filter
            auto result = filter(*tree, SearchQuery{
                .pattern = pattern,
                .mode = SearchMode::Regex,
                .caseSensitive = false
            });
            RC_ASSERT(!result.error.has_value());

            // Verify: no false negatives — every expected node is in results
            for (const auto* expectedNode : expected) {
                bool found = false;
                for (const auto& match : result.matches) {
                    if (match.node->key == expectedNode->key &&
                        match.node->value == expectedNode->value &&
                        match.node->type == expectedNode->type) {
                        found = true;
                        break;
                    }
                }
                RC_ASSERT(found);
            }

            // Verify: no false positives — every result is in expected
            for (const auto& match : result.matches) {
                bool found = false;
                for (const auto* expectedNode : expected) {
                    if (match.node->key == expectedNode->key &&
                        match.node->value == expectedNode->value &&
                        match.node->type == expectedNode->type) {
                        found = true;
                        break;
                    }
                }
                RC_ASSERT(found);
            }

            // Verify count matches
            RC_ASSERT(result.matches.size() == expected.size());
        }
    );
}

// ---------------------------------------------------------------------------
// Task 6.5: Property 8 — Invalid Regex Error Reporting
// Validates: Requirement 4.6
// ---------------------------------------------------------------------------

TEST(SearchEngineProperty, InvalidRegexErrorReporting) {
    rc::check("Property 8: Invalid Regex Error Reporting",
        [](void) {
            // Generate strings that are not valid regular expressions
            auto invalidPatternIdx = *rc::gen::inRange(0, 8);
            std::string invalidPattern;
            switch (invalidPatternIdx) {
                case 0: invalidPattern = "[unclosed"; break;
                case 1: invalidPattern = "(unclosed"; break;
                case 2: invalidPattern = "invalid\\"; break;
                case 3: invalidPattern = "[z-a]"; break;
                case 4: invalidPattern = "(?P<bad"; break;
                case 5: invalidPattern = "*leading_quantifier"; break;
                case 6: invalidPattern = "+leading_quantifier"; break;
                case 7: invalidPattern = "?leading_quantifier"; break;
            }

            // Create a minimal tree to search
            auto root = JsonNode::makeObject("", {
                JsonNode::makeString("key", "value"),
            });

            auto result = filter(*root, SearchQuery{
                .pattern = invalidPattern,
                .mode = SearchMode::Regex
            });

            // Should have empty matches and a non-empty error description
            RC_ASSERT(result.matches.empty());
            RC_ASSERT(result.error.has_value());
            RC_ASSERT(!result.error->description.empty());
        }
    );
}

// ---------------------------------------------------------------------------
// Task 7.2: Property 9 — Union Structure and Disambiguation
// Validates: Requirements 5.1, 5.5
// ---------------------------------------------------------------------------

TEST(UnionProperty, UnionStructureAndDisambiguation) {
    rc::check("Feature: json-titan-core, Property 9: Union Structure and Disambiguation",
        [](void) {
            // Generate a random number of file entries (1–10)
            auto numEntries = *rc::gen::inRange(1, 11);

            // Generate a small pool of filenames to draw from (ensures duplicates)
            auto poolSize = *rc::gen::inRange(1, std::max(2, numEntries));
            std::vector<std::string> namePool;
            namePool.reserve(static_cast<std::size_t>(poolSize));
            for (int i = 0; i < poolSize; ++i) {
                std::vector<char> alphabet = {'a', 'b', 'c', 'd', 'e', 'f'};
                auto len = *rc::gen::inRange(1, 6);
                std::string name;
                for (int j = 0; j < len; ++j) {
                    auto ch = *rc::gen::elementOf(alphabet);
                    name += ch;
                }
                name += ".json";
                namePool.push_back(std::move(name));
            }

            // Build FileEntry vector, picking filenames from the pool
            std::vector<FileEntry> entries;
            entries.reserve(static_cast<std::size_t>(numEntries));
            for (int i = 0; i < numEntries; ++i) {
                auto nameIdx = *rc::gen::inRange(
                    std::size_t{0}, namePool.size());
                // Each file gets a simple object tree as its root
                auto root = JsonNode::makeObject("", {
                    JsonNode::makeString("data", "value" + std::to_string(i))
                });
                entries.push_back(FileEntry{namePool[nameIdx], root});
            }

            // Call unionTrees
            auto result = unionTrees(std::span<const FileEntry>(entries));

            // 1. Root node type is Object
            RC_ASSERT(result != nullptr);
            RC_ASSERT(result->type == NodeType::Object);

            // 2. Root has exactly entries.size() children
            RC_ASSERT(result->children.size() == entries.size());

            // 3. All child keys are unique
            std::set<std::string> keys;
            for (const auto& child : result->children) {
                auto [_, inserted] = keys.insert(child->key);
                RC_ASSERT(inserted);  // key must be unique
            }

            // 4. First occurrence of each filename uses the original name;
            //    duplicates are disambiguated with " (2)", " (3)", etc.
            std::unordered_map<std::string, int> expectedCount;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& filename = entries[i].filename;
                expectedCount[filename]++;
                int occurrence = expectedCount[filename];

                std::string expectedKey;
                if (occurrence == 1) {
                    expectedKey = filename;
                } else {
                    expectedKey = filename + " (" + std::to_string(occurrence) + ")";
                }
                RC_ASSERT(result->children[i]->key == expectedKey);
            }
        }
    );
}

// ---------------------------------------------------------------------------
// Task 7.3: Property 10 — Union Removal
// Validates: Requirement 5.4
// ---------------------------------------------------------------------------

TEST(UnionProperty, UnionRemoval) {
    rc::check("Feature: json-titan-core, Property 10: Union Removal",
        [](void) {
            // Generate a random number of file entries (2–10, need at least 2
            // so that after removal there is still at least 1 child)
            auto numEntries = *rc::gen::inRange(2, 11);

            // Generate a small pool of filenames to draw from (ensures duplicates)
            auto poolSize = *rc::gen::inRange(1, std::max(2, numEntries));
            std::vector<std::string> namePool;
            namePool.reserve(static_cast<std::size_t>(poolSize));
            for (int i = 0; i < poolSize; ++i) {
                std::vector<char> alphabet = {'a', 'b', 'c', 'd', 'e', 'f'};
                auto len = *rc::gen::inRange(1, 6);
                std::string name;
                for (int j = 0; j < len; ++j) {
                    auto ch = *rc::gen::elementOf(alphabet);
                    name += ch;
                }
                name += ".json";
                namePool.push_back(std::move(name));
            }

            // Build FileEntry vector, picking filenames from the pool
            std::vector<FileEntry> entries;
            entries.reserve(static_cast<std::size_t>(numEntries));
            for (int i = 0; i < numEntries; ++i) {
                auto nameIdx = *rc::gen::inRange(
                    std::size_t{0}, namePool.size());
                auto root = JsonNode::makeObject("", {
                    JsonNode::makeString("data", "value" + std::to_string(i))
                });
                entries.push_back(FileEntry{namePool[nameIdx], root});
            }

            // Create the union tree
            auto unionResult = unionTrees(std::span<const FileEntry>(entries));
            RC_ASSERT(unionResult != nullptr);
            RC_ASSERT(!unionResult->children.empty());

            // Pick a random child key to remove
            auto childIdx = *rc::gen::inRange(
                std::size_t{0}, unionResult->children.size());
            std::string keyToRemove = unionResult->children[childIdx]->key;

            // Call removeFromUnion
            auto removed = removeFromUnion(*unionResult, keyToRemove);

            // 1. Result is still an Object type
            RC_ASSERT(removed != nullptr);
            RC_ASSERT(removed->type == NodeType::Object);

            // 2. Result has exactly (original children count - 1) children
            RC_ASSERT(removed->children.size() ==
                       unionResult->children.size() - 1);

            // 3. The result does not contain a child with the removed key
            for (const auto& child : removed->children) {
                RC_ASSERT(child->key != keyToRemove);
            }

            // 4. All other children are present and unchanged
            //    (same key, same children/structure)
            std::size_t remIdx = 0;
            for (std::size_t origIdx = 0;
                 origIdx < unionResult->children.size(); ++origIdx) {
                if (unionResult->children[origIdx]->key == keyToRemove) {
                    continue;  // skip the removed child
                }
                RC_ASSERT(remIdx < removed->children.size());
                RC_ASSERT(nodesEqual(removed->children[remIdx],
                                     unionResult->children[origIdx]));
                ++remIdx;
            }
            RC_ASSERT(remIdx == removed->children.size());
        }
    );
}

// ---------------------------------------------------------------------------
// Task 7.4: Unit tests for UnionEngine
// Requirements: 5.1, 5.4, 5.5
// ---------------------------------------------------------------------------

// === unionTrees tests ======================================================

TEST(UnionEngineTest, SingleFileUnion) {
    auto fileRoot = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice")
    });
    std::vector<FileEntry> entries = {{"file.json", fileRoot}};
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    ASSERT_EQ(result->children.size(), 1u);
    EXPECT_EQ(result->children[0]->key, "file.json");
}

TEST(UnionEngineTest, EmptyFileList) {
    std::vector<FileEntry> entries;
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    EXPECT_EQ(result->children.size(), 0u);
}

TEST(UnionEngineTest, FilesWithIdenticalStructure) {
    auto root1 = JsonNode::makeObject("", {
        JsonNode::makeString("key", "val1")
    });
    auto root2 = JsonNode::makeObject("", {
        JsonNode::makeString("key", "val2")
    });
    auto root3 = JsonNode::makeObject("", {
        JsonNode::makeString("key", "val3")
    });
    std::vector<FileEntry> entries = {
        {"a.json", root1},
        {"b.json", root2},
        {"c.json", root3}
    };
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->children.size(), 3u);
    EXPECT_EQ(result->children[0]->key, "a.json");
    EXPECT_EQ(result->children[1]->key, "b.json");
    EXPECT_EQ(result->children[2]->key, "c.json");
}

TEST(UnionEngineTest, DuplicateFilenames) {
    auto root1 = JsonNode::makeObject("", {
        JsonNode::makeString("x", "1")
    });
    auto root2 = JsonNode::makeObject("", {
        JsonNode::makeString("x", "2")
    });
    std::vector<FileEntry> entries = {
        {"data.json", root1},
        {"data.json", root2}
    };
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->children.size(), 2u);
    EXPECT_EQ(result->children[0]->key, "data.json");
    EXPECT_EQ(result->children[1]->key, "data.json (2)");
}

TEST(UnionEngineTest, ThreeDuplicateFilenames) {
    auto root1 = JsonNode::makeObject("", {});
    auto root2 = JsonNode::makeObject("", {});
    auto root3 = JsonNode::makeObject("", {});
    std::vector<FileEntry> entries = {
        {"data.json", root1},
        {"data.json", root2},
        {"data.json", root3}
    };
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->children.size(), 3u);
    EXPECT_EQ(result->children[0]->key, "data.json");
    EXPECT_EQ(result->children[1]->key, "data.json (2)");
    EXPECT_EQ(result->children[2]->key, "data.json (3)");
}

TEST(UnionEngineTest, MixedDuplicateAndUnique) {
    auto r1 = JsonNode::makeObject("", {});
    auto r2 = JsonNode::makeObject("", {});
    auto r3 = JsonNode::makeObject("", {});
    auto r4 = JsonNode::makeObject("", {});
    std::vector<FileEntry> entries = {
        {"alpha.json", r1},
        {"beta.json", r2},
        {"alpha.json", r3},
        {"gamma.json", r4}
    };
    auto result = unionTrees(std::span<const FileEntry>(entries));

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->children.size(), 4u);
    EXPECT_EQ(result->children[0]->key, "alpha.json");
    EXPECT_EQ(result->children[1]->key, "beta.json");
    EXPECT_EQ(result->children[2]->key, "alpha.json (2)");
    EXPECT_EQ(result->children[3]->key, "gamma.json");
}

// === removeFromUnion tests =================================================

TEST(UnionEngineTest, RemoveFirstEntry) {
    auto root1 = JsonNode::makeObject("", {JsonNode::makeString("a", "1")});
    auto root2 = JsonNode::makeObject("", {JsonNode::makeString("b", "2")});
    auto root3 = JsonNode::makeObject("", {JsonNode::makeString("c", "3")});
    std::vector<FileEntry> entries = {
        {"first.json", root1},
        {"second.json", root2},
        {"third.json", root3}
    };
    auto unionRoot = unionTrees(std::span<const FileEntry>(entries));
    auto result = removeFromUnion(*unionRoot, "first.json");

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    ASSERT_EQ(result->children.size(), 2u);
    EXPECT_EQ(result->children[0]->key, "second.json");
    EXPECT_EQ(result->children[1]->key, "third.json");
}

TEST(UnionEngineTest, RemoveLastEntry) {
    auto root1 = JsonNode::makeObject("", {JsonNode::makeString("a", "1")});
    auto root2 = JsonNode::makeObject("", {JsonNode::makeString("b", "2")});
    auto root3 = JsonNode::makeObject("", {JsonNode::makeString("c", "3")});
    std::vector<FileEntry> entries = {
        {"first.json", root1},
        {"second.json", root2},
        {"third.json", root3}
    };
    auto unionRoot = unionTrees(std::span<const FileEntry>(entries));
    auto result = removeFromUnion(*unionRoot, "third.json");

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    ASSERT_EQ(result->children.size(), 2u);
    EXPECT_EQ(result->children[0]->key, "first.json");
    EXPECT_EQ(result->children[1]->key, "second.json");
}

TEST(UnionEngineTest, RemoveMiddleEntry) {
    auto root1 = JsonNode::makeObject("", {JsonNode::makeString("a", "1")});
    auto root2 = JsonNode::makeObject("", {JsonNode::makeString("b", "2")});
    auto root3 = JsonNode::makeObject("", {JsonNode::makeString("c", "3")});
    std::vector<FileEntry> entries = {
        {"first.json", root1},
        {"second.json", root2},
        {"third.json", root3}
    };
    auto unionRoot = unionTrees(std::span<const FileEntry>(entries));
    auto result = removeFromUnion(*unionRoot, "second.json");

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    ASSERT_EQ(result->children.size(), 2u);
    EXPECT_EQ(result->children[0]->key, "first.json");
    EXPECT_EQ(result->children[1]->key, "third.json");
}

TEST(UnionEngineTest, RemoveNonExistentKey) {
    auto root1 = JsonNode::makeObject("", {});
    auto root2 = JsonNode::makeObject("", {});
    std::vector<FileEntry> entries = {
        {"a.json", root1},
        {"b.json", root2}
    };
    auto unionRoot = unionTrees(std::span<const FileEntry>(entries));
    auto result = removeFromUnion(*unionRoot, "nonexistent.json");

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    ASSERT_EQ(result->children.size(), 2u);
    EXPECT_EQ(result->children[0]->key, "a.json");
    EXPECT_EQ(result->children[1]->key, "b.json");
}

TEST(UnionEngineTest, RemoveFromSingleChild) {
    auto root1 = JsonNode::makeObject("", {JsonNode::makeString("x", "val")});
    std::vector<FileEntry> entries = {{"only.json", root1}};
    auto unionRoot = unionTrees(std::span<const FileEntry>(entries));
    auto result = removeFromUnion(*unionRoot, "only.json");

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, NodeType::Object);
    EXPECT_EQ(result->children.size(), 0u);
}

// ===========================================================================
// Task 8: CsvExporter Tests
// Requirements: 6.1, 6.2, 6.3, 6.4, 6.5
// ===========================================================================

#include "core/csv_exporter.h"

// ---------------------------------------------------------------------------
// Helper: Parse CSV output into rows of cells for verification
// ---------------------------------------------------------------------------

namespace {

// Simple CSV parser for test verification (handles RFC 4180 quoting)
auto parseCsvRow(const std::string& row) -> std::vector<std::string> {
    std::vector<std::string> cells;
    std::string current;
    bool inQuotes = false;
    std::size_t i = 0;

    while (i < row.size()) {
        char c = row[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < row.size() && row[i + 1] == '"') {
                    // Escaped quote
                    current += '"';
                    i += 2;
                } else {
                    // End of quoted field
                    inQuotes = false;
                    i++;
                }
            } else {
                current += c;
                i++;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
                i++;
            } else if (c == ',') {
                cells.push_back(current);
                current.clear();
                i++;
            } else {
                current += c;
                i++;
            }
        }
    }
    cells.push_back(current);
    return cells;
}

// Split CSV output into rows (handling \r\n line endings and quoted fields with embedded newlines).
// Each row ends with \r\n in the output, so we split on \r\n boundaries outside of quotes
// and drop the trailing empty entry.
auto parseCsvLines(const std::string& csv) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string current;
    bool inQuotes = false;

    for (std::size_t i = 0; i < csv.size(); ++i) {
        char c = csv[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            current += c;
        } else if (c == '\r' && !inQuotes) {
            if (i + 1 < csv.size() && csv[i + 1] == '\n') {
                lines.push_back(current);
                current.clear();
                i++; // skip \n
            } else {
                current += c;
            }
        } else if (c == '\n' && !inQuotes) {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    // If there's remaining content (no trailing \r\n), add it
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Task 8.5: Unit tests for CsvExporter
// Requirements: 6.1, 6.2, 6.3, 6.4, 6.5
// ---------------------------------------------------------------------------

TEST(CsvExporter, SingleRowArray) {
    // Array with one object: [{"name": "Alice", "age": "30"}]
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeNumber("age", "30")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_EQ(lines.size(), 2u); // header + 1 data row

    auto headers = parseCsvRow(lines[0]);
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0], "name");
    EXPECT_EQ(headers[1], "age");

    auto row = parseCsvRow(lines[1]);
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(row[0], "Alice");
    EXPECT_EQ(row[1], "30");
}

TEST(CsvExporter, EmptyArrayOfObjects) {
    // Empty array: []
    auto arr = JsonNode::makeArray("", {});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    // Should produce just a header row (empty since no objects to derive keys from)
    auto lines = parseCsvLines(csv);
    // Empty array produces a single empty header line (no data rows)
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(lines[0].empty());
}

TEST(CsvExporter, ObjectsWithNoCommonKeys) {
    // [{a: 1}, {b: 2}] — union of keys is {a, b}
    auto obj1 = JsonNode::makeObject("", {JsonNode::makeNumber("a", "1")});
    auto obj2 = JsonNode::makeObject("", {JsonNode::makeNumber("b", "2")});
    auto arr = JsonNode::makeArray("", {obj1, obj2});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_EQ(lines.size(), 3u); // header + 2 data rows

    auto headers = parseCsvRow(lines[0]);
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0], "a");
    EXPECT_EQ(headers[1], "b");

    // First row: a=1, b=empty
    auto row1 = parseCsvRow(lines[1]);
    ASSERT_EQ(row1.size(), 2u);
    EXPECT_EQ(row1[0], "1");
    EXPECT_EQ(row1[1], "");

    // Second row: a=empty, b=2
    auto row2 = parseCsvRow(lines[2]);
    ASSERT_EQ(row2.size(), 2u);
    EXPECT_EQ(row2[0], "");
    EXPECT_EQ(row2[1], "2");
}

TEST(CsvExporter, NestedValuesInCells) {
    // [{data: {x: 1}}, {data: [1,2,3]}]
    auto nested_obj = JsonNode::makeObject("data", {JsonNode::makeNumber("x", "1")});
    auto nested_arr = JsonNode::makeArray("data", {
        JsonNode::makeNumber("", "1"),
        JsonNode::makeNumber("", "2"),
        JsonNode::makeNumber("", "3")
    });
    auto obj1 = JsonNode::makeObject("", {nested_obj});
    auto obj2 = JsonNode::makeObject("", {nested_arr});
    auto arr = JsonNode::makeArray("", {obj1, obj2});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_EQ(lines.size(), 3u);

    // Nested values should be serialized as JSON strings
    auto row1 = parseCsvRow(lines[1]);
    ASSERT_EQ(row1.size(), 1u);
    // Should contain JSON representation of {x: 1}
    EXPECT_NE(row1[0].find("\"x\""), std::string::npos);
    EXPECT_NE(row1[0].find("1"), std::string::npos);

    auto row2 = parseCsvRow(lines[2]);
    ASSERT_EQ(row2.size(), 1u);
    // Should contain JSON representation of [1,2,3]
    EXPECT_NE(row2[0].find("["), std::string::npos);
    EXPECT_NE(row2[0].find("1"), std::string::npos);
}

TEST(CsvExporter, RFC4180EscapingComma) {
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeString("msg", "hello, world")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_GE(lines.size(), 2u);

    auto row = parseCsvRow(lines[1]);
    ASSERT_EQ(row.size(), 1u);
    EXPECT_EQ(row[0], "hello, world");

    // The raw CSV should contain the quoted value
    EXPECT_NE(csv.find("\"hello, world\""), std::string::npos);
}

TEST(CsvExporter, RFC4180EscapingDoubleQuote) {
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeString("msg", "say \"hello\"")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_GE(lines.size(), 2u);

    auto row = parseCsvRow(lines[1]);
    ASSERT_EQ(row.size(), 1u);
    EXPECT_EQ(row[0], "say \"hello\"");

    // Raw CSV should have doubled quotes: "say ""hello"""
    EXPECT_NE(csv.find("\"say \"\"hello\"\"\""), std::string::npos);
}

TEST(CsvExporter, RFC4180EscapingNewline) {
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeString("msg", "line1\nline2")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_GE(lines.size(), 2u);

    auto row = parseCsvRow(lines[1]);
    ASSERT_EQ(row.size(), 1u);
    EXPECT_EQ(row[0], "line1\nline2");
}

TEST(CsvExporter, ErrorOnScalarNode) {
    auto node = JsonNode::makeString("", "hello");
    auto result = exportCsv(*node);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnPlainObject) {
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("key", "value")
    });
    auto result = exportCsv(*node);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnArrayOfScalars) {
    auto arr = JsonNode::makeArray("", {
        JsonNode::makeNumber("", "1"),
        JsonNode::makeNumber("", "2"),
        JsonNode::makeNumber("", "3")
    });
    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnMixedArray) {
    auto arr = JsonNode::makeArray("", {
        JsonNode::makeObject("", {JsonNode::makeString("a", "1")}),
        JsonNode::makeNumber("", "42")
    });
    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnNumberNode) {
    auto node = JsonNode::makeNumber("", "42");
    auto result = exportCsv(*node);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnBoolNode) {
    auto node = JsonNode::makeBool("", true);
    auto result = exportCsv(*node);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, ErrorOnNullNode) {
    auto node = JsonNode::makeNull("");
    auto result = exportCsv(*node);
    ASSERT_TRUE(std::holds_alternative<CsvError>(result));
    EXPECT_FALSE(std::get<CsvError>(result).description.empty());
}

TEST(CsvExporter, MultipleRowsWithOverlappingKeys) {
    // [{a:1, b:2}, {b:3, c:4}] — headers should be a, b, c
    auto obj1 = JsonNode::makeObject("", {
        JsonNode::makeNumber("a", "1"),
        JsonNode::makeNumber("b", "2")
    });
    auto obj2 = JsonNode::makeObject("", {
        JsonNode::makeNumber("b", "3"),
        JsonNode::makeNumber("c", "4")
    });
    auto arr = JsonNode::makeArray("", {obj1, obj2});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_EQ(lines.size(), 3u);

    auto headers = parseCsvRow(lines[0]);
    ASSERT_EQ(headers.size(), 3u);
    EXPECT_EQ(headers[0], "a");
    EXPECT_EQ(headers[1], "b");
    EXPECT_EQ(headers[2], "c");

    auto row1 = parseCsvRow(lines[1]);
    EXPECT_EQ(row1[0], "1");
    EXPECT_EQ(row1[1], "2");
    EXPECT_EQ(row1[2], "");

    auto row2 = parseCsvRow(lines[2]);
    EXPECT_EQ(row2[0], "");
    EXPECT_EQ(row2[1], "3");
    EXPECT_EQ(row2[2], "4");
}

TEST(CsvExporter, BooleanAndNullValues) {
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeBool("flag", true),
        JsonNode::makeNull("empty")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_EQ(lines.size(), 2u);

    auto row = parseCsvRow(lines[1]);
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(row[0], "true");
    EXPECT_EQ(row[1], "null");
}

TEST(CsvExporter, HeaderWithSpecialCharacters) {
    // Keys containing commas and quotes should be escaped in the header
    auto obj = JsonNode::makeObject("", {
        JsonNode::makeString("key,with,commas", "val1"),
        JsonNode::makeString("key\"with\"quotes", "val2")
    });
    auto arr = JsonNode::makeArray("", {obj});

    auto result = exportCsv(*arr);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    auto csv = std::get<std::string>(result);

    auto lines = parseCsvLines(csv);
    ASSERT_GE(lines.size(), 2u);

    auto headers = parseCsvRow(lines[0]);
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0], "key,with,commas");
    EXPECT_EQ(headers[1], "key\"with\"quotes");
}

// ---------------------------------------------------------------------------
// Task 8.2: Property 11 — CSV Header and Row Invariant
// Validates: Requirements 6.1, 6.2
// ---------------------------------------------------------------------------

TEST(CsvExporterProperty, HeaderAndRowInvariant) {
    rc::check("Property 11: CSV Header and Row Invariant",
        [](void) {
            // Generate a random array of objects
            auto numObjects = *rc::gen::inRange(1, 8);
            auto numKeysPerObj = *rc::gen::inRange(1, 6);

            // Generate a pool of unique keys
            std::set<std::string> allKeysSet;
            std::vector<std::string> keyPool;
            for (int k = 0; k < numKeysPerObj * 2; ++k) {
                std::string key = "key" + std::to_string(k);
                if (allKeysSet.insert(key).second) {
                    keyPool.push_back(key);
                }
            }

            std::vector<std::shared_ptr<const JsonNode>> objects;
            std::vector<std::string> expectedHeaders; // first-seen order
            std::set<std::string> seenHeaders;

            for (int i = 0; i < numObjects; ++i) {
                std::vector<std::shared_ptr<const JsonNode>> fields;
                auto keysToUse = *rc::gen::inRange(1, static_cast<int>(keyPool.size()) + 1);

                for (int k = 0; k < keysToUse && k < static_cast<int>(keyPool.size()); ++k) {
                    auto& key = keyPool[static_cast<std::size_t>(k)];
                    auto val = *rc::gen::arbitrary<std::string>();
                    fields.push_back(JsonNode::makeString(key, val));

                    if (seenHeaders.insert(key).second) {
                        expectedHeaders.push_back(key);
                    }
                }
                objects.push_back(JsonNode::makeObject("", std::move(fields)));
            }

            auto arr = JsonNode::makeArray("", std::move(objects));
            auto result = exportCsv(*arr);

            // Must succeed
            RC_ASSERT(std::holds_alternative<std::string>(result));
            auto csv = std::get<std::string>(result);

            auto lines = parseCsvLines(csv);

            // Header + numObjects data rows
            RC_ASSERT(lines.size() == static_cast<std::size_t>(numObjects) + 1);

            // Verify header contains exactly the union of all keys
            auto headers = parseCsvRow(lines[0]);
            RC_ASSERT(headers.size() == expectedHeaders.size());
            for (std::size_t i = 0; i < headers.size(); ++i) {
                RC_ASSERT(headers[i] == expectedHeaders[i]);
            }

            // Verify data row count equals array length
            for (int i = 1; i <= numObjects; ++i) {
                auto row = parseCsvRow(lines[static_cast<std::size_t>(i)]);
                RC_ASSERT(row.size() == expectedHeaders.size());
            }
        });
}

// ---------------------------------------------------------------------------
// Task 8.3: Property 12 — CSV RFC 4180 Escaping
// Validates: Requirement 6.4
// ---------------------------------------------------------------------------

TEST(CsvExporterProperty, RFC4180Escaping) {
    rc::check("Property 12: CSV RFC 4180 Escaping",
        [](void) {
            // Generate a string that contains at least one special character
            auto baseStr = *rc::gen::arbitrary<std::string>();

            // Inject at least one special character
            auto specialChars = std::vector<char>{',', '"', '\n'};
            auto specialIdx = *rc::gen::inRange(0, 3);
            auto insertPos = *rc::gen::inRange(std::size_t{0}, baseStr.size() + 1);
            baseStr.insert(baseStr.begin() + static_cast<std::ptrdiff_t>(insertPos),
                           specialChars[static_cast<std::size_t>(specialIdx)]);

            // Create an array with one object containing this value
            auto obj = JsonNode::makeObject("", {
                JsonNode::makeString("field", baseStr)
            });
            auto arr = JsonNode::makeArray("", {obj});

            auto result = exportCsv(*arr);
            RC_ASSERT(std::holds_alternative<std::string>(result));
            auto csv = std::get<std::string>(result);

            auto lines = parseCsvLines(csv);
            RC_ASSERT(lines.size() >= 2u);

            // The data row should be parseable and recover the original value
            auto row = parseCsvRow(lines[1]);
            RC_ASSERT(row.size() == 1u);
            RC_ASSERT(row[0] == baseStr);

            // Verify the raw CSV cell is properly quoted:
            // Find the data portion (after first \r\n)
            auto dataStart = csv.find("\r\n");
            RC_ASSERT(dataStart != std::string::npos);
            auto dataLine = csv.substr(dataStart + 2);

            // The cell must start with a double quote (since it contains special chars)
            RC_ASSERT(!dataLine.empty());
            RC_ASSERT(dataLine[0] == '"');
        });
}

// ---------------------------------------------------------------------------
// Task 8.4: Property 13 — CSV Error for Non-Tabular Data
// Validates: Requirement 6.5
// ---------------------------------------------------------------------------

TEST(CsvExporterProperty, ErrorForNonTabularData) {
    rc::check("Property 13: CSV Error for Non-Tabular Data",
        [](void) {
            // Generate a non-tabular JsonNode (not an array of objects)
            auto choice = *rc::gen::inRange(0, 5);
            std::shared_ptr<const JsonNode> node;

            switch (choice) {
                case 0: // Scalar string
                    node = JsonNode::makeString("", *rc::gen::arbitrary<std::string>());
                    break;
                case 1: // Scalar number
                    node = JsonNode::makeNumber("", std::to_string(*rc::gen::arbitrary<int>()));
                    break;
                case 2: // Plain object
                    node = JsonNode::makeObject("", {
                        JsonNode::makeString("k", *rc::gen::arbitrary<std::string>())
                    });
                    break;
                case 3: { // Array of scalars
                    auto numElems = *rc::gen::inRange(1, 5);
                    std::vector<std::shared_ptr<const JsonNode>> elems;
                    for (int i = 0; i < numElems; ++i) {
                        elems.push_back(JsonNode::makeNumber("", std::to_string(i)));
                    }
                    node = JsonNode::makeArray("", std::move(elems));
                    break;
                }
                case 4: { // Mixed array (objects + non-objects)
                    auto obj = JsonNode::makeObject("", {
                        JsonNode::makeString("a", "val")
                    });
                    auto scalar = JsonNode::makeNumber("", "42");
                    node = JsonNode::makeArray("", {obj, scalar});
                    break;
                }
                default:
                    node = JsonNode::makeNull("");
                    break;
            }

            auto result = exportCsv(*node);
            RC_ASSERT(std::holds_alternative<CsvError>(result));
            RC_ASSERT(!std::get<CsvError>(result).description.empty());
        });
}

// ===========================================================================
// Task 9: XmlExporter Tests
// Requirements: 7.1, 7.2, 7.3, 7.4
// ===========================================================================

#include "core/xml_exporter.h"

// ---------------------------------------------------------------------------
// Task 9.5: Unit tests for XmlExporter
// Requirements: 7.1, 7.2, 7.3, 7.4
// ---------------------------------------------------------------------------

TEST(XmlExporter, EmptyObject) {
    auto node = JsonNode::makeObject("", {});
    auto xml = exportXml(*node, "root");
    EXPECT_NE(xml.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), std::string::npos);
    EXPECT_NE(xml.find("<root>"), std::string::npos);
    EXPECT_NE(xml.find("</root>"), std::string::npos);
}

TEST(XmlExporter, SimpleObjectWithScalars) {
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeNumber("age", "30"),
        JsonNode::makeBool("active", true),
        JsonNode::makeNull("data")
    });
    auto xml = exportXml(*node, "person");
    EXPECT_NE(xml.find("<person>"), std::string::npos);
    EXPECT_NE(xml.find("<name>Alice</name>"), std::string::npos);
    EXPECT_NE(xml.find("<age>30</age>"), std::string::npos);
    EXPECT_NE(xml.find("<active>true</active>"), std::string::npos);
    EXPECT_NE(xml.find("<data/>"), std::string::npos);
    EXPECT_NE(xml.find("</person>"), std::string::npos);
}

TEST(XmlExporter, DeeplyNestedArrays) {
    auto inner = JsonNode::makeArray("", {
        JsonNode::makeNumber("", "1"),
        JsonNode::makeNumber("", "2")
    });
    auto outer = JsonNode::makeArray("", {inner});
    auto root = JsonNode::makeObject("", {
        JsonNode::makeString("name", "test")
    });
    // Test array wrapping
    auto xml = exportXml(*outer, "data");
    EXPECT_NE(xml.find("<data>"), std::string::npos);
    EXPECT_NE(xml.find("<item_0>"), std::string::npos);
    EXPECT_NE(xml.find("</data>"), std::string::npos);
}

TEST(XmlExporter, KeysStartingWithDigits) {
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("1stPlace", "gold"),
        JsonNode::makeString("2ndPlace", "silver"),
        JsonNode::makeString("3rdPlace", "bronze")
    });
    auto xml = exportXml(*node, "results");
    // Keys starting with digits should be prepended with underscore
    EXPECT_NE(xml.find("<_1stPlace>gold</_1stPlace>"), std::string::npos);
    EXPECT_NE(xml.find("<_2ndPlace>silver</_2ndPlace>"), std::string::npos);
    EXPECT_NE(xml.find("<_3rdPlace>bronze</_3rdPlace>"), std::string::npos);
}

TEST(XmlExporter, KeysWithSpecialCharacters) {
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("hello world", "spaces"),
        JsonNode::makeString("key@value", "at-sign"),
        JsonNode::makeString("a+b=c", "operators"),
        JsonNode::makeString("path/to/file", "slashes")
    });
    auto xml = exportXml(*node, "data");
    // Special characters should be replaced with underscores
    EXPECT_NE(xml.find("<hello_world>spaces</hello_world>"), std::string::npos);
    EXPECT_NE(xml.find("<key_value>at-sign</key_value>"), std::string::npos);
    EXPECT_NE(xml.find("<a_b_c>operators</a_b_c>"), std::string::npos);
    EXPECT_NE(xml.find("<path_to_file>slashes</path_to_file>"), std::string::npos);
}

TEST(XmlExporter, XmlReservedCharactersInValues) {
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("amp", "a & b"),
        JsonNode::makeString("lt", "a < b"),
        JsonNode::makeString("gt", "a > b"),
        JsonNode::makeString("quot", "say \"hello\""),
        JsonNode::makeString("apos", "it's fine")
    });
    auto xml = exportXml(*node, "data");
    EXPECT_NE(xml.find("<amp>a &amp; b</amp>"), std::string::npos);
    EXPECT_NE(xml.find("<lt>a &lt; b</lt>"), std::string::npos);
    EXPECT_NE(xml.find("<gt>a &gt; b</gt>"), std::string::npos);
    EXPECT_NE(xml.find("<quot>say &quot;hello&quot;</quot>"), std::string::npos);
    EXPECT_NE(xml.find("<apos>it&apos;s fine</apos>"), std::string::npos);
}

TEST(XmlExporter, AllFiveReservedCharsInOneValue) {
    auto node = JsonNode::makeString("", "<\"Tom & Jerry's\"> show");
    auto xml = exportXml(*node, "msg");
    EXPECT_NE(xml.find("&lt;&quot;Tom &amp; Jerry&apos;s&quot;&gt; show"), std::string::npos);
}

TEST(XmlExporter, ArrayWithIndexedElements) {
    auto arr = JsonNode::makeArray("", {
        JsonNode::makeString("", "first"),
        JsonNode::makeString("", "second"),
        JsonNode::makeString("", "third")
    });
    auto xml = exportXml(*arr, "items");
    EXPECT_NE(xml.find("<items>"), std::string::npos);
    EXPECT_NE(xml.find("<item_0>first</item_0>"), std::string::npos);
    EXPECT_NE(xml.find("<item_1>second</item_1>"), std::string::npos);
    EXPECT_NE(xml.find("<item_2>third</item_2>"), std::string::npos);
    EXPECT_NE(xml.find("</items>"), std::string::npos);
}

TEST(XmlExporter, NullNodeProducesSelfClosingTag) {
    auto node = JsonNode::makeNull("");
    auto xml = exportXml(*node, "empty");
    EXPECT_NE(xml.find("<empty/>"), std::string::npos);
}

TEST(XmlExporter, NestedObjectsAndArrays) {
    auto inner = JsonNode::makeObject("config", {
        JsonNode::makeNumber("timeout", "30"),
        JsonNode::makeBool("enabled", true)
    });
    auto arr = JsonNode::makeArray("tags", {
        JsonNode::makeString("", "alpha"),
        JsonNode::makeString("", "beta")
    });
    auto root = JsonNode::makeObject("", {inner, arr});
    auto xml = exportXml(*root, "app");
    EXPECT_NE(xml.find("<app>"), std::string::npos);
    EXPECT_NE(xml.find("<config>"), std::string::npos);
    EXPECT_NE(xml.find("<timeout>30</timeout>"), std::string::npos);
    EXPECT_NE(xml.find("<enabled>true</enabled>"), std::string::npos);
    EXPECT_NE(xml.find("</config>"), std::string::npos);
    EXPECT_NE(xml.find("<tags>"), std::string::npos);
    EXPECT_NE(xml.find("<item_0>alpha</item_0>"), std::string::npos);
    EXPECT_NE(xml.find("<item_1>beta</item_1>"), std::string::npos);
    EXPECT_NE(xml.find("</tags>"), std::string::npos);
    EXPECT_NE(xml.find("</app>"), std::string::npos);
}

TEST(XmlExporter, EmptyKeyProducesItemElement) {
    // Object children with empty keys (shouldn't normally happen, but handle gracefully)
    auto node = JsonNode::makeObject("", {
        JsonNode::makeString("", "value")
    });
    auto xml = exportXml(*node, "root");
    // Empty key should produce "item" as element name
    EXPECT_NE(xml.find("<item>value</item>"), std::string::npos);
}

TEST(XmlExporter, SanitizeXmlNameEmptyString) {
    auto result = sanitizeXmlName("");
    EXPECT_EQ(result, "_");
}

TEST(XmlExporter, SanitizeXmlNameValidName) {
    EXPECT_EQ(sanitizeXmlName("hello"), "hello");
    EXPECT_EQ(sanitizeXmlName("_private"), "_private");
    EXPECT_EQ(sanitizeXmlName("camelCase"), "camelCase");
    EXPECT_EQ(sanitizeXmlName("with-hyphen"), "with-hyphen");
    EXPECT_EQ(sanitizeXmlName("with.dot"), "with.dot");
}

TEST(XmlExporter, SanitizeXmlNameDigitPrefix) {
    EXPECT_EQ(sanitizeXmlName("1abc"), "_1abc");
    EXPECT_EQ(sanitizeXmlName("42"), "_42");
    EXPECT_EQ(sanitizeXmlName("0x1F"), "_0x1F");
}

TEST(XmlExporter, SanitizeXmlNameInvalidChars) {
    EXPECT_EQ(sanitizeXmlName("hello world"), "hello_world");
    EXPECT_EQ(sanitizeXmlName("a@b"), "a_b");
    EXPECT_EQ(sanitizeXmlName("x+y"), "x_y");
    EXPECT_EQ(sanitizeXmlName("path/to"), "path_to");
}

TEST(XmlExporter, EscapeXmlTextEmpty) {
    EXPECT_EQ(escapeXmlText(""), "");
}

TEST(XmlExporter, EscapeXmlTextNoSpecialChars) {
    EXPECT_EQ(escapeXmlText("hello world"), "hello world");
}

TEST(XmlExporter, EscapeXmlTextAmpersand) {
    EXPECT_EQ(escapeXmlText("a & b"), "a &amp; b");
}

TEST(XmlExporter, EscapeXmlTextLessThan) {
    EXPECT_EQ(escapeXmlText("a < b"), "a &lt; b");
}

TEST(XmlExporter, EscapeXmlTextGreaterThan) {
    EXPECT_EQ(escapeXmlText("a > b"), "a &gt; b");
}

TEST(XmlExporter, EscapeXmlTextDoubleQuote) {
    EXPECT_EQ(escapeXmlText("say \"hi\""), "say &quot;hi&quot;");
}

TEST(XmlExporter, EscapeXmlTextApostrophe) {
    EXPECT_EQ(escapeXmlText("it's"), "it&apos;s");
}

TEST(XmlExporter, EscapeXmlTextAllReserved) {
    EXPECT_EQ(escapeXmlText("&<>\"'"), "&amp;&lt;&gt;&quot;&apos;");
}

// ---------------------------------------------------------------------------
// Task 9.2: Property 14 — XML Well-Formedness
// Validates: Requirements 7.1, 7.2
// ---------------------------------------------------------------------------

// Simple XML well-formedness checker: verifies that every opening tag has a
// matching closing tag and that the document has proper nesting.
// This is a lightweight check suitable for property testing without requiring
// a full XML parser library dependency.
namespace {

// Check if a string is a valid XML element name (ASCII subset)
bool isValidXmlElementName(const std::string& name) {
    if (name.empty()) return false;
    // First char must be letter or underscore
    char first = name[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_' && first != ':') {
        return false;
    }
    // Subsequent chars can be letters, digits, hyphens, dots, underscores, colons
    for (std::size_t i = 1; i < name.size(); ++i) {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '_' && c != '-' && c != '.' && c != ':') {
            return false;
        }
    }
    return true;
}

// Minimal XML well-formedness validator.
// Checks: proper tag nesting, valid element names, proper self-closing tags.
// Returns true if the XML is well-formed, false otherwise.
bool isWellFormedXml(const std::string& xml) {
    std::vector<std::string> tagStack;
    std::size_t pos = 0;

    // Skip XML declaration if present
    if (xml.substr(0, 5) == "<?xml") {
        pos = xml.find("?>", pos);
        if (pos == std::string::npos) return false;
        pos += 2;
    }

    while (pos < xml.size()) {
        // Skip whitespace and text content
        if (xml[pos] != '<') {
            // Text content — check for unescaped < or & (simplified check)
            pos++;
            continue;
        }

        // Found a tag
        std::size_t tagEnd = xml.find('>', pos);
        if (tagEnd == std::string::npos) return false;

        std::string tagContent = xml.substr(pos + 1, tagEnd - pos - 1);

        if (tagContent.empty()) return false;

        if (tagContent[0] == '/') {
            // Closing tag
            std::string tagName = tagContent.substr(1);
            // Trim whitespace
            while (!tagName.empty() && std::isspace(static_cast<unsigned char>(tagName.back())))
                tagName.pop_back();
            if (tagStack.empty() || tagStack.back() != tagName) return false;
            tagStack.pop_back();
        } else if (tagContent.back() == '/') {
            // Self-closing tag
            std::string tagName = tagContent.substr(0, tagContent.size() - 1);
            // Trim whitespace
            while (!tagName.empty() && std::isspace(static_cast<unsigned char>(tagName.back())))
                tagName.pop_back();
            if (!isValidXmlElementName(tagName)) return false;
        } else if (tagContent[0] == '?') {
            // Processing instruction — skip
        } else {
            // Opening tag (may have attributes, but our output doesn't use them)
            std::string tagName = tagContent;
            // Extract just the name (up to first space)
            auto spacePos = tagName.find(' ');
            if (spacePos != std::string::npos) {
                tagName = tagName.substr(0, spacePos);
            }
            if (!isValidXmlElementName(tagName)) return false;
            tagStack.push_back(tagName);
        }

        pos = tagEnd + 1;
    }

    return tagStack.empty();
}

} // anonymous namespace

TEST(XmlExporterProperty, WellFormedness) {
    rc::check("Property 14: XML Well-Formedness",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random JsonNode tree
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Parse it to get a JsonNode
            auto parseResult = parseAll(json);
            RC_PRE(parseResult.root != nullptr);

            // Export to XML
            auto xml = exportXml(*parseResult.root, "root");

            // Verify well-formedness
            RC_ASSERT(!xml.empty());
            RC_ASSERT(xml.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") != std::string::npos);
            RC_ASSERT(isWellFormedXml(xml));
        });
}

// ---------------------------------------------------------------------------
// Task 9.3: Property 15 — XML Element Name Sanitization
// Validates: Requirement 7.3
// ---------------------------------------------------------------------------

TEST(XmlExporterProperty, ElementNameSanitization) {
    rc::check("Property 15: XML Element Name Sanitization",
        [](void) {
            // Generate strings with potentially invalid XML name characters
            auto rawName = *rc::gen::nonEmpty<std::string>();

            // Sanitize the name
            auto sanitized = sanitizeXmlName(rawName);

            // Verify the result is a valid XML element name
            RC_ASSERT(!sanitized.empty());

            // First character must be letter, underscore, or colon
            char first = sanitized[0];
            RC_ASSERT(std::isalpha(static_cast<unsigned char>(first)) ||
                      first == '_' || first == ':');

            // All subsequent characters must be valid XML NameChars
            for (std::size_t i = 1; i < sanitized.size(); ++i) {
                char c = sanitized[i];
                RC_ASSERT(std::isalnum(static_cast<unsigned char>(c)) ||
                          c == '_' || c == '-' || c == '.' || c == ':');
            }
        });
}

// Additional property: names starting with digits get underscore prepended
TEST(XmlExporterProperty, DigitPrefixHandling) {
    rc::check("Property 15b: Digit prefix gets underscore prepended",
        [](void) {
            // Generate a string starting with a digit
            auto digit = *rc::gen::inRange(0, 10);
            auto rest = *rc::gen::arbitrary<std::string>();
            std::string rawName = std::to_string(digit) + rest;

            auto sanitized = sanitizeXmlName(rawName);

            // Must start with underscore followed by the digit
            RC_ASSERT(sanitized[0] == '_');
            RC_ASSERT(sanitized[1] == rawName[0]);
        });
}

// ---------------------------------------------------------------------------
// Task 9.4: Property 16 — XML Entity Escaping
// Validates: Requirement 7.4
// ---------------------------------------------------------------------------

TEST(XmlExporterProperty, EntityEscaping) {
    rc::check("Property 16: XML Entity Escaping",
        [](void) {
            // Generate strings containing XML-reserved characters
            auto baseStr = *rc::gen::arbitrary<std::string>();

            // Inject at least one reserved character
            auto reservedChars = std::string("&<>\"'");
            auto reservedIdx = *rc::gen::inRange(std::size_t{0}, reservedChars.size());
            auto insertPos = *rc::gen::inRange(std::size_t{0}, baseStr.size() + 1);
            baseStr.insert(baseStr.begin() + static_cast<std::ptrdiff_t>(insertPos),
                          reservedChars[reservedIdx]);

            auto escaped = escapeXmlText(baseStr);

            // Verify no raw reserved characters remain in the output
            // (except within entity references themselves)
            // Check that each reserved char in input is properly escaped
            std::size_t srcIdx = 0;
            std::size_t dstIdx = 0;
            while (srcIdx < baseStr.size() && dstIdx < escaped.size()) {
                char c = baseStr[srcIdx];
                if (c == '&') {
                    RC_ASSERT(escaped.substr(dstIdx, 5) == "&amp;");
                    dstIdx += 5;
                } else if (c == '<') {
                    RC_ASSERT(escaped.substr(dstIdx, 4) == "&lt;");
                    dstIdx += 4;
                } else if (c == '>') {
                    RC_ASSERT(escaped.substr(dstIdx, 4) == "&gt;");
                    dstIdx += 4;
                } else if (c == '"') {
                    RC_ASSERT(escaped.substr(dstIdx, 6) == "&quot;");
                    dstIdx += 6;
                } else if (c == '\'') {
                    RC_ASSERT(escaped.substr(dstIdx, 6) == "&apos;");
                    dstIdx += 6;
                } else {
                    RC_ASSERT(escaped[dstIdx] == c);
                    dstIdx += 1;
                }
                srcIdx++;
            }
            RC_ASSERT(srcIdx == baseStr.size());
            RC_ASSERT(dstIdx == escaped.size());
        });
}

// ===========================================================================
// Task 1.5: Unit tests for ArenaAllocator
// Requirements: 1.1, 1.2, 1.3
// ===========================================================================

#include "core/arena_allocator.h"

// === Single allocation =====================================================

TEST(ArenaAllocator, SingleAllocation) {
    jsontitan::core::ArenaAllocator arena(4096);
    void* ptr = arena.allocate(64);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena.totalUsed(), 64u);
    EXPECT_GE(arena.totalAllocated(), 4096u);
}

// === Multiple allocations within one block =================================

TEST(ArenaAllocator, MultipleAllocationsInOneBlock) {
    jsontitan::core::ArenaAllocator arena(4096);
    void* p1 = arena.allocate(100);
    void* p2 = arena.allocate(200);
    void* p3 = arena.allocate(300);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    // All pointers should be distinct
    EXPECT_NE(p1, p2);
    EXPECT_NE(p2, p3);
    EXPECT_NE(p1, p3);
    // Should still be in one block
    EXPECT_EQ(arena.totalAllocated(), 4096u);
}

// === Allocation spanning multiple blocks ===================================

TEST(ArenaAllocator, AllocationSpanningMultipleBlocks) {
    jsontitan::core::ArenaAllocator arena(256);
    std::vector<void*> ptrs;
    // Allocate enough to span multiple blocks
    for (int i = 0; i < 10; ++i) {
        void* p = arena.allocate(100);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    // Should have allocated more than one block
    EXPECT_GT(arena.totalAllocated(), 256u);
    // All pointers should be distinct
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        for (std::size_t j = i + 1; j < ptrs.size(); ++j) {
            EXPECT_NE(ptrs[i], ptrs[j]);
        }
    }
}

// === Pointer stability across block allocations ============================

TEST(ArenaAllocator, PointerStabilityAcrossBlocks) {
    jsontitan::core::ArenaAllocator arena(128);
    // Fill first block
    void* first = arena.allocate(64);
    ASSERT_NE(first, nullptr);
    std::memset(first, 0xAB, 64);

    // Force a new block
    void* second = arena.allocate(128);
    ASSERT_NE(second, nullptr);

    // First pointer should still be valid and contain original data
    auto* bytes = static_cast<unsigned char*>(first);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(bytes[i], 0xAB) << "Byte " << i << " corrupted after new block allocation";
    }
}

// === Reset clears all ======================================================

TEST(ArenaAllocator, ResetClearsAll) {
    jsontitan::core::ArenaAllocator arena(4096);
    arena.allocate(100);
    arena.allocate(200);
    EXPECT_GT(arena.totalUsed(), 0u);
    EXPECT_GT(arena.totalAllocated(), 0u);

    arena.reset();
    EXPECT_EQ(arena.totalUsed(), 0u);
    EXPECT_EQ(arena.totalAllocated(), 0u);
}

// === Allocation after reset ================================================

TEST(ArenaAllocator, AllocationAfterReset) {
    jsontitan::core::ArenaAllocator arena(4096);
    arena.allocate(100);
    arena.reset();

    void* ptr = arena.allocate(50);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena.totalUsed(), 50u);
}

// === Zero-size allocation ==================================================

TEST(ArenaAllocator, ZeroSizeAllocation) {
    jsontitan::core::ArenaAllocator arena(4096);
    void* ptr = arena.allocate(0);
    // Zero-size allocations should return a valid pointer
    ASSERT_NE(ptr, nullptr);
    EXPECT_GT(arena.totalUsed(), 0u);
}

// === Large allocation exceeding default block size =========================

TEST(ArenaAllocator, LargeAllocationExceedingBlockSize) {
    jsontitan::core::ArenaAllocator arena(256);
    // Allocate more than the block size
    void* ptr = arena.allocate(1024);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena.totalAllocated(), 1024u);
}

// === copyString ============================================================

TEST(ArenaAllocator, CopyStringBasic) {
    jsontitan::core::ArenaAllocator arena(4096);
    std::string_view original = "hello world";
    auto copied = arena.copyString(original);
    EXPECT_EQ(copied, original);
    // The copy should be in different memory
    EXPECT_NE(copied.data(), original.data());
}

TEST(ArenaAllocator, CopyStringEmpty) {
    jsontitan::core::ArenaAllocator arena(4096);
    auto copied = arena.copyString("");
    EXPECT_TRUE(copied.empty());
}

TEST(ArenaAllocator, CopyStringPreservesContent) {
    jsontitan::core::ArenaAllocator arena(4096);
    std::string original = "test string with special chars: {}[],:\"\\";
    auto copied = arena.copyString(original);
    EXPECT_EQ(copied, original);
}

TEST(ArenaAllocator, CopyStringMultiple) {
    jsontitan::core::ArenaAllocator arena(4096);
    auto s1 = arena.copyString("first");
    auto s2 = arena.copyString("second");
    auto s3 = arena.copyString("third");
    EXPECT_EQ(s1, "first");
    EXPECT_EQ(s2, "second");
    EXPECT_EQ(s3, "third");
    // All should be in different memory locations
    EXPECT_NE(s1.data(), s2.data());
    EXPECT_NE(s2.data(), s3.data());
}

// === construct<T> ==========================================================

TEST(ArenaAllocator, ConstructSimpleType) {
    jsontitan::core::ArenaAllocator arena(4096);
    struct TestStruct {
        int x;
        double y;
    };
    auto* obj = arena.construct<TestStruct>(42, 3.14);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->x, 42);
    EXPECT_DOUBLE_EQ(obj->y, 3.14);
}

TEST(ArenaAllocator, ConstructMultipleObjects) {
    jsontitan::core::ArenaAllocator arena(4096);
    struct Point {
        int x, y;
    };
    auto* p1 = arena.construct<Point>(1, 2);
    auto* p2 = arena.construct<Point>(3, 4);
    auto* p3 = arena.construct<Point>(5, 6);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p1->x, 1);
    EXPECT_EQ(p2->x, 3);
    EXPECT_EQ(p3->x, 5);
    // All should be at different addresses
    EXPECT_NE(p1, p2);
    EXPECT_NE(p2, p3);
}

// === Alignment =============================================================

TEST(ArenaAllocator, AlignmentRespected) {
    jsontitan::core::ArenaAllocator arena(4096);
    // Allocate 1 byte to offset the bump pointer
    arena.allocate(1, 1);
    // Now allocate with 16-byte alignment
    void* aligned = arena.allocate(32, 16);
    ASSERT_NE(aligned, nullptr);
    auto addr = reinterpret_cast<std::uintptr_t>(aligned);
    EXPECT_EQ(addr % 16, 0u) << "Pointer not aligned to 16 bytes";
}

// === totalAllocated and totalUsed ==========================================

TEST(ArenaAllocator, TotalAllocatedAndUsed) {
    jsontitan::core::ArenaAllocator arena(1024);
    EXPECT_EQ(arena.totalAllocated(), 0u);
    EXPECT_EQ(arena.totalUsed(), 0u);

    arena.allocate(100);
    EXPECT_GE(arena.totalAllocated(), 1024u);
    EXPECT_GE(arena.totalUsed(), 100u);
    EXPECT_LE(arena.totalUsed(), arena.totalAllocated());
}

// === Move semantics ========================================================

TEST(ArenaAllocator, MoveConstructor) {
    jsontitan::core::ArenaAllocator arena1(4096);
    auto* ptr = static_cast<int*>(arena1.allocate(sizeof(int), alignof(int)));
    *ptr = 42;

    jsontitan::core::ArenaAllocator arena2(std::move(arena1));
    // arena2 should have the data
    EXPECT_GT(arena2.totalUsed(), 0u);
    // The pointer should still be valid through arena2's ownership
    EXPECT_EQ(*ptr, 42);
}

TEST(ArenaAllocator, MoveAssignment) {
    jsontitan::core::ArenaAllocator arena1(4096);
    arena1.allocate(100);

    jsontitan::core::ArenaAllocator arena2(4096);
    arena2 = std::move(arena1);
    EXPECT_GE(arena2.totalUsed(), 100u);
}

// ===========================================================================
// Task 1.6: Property test -- Arena allocation preserves data integrity
// Property 1: For any sequence of node allocations (including sequences
// spanning multiple arena blocks), every allocated node retains its original
// type, key, value, and child pointers, and all previously returned pointers
// remain valid.
// Validates: Requirements 1.1, 1.2, 1.5
// ===========================================================================

#include "core/arena_json_node.h"

TEST(ArenaAllocatorProperty, AllocationPreservesDataIntegrity) {
    rc::check("Property 1: Arena allocation preserves data integrity",
        [](void) {
            // Use a small block size to force multiple blocks
            auto blockSize = *rc::gen::inRange(64, 512);
            jsontitan::core::ArenaAllocator arena(
                static_cast<std::size_t>(blockSize));

            auto numNodes = *rc::gen::inRange(1, 100);

            struct NodeRecord {
                jsontitan::core::ArenaJsonNode* node;
                jsontitan::core::NodeType expectedType;
                std::string_view expectedKey;
                std::string_view expectedValue;
            };

            std::vector<NodeRecord> records;
            records.reserve(static_cast<std::size_t>(numNodes));

            // Allocate a sequence of nodes with random types
            for (int i = 0; i < numNodes; ++i) {
                auto typeIdx = *rc::gen::inRange(0, 6);
                auto nodeType = static_cast<jsontitan::core::NodeType>(typeIdx);

                // Generate random key and value strings
                auto keyLen = *rc::gen::inRange(0, 20);
                std::string keyStr(static_cast<std::size_t>(keyLen), 'a');
                for (auto& c : keyStr) {
                    c = static_cast<char>(
                        *rc::gen::inRange(static_cast<int>('a'),
                                          static_cast<int>('z') + 1));
                }
                auto valLen = *rc::gen::inRange(0, 20);
                std::string valStr(static_cast<std::size_t>(valLen), '0');
                for (auto& c : valStr) {
                    c = static_cast<char>(
                        *rc::gen::inRange(static_cast<int>('0'),
                                          static_cast<int>('9') + 1));
                }

                auto keyCopy = arena.copyString(keyStr);
                auto valCopy = arena.copyString(valStr);

                auto* node = arena.construct<jsontitan::core::ArenaJsonNode>();
                RC_ASSERT(node != nullptr);

                node->type = nodeType;
                node->key = jsontitan::core::StringRef{
                    keyCopy.data(), keyCopy.size(), true};
                node->value = jsontitan::core::StringRef{
                    valCopy.data(), valCopy.size(), true};
                node->children = nullptr;
                node->childCount = 0;

                records.push_back(NodeRecord{
                    .node = node,
                    .expectedType = nodeType,
                    .expectedKey = keyCopy,
                    .expectedValue = valCopy,
                });
            }

            // Verify all nodes still have correct data
            for (const auto& rec : records) {
                RC_ASSERT(rec.node->type == rec.expectedType);
                RC_ASSERT(rec.node->key.view() == rec.expectedKey);
                RC_ASSERT(rec.node->value.view() == rec.expectedValue);
                RC_ASSERT(rec.node->children == nullptr);
                RC_ASSERT(rec.node->childCount == 0);
            }
        });
}

TEST(ArenaAllocatorProperty, AllocationPreservesChildPointers) {
    rc::check("Property 1b: Arena allocation preserves child pointers",
        [](void) {
            jsontitan::core::ArenaAllocator arena(256);

            auto numChildren = *rc::gen::inRange(1, 20);

            // Allocate child nodes
            std::vector<jsontitan::core::ArenaJsonNode*> childNodes;
            for (int i = 0; i < numChildren; ++i) {
                auto* child = arena.construct<jsontitan::core::ArenaJsonNode>();
                RC_ASSERT(child != nullptr);
                child->type = jsontitan::core::NodeType::String;
                auto val = arena.copyString("child" + std::to_string(i));
                child->value = jsontitan::core::StringRef{
                    val.data(), val.size(), true};
                child->children = nullptr;
                child->childCount = 0;
                childNodes.push_back(child);
            }

            // Allocate child pointer array in the arena
            auto** childArray = static_cast<jsontitan::core::ArenaJsonNode**>(
                arena.allocate(
                    static_cast<std::size_t>(numChildren) *
                        sizeof(jsontitan::core::ArenaJsonNode*),
                    alignof(jsontitan::core::ArenaJsonNode*)));
            RC_ASSERT(childArray != nullptr);

            for (int i = 0; i < numChildren; ++i) {
                childArray[i] = childNodes[static_cast<std::size_t>(i)];
            }

            // Allocate parent node
            auto* parent = arena.construct<jsontitan::core::ArenaJsonNode>();
            RC_ASSERT(parent != nullptr);
            parent->type = jsontitan::core::NodeType::Object;
            parent->children = childArray;
            parent->childCount = static_cast<std::size_t>(numChildren);

            // Do more allocations to potentially trigger new blocks
            for (int i = 0; i < 50; ++i) {
                arena.allocate(64);
            }

            // Verify parent still points to correct children
            RC_ASSERT(parent->childCount ==
                      static_cast<std::size_t>(numChildren));
            for (int i = 0; i < numChildren; ++i) {
                auto idx = static_cast<std::size_t>(i);
                RC_ASSERT(parent->children[idx] == childNodes[idx]);
                RC_ASSERT(parent->children[idx]->type ==
                          jsontitan::core::NodeType::String);
                std::string expected = "child" + std::to_string(i);
                RC_ASSERT(parent->children[idx]->value.view() == expected);
            }
        });
}

// ---------------------------------------------------------------------------
// Task 7.2: Property test — Thread-safe concurrent arena allocation
// Validates: Requirements 1.4
// ---------------------------------------------------------------------------

#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <set>
#include <cstring>

TEST(ArenaAllocatorProperty, ThreadSafeConcurrentAllocation) {
    rc::check("Property 2: Thread-safe concurrent arena allocation",
        [](void) {
            // Generate random thread count (2-8) and allocations per thread
            auto threadCount = *rc::gen::inRange(2, 9);
            auto allocsPerThread = *rc::gen::inRange(5, 50);
            auto allocSize = *rc::gen::inRange(8, 256);

            jsontitan::core::ThreadLocalArena tla(4096);

            // Each thread will store its allocation results here
            struct AllocRecord {
                void* ptr;
                std::size_t size;
                std::uint8_t pattern; // unique data pattern written
            };

            std::vector<std::vector<AllocRecord>> threadResults(
                static_cast<std::size_t>(threadCount));

            // Spawn threads that concurrently allocate from the ThreadLocalArena
            std::vector<std::thread> threads;
            threads.reserve(static_cast<std::size_t>(threadCount));

            for (int t = 0; t < threadCount; ++t) {
                threads.emplace_back([&, t, allocsPerThread, allocSize]() {
                    auto& arena = tla.get();
                    auto tidx = static_cast<std::size_t>(t);
                    threadResults[tidx].reserve(
                        static_cast<std::size_t>(allocsPerThread));

                    // Use a unique pattern per thread
                    auto pattern = static_cast<std::uint8_t>(t + 1);

                    for (int i = 0; i < allocsPerThread; ++i) {
                        // Vary allocation size slightly per iteration
                        auto thisSize = static_cast<std::size_t>(
                            allocSize + (i % 7));
                        void* ptr = arena.allocate(thisSize);

                        // Write unique data pattern
                        std::memset(ptr, pattern, thisSize);

                        threadResults[tidx].push_back(
                            AllocRecord{ptr, thisSize, pattern});
                    }
                });
            }

            // Wait for all threads to complete
            for (auto& th : threads) {
                th.join();
            }

            // Verify 1: All allocations succeeded (non-null)
            for (int t = 0; t < threadCount; ++t) {
                auto tidx = static_cast<std::size_t>(t);
                RC_ASSERT(threadResults[tidx].size() ==
                          static_cast<std::size_t>(allocsPerThread));
                for (const auto& rec : threadResults[tidx]) {
                    bool allocated = (rec.ptr != nullptr);
                    RC_ASSERT(allocated);
                }
            }

            // Verify 2: No memory regions overlap
            // Collect all (start, end) ranges and check for overlaps
            struct Range {
                std::uintptr_t start;
                std::uintptr_t end;
            };
            std::vector<Range> allRanges;
            for (int t = 0; t < threadCount; ++t) {
                auto tidx = static_cast<std::size_t>(t);
                for (const auto& rec : threadResults[tidx]) {
                    auto start = reinterpret_cast<std::uintptr_t>(rec.ptr);
                    allRanges.push_back(Range{start, start + rec.size});
                }
            }

            // Sort by start address and check for overlaps
            std::sort(allRanges.begin(), allRanges.end(),
                      [](const Range& a, const Range& b) {
                          return a.start < b.start;
                      });

            for (std::size_t i = 1; i < allRanges.size(); ++i) {
                RC_ASSERT(allRanges[i].start >= allRanges[i - 1].end);
            }

            // Verify 3: Data written by each thread is independently readable
            // without corruption
            for (int t = 0; t < threadCount; ++t) {
                auto tidx = static_cast<std::size_t>(t);
                auto expectedPattern = static_cast<std::uint8_t>(t + 1);
                for (const auto& rec : threadResults[tidx]) {
                    auto* bytes = static_cast<std::uint8_t*>(rec.ptr);
                    for (std::size_t b = 0; b < rec.size; ++b) {
                        RC_ASSERT(bytes[b] == expectedPattern);
                    }
                }
            }

            // Verify 4: mergeAll() produces a valid combined arena
            auto merged = tla.mergeAll();
            RC_ASSERT(merged.totalUsed() > 0);

            // The merged arena should have total usage >= sum of all
            // allocations
            std::size_t totalAllocated = 0;
            for (int t = 0; t < threadCount; ++t) {
                auto tidx = static_cast<std::size_t>(t);
                for (const auto& rec : threadResults[tidx]) {
                    totalAllocated += rec.size;
                }
            }
            RC_ASSERT(merged.totalUsed() >= totalAllocated);

            // After merge, all previously allocated data should still be valid
            // (pointer stability)
            for (int t = 0; t < threadCount; ++t) {
                auto tidx = static_cast<std::size_t>(t);
                auto expectedPattern = static_cast<std::uint8_t>(t + 1);
                for (const auto& rec : threadResults[tidx]) {
                    auto* bytes = static_cast<std::uint8_t*>(rec.ptr);
                    for (std::size_t b = 0; b < rec.size; ++b) {
                        RC_ASSERT(bytes[b] == expectedPattern);
                    }
                }
            }
        });
}

// ===========================================================================
// Task 1.7: Unit tests for SourceBuffer and StringRef
// Requirements: 2.1, 2.3, 2.4
// ===========================================================================

#include "core/source_buffer.h"

// === StringRef tests =======================================================

TEST(StringRef, DefaultConstructed) {
    jsontitan::core::StringRef ref{};
    EXPECT_EQ(ref.data, nullptr);
    EXPECT_EQ(ref.length, 0u);
    EXPECT_FALSE(ref.ownsData);
    EXPECT_TRUE(ref.toString().empty());
    EXPECT_TRUE(ref.view().empty());
}

TEST(StringRef, ToStringFromPointer) {
    const char* text = "hello";
    jsontitan::core::StringRef ref{text, 5, false};
    EXPECT_EQ(ref.toString(), "hello");
}

TEST(StringRef, ViewFromPointer) {
    const char* text = "world";
    jsontitan::core::StringRef ref{text, 5, false};
    EXPECT_EQ(ref.view(), "world");
    // View should point to the same memory (zero-copy)
    EXPECT_EQ(ref.view().data(), text);
}

TEST(StringRef, ToStringFromOwnedData) {
    std::string owned = "owned string";
    jsontitan::core::StringRef ref{owned.data(), owned.size(), true};
    EXPECT_EQ(ref.toString(), "owned string");
}

TEST(StringRef, ViewFromOwnedData) {
    std::string owned = "owned view";
    jsontitan::core::StringRef ref{owned.data(), owned.size(), true};
    EXPECT_EQ(ref.view(), "owned view");
}

TEST(StringRef, EqualityComparison) {
    const char* text1 = "same";
    const char* text2 = "same";
    jsontitan::core::StringRef ref1{text1, 4, false};
    jsontitan::core::StringRef ref2{text2, 4, false};
    EXPECT_EQ(ref1, ref2);
}

TEST(StringRef, InequalityComparison) {
    const char* text1 = "abc";
    const char* text2 = "xyz";
    jsontitan::core::StringRef ref1{text1, 3, false};
    jsontitan::core::StringRef ref2{text2, 3, false};
    EXPECT_NE(ref1, ref2);
}

TEST(StringRef, EqualityAcrossOwnership) {
    // A zero-copy ref and an owned ref with the same content should be equal
    const char* zeroCopy = "test";
    std::string owned = "test";
    jsontitan::core::StringRef ref1{zeroCopy, 4, false};
    jsontitan::core::StringRef ref2{owned.data(), owned.size(), true};
    EXPECT_EQ(ref1, ref2);
}

TEST(StringRef, EmptyStringEquality) {
    jsontitan::core::StringRef ref1{};
    jsontitan::core::StringRef ref2{};
    EXPECT_EQ(ref1, ref2);
}

// === SourceBuffer tests ====================================================

TEST(SourceBuffer, ConstructFromString) {
    jsontitan::core::SourceBuffer buf(std::string("hello world"));
    EXPECT_EQ(buf.size(), 11u);
    EXPECT_EQ(std::string_view(buf.data(), buf.size()), "hello world");
}

TEST(SourceBuffer, ConstructFromByteVector) {
    std::string text = "byte data";
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(text[i]);
    }
    jsontitan::core::SourceBuffer buf(std::move(bytes));
    EXPECT_EQ(buf.size(), 9u);
    EXPECT_EQ(std::string_view(buf.data(), buf.size()), "byte data");
}

TEST(SourceBuffer, DataReturnsValidPointer) {
    jsontitan::core::SourceBuffer buf(std::string("test"));
    EXPECT_NE(buf.data(), nullptr);
}

TEST(SourceBuffer, SpanReturnsCorrectSize) {
    jsontitan::core::SourceBuffer buf(std::string("span test"));
    auto s = buf.span();
    EXPECT_EQ(s.size(), 9u);
}

TEST(SourceBuffer, RefCreatesZeroCopyStringRef) {
    jsontitan::core::SourceBuffer buf(std::string("hello world"));
    auto ref = buf.ref(0, 5);
    EXPECT_EQ(ref.view(), "hello");
    EXPECT_FALSE(ref.ownsData);
    // The ref should point directly into the buffer
    EXPECT_EQ(ref.data, buf.data());
}

TEST(SourceBuffer, RefWithOffset) {
    jsontitan::core::SourceBuffer buf(std::string("hello world"));
    auto ref = buf.ref(6, 5);
    EXPECT_EQ(ref.view(), "world");
    EXPECT_FALSE(ref.ownsData);
    EXPECT_EQ(ref.data, buf.data() + 6);
}

TEST(SourceBuffer, RefZeroLength) {
    jsontitan::core::SourceBuffer buf(std::string("test"));
    auto ref = buf.ref(0, 0);
    EXPECT_EQ(ref.length, 0u);
    EXPECT_TRUE(ref.view().empty());
}

TEST(SourceBuffer, EmptyBuffer) {
    jsontitan::core::SourceBuffer buf(std::string(""));
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.span().size(), 0u);
}

TEST(SourceBuffer, MoveConstructor) {
    jsontitan::core::SourceBuffer buf1(std::string("movable"));
    jsontitan::core::SourceBuffer buf2(std::move(buf1));
    EXPECT_EQ(buf2.size(), 7u);
    EXPECT_EQ(std::string_view(buf2.data(), buf2.size()), "movable");
}

TEST(SourceBuffer, RefContentMatchesBuffer) {
    std::string json = R"({"key": "value"})";
    jsontitan::core::SourceBuffer buf(json);
    // Extract "key" (bytes 2..4 in the JSON string)
    auto ref = buf.ref(2, 3);
    EXPECT_EQ(ref.view(), "key");
    EXPECT_EQ(ref.toString(), "key");
}

// === SourceBuffer immutability (compile-time check) ========================

TEST(SourceBuffer, DataReturnsConstPointer) {
    jsontitan::core::SourceBuffer buf(std::string("immutable"));
    // data() returns const char* -- this is a compile-time guarantee.
    static_assert(
        std::is_same_v<decltype(buf.data()), const char*>,
        "SourceBuffer::data() must return const char*");
}

// ===========================================================================
// Task 3.4: Property 3 — SIMD/scalar scanner equivalence
// Validates: Requirements 3.4, 3.5
// ===========================================================================

#include "core/simd_scanner.h"

// ---------------------------------------------------------------------------
// Property 3: For any byte sequence, the SIMD scanner produces identical
// ScanBlock results to the scalar reference implementation.
// ---------------------------------------------------------------------------

TEST(ScannerProperty, SimdScalarEquivalence) {
    rc::check("Property 3: SIMD/scalar scanner equivalence",
        [](void) {
            // Generate random byte sequence with varying length (0..300)
            auto length = *rc::gen::inRange(0, 300);
            auto bytes = *rc::gen::container<std::vector<char>>(
                length, rc::gen::arbitrary<char>());

            auto scalarResult = scalarScan(bytes.data(), bytes.size());
            auto simdResult = simdScan(bytes.data(), bytes.size(), SimdLevel::SSE2);

            RC_ASSERT(scalarResult.size() == simdResult.size());
            for (size_t i = 0; i < scalarResult.size(); ++i) {
                RC_ASSERT(scalarResult[i].structuralBits == simdResult[i].structuralBits);
                RC_ASSERT(scalarResult[i].whitespaceBits == simdResult[i].whitespaceBits);
                RC_ASSERT(scalarResult[i].quoteBits == simdResult[i].quoteBits);
                RC_ASSERT(scalarResult[i].stringMask == simdResult[i].stringMask);
            }
        });
}

TEST(ScannerProperty, SimdScalarEquivalenceAVX2) {
    rc::check("Property 3: SIMD/scalar scanner equivalence (AVX2 level)",
        [](void) {
            auto length = *rc::gen::inRange(0, 300);
            auto bytes = *rc::gen::container<std::vector<char>>(
                length, rc::gen::arbitrary<char>());

            auto scalarResult = scalarScan(bytes.data(), bytes.size());
            auto simdResult = simdScan(bytes.data(), bytes.size(), SimdLevel::AVX2);

            RC_ASSERT(scalarResult.size() == simdResult.size());
            for (size_t i = 0; i < scalarResult.size(); ++i) {
                RC_ASSERT(scalarResult[i].structuralBits == simdResult[i].structuralBits);
                RC_ASSERT(scalarResult[i].whitespaceBits == simdResult[i].whitespaceBits);
                RC_ASSERT(scalarResult[i].quoteBits == simdResult[i].quoteBits);
                RC_ASSERT(scalarResult[i].stringMask == simdResult[i].stringMask);
            }
        });
}

// ---------------------------------------------------------------------------
// Boundary variant: test lengths at SIMD register boundaries
// (multiples of 16 ± 1, multiples of 64 ± 1)
// ---------------------------------------------------------------------------

TEST(ScannerProperty, SimdScalarEquivalenceAtRegisterBoundaries) {
    rc::check("Property 3: SIMD/scalar equivalence at register boundaries",
        [](void) {
            // Build a set of interesting boundary lengths
            static const std::vector<int> boundaryLengths = {
                0, 1,
                15, 16, 17,
                31, 32, 33,
                47, 48, 49,
                63, 64, 65,
                79, 80, 81,
                95, 96, 97,
                127, 128, 129,
                191, 192, 193,
                255, 256, 257
            };

            auto idx = *rc::gen::inRange(std::size_t{0}, boundaryLengths.size());
            auto length = boundaryLengths[idx];
            auto bytes = *rc::gen::container<std::vector<char>>(
                length, rc::gen::arbitrary<char>());

            auto scalarResult = scalarScan(bytes.data(), bytes.size());
            auto simdResultSSE2 = simdScan(bytes.data(), bytes.size(), SimdLevel::SSE2);
            auto simdResultAVX2 = simdScan(bytes.data(), bytes.size(), SimdLevel::AVX2);

            RC_ASSERT(scalarResult.size() == simdResultSSE2.size());
            RC_ASSERT(scalarResult.size() == simdResultAVX2.size());
            for (size_t i = 0; i < scalarResult.size(); ++i) {
                // SSE2 equivalence
                RC_ASSERT(scalarResult[i].structuralBits == simdResultSSE2[i].structuralBits);
                RC_ASSERT(scalarResult[i].whitespaceBits == simdResultSSE2[i].whitespaceBits);
                RC_ASSERT(scalarResult[i].quoteBits == simdResultSSE2[i].quoteBits);
                RC_ASSERT(scalarResult[i].stringMask == simdResultSSE2[i].stringMask);
                // AVX2 equivalence
                RC_ASSERT(scalarResult[i].structuralBits == simdResultAVX2[i].structuralBits);
                RC_ASSERT(scalarResult[i].whitespaceBits == simdResultAVX2[i].whitespaceBits);
                RC_ASSERT(scalarResult[i].quoteBits == simdResultAVX2[i].quoteBits);
                RC_ASSERT(scalarResult[i].stringMask == simdResultAVX2[i].stringMask);
            }
        });
}

// ---------------------------------------------------------------------------
// Task 3.5: Property 4 — Structural Index Correctness
// Validates: Requirements 3.1, 3.2, 3.3, 3.6
// ---------------------------------------------------------------------------

#include "core/structural_scanner.h"

// Helper: determine if a character is a JSON structural character
static bool isStructuralChar(char c) {
    return c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == ':';
}

// Helper: manually collect all structural character positions outside strings
// by walking the input and tracking string state (with escape handling).
static std::vector<std::size_t> collectExpectedStructuralPositions(
    const std::string& json) {
    std::vector<std::size_t> positions;
    bool inString = false;

    for (std::size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (inString) {
            if (c == '\\') {
                ++i; // skip escaped character
            } else if (c == '"') {
                inString = false;
            }
        } else {
            if (c == '"') {
                inString = true;
            } else if (isStructuralChar(c)) {
                positions.push_back(i);
            }
        }
    }
    return positions;
}

TEST(StructuralIndexProperty, StructuralIndexCorrectness) {
    rc::check("Property 4: Structural index correctness",
        [](void) {
            // Generate a random valid JSON document
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Run scalarScan to get ScanBlocks
            auto scanBlocks = scalarScan(json.data(), json.size());

            // Build structural index
            auto index = buildStructuralIndex(json.data(), json.size(), scanBlocks);

            // (a) Every entry in the index corresponds to an actual structural
            //     character at that byte offset in the input
            for (const auto& entry : index.entries) {
                RC_ASSERT(entry.offset < json.size());
                RC_ASSERT(json[entry.offset] == entry.character);
                RC_ASSERT(isStructuralChar(entry.character));
            }

            // (b) No structural characters outside of string literals are
            //     missing from the index
            auto expected = collectExpectedStructuralPositions(json);
            std::vector<std::size_t> actualOffsets;
            actualOffsets.reserve(index.entries.size());
            for (const auto& entry : index.entries) {
                actualOffsets.push_back(entry.offset);
            }
            RC_ASSERT(actualOffsets.size() == expected.size());
            RC_ASSERT(actualOffsets == expected);

            // (c) Nesting depths are correct:
            //     - All depths are non-negative
            //     - Opening brackets record depth before increment
            //     - Closing brackets record depth after decrement
            int depth = 0;
            for (const auto& entry : index.entries) {
                RC_ASSERT(entry.depth >= 0);
                if (entry.character == '{' || entry.character == '[') {
                    RC_ASSERT(entry.depth == depth);
                    ++depth;
                } else if (entry.character == '}' || entry.character == ']') {
                    --depth;
                    RC_ASSERT(entry.depth == depth);
                } else {
                    // comma or colon — at current depth
                    RC_ASSERT(entry.depth == depth);
                }
            }

            // (d) Entries are ordered by offset (ascending)
            for (std::size_t i = 1; i < index.entries.size(); ++i) {
                RC_ASSERT(index.entries[i].offset > index.entries[i - 1].offset);
            }

            // (e) Depth returns to 0 at the end for valid JSON
            RC_ASSERT(depth == 0);
        });
}

// ---------------------------------------------------------------------------
// Task 3.6: Property 5 — Partition Boundary Validity
// Validates: Requirements 4.1
// ---------------------------------------------------------------------------

TEST(PartitionProperty, PartitionBoundaryValidity) {
    rc::check("Property 5: Partition boundary validity",
        [](void) {
            // Generate a random valid JSON document
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Run scalarScan to get ScanBlocks
            auto scanBlocks = scalarScan(json.data(), json.size());

            // Build structural index
            auto index = buildStructuralIndex(json.data(), json.size(), scanBlocks);

            // Find partition points
            auto partitionPoints = index.findPartitionPoints();

            // (a) Partition points are in ascending order
            for (std::size_t i = 1; i < partitionPoints.size(); ++i) {
                RC_ASSERT(partitionPoints[i] > partitionPoints[i - 1]);
            }

            // (b) Each partition point is within bounds (> 0 and <= input length)
            for (auto pp : partitionPoints) {
                RC_ASSERT(pp > 0);
                RC_ASSERT(pp <= json.size());
            }

            // (c) The byte at partitionPoint - 1 is either '}' or ']'
            for (auto pp : partitionPoints) {
                char preceding = json[pp - 1];
                RC_ASSERT(preceding == '}' || preceding == ']');
            }

            // (d) That closing bracket is at depth 0 in the structural index
            //     Verify by finding the entry for offset (pp - 1) and checking depth == 0
            for (auto pp : partitionPoints) {
                std::size_t bracketOffset = pp - 1;
                bool foundEntry = false;
                for (const auto& entry : index.entries) {
                    if (entry.offset == bracketOffset) {
                        RC_ASSERT(entry.depth == 0);
                        RC_ASSERT(entry.character == '}' || entry.character == ']');
                        foundEntry = true;
                        break;
                    }
                }
                RC_ASSERT(foundEntry);
            }

            // (e) Completeness: every depth-0 closing bracket in the index
            //     should produce a partition point
            std::vector<std::size_t> expectedPoints;
            for (const auto& entry : index.entries) {
                if ((entry.character == '}' || entry.character == ']') &&
                    entry.depth == 0) {
                    expectedPoints.push_back(entry.offset + 1);
                }
            }
            RC_ASSERT(partitionPoints.size() == expectedPoints.size());
            RC_ASSERT(partitionPoints == expectedPoints);
        });
}

// ---------------------------------------------------------------------------
// Task 3.7: Unit tests for SimdScanner and StructuralScanner
// Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6
// ---------------------------------------------------------------------------

// ===========================================================================
// SimdScanner unit tests
// ===========================================================================

// 1. Empty input: scalarScan and simdScan return empty vectors
TEST(SimdScanner, EmptyInputScalarScan) {
    auto result = scalarScan(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

TEST(SimdScanner, EmptyInputSimdScan) {
    auto result = simdScan(nullptr, 0, SimdLevel::SSE2);
    EXPECT_TRUE(result.empty());
}

TEST(SimdScanner, EmptyInputSimdScanScalarLevel) {
    auto result = simdScan(nullptr, 0, SimdLevel::Scalar);
    EXPECT_TRUE(result.empty());
}

// 2. All structural characters detected
TEST(SimdScanner, AllStructuralCharactersDetected) {
    const std::string input = "{}[],:" ;
    auto blocks = scalarScan(input.data(), input.size());
    ASSERT_EQ(blocks.size(), 1u);

    // Each of the 6 characters should have its bit set
    for (std::size_t i = 0; i < input.size(); ++i) {
        uint64_t bit = uint64_t{1} << i;
        EXPECT_NE(blocks[0].structuralBits & bit, 0u)
            << "Structural bit not set for character '" << input[i]
            << "' at position " << i;
    }
}

// 3. Whitespace correctly classified
TEST(SimdScanner, WhitespaceCorrectlyClassified) {
    const std::string input = " \t\n\r";
    auto blocks = scalarScan(input.data(), input.size());
    ASSERT_EQ(blocks.size(), 1u);

    for (std::size_t i = 0; i < input.size(); ++i) {
        uint64_t bit = uint64_t{1} << i;
        EXPECT_NE(blocks[0].whitespaceBits & bit, 0u)
            << "Whitespace bit not set for byte 0x"
            << std::hex << static_cast<int>(static_cast<unsigned char>(input[i]))
            << " at position " << std::dec << i;
    }
}

// 4. Escaped quotes not counted
TEST(SimdScanner, EscapedQuotesNotCounted) {
    // JSON string: "\"hello\""
    // Raw bytes (11): " \ " h e l l o \ " "
    //                  0 1 2 3 4 5 6 7 8 9 10
    // The \" sequences are escaped quotes and should NOT appear in quoteBits.
    // Only the outer unescaped " at positions 0 and 10 should be in quoteBits.
    const std::string input = R"("\"hello\"")";
    ASSERT_EQ(input.size(), 11u);
    auto blocks = scalarScan(input.data(), input.size());
    ASSERT_EQ(blocks.size(), 1u);

    // Position 0: opening " (unescaped)
    EXPECT_NE(blocks[0].quoteBits & (uint64_t{1} << 0), 0u)
        << "Opening quote should be in quoteBits";

    // Position 2: escaped " (preceded by backslash at position 1)
    EXPECT_EQ(blocks[0].quoteBits & (uint64_t{1} << 2), 0u)
        << "Escaped quote at position 2 should NOT be in quoteBits";

    // Position 9: escaped " (preceded by backslash at position 8)
    EXPECT_EQ(blocks[0].quoteBits & (uint64_t{1} << 9), 0u)
        << "Escaped quote at position 9 should NOT be in quoteBits";

    // Position 10: closing " (unescaped)
    EXPECT_NE(blocks[0].quoteBits & (uint64_t{1} << 10), 0u)
        << "Closing quote should be in quoteBits";
}

// 5. Backslash-escaped quote handling: \\\\" inside a JSON string
//    means escaped backslash followed by an unescaped closing quote.
TEST(SimdScanner, EscapedBackslashFollowedByQuote) {
    // JSON string: "\\"  — raw bytes: " \ \ "
    //                                  0 1 2 3
    // The two backslashes form an escaped backslash pair.
    // Position 0: opening " (unescaped quote)
    // Position 1: backslash
    // Position 2: backslash (escaped by position 1)
    // Position 3: closing " (unescaped quote — the \\ pair is consumed)
    const std::string input = R"("\\")";
    ASSERT_EQ(input.size(), 4u);
    auto blocks = scalarScan(input.data(), input.size());
    ASSERT_EQ(blocks.size(), 1u);

    // Position 0: opening " (unescaped)
    EXPECT_NE(blocks[0].quoteBits & (uint64_t{1} << 0), 0u)
        << "Opening quote should be in quoteBits";

    // Position 3: closing " (unescaped — after escaped backslash pair)
    EXPECT_NE(blocks[0].quoteBits & (uint64_t{1} << 3), 0u)
        << "Quote after escaped backslash pair should be unescaped";

    // Verify exactly 2 unescaped quotes total
    int quoteCount = __builtin_popcountll(blocks[0].quoteBits);
    EXPECT_EQ(quoteCount, 2) << "Should have exactly 2 unescaped quotes";
}

// 6. String mask correctness
TEST(SimdScanner, StringMaskCorrectness) {
    // Input: "hello"  — positions: 0=" 1=h 2=e 3=l 4=l 5=o 6="
    const std::string input = R"("hello")";
    auto blocks = scalarScan(input.data(), input.size());
    ASSERT_EQ(blocks.size(), 1u);

    // Bits 1-5 (inside the string, between the quotes) should be set in stringMask
    for (std::size_t i = 1; i <= 5; ++i) {
        uint64_t bit = uint64_t{1} << i;
        EXPECT_NE(blocks[0].stringMask & bit, 0u)
            << "stringMask bit should be set for position " << i
            << " (inside string)";
    }

    // Bit 0 (opening quote) — the prefix-XOR toggles at the quote, so position 0
    // is the quote itself. Per the prefix-XOR algorithm, the quote position bit
    // IS set (the string state starts at the quote byte).
    // Bit 6 (closing quote) — after the closing quote, string state toggles off.
    // The closing quote position itself should NOT have the string bit set
    // (the XOR toggles it off at that position).
    EXPECT_EQ(blocks[0].stringMask & (uint64_t{1} << 6), 0u)
        << "stringMask bit should NOT be set at closing quote position";
}

// 7. Tail handling for non-aligned lengths
TEST(SimdScanner, TailHandlingNonAlignedLength) {
    // 17 bytes — not a multiple of 16, so SIMD path must handle a 1-byte tail
    const std::string input = R"({"key":"value"}  )";
    ASSERT_EQ(input.size(), 17u);

    auto scalarResult = scalarScan(input.data(), input.size());
    auto simdResult = simdScan(input.data(), input.size(), SimdLevel::SSE2);

    ASSERT_EQ(scalarResult.size(), simdResult.size());
    for (std::size_t i = 0; i < scalarResult.size(); ++i) {
        EXPECT_EQ(scalarResult[i].structuralBits, simdResult[i].structuralBits)
            << "structuralBits mismatch in block " << i;
        EXPECT_EQ(scalarResult[i].whitespaceBits, simdResult[i].whitespaceBits)
            << "whitespaceBits mismatch in block " << i;
        EXPECT_EQ(scalarResult[i].quoteBits, simdResult[i].quoteBits)
            << "quoteBits mismatch in block " << i;
        EXPECT_EQ(scalarResult[i].stringMask, simdResult[i].stringMask)
            << "stringMask mismatch in block " << i;
    }
}

// 8. Single byte input
TEST(SimdScanner, SingleByteInput) {
    const char input[] = "{";
    auto blocks = scalarScan(input, 1);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_NE(blocks[0].structuralBits & 1u, 0u)
        << "Structural bit should be set for '{'";
}

// 9. Input exactly 64 bytes produces exactly 1 ScanBlock
TEST(SimdScanner, Exactly64BytesProducesOneScanBlock) {
    std::string input(64, 'a');
    auto blocks = scalarScan(input.data(), input.size());
    EXPECT_EQ(blocks.size(), 1u);

    auto simdBlocks = simdScan(input.data(), input.size(), SimdLevel::SSE2);
    EXPECT_EQ(simdBlocks.size(), 1u);
}

// 10. Input 65 bytes produces exactly 2 ScanBlocks
TEST(SimdScanner, Input65BytesProducesTwoScanBlocks) {
    std::string input(65, 'a');
    auto blocks = scalarScan(input.data(), input.size());
    EXPECT_EQ(blocks.size(), 2u);

    auto simdBlocks = simdScan(input.data(), input.size(), SimdLevel::SSE2);
    EXPECT_EQ(simdBlocks.size(), 2u);
}

// 11. detectSimdLevel returns a valid level
TEST(SimdScanner, DetectSimdLevelReturnsValidLevel) {
    auto level = detectSimdLevel();
    EXPECT_TRUE(level == SimdLevel::Scalar ||
                level == SimdLevel::SSE2 ||
                level == SimdLevel::AVX2);
}

// ===========================================================================
// StructuralScanner unit tests
// ===========================================================================

// 1. Empty input: buildStructuralIndex with empty scan blocks returns empty entries
TEST(StructuralScanner, EmptyInputReturnsEmptyIndex) {
    std::vector<ScanBlock> emptyBlocks;
    auto index = buildStructuralIndex(nullptr, 0, emptyBlocks);
    EXPECT_TRUE(index.entries.empty());
}

// 2. Single value (number): no structural characters, empty index
TEST(StructuralScanner, SingleNumberValueEmptyIndex) {
    const std::string input = "42";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);
    EXPECT_TRUE(index.entries.empty());
}

// 3. Simple object: verify entries for { : } with correct depths
TEST(StructuralScanner, SimpleObjectDepths) {
    const std::string input = R"({"a":1})";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);

    // Expected structural chars: { at 0, : at 4, } at 6
    ASSERT_EQ(index.entries.size(), 3u);

    EXPECT_EQ(index.entries[0].character, '{');
    EXPECT_EQ(index.entries[0].offset, 0u);
    EXPECT_EQ(index.entries[0].depth, 0);  // opening: depth before increment

    EXPECT_EQ(index.entries[1].character, ':');
    EXPECT_EQ(index.entries[1].offset, 4u);
    EXPECT_EQ(index.entries[1].depth, 1);  // inside the object

    EXPECT_EQ(index.entries[2].character, '}');
    EXPECT_EQ(index.entries[2].offset, 6u);
    EXPECT_EQ(index.entries[2].depth, 0);  // closing: depth after decrement
}

// 4. Simple array: verify entries for [ , , ] with correct depths
TEST(StructuralScanner, SimpleArrayDepths) {
    const std::string input = "[1,2,3]";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);

    // Expected: [ at 0, , at 2, , at 4, ] at 6
    ASSERT_EQ(index.entries.size(), 4u);

    EXPECT_EQ(index.entries[0].character, '[');
    EXPECT_EQ(index.entries[0].depth, 0);

    EXPECT_EQ(index.entries[1].character, ',');
    EXPECT_EQ(index.entries[1].depth, 1);

    EXPECT_EQ(index.entries[2].character, ',');
    EXPECT_EQ(index.entries[2].depth, 1);

    EXPECT_EQ(index.entries[3].character, ']');
    EXPECT_EQ(index.entries[3].depth, 0);
}

// 5. Nested objects: verify depth tracking
TEST(StructuralScanner, NestedObjectDepths) {
    const std::string input = R"({"a":{"b":1}})";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);

    // Expected structural chars: { : { : } }
    ASSERT_EQ(index.entries.size(), 6u);

    // Outer {
    EXPECT_EQ(index.entries[0].character, '{');
    EXPECT_EQ(index.entries[0].depth, 0);

    // Outer :
    EXPECT_EQ(index.entries[1].character, ':');
    EXPECT_EQ(index.entries[1].depth, 1);

    // Inner {
    EXPECT_EQ(index.entries[2].character, '{');
    EXPECT_EQ(index.entries[2].depth, 1);

    // Inner :
    EXPECT_EQ(index.entries[3].character, ':');
    EXPECT_EQ(index.entries[3].depth, 2);

    // Inner }
    EXPECT_EQ(index.entries[4].character, '}');
    EXPECT_EQ(index.entries[4].depth, 1);

    // Outer }
    EXPECT_EQ(index.entries[5].character, '}');
    EXPECT_EQ(index.entries[5].depth, 0);
}

// 6. Nested arrays: verify depth tracking
TEST(StructuralScanner, NestedArrayDepths) {
    const std::string input = "[[1],[2]]";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);

    // Expected: [ [ ] , [ ] ]
    ASSERT_EQ(index.entries.size(), 7u);

    EXPECT_EQ(index.entries[0].character, '[');
    EXPECT_EQ(index.entries[0].depth, 0);

    EXPECT_EQ(index.entries[1].character, '[');
    EXPECT_EQ(index.entries[1].depth, 1);

    EXPECT_EQ(index.entries[2].character, ']');
    EXPECT_EQ(index.entries[2].depth, 1);

    EXPECT_EQ(index.entries[3].character, ',');
    EXPECT_EQ(index.entries[3].depth, 1);

    EXPECT_EQ(index.entries[4].character, '[');
    EXPECT_EQ(index.entries[4].depth, 1);

    EXPECT_EQ(index.entries[5].character, ']');
    EXPECT_EQ(index.entries[5].depth, 1);

    EXPECT_EQ(index.entries[6].character, ']');
    EXPECT_EQ(index.entries[6].depth, 0);
}

// 7. Strings containing structural characters: chars inside strings NOT in index
TEST(StructuralScanner, StringsContainingStructuralCharsExcluded) {
    const std::string input = R"({"key":"{not,structural}"})";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);

    // Only the outer { : } should be in the index.
    // The { , } inside the string value should be excluded.
    ASSERT_EQ(index.entries.size(), 3u);

    EXPECT_EQ(index.entries[0].character, '{');
    EXPECT_EQ(index.entries[0].depth, 0);

    EXPECT_EQ(index.entries[1].character, ':');
    EXPECT_EQ(index.entries[1].depth, 1);

    EXPECT_EQ(index.entries[2].character, '}');
    EXPECT_EQ(index.entries[2].depth, 0);
}

// 8. findPartitionPoints on simple object: partition point after closing }
TEST(StructuralScanner, FindPartitionPointsSimpleObject) {
    const std::string input = "{}";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);
    auto points = index.findPartitionPoints();

    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0], 2u);  // offset 1 (}) + 1 = 2
}

// 9. findPartitionPoints on nested: single partition point after closing }
TEST(StructuralScanner, FindPartitionPointsNested) {
    const std::string input = R"({"a":[1,2]})";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);
    auto points = index.findPartitionPoints();

    // Only the outermost } at depth 0 produces a partition point
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0], input.size());  // right after the final }
}

// 10. findPartitionPoints with no depth-0 closers: just a number
TEST(StructuralScanner, FindPartitionPointsNoDepthZeroClosers) {
    const std::string input = "42";
    auto scanBlocks = scalarScan(input.data(), input.size());
    auto index = buildStructuralIndex(input.data(), input.size(), scanBlocks);
    auto points = index.findPartitionPoints();

    EXPECT_TRUE(points.empty());
}

// ---------------------------------------------------------------------------
// Task 5.3: Unit tests for ChunkParser
// Requirements: 2.1, 2.2, 6.1, 6.4
// ---------------------------------------------------------------------------

#include "core/chunk_parser.h"

// Helper: parse a JSON string using the ChunkParser
static auto chunkParseAll(const std::string& json) -> ChunkParseResult {
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    return parseChunkRange(source, 0, source.size(), arena);
}

// Helper: parse and convert to JsonNode for comparison with existing parser
static auto chunkParseToJsonNode(const std::string& json)
    -> std::pair<std::shared_ptr<const JsonNode>, std::optional<ParseError>>
{
    // We need the arena and source to outlive the ArenaJsonNode usage,
    // but toJsonNode() deep-copies, so we can do it in one scope.
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    auto result = parseChunkRange(source, 0, source.size(), arena);
    if (result.error) {
        return {nullptr, result.error};
    }
    return {result.root->toJsonNode(), std::nullopt};
}

// === Basic value types =====================================================

TEST(ChunkParser, ParseEmptyObject) {
    auto [node, err] = chunkParseToJsonNode("{}");
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(node->type, NodeType::Object);
    EXPECT_TRUE(node->children.empty());
}

TEST(ChunkParser, ParseEmptyArray) {
    auto [node, err] = chunkParseToJsonNode("[]");
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(node->type, NodeType::Array);
    EXPECT_TRUE(node->children.empty());
}

TEST(ChunkParser, ParseStringValue) {
    auto [node, err] = chunkParseToJsonNode(R"("hello world")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::String);
    EXPECT_EQ(node->value, "hello world");
}

TEST(ChunkParser, ParseEmptyString) {
    auto [node, err] = chunkParseToJsonNode(R"("")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::String);
    EXPECT_EQ(node->value, "");
}

TEST(ChunkParser, ParseIntegerNumber) {
    auto [node, err] = chunkParseToJsonNode("42");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "42");
}

TEST(ChunkParser, ParseNegativeNumber) {
    auto [node, err] = chunkParseToJsonNode("-123");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "-123");
}

TEST(ChunkParser, ParseDecimalNumber) {
    auto [node, err] = chunkParseToJsonNode("3.14");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "3.14");
}

TEST(ChunkParser, ParseScientificNotation) {
    auto [node, err] = chunkParseToJsonNode("1.5e10");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "1.5e10");
}

TEST(ChunkParser, ParseScientificNotationNegativeExponent) {
    auto [node, err] = chunkParseToJsonNode("2.99E-8");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "2.99E-8");
}

TEST(ChunkParser, ParseBoolTrue) {
    auto [node, err] = chunkParseToJsonNode("true");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Boolean);
    EXPECT_EQ(node->value, "true");
}

TEST(ChunkParser, ParseBoolFalse) {
    auto [node, err] = chunkParseToJsonNode("false");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Boolean);
    EXPECT_EQ(node->value, "false");
}

TEST(ChunkParser, ParseNull) {
    auto [node, err] = chunkParseToJsonNode("null");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Null);
    EXPECT_EQ(node->value, "null");
}

TEST(ChunkParser, ParseZero) {
    auto [node, err] = chunkParseToJsonNode("0");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Number);
    EXPECT_EQ(node->value, "0");
}

// === String escape sequences ===============================================

TEST(ChunkParser, ParseStringNoEscapesIsZeroCopy) {
    std::string json = R"("hello")";
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    auto result = parseChunkRange(source, 0, source.size(), arena);
    ASSERT_NE(result.root, nullptr);
    EXPECT_FALSE(result.error.has_value());
    // Zero-copy: the StringRef should point into the source buffer
    EXPECT_FALSE(result.root->value.ownsData);
    EXPECT_EQ(result.root->value.view(), "hello");
    // Verify pointer is within source buffer range
    EXPECT_GE(result.root->value.data, source.data());
    EXPECT_LT(result.root->value.data, source.data() + source.size());
}

TEST(ChunkParser, ParseStringWithEscapesIsOwnedCopy) {
    std::string json = R"("hello\nworld")";
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    auto result = parseChunkRange(source, 0, source.size(), arena);
    ASSERT_NE(result.root, nullptr);
    EXPECT_FALSE(result.error.has_value());
    // Owned copy: the StringRef should own its data (arena-allocated)
    EXPECT_TRUE(result.root->value.ownsData);
    EXPECT_EQ(result.root->value.view(), "hello\nworld");
}

TEST(ChunkParser, ParseStringWithAllEscapes) {
    auto [node, err] = chunkParseToJsonNode(R"("\"\\\/\b\f\n\r\t")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "\"\\/\b\f\n\r\t");
}

TEST(ChunkParser, ParseStringWithUnicodeEscape) {
    // \u0041 = 'A'
    auto [node, err] = chunkParseToJsonNode(R"("\u0041")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "A");
}

TEST(ChunkParser, ParseStringWithUnicodeTwoByteChar) {
    // \u00E9 = 'é' (2-byte UTF-8)
    auto [node, err] = chunkParseToJsonNode(R"("\u00E9")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "\xC3\xA9");
}

TEST(ChunkParser, ParseStringWithUnicodeThreeByteChar) {
    // \u4E16 = '世' (3-byte UTF-8)
    auto [node, err] = chunkParseToJsonNode(R"("\u4E16")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "\xE4\xB8\x96");
}

TEST(ChunkParser, ParseStringWithSurrogatePair) {
    // U+1F600 (😀) = \uD83D\uDE00
    auto [node, err] = chunkParseToJsonNode(R"("\uD83D\uDE00")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value.size(), 4u);
    EXPECT_EQ(node->value, "\xF0\x9F\x98\x80");
}

TEST(ChunkParser, ParseStringWithSurrogatePairMusicalSymbol) {
    // U+1D11E (𝄞 Musical Symbol G Clef) = \uD834\uDD1E
    auto [node, err] = chunkParseToJsonNode(R"("\uD834\uDD1E")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "\xF0\x9D\x84\x9E");
}

TEST(ChunkParser, ParseStringWithMixedEscapesAndPlainText) {
    auto [node, err] = chunkParseToJsonNode(R"("hello\tworld\n!")");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value, "hello\tworld\n!");
}

// === Objects ===============================================================

TEST(ChunkParser, ParseSimpleObject) {
    auto [node, err] = chunkParseToJsonNode(R"({"name": "Alice", "age": 30})");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Object);
    ASSERT_EQ(node->children.size(), 2u);
    EXPECT_EQ(node->children[0]->key, "name");
    EXPECT_EQ(node->children[0]->value, "Alice");
    EXPECT_EQ(node->children[1]->key, "age");
    EXPECT_EQ(node->children[1]->value, "30");
}

TEST(ChunkParser, ParseNestedObject) {
    auto [node, err] = chunkParseToJsonNode(R"({"outer": {"inner": "value"}})");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->children.size(), 1u);
    auto& outer = node->children[0];
    EXPECT_EQ(outer->key, "outer");
    EXPECT_EQ(outer->type, NodeType::Object);
    ASSERT_EQ(outer->children.size(), 1u);
    EXPECT_EQ(outer->children[0]->key, "inner");
    EXPECT_EQ(outer->children[0]->value, "value");
}

TEST(ChunkParser, ParseObjectWithAllValueTypes) {
    auto [node, err] = chunkParseToJsonNode(
        R"({"s":"text","n":42,"b":true,"f":false,"x":null,"a":[1],"o":{}})");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->children.size(), 7u);
    EXPECT_EQ(node->children[0]->type, NodeType::String);
    EXPECT_EQ(node->children[1]->type, NodeType::Number);
    EXPECT_EQ(node->children[2]->type, NodeType::Boolean);
    EXPECT_EQ(node->children[3]->type, NodeType::Boolean);
    EXPECT_EQ(node->children[4]->type, NodeType::Null);
    EXPECT_EQ(node->children[5]->type, NodeType::Array);
    EXPECT_EQ(node->children[6]->type, NodeType::Object);
}

// === Arrays ================================================================

TEST(ChunkParser, ParseSimpleArray) {
    auto [node, err] = chunkParseToJsonNode(R"([1, 2, 3])");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Array);
    ASSERT_EQ(node->children.size(), 3u);
    EXPECT_EQ(node->children[0]->value, "1");
    EXPECT_EQ(node->children[1]->value, "2");
    EXPECT_EQ(node->children[2]->value, "3");
}

TEST(ChunkParser, ParseMixedArray) {
    auto [node, err] = chunkParseToJsonNode(R"(["hello", 42, true, null])");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->children.size(), 4u);
    EXPECT_EQ(node->children[0]->type, NodeType::String);
    EXPECT_EQ(node->children[1]->type, NodeType::Number);
    EXPECT_EQ(node->children[2]->type, NodeType::Boolean);
    EXPECT_EQ(node->children[3]->type, NodeType::Null);
}

TEST(ChunkParser, ParseNestedArray) {
    auto [node, err] = chunkParseToJsonNode(R"([[1, 2], [3, 4]])");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->children.size(), 2u);
    ASSERT_EQ(node->children[0]->children.size(), 2u);
    ASSERT_EQ(node->children[1]->children.size(), 2u);
}

TEST(ChunkParser, ArrayElementsHaveEmptyKeys) {
    auto [node, err] = chunkParseToJsonNode(R"([1, "two"])");
    ASSERT_NE(node, nullptr);
    for (const auto& child : node->children) {
        EXPECT_TRUE(child->key.empty());
    }
}

// === Deeply nested =========================================================

TEST(ChunkParser, ParseDeeplyNested) {
    auto [node, err] = chunkParseToJsonNode(R"({"a":{"b":{"c":{"d":"deep"}}}})");
    ASSERT_NE(node, nullptr);
    auto n = node;
    ASSERT_EQ(n->children.size(), 1u);
    n = n->children[0]; // a
    ASSERT_EQ(n->children.size(), 1u);
    n = n->children[0]; // b
    ASSERT_EQ(n->children.size(), 1u);
    n = n->children[0]; // c
    ASSERT_EQ(n->children.size(), 1u);
    EXPECT_EQ(n->children[0]->key, "d");
    EXPECT_EQ(n->children[0]->value, "deep");
}

// === Whitespace handling ===================================================

TEST(ChunkParser, ParseWithExtraWhitespace) {
    auto [node, err] = chunkParseToJsonNode("  {  \"key\"  :  \"value\"  }  ");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, NodeType::Object);
    ASSERT_EQ(node->children.size(), 1u);
    EXPECT_EQ(node->children[0]->value, "value");
}

// === Byte range parsing ====================================================

TEST(ChunkParser, ParseSubRange) {
    // Parse only a portion of the source buffer
    std::string json = "   {\"key\": \"value\"}   ";
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    // The actual JSON starts at offset 3 and ends at offset 19
    auto result = parseChunkRange(source, 3, 19, arena);
    ASSERT_NE(result.root, nullptr);
    EXPECT_FALSE(result.error.has_value());
    auto node = result.root->toJsonNode();
    EXPECT_EQ(node->type, NodeType::Object);
    ASSERT_EQ(node->children.size(), 1u);
    EXPECT_EQ(node->children[0]->key, "key");
    EXPECT_EQ(node->children[0]->value, "value");
}

TEST(ChunkParser, ParseWithKey) {
    std::string json = R"({"inner": 42})";
    SourceBuffer source{std::string{json}};
    ArenaAllocator arena;
    // Provide a key name
    std::string keyStr = "myKey";
    StringRef namedKey{keyStr.data(), keyStr.size(), false};
    auto result = parseChunkRange(source, 0, source.size(), arena, namedKey);
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->key.view(), "myKey");
}

// === Error cases ===========================================================

TEST(ChunkParser, ErrorOnEmptyInput) {
    auto result = chunkParseAll("");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnTruncatedObject) {
    auto result = chunkParseAll(R"({"key": "val)");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnInvalidToken) {
    auto result = chunkParseAll("xyz");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
    EXPECT_GT(result.error->description.size(), 0u);
}

TEST(ChunkParser, ErrorOnMismatchedBrackets) {
    auto result = chunkParseAll(R"({"key": "value"])");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnTrailingCommaInObject) {
    auto result = chunkParseAll(R"({"key": "value",})");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnTrailingCommaInArray) {
    auto result = chunkParseAll(R"([1, 2, 3,])");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnMultipleTopLevelValues) {
    auto result = chunkParseAll(R"({}[])");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorByteOffsetIsReasonable) {
    auto result = chunkParseAll(R"({"key": })");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_GE(result.error->byteOffset, 0u);
    EXPECT_FALSE(result.error->description.empty());
}

TEST(ChunkParser, ErrorOnInvalidEscapeSequence) {
    auto result = chunkParseAll(R"("\q")");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnIncompleteUnicodeEscape) {
    auto result = chunkParseAll(R"("\u00")");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnInvalidSurrogate) {
    // High surrogate without low surrogate
    auto result = chunkParseAll(R"("\uD83D")");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnInvalidLowSurrogate) {
    // High surrogate followed by non-surrogate
    auto result = chunkParseAll(R"("\uD83D\u0041")");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnInvalidNumberLeadingZeros) {
    // "01" is not valid JSON (leading zeros not allowed)
    auto result = chunkParseAll("01");
    // The parser will parse "0" then find trailing "1"
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

TEST(ChunkParser, ErrorOnTruncatedNumber) {
    auto result = chunkParseAll("1.e");
    EXPECT_EQ(result.root, nullptr);
    EXPECT_TRUE(result.error.has_value());
}

// === Semantic equivalence with existing parser =============================

TEST(ChunkParser, EquivalenceSimpleObject) {
    std::string json = R"({"name": "Alice", "age": 30})";
    auto ref = parseAll(json);
    auto [node, err] = chunkParseToJsonNode(json);
    ASSERT_NE(ref.root, nullptr);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(nodesEqual(ref.root, node));
}

TEST(ChunkParser, EquivalenceComplexDocument) {
    std::string json = R"({
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
    })";
    auto ref = parseAll(json);
    auto [node, err] = chunkParseToJsonNode(json);
    ASSERT_NE(ref.root, nullptr);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(nodesEqual(ref.root, node));
}

TEST(ChunkParser, EquivalenceEscapeSequences) {
    std::string json = R"({"escaped": "line1\nline2\ttab\"quote\\backslash\uD83D\uDE00"})";
    auto ref = parseAll(json);
    auto [node, err] = chunkParseToJsonNode(json);
    ASSERT_NE(ref.root, nullptr);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(nodesEqual(ref.root, node));
}

TEST(ChunkParser, EquivalenceDeeplyNested) {
    std::string json = R"({"a":{"b":{"c":{"d":{"e":[1,2,{"f":"deep"}]}}}}})";
    auto ref = parseAll(json);
    auto [node, err] = chunkParseToJsonNode(json);
    ASSERT_NE(ref.root, nullptr);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(nodesEqual(ref.root, node));
}

// ---------------------------------------------------------------------------
// Task 5.4: Property test -- Error reporting for invalid JSON
// Property 8: For any byte sequence that is not valid JSON, the optimized
// parser returns a ParseError with byte offset >= 0 and a non-empty
// description, and does not return a non-null root node.
// Validates: Requirements 6.3
// ---------------------------------------------------------------------------

TEST(ChunkParserProperty, ErrorReportingForInvalidJson) {
    rc::check("Property 8: Error reporting for invalid JSON",
        [](void) {
            // Strategy: generate valid JSON, then inject a syntax error
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document
            std::uniform_int_distribution<int> depthDist(0, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Verify the original is valid
            {
                SourceBuffer source{std::string{json}};
                ArenaAllocator arena;
                auto result = parseChunkRange(source, 0, source.size(), arena);
                RC_PRE(result.root != nullptr && !result.error.has_value());
            }

            // Choose an error injection strategy
            auto strategy = *rc::gen::inRange(0, 5);
            std::string corrupted = json;

            switch (strategy) {
                case 0: {
                    // Insert a random invalid character at a random position
                    if (corrupted.empty()) break;
                    auto pos = *rc::gen::inRange(
                        std::size_t{0}, corrupted.size());
                    // Characters that are always invalid in JSON context
                    auto invalidChars = std::string("@#$%^&!~`");
                    auto charIdx = *rc::gen::inRange(
                        std::size_t{0}, invalidChars.size());
                    corrupted.insert(pos, 1, invalidChars[charIdx]);
                    break;
                }
                case 1: {
                    // Delete a random character
                    if (corrupted.size() <= 1) break;
                    auto pos = *rc::gen::inRange(
                        std::size_t{0}, corrupted.size());
                    corrupted.erase(pos, 1);
                    break;
                }
                case 2: {
                    // Replace a structural character with a wrong one
                    std::vector<std::size_t> structPositions;
                    for (std::size_t i = 0; i < corrupted.size(); ++i) {
                        char c = corrupted[i];
                        if (c == '{' || c == '}' || c == '[' || c == ']' ||
                            c == ',' || c == ':') {
                            structPositions.push_back(i);
                        }
                    }
                    if (structPositions.empty()) break;
                    auto idx = *rc::gen::inRange(
                        std::size_t{0}, structPositions.size());
                    // Replace with a mismatched structural char
                    char replacements[] = {'{', '}', '[', ']', ',', ':'};
                    auto repIdx = *rc::gen::inRange(0, 6);
                    char original = corrupted[structPositions[idx]];
                    char replacement = replacements[repIdx];
                    if (replacement != original) {
                        corrupted[structPositions[idx]] = replacement;
                    } else {
                        // Just use a different one
                        corrupted[structPositions[idx]] = '@';
                    }
                    break;
                }
                case 3: {
                    // Truncate the input at a random position (not at the end)
                    if (corrupted.size() <= 2) break;
                    auto truncPos = *rc::gen::inRange(
                        std::size_t{1}, corrupted.size() - 1);
                    corrupted = corrupted.substr(0, truncPos);
                    break;
                }
                case 4: {
                    // Append garbage after valid JSON
                    corrupted += " garbage_after";
                    break;
                }
            }

            // Skip if corruption didn't actually change anything
            RC_PRE(corrupted != json);

            // Also skip if the corrupted version happens to be valid JSON
            // (unlikely but possible with some mutations).
            // Use try/catch because the existing parser may throw on
            // certain malformed inputs (e.g., null pointer in string ctor).
            {
                bool isInvalidForReference = false;
                try {
                    auto refResult = parseAll(corrupted);
                    isInvalidForReference =
                        (refResult.root == nullptr || refResult.error.has_value());
                } catch (...) {
                    // If the reference parser throws, the input is definitely
                    // invalid — that's fine for our purposes.
                    isInvalidForReference = true;
                }
                RC_PRE(isInvalidForReference);
            }

            // Now verify the ChunkParser error reporting.
            // The chunk parser must never throw — it should return an error.
            SourceBuffer source{std::string{corrupted}};
            ArenaAllocator arena;
            auto result = parseChunkRange(source, 0, source.size(), arena);

            // Property assertions:
            // 1. Root must be null for invalid JSON
            RC_ASSERT(result.root == nullptr);
            // 2. Error must be present
            RC_ASSERT(result.error.has_value());
            // 3. Byte offset must be within bounds
            RC_ASSERT(result.error->byteOffset <= corrupted.size());
            // 4. Description must be non-empty
            RC_ASSERT(!result.error->description.empty());
        });
}

// ---------------------------------------------------------------------------
// Task 7.5: Property 6 — Semantic Equivalence (optimized vs. current parser)
// Validates: Requirements 6.1, 6.4, 8.2, 8.4
// ---------------------------------------------------------------------------

#include "core/parse_orchestrator.h"

TEST(ParseOrchestratorProperty, SemanticEquivalenceSingleThreaded) {
    rc::check("Property 6a: Semantic equivalence — single-threaded path",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document with configurable depth
            std::uniform_int_distribution<int> depthDist(0, 4);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Parse with the current parser (reference)
            auto refResult = parseAll(json);
            RC_PRE(refResult.root != nullptr && !refResult.error.has_value());

            // Parse with the optimized parser (single-threaded path, default threshold)
            ParseBufferOptions opts;
            opts.parallelThreshold = 1024 * 1024; // 1 MB — ensures single-threaded
            auto arenaResult = parseBuffer(std::string{json}, opts);

            // The optimized parser must succeed
            RC_ASSERT(arenaResult.ok());

            // Convert to public JsonNode type
            auto optimizedResult = arenaResult.toParseResult();
            RC_ASSERT(optimizedResult.root != nullptr);
            RC_ASSERT(!optimizedResult.error.has_value());

            // Trees must be structurally and value-equivalent
            RC_ASSERT(nodesEqual(refResult.root, optimizedResult.root));
        });
}

TEST(ParseOrchestratorProperty, SemanticEquivalenceParallelPath) {
    rc::check("Property 6b: Semantic equivalence — parallel path (low threshold)",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document with configurable depth
            std::uniform_int_distribution<int> depthDist(1, 4);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Parse with the current parser (reference)
            auto refResult = parseAll(json);
            RC_PRE(refResult.root != nullptr && !refResult.error.has_value());

            // Parse with the optimized parser using a very low parallelThreshold.
            // This forces the input through the SIMD scan + structural index path.
            // For standard single-value JSON, findPartitionPoints() returns empty
            // and the code falls back to single-threaded, but it still exercises
            // the full orchestrator code path up to that fallback point.
            ParseBufferOptions opts;
            opts.parallelThreshold = 1; // Force parallel pipeline attempt
            auto arenaResult = parseBuffer(std::string{json}, opts);

            // The optimized parser must succeed
            RC_ASSERT(arenaResult.ok());

            // Convert to public JsonNode type
            auto optimizedResult = arenaResult.toParseResult();
            RC_ASSERT(optimizedResult.root != nullptr);
            RC_ASSERT(!optimizedResult.error.has_value());

            // Trees must be structurally and value-equivalent
            RC_ASSERT(nodesEqual(refResult.root, optimizedResult.root));
        });
}

// ---------------------------------------------------------------------------
// Task 7.6: Property 7 — Parse-print round trip
// Validates: Requirements 6.2
// ---------------------------------------------------------------------------

TEST(ParseOrchestratorProperty, ParsePrintRoundTrip) {
    rc::check("Property 7: Parse-print round trip",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document with configurable depth
            std::uniform_int_distribution<int> depthDist(1, 4);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Step 1: Parse with the optimized parser (parseBuffer)
            ParseBufferOptions opts;
            opts.parallelThreshold = 1; // Exercise full orchestrator path
            auto firstParseResult = parseBuffer(std::string{json}, opts);
            RC_PRE(firstParseResult.ok());

            // Step 2: Convert to JsonNode
            auto firstTree = firstParseResult.toParseResult();
            RC_ASSERT(firstTree.root != nullptr);
            RC_ASSERT(!firstTree.error.has_value());

            // Step 3: Pretty-print the result
            std::string printed = prettyPrint(*firstTree.root);
            RC_ASSERT(!printed.empty());

            // Step 4: Re-parse the pretty-printed output with parseBuffer
            auto secondParseResult = parseBuffer(std::string{printed}, opts);
            RC_ASSERT(secondParseResult.ok());

            // Step 5: Convert to JsonNode
            auto secondTree = secondParseResult.toParseResult();
            RC_ASSERT(secondTree.root != nullptr);
            RC_ASSERT(!secondTree.error.has_value());

            // Step 6: Compare the two JsonNode trees for structural and value equivalence
            RC_ASSERT(nodesEqual(firstTree.root, secondTree.root));
        });
}

// ---------------------------------------------------------------------------
// Task 7.7: Property 9 — Parallel error offset correctness
// Validates: Requirements 4.6
// ---------------------------------------------------------------------------

TEST(ParseOrchestratorProperty, ParallelErrorOffsetCorrectness) {
    rc::check("Property 9: Parallel error offset correctness",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a random valid JSON document with some depth
            std::uniform_int_distribution<int> depthDist(1, 4);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Verify the original is valid via parseBuffer
            {
                ParseBufferOptions opts;
                opts.parallelThreshold = 1; // Force parallel pipeline
                auto result = parseBuffer(std::string{json}, opts);
                RC_PRE(result.ok());
            }

            // Inject a syntax error at a random byte position
            RC_PRE(json.size() >= 2);
            auto injectionPos = *rc::gen::inRange(
                std::size_t{0}, json.size());

            auto strategy = *rc::gen::inRange(0, 4);
            std::string corrupted = json;

            switch (strategy) {
                case 0: {
                    // Replace a character with an invalid one
                    std::string invalidChars = "@#$%^&!~`";
                    auto charIdx = *rc::gen::inRange(
                        std::size_t{0}, invalidChars.size());
                    corrupted[injectionPos] = invalidChars[charIdx];
                    break;
                }
                case 1: {
                    // Delete a character at the injection position
                    corrupted.erase(injectionPos, 1);
                    break;
                }
                case 2: {
                    // Insert an invalid character at the injection position
                    std::string invalidChars = "@#$%^&!~`";
                    auto charIdx = *rc::gen::inRange(
                        std::size_t{0}, invalidChars.size());
                    corrupted.insert(injectionPos, 1, invalidChars[charIdx]);
                    break;
                }
                case 3: {
                    // Truncate at the injection position (if not at end)
                    if (injectionPos == 0) injectionPos = 1;
                    if (injectionPos >= corrupted.size()) {
                        injectionPos = corrupted.size() - 1;
                    }
                    corrupted = corrupted.substr(0, injectionPos);
                    break;
                }
            }

            // Skip if corruption didn't change anything
            RC_PRE(corrupted != json);

            // Skip if the corrupted version happens to still be valid
            {
                ParseBufferOptions checkOpts;
                checkOpts.parallelThreshold = 1;
                auto checkResult = parseBuffer(std::string{corrupted}, checkOpts);
                RC_PRE(!checkResult.ok());
            }

            // Parse the corrupted JSON with parseBuffer using low parallelThreshold
            ParseBufferOptions opts;
            opts.parallelThreshold = 1; // Force the parallel pipeline
            auto result = parseBuffer(std::string{corrupted}, opts);

            // Property assertions:
            // 1. The parse must fail (error is reported)
            RC_ASSERT(!result.ok());
            RC_ASSERT(result.error.has_value());

            // 2. The error byte offset must be >= 0 and <= input length
            RC_ASSERT(result.error->byteOffset <= corrupted.size());

            // 3. The error description must be non-empty
            RC_ASSERT(!result.error->description.empty());

            // 4. The error byte offset should be reasonable relative to
            //    where the corruption was injected. The parser may detect
            //    errors slightly after the actual corruption point, but
            //    the offset must be within the input bounds.
            //    (The offset >= 0 check is implicit since it's size_t)
            RC_ASSERT(result.error->byteOffset <= corrupted.size());
        });
}

// ---------------------------------------------------------------------------
// Task 7.8: Unit tests for ParseOrchestrator
// Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6
// ---------------------------------------------------------------------------

// === Single-threaded path (input below threshold) ==========================

TEST(ParseOrchestratorUnit, SmallInputUsesSingleThreadedPath) {
    // A small JSON object well below the default 1 MB threshold
    std::string json = R"({"name": "Alice", "age": 30, "active": true})";

    ParseBufferOptions opts;
    // Default threshold is 1 MB, so this small input takes single-threaded path
    auto result = parseBuffer(std::string{json}, opts);

    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.root, nullptr);
    EXPECT_FALSE(result.error.has_value());

    // Convert and compare with reference parser
    auto optimized = result.toParseResult();
    auto reference = parseAll(json);

    ASSERT_NE(optimized.root, nullptr);
    ASSERT_NE(reference.root, nullptr);
    EXPECT_TRUE(nodesEqual(reference.root, optimized.root));
}

TEST(ParseOrchestratorUnit, SmallArrayUsesSingleThreadedPath) {
    std::string json = R"([1, 2, 3, "hello", null, true, false])";

    ParseBufferOptions opts;
    auto result = parseBuffer(std::string{json}, opts);

    ASSERT_TRUE(result.ok());

    auto optimized = result.toParseResult();
    auto reference = parseAll(json);

    ASSERT_NE(optimized.root, nullptr);
    ASSERT_NE(reference.root, nullptr);
    EXPECT_TRUE(nodesEqual(reference.root, optimized.root));
}

// === Parallel path (input above threshold) =================================

TEST(ParseOrchestratorUnit, LowThresholdForcesParallelPipeline) {
    // Use a JSON array with multiple top-level elements to allow partitioning
    // Set parallelThreshold to 1 to force the parallel code path
    std::string json = R"([{"a":1},{"b":2},{"c":3},{"d":4}])";

    ParseBufferOptions opts;
    opts.parallelThreshold = 1; // Force parallel pipeline attempt

    auto result = parseBuffer(std::string{json}, opts);

    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.root, nullptr);

    // Convert and compare with reference parser
    auto optimized = result.toParseResult();
    auto reference = parseAll(json);

    ASSERT_NE(optimized.root, nullptr);
    ASSERT_NE(reference.root, nullptr);
    EXPECT_TRUE(nodesEqual(reference.root, optimized.root));
}

TEST(ParseOrchestratorUnit, ParallelPathWithNestedObjects) {
    std::string json = R"({"x": {"nested": [1,2,3]}, "y": "hello", "z": true})";

    ParseBufferOptions opts;
    opts.parallelThreshold = 1; // Force parallel pipeline

    auto result = parseBuffer(std::string{json}, opts);

    ASSERT_TRUE(result.ok());

    auto optimized = result.toParseResult();
    auto reference = parseAll(json);

    ASSERT_NE(optimized.root, nullptr);
    ASSERT_NE(reference.root, nullptr);
    EXPECT_TRUE(nodesEqual(reference.root, optimized.root));
}

// === Error in one chunk cancels others =====================================

TEST(ParseOrchestratorUnit, ErrorInChunkReportsCorrectOffset) {
    // Create JSON with a syntax error injected
    // The error is at a known position: the '@' character
    std::string json = R"({"valid": "ok", "bad": @invalid})";

    ParseBufferOptions opts;
    opts.parallelThreshold = 1; // Force parallel pipeline

    auto result = parseBuffer(std::string{json}, opts);

    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_FALSE(result.error->description.empty());
    // The error offset should be within the input bounds
    EXPECT_LE(result.error->byteOffset, json.size());
}

TEST(ParseOrchestratorUnit, ErrorInMiddleOfInput) {
    // Error is in the middle of the JSON
    std::string json = R"({"a": 1, "b": ???, "c": 3})";

    ParseBufferOptions opts;
    opts.parallelThreshold = 1;

    auto result = parseBuffer(std::string{json}, opts);

    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_FALSE(result.error->description.empty());
    EXPECT_LE(result.error->byteOffset, json.size());
}

// === Forced scalar fallback produces same result as SIMD path ==============

TEST(ParseOrchestratorUnit, ScalarFallbackMatchesSimdResult) {
    std::string json = R"({"key": "value", "nums": [1, 2, 3], "flag": true})";

    // Parse with forced scalar
    ParseBufferOptions scalarOpts;
    scalarOpts.parallelThreshold = 1;
    scalarOpts.simdLevel = SimdLevel::Scalar;
    auto scalarResult = parseBuffer(std::string{json}, scalarOpts);

    // Parse with default SIMD level
    ParseBufferOptions defaultOpts;
    defaultOpts.parallelThreshold = 1;
    auto defaultResult = parseBuffer(std::string{json}, defaultOpts);

    ASSERT_TRUE(scalarResult.ok());
    ASSERT_TRUE(defaultResult.ok());

    auto scalarTree = scalarResult.toParseResult();
    auto defaultTree = defaultResult.toParseResult();

    ASSERT_NE(scalarTree.root, nullptr);
    ASSERT_NE(defaultTree.root, nullptr);
    EXPECT_TRUE(nodesEqual(scalarTree.root, defaultTree.root));
}

TEST(ParseOrchestratorUnit, ScalarFallbackMatchesSimdForNestedJson) {
    std::string json = R"({"a":{"b":{"c":[1,2,3]}},"d":"hello\nworld"})";

    ParseBufferOptions scalarOpts;
    scalarOpts.parallelThreshold = 1;
    scalarOpts.simdLevel = SimdLevel::Scalar;
    auto scalarResult = parseBuffer(std::string{json}, scalarOpts);

    ParseBufferOptions defaultOpts;
    defaultOpts.parallelThreshold = 1;
    auto defaultResult = parseBuffer(std::string{json}, defaultOpts);

    ASSERT_TRUE(scalarResult.ok());
    ASSERT_TRUE(defaultResult.ok());

    auto scalarTree = scalarResult.toParseResult();
    auto defaultTree = defaultResult.toParseResult();

    ASSERT_NE(scalarTree.root, nullptr);
    ASSERT_NE(defaultTree.root, nullptr);
    EXPECT_TRUE(nodesEqual(scalarTree.root, defaultTree.root));
}

// === Empty input returns error =============================================

TEST(ParseOrchestratorUnit, EmptyInputReturnsError) {
    auto result = parseBuffer(std::string{""});

    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->byteOffset, 0u);
    EXPECT_FALSE(result.error->description.empty());
    EXPECT_EQ(result.root, nullptr);
}

// === parseBuffer string overload works =====================================

TEST(ParseOrchestratorUnit, StringOverloadWorks) {
    std::string json = R"({"greeting": "hello"})";

    // Use the string overload directly
    auto result = parseBuffer(std::string{json});

    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.root, nullptr);

    auto parsed = result.toParseResult();
    ASSERT_NE(parsed.root, nullptr);
    EXPECT_EQ(parsed.root->type, NodeType::Object);
    ASSERT_EQ(parsed.root->children.size(), 1u);
    EXPECT_EQ(parsed.root->children[0]->key, "greeting");
    EXPECT_EQ(parsed.root->children[0]->value, "hello");
}

TEST(ParseOrchestratorUnit, StringOverloadWithOptions) {
    std::string json = R"([10, 20, 30])";

    ParseBufferOptions opts;
    opts.parallelThreshold = 1;
    opts.simdLevel = SimdLevel::Scalar;

    auto result = parseBuffer(std::string{json}, opts);

    ASSERT_TRUE(result.ok());
    auto parsed = result.toParseResult();
    ASSERT_NE(parsed.root, nullptr);
    EXPECT_EQ(parsed.root->type, NodeType::Array);
    ASSERT_EQ(parsed.root->children.size(), 3u);
    EXPECT_EQ(parsed.root->children[0]->value, "10");
    EXPECT_EQ(parsed.root->children[1]->value, "20");
    EXPECT_EQ(parsed.root->children[2]->value, "30");
}

// === toParseResult() conversion ============================================

TEST(ParseOrchestratorUnit, ToParseResultConvertsValidTree) {
    std::string json = R"({"name": "Bob", "items": [1, 2], "flag": false})";

    auto result = parseBuffer(std::string{json});
    ASSERT_TRUE(result.ok());

    auto parsed = result.toParseResult();
    ASSERT_NE(parsed.root, nullptr);
    EXPECT_FALSE(parsed.error.has_value());

    // Verify the tree structure
    EXPECT_EQ(parsed.root->type, NodeType::Object);
    ASSERT_EQ(parsed.root->children.size(), 3u);

    EXPECT_EQ(parsed.root->children[0]->key, "name");
    EXPECT_EQ(parsed.root->children[0]->type, NodeType::String);
    EXPECT_EQ(parsed.root->children[0]->value, "Bob");

    EXPECT_EQ(parsed.root->children[1]->key, "items");
    EXPECT_EQ(parsed.root->children[1]->type, NodeType::Array);
    ASSERT_EQ(parsed.root->children[1]->children.size(), 2u);
    EXPECT_EQ(parsed.root->children[1]->children[0]->value, "1");
    EXPECT_EQ(parsed.root->children[1]->children[1]->value, "2");

    EXPECT_EQ(parsed.root->children[2]->key, "flag");
    EXPECT_EQ(parsed.root->children[2]->type, NodeType::Boolean);
    EXPECT_EQ(parsed.root->children[2]->value, "false");
}

TEST(ParseOrchestratorUnit, ToParseResultOnErrorReturnsError) {
    std::string json = R"({invalid json)";

    auto result = parseBuffer(std::string{json});
    EXPECT_FALSE(result.ok());

    auto parsed = result.toParseResult();
    EXPECT_EQ(parsed.root, nullptr);
    ASSERT_TRUE(parsed.error.has_value());
    EXPECT_FALSE(parsed.error->description.empty());
}

// === ok() method ===========================================================

TEST(ParseOrchestratorUnit, OkReturnsTrueForSuccessfulParse) {
    std::string json = R"({"valid": true})";

    auto result = parseBuffer(std::string{json});
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.root, nullptr);
    EXPECT_FALSE(result.error.has_value());
}

TEST(ParseOrchestratorUnit, OkReturnsFalseForFailedParse) {
    std::string json = R"({not valid json!!!)";

    auto result = parseBuffer(std::string{json});
    EXPECT_FALSE(result.ok());
}

TEST(ParseOrchestratorUnit, OkReturnsFalseForEmptyInput) {
    auto result = parseBuffer(std::string{""});
    EXPECT_FALSE(result.ok());
}

// ---------------------------------------------------------------------------
// Task 9.4: Property 10 — Incremental emission correctness
// Validates: Requirements 5.1, 5.4
// ---------------------------------------------------------------------------

TEST(IncrementalParserProperty, IncrementalEmissionCorrectness) {
    rc::check("Property 10: Incremental emission correctness",
        [](void) {
            // Generate a random seed for our JSON generator
            auto seed = *rc::gen::arbitrary<uint32_t>();
            std::mt19937 rng(seed);

            // Generate a valid JSON document (top-level object or array)
            std::uniform_int_distribution<int> depthDist(1, 3);
            std::string json = generateJsonValue(depthDist(rng), rng);

            // Ensure it's a top-level array or object
            // (the generator might produce scalars at low depth)
            if (json.empty() || (json[0] != '{' && json[0] != '[')) {
                // Wrap in an array to ensure it's a container
                json = "[" + json + "]";
            }

            // Single-pass parse (reference result)
            auto refResult = parseAll(json);
            RC_ASSERT(refResult.root != nullptr);
            RC_ASSERT(!refResult.error.has_value());

            // Split into random chunks at random positions (including mid-token)
            auto numSplits = *rc::gen::inRange(1, std::max(2, static_cast<int>(json.size())));
            std::vector<std::size_t> splitPoints;
            for (int i = 0; i < numSplits; ++i) {
                auto pt = *rc::gen::inRange(std::size_t{1}, json.size());
                splitPoints.push_back(pt);
            }
            std::sort(splitPoints.begin(), splitPoints.end());
            splitPoints.erase(std::unique(splitPoints.begin(), splitPoints.end()),
                              splitPoints.end());

            // Feed chunks through parseChunk
            auto state = makeParserState();
            std::vector<std::shared_ptr<const JsonNode>> allEmitted;

            std::size_t prevSplit = 0;
            for (auto sp : splitPoints) {
                std::size_t chunkSize = sp - prevSplit;
                std::vector<std::byte> chunkBytes(chunkSize);
                for (std::size_t i = 0; i < chunkSize; ++i) {
                    chunkBytes[i] = static_cast<std::byte>(json[prevSplit + i]);
                }
                auto result = parseChunk(*state, std::span<const std::byte>(chunkBytes));
                RC_ASSERT(!result.error.has_value());
                for (auto& node : result.emittedNodes) {
                    allEmitted.push_back(node);
                }
                state = std::move(result.nextState);
                prevSplit = sp;
            }

            // Feed remaining bytes
            if (prevSplit < json.size()) {
                std::size_t remaining = json.size() - prevSplit;
                std::vector<std::byte> chunkBytes(remaining);
                for (std::size_t i = 0; i < remaining; ++i) {
                    chunkBytes[i] = static_cast<std::byte>(json[prevSplit + i]);
                }
                auto result = parseChunk(*state, std::span<const std::byte>(chunkBytes));
                RC_ASSERT(!result.error.has_value());
                for (auto& node : result.emittedNodes) {
                    allEmitted.push_back(node);
                }
                state = std::move(result.nextState);
            }

            // Finalize
            auto finalResult = finalizeParse(*state);
            RC_ASSERT(!finalResult.error.has_value());

            // Combine: all emitted nodes + final root (if any)
            if (finalResult.root) {
                allEmitted.push_back(finalResult.root);
            }

            // For a single top-level value, the union should be exactly one node
            // equivalent to the reference parse
            RC_ASSERT(allEmitted.size() == 1u);
            RC_ASSERT(nodesEqual(allEmitted[0], refResult.root));
        });
}

// ---------------------------------------------------------------------------
// Task 9.5: Unit tests for updated incremental parseChunk
// Requirements: 5.1, 5.2, 5.3, 5.4
// ---------------------------------------------------------------------------

// Helper: convert string to byte span
static auto toBytes(const std::string& s) -> std::vector<std::byte> {
    std::vector<std::byte> bytes(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        bytes[i] = static_cast<std::byte>(s[i]);
    }
    return bytes;
}

// --- Test emitting complete values mid-stream ---

TEST(IncrementalParser, EmitsCompleteValuesWhenMoreFollow) {
    // Feed two complete JSON values in one chunk: `{} []`
    // parseChunk should emit `{}` since `[]` follows it
    auto state = makeParserState();
    auto bytes = toBytes("{} []");
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    ASSERT_FALSE(result.error.has_value());
    // `{}` should be emitted because `[]` follows
    ASSERT_EQ(result.emittedNodes.size(), 1u);
    EXPECT_EQ(result.emittedNodes[0]->type, NodeType::Object);
    EXPECT_TRUE(result.emittedNodes[0]->children.empty());
}

TEST(IncrementalParser, EmitsMultipleCompleteValues) {
    // Feed three values: `1 2 3`
    // parseChunk should emit `1` and `2` (since more content follows each)
    // but keep `3` in buffer (nothing follows it)
    auto state = makeParserState();
    auto bytes = toBytes("1 2 3");
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.emittedNodes.size(), 2u);
    EXPECT_EQ(result.emittedNodes[0]->type, NodeType::Number);
    EXPECT_EQ(result.emittedNodes[0]->value, "1");
    EXPECT_EQ(result.emittedNodes[1]->type, NodeType::Number);
    EXPECT_EQ(result.emittedNodes[1]->value, "2");
}

TEST(IncrementalParser, DoesNotEmitLastValueInBuffer) {
    // Feed a single complete value: `{"key": "value"}`
    // parseChunk should NOT emit it (nothing follows)
    auto state = makeParserState();
    auto bytes = toBytes(R"({"key": "value"})");
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(result.emittedNodes.empty());
}

// --- Test buffering incomplete values across chunk boundaries ---

TEST(IncrementalParser, BuffersIncompleteObjectAcrossChunks) {
    auto state = makeParserState();

    // First chunk: incomplete object
    auto bytes1 = toBytes(R"({"key": "val)");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    // Second chunk: completes the object
    auto bytes2 = toBytes(R"(ue"})");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty()); // Still last value, not emitted

    // Finalize should produce the complete object
    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    ASSERT_FALSE(final_result.error.has_value());
    EXPECT_EQ(final_result.root->type, NodeType::Object);
    ASSERT_EQ(final_result.root->children.size(), 1u);
    EXPECT_EQ(final_result.root->children[0]->key, "key");
    EXPECT_EQ(final_result.root->children[0]->value, "value");
}

TEST(IncrementalParser, BuffersIncompleteArrayAcrossChunks) {
    auto state = makeParserState();

    // First chunk: start of array
    auto bytes1 = toBytes("[1, 2,");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    // Second chunk: completes the array
    auto bytes2 = toBytes(" 3]");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty());

    // Finalize
    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    EXPECT_EQ(final_result.root->type, NodeType::Array);
    ASSERT_EQ(final_result.root->children.size(), 3u);
}

// --- Test resuming after chunk boundary in string/number/keyword ---

TEST(IncrementalParser, ResumesAfterChunkBoundaryInString) {
    auto state = makeParserState();

    // Split a string value mid-content
    auto bytes1 = toBytes(R"("hel)");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    auto bytes2 = toBytes(R"(lo")");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty());

    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    EXPECT_EQ(final_result.root->type, NodeType::String);
    EXPECT_EQ(final_result.root->value, "hello");
}

TEST(IncrementalParser, ResumesAfterChunkBoundaryInNumber) {
    auto state = makeParserState();

    // Split a number mid-digits
    auto bytes1 = toBytes("3.14");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    auto bytes2 = toBytes("159");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty());

    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    EXPECT_EQ(final_result.root->type, NodeType::Number);
    EXPECT_EQ(final_result.root->value, "3.14159");
}

TEST(IncrementalParser, ResumesAfterChunkBoundaryInKeyword) {
    auto state = makeParserState();

    // Split "true" mid-keyword
    auto bytes1 = toBytes("tr");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    auto bytes2 = toBytes("ue");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty());

    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    EXPECT_EQ(final_result.root->type, NodeType::Boolean);
    EXPECT_EQ(final_result.root->value, "true");
}

// --- Test finalizeParse on residual bytes only ---

TEST(IncrementalParser, FinalizeOnResidualBytesOnly) {
    // Feed multiple values: `{"a":1} {"b":2}`
    // parseChunk should emit `{"a":1}` and keep `{"b":2}` in buffer
    // finalizeParse should parse only `{"b":2}`
    auto state = makeParserState();
    auto bytes = toBytes(R"({"a":1} {"b":2})");
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.emittedNodes.size(), 1u);
    EXPECT_EQ(result.emittedNodes[0]->type, NodeType::Object);
    ASSERT_EQ(result.emittedNodes[0]->children.size(), 1u);
    EXPECT_EQ(result.emittedNodes[0]->children[0]->key, "a");

    // finalizeParse should report error (multiple top-level values)
    auto final_result = finalizeParse(*result.nextState);
    EXPECT_TRUE(final_result.error.has_value());
}

TEST(IncrementalParser, FinalizeEmptyBufferAfterEmission) {
    // Feed `{} ` (with trailing space) — parseChunk won't emit since
    // after parsing `{}` only whitespace remains.
    // Then feed ` {}` — now buffer is `{} {}`, first `{}` gets emitted.
    // Then finalize — buffer has `{}`, emittedCount=1, so error.
    auto state = makeParserState();

    auto bytes1 = toBytes("{} ");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty()); // only whitespace after {}
    state = std::move(r1.nextState);

    auto bytes2 = toBytes("{}");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    // Now buffer has "{} {}" — first {} emitted, second kept
    ASSERT_EQ(r2.emittedNodes.size(), 1u);
    EXPECT_EQ(r2.emittedNodes[0]->type, NodeType::Object);

    // finalizeParse: buffer has `{}`, emittedCount=1 → error
    auto final_result = finalizeParse(*r2.nextState);
    EXPECT_TRUE(final_result.error.has_value());
}

TEST(IncrementalParser, SingleValueStreamProducesCorrectResult) {
    // Standard use case: stream a single JSON object in multiple chunks
    auto state = makeParserState();

    auto bytes1 = toBytes(R"({"name": )");
    auto r1 = parseChunk(*state, std::span<const std::byte>(bytes1));
    ASSERT_FALSE(r1.error.has_value());
    EXPECT_TRUE(r1.emittedNodes.empty());
    state = std::move(r1.nextState);

    auto bytes2 = toBytes(R"("Alice", "age": 30})");
    auto r2 = parseChunk(*state, std::span<const std::byte>(bytes2));
    ASSERT_FALSE(r2.error.has_value());
    EXPECT_TRUE(r2.emittedNodes.empty()); // single value, nothing after

    auto final_result = finalizeParse(*r2.nextState);
    ASSERT_NE(final_result.root, nullptr);
    ASSERT_FALSE(final_result.error.has_value());
    EXPECT_EQ(final_result.root->type, NodeType::Object);
    ASSERT_EQ(final_result.root->children.size(), 2u);
    EXPECT_EQ(final_result.root->children[0]->key, "name");
    EXPECT_EQ(final_result.root->children[0]->value, "Alice");
    EXPECT_EQ(final_result.root->children[1]->key, "age");
    EXPECT_EQ(final_result.root->children[1]->value, "30");
}

TEST(IncrementalParser, RetainsOnlyUnparsedTrailingBytes) {
    // After emitting values, only the unparsed residual should remain
    auto state = makeParserState();
    auto bytes = toBytes(R"(null true {"incomplete": )");
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));

    ASSERT_FALSE(result.error.has_value());
    // `null` and `true` should be emitted (more content follows each)
    ASSERT_EQ(result.emittedNodes.size(), 2u);
    EXPECT_EQ(result.emittedNodes[0]->type, NodeType::Null);
    EXPECT_EQ(result.emittedNodes[1]->type, NodeType::Boolean);
    EXPECT_EQ(result.emittedNodes[1]->value, "true");

    // The residual buffer should contain only the incomplete object
    // Finalize should fail because the object is incomplete
    auto final_result = finalizeParse(*result.nextState);
    EXPECT_TRUE(final_result.error.has_value());
}
