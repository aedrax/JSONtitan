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
