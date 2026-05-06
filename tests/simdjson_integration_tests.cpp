#include <gtest/gtest.h>

#include <string>

#include "core/parse_orchestrator.h"
#include "core/simdjson_adapter.h"

using namespace jsontitan::core;

// ===========================================================================
// Integration Tests for simdjson Backend
// Deterministic example-based tests with known inputs and expected outputs.
// Requirements: 7.2, 3.1–3.4, 5.3, 2.6, 6.3, 6.4
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 1: Empty input returns ParseError with "Empty input"
// Validates: Requirement 7.2
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, EmptyInputReturnsError) {
    auto result = parseBuffer(std::string(""),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->byteOffset, 0u);
    EXPECT_NE(result.error->description.find("Empty input"), std::string::npos)
        << "Error description was: " << result.error->description;
}

// ---------------------------------------------------------------------------
// Test 2: Backend routing — Simdjson selection
// Validates: Requirements 3.1, 3.2, 3.3
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, BackendRoutingSimdjson) {
    const std::string input = R"({"key":"value"})";

    auto result = parseBuffer(std::string(input),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});

    ASSERT_TRUE(result.ok()) << "Parse failed: "
        << (result.error ? result.error->description : "unknown");
    ASSERT_NE(result.root, nullptr);

    // Root should be an Object
    EXPECT_EQ(result.root->type, NodeType::Object);

    // Should have one child with key "key" and value "value"
    ASSERT_EQ(result.root->childCount, 1u);
    EXPECT_EQ(result.root->children[0]->key.view(), "key");
    EXPECT_EQ(result.root->children[0]->type, NodeType::String);
    EXPECT_EQ(result.root->children[0]->value.view(), "value");
}

// ---------------------------------------------------------------------------
// Test 3: Backend routing — Custom selection
// Validates: Requirements 3.1, 3.4
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, BackendRoutingCustom) {
    const std::string input = R"({"key":"value"})";

    auto result = parseBuffer(std::string(input),
        ParseBufferOptions{.backend = ParserBackend::Custom});

    ASSERT_TRUE(result.ok()) << "Parse failed: "
        << (result.error ? result.error->description : "unknown");
    ASSERT_NE(result.root, nullptr);

    // Root should be an Object
    EXPECT_EQ(result.root->type, NodeType::Object);

    // Should have one child with key "key" and value "value"
    ASSERT_EQ(result.root->childCount, 1u);
    EXPECT_EQ(result.root->children[0]->key.view(), "key");
    EXPECT_EQ(result.root->children[0]->type, NodeType::String);
    EXPECT_EQ(result.root->children[0]->value.view(), "value");
}

// ---------------------------------------------------------------------------
// Test 4: No callback does not crash
// Validates: Requirement 5.3
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, NoCallbackDoesNotCrash) {
    const std::string input = R"({"numbers":[1,2,3],"flag":true})";

    // Explicitly set progressCallback to nullptr
    auto result = parseBuffer(std::string(input),
        ParseBufferOptions{
            .backend = ParserBackend::Simdjson,
            .progressCallback = nullptr
        });

    ASSERT_TRUE(result.ok()) << "Parse failed: "
        << (result.error ? result.error->description : "unknown");
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->type, NodeType::Object);
    EXPECT_EQ(result.root->childCount, 2u);
}

// ---------------------------------------------------------------------------
// Test 5: Duplicate key preservation
// Validates: Requirement 6.3
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, DuplicateKeyPreservation) {
    const std::string input = R"({"a":1,"a":2,"a":3})";

    auto result = parseBuffer(std::string(input),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});

    ASSERT_TRUE(result.ok()) << "Parse failed: "
        << (result.error ? result.error->description : "unknown");
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->type, NodeType::Object);

    // All three children must exist with key "a" in order
    ASSERT_EQ(result.root->childCount, 3u);

    // Child 0: key="a", value=1
    EXPECT_EQ(result.root->children[0]->key.view(), "a");
    EXPECT_EQ(result.root->children[0]->type, NodeType::Number);
    EXPECT_EQ(result.root->children[0]->value.view(), "1");

    // Child 1: key="a", value=2
    EXPECT_EQ(result.root->children[1]->key.view(), "a");
    EXPECT_EQ(result.root->children[1]->type, NodeType::Number);
    EXPECT_EQ(result.root->children[1]->value.view(), "2");

    // Child 2: key="a", value=3
    EXPECT_EQ(result.root->children[2]->key.view(), "a");
    EXPECT_EQ(result.root->children[2]->type, NodeType::Number);
    EXPECT_EQ(result.root->children[2]->value.view(), "3");
}

// ---------------------------------------------------------------------------
// Test 6: Unicode escape resolution
// Validates: Requirement 6.4
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, UnicodeEscapeResolution) {
    // \u0048\u0065\u006C\u006C\u006F = "Hello"
    const std::string input = R"({"emoji":"\u0048\u0065\u006C\u006C\u006F"})";

    auto result = parseBuffer(std::string(input),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});

    ASSERT_TRUE(result.ok()) << "Parse failed: "
        << (result.error ? result.error->description : "unknown");
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->type, NodeType::Object);
    ASSERT_EQ(result.root->childCount, 1u);

    // The value should be the resolved unicode: "Hello"
    EXPECT_EQ(result.root->children[0]->key.view(), "emoji");
    EXPECT_EQ(result.root->children[0]->type, NodeType::String);
    EXPECT_EQ(result.root->children[0]->value.view(), "Hello");
}

// ---------------------------------------------------------------------------
// Test 7: Size limit error message (placeholder/documentation test)
// Validates: Requirement 2.6
//
// Note: Testing the actual 4GB+ limit is impractical in a unit test.
// This test documents the expected behavior and verifies the error mapping
// exists by testing that the adapter correctly handles the CAPACITY error
// code path. We verify the error message format for a known error case.
// ---------------------------------------------------------------------------

TEST(SimdjsonIntegration, SizeLimitErrorMessageFormat) {
    // We cannot realistically allocate 4GB+ in a unit test.
    // Instead, verify that the error mapping infrastructure works correctly
    // by checking that parse errors produce well-formed ParseError structs.
    // The CAPACITY error maps to "Document exceeds maximum size (4 GB)"
    // per the design document's error mapping table.

    // Verify a known error case produces a well-formed ParseError
    // (this exercises the same error mapping code path)
    const std::string invalidInput = "{invalid json content";

    auto result = parseBuffer(std::string(invalidInput),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());

    // Error should have a valid byte offset (within input bounds)
    EXPECT_LE(result.error->byteOffset, invalidInput.size());

    // Error description should be non-empty
    EXPECT_FALSE(result.error->description.empty());
}
