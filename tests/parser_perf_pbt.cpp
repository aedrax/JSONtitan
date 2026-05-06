// ---------------------------------------------------------------------------
// Bug Condition Exploration Property-Based Test
// Feature: parser-performance-optimization
// ---------------------------------------------------------------------------
// This test demonstrates the performance bottleneck in the simdjson pipeline:
// After parseBuffer() builds an ArenaJsonNode tree in arena memory,
// toParseResult() deep-copies the entire tree into individually heap-allocated
// shared_ptr<const JsonNode> nodes. For large inputs, this deep-copy dominates
// the total pipeline time.
//
// **Validates: Requirements 1.1, 1.2, 1.3**
//
// EXPECTED OUTCOME on UNFIXED code: FAIL
// The property asserts that toParseResult() should NOT be the bottleneck
// (i.e., it should take less than 10% of total pipeline time). On unfixed code,
// toParseResult() takes MORE time than parseBuffer() itself for large inputs.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "core/arena_allocator.h"
#include "core/arena_json_node.h"
#include "core/parse_orchestrator.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Helper: Count nodes in an ArenaJsonNode tree
// ---------------------------------------------------------------------------
std::size_t countArenaNodes(const ArenaJsonNode* node) {
    if (!node) return 0;
    std::size_t count = 1;
    for (std::size_t i = 0; i < node->childCount; ++i) {
        count += countArenaNodes(node->children[i]);
    }
    return count;
}

// ---------------------------------------------------------------------------
// Helper: Generate a JSON array with N simple objects
// Produces: [{"id":0,"name":"item_0","value":123.456}, ...]
// ---------------------------------------------------------------------------
std::string generateLargeJsonArray(std::size_t objectCount) {
    std::string json = "[";
    for (std::size_t i = 0; i < objectCount; ++i) {
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(i) +
                ",\"name\":\"item_" + std::to_string(i) + "\"" +
                ",\"value\":" + std::to_string(123.456 + static_cast<double>(i)) +
                ",\"active\":true" +
                ",\"tags\":[\"tag_a\",\"tag_b\",\"tag_c\"]" +
                "}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: JSON arrays of varying sizes (100–10000 objects)
// ---------------------------------------------------------------------------
rc::Gen<std::string> genLargeJsonArray() {
    return rc::gen::map(
        rc::gen::inRange(100, 5001),
        [](int count) {
            return generateLargeJsonArray(static_cast<std::size_t>(count));
        });
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: Nested JSON structures of varying depth and breadth
// ---------------------------------------------------------------------------
std::string generateNestedJson(int depth, int breadth) {
    if (depth <= 0) {
        return "\"leaf_value\"";
    }
    std::string json = "{";
    for (int i = 0; i < breadth; ++i) {
        if (i > 0) json += ",";
        json += "\"key_" + std::to_string(i) + "\":";
        json += generateNestedJson(depth - 1, breadth);
    }
    json += "}";
    return json;
}

rc::Gen<std::string> genNestedJson() {
    return rc::gen::exec([]() -> std::string {
        int depth = *rc::gen::inRange(3, 7);
        int breadth = *rc::gen::inRange(3, 8);
        return generateNestedJson(depth, breadth);
    });
}

} // anonymous namespace

// ===========================================================================
// Property 1: Bug Condition — Deep-Copy Performance Bottleneck
//
// For any JSON input parsed via the simdjson backend that produces more than
// 100 nodes, the time spent in toParseResult() (deep-copy) SHALL be less than
// 10% of the total pipeline time (parseBuffer + toParseResult).
//
// On UNFIXED code, toParseResult() dominates for large inputs — this FAILS.
//
// **Validates: Requirements 1.1, 1.2, 1.3**
// ===========================================================================

TEST(ParserPerfBugCondition, DeepCopyBottleneck_LargeArrays) {
    rc::check("Bug Condition: toParseResult() should NOT dominate pipeline time for large arrays",
        []() {
            const auto json = *genLargeJsonArray();

            // Step 1: Parse via simdjson backend (this is the fast part)
            auto startParse = std::chrono::high_resolution_clock::now();
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});
            auto endParse = std::chrono::high_resolution_clock::now();

            RC_PRE(arenaResult.ok());

            // Verify we have a substantial tree (>100 nodes)
            std::size_t nodeCount = countArenaNodes(arenaResult.root);
            RC_PRE(nodeCount > 100);

            // Step 2: Perform the deep-copy via toParseResult() (this is the bottleneck)
            auto startCopy = std::chrono::high_resolution_clock::now();
            auto parseResult = arenaResult.toParseResult();
            auto endCopy = std::chrono::high_resolution_clock::now();

            RC_ASSERT(parseResult.root != nullptr);

            // Measure times
            auto parseTime = std::chrono::duration_cast<std::chrono::microseconds>(
                endParse - startParse).count();
            auto copyTime = std::chrono::duration_cast<std::chrono::microseconds>(
                endCopy - startCopy).count();
            auto totalTime = parseTime + copyTime;

            // Property: toParseResult() should take less than 10% of total time.
            // On unfixed code, toParseResult() typically takes MORE than parseBuffer()
            // itself (i.e., >50% of total), so this assertion FAILS.
            if (totalTime > 0) {
                double copyFraction = static_cast<double>(copyTime) / static_cast<double>(totalTime);
                RC_ASSERT(copyFraction < 0.10);
            }
        });
}

TEST(ParserPerfBugCondition, DeepCopyBottleneck_NestedStructures) {
    rc::check("Bug Condition: toParseResult() should NOT dominate pipeline time for nested JSON",
        []() {
            const auto json = *genNestedJson();

            // Step 1: Parse via simdjson backend
            auto startParse = std::chrono::high_resolution_clock::now();
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});
            auto endParse = std::chrono::high_resolution_clock::now();

            RC_PRE(arenaResult.ok());

            // Only test with substantial trees
            std::size_t nodeCount = countArenaNodes(arenaResult.root);
            RC_PRE(nodeCount > 100);

            // Step 2: Deep-copy via toParseResult()
            auto startCopy = std::chrono::high_resolution_clock::now();
            auto parseResult = arenaResult.toParseResult();
            auto endCopy = std::chrono::high_resolution_clock::now();

            RC_ASSERT(parseResult.root != nullptr);

            auto parseTime = std::chrono::duration_cast<std::chrono::microseconds>(
                endParse - startParse).count();
            auto copyTime = std::chrono::duration_cast<std::chrono::microseconds>(
                endCopy - startCopy).count();
            auto totalTime = parseTime + copyTime;

            // Property: toParseResult() should take less than 10% of total time.
            if (totalTime > 0) {
                double copyFraction = static_cast<double>(copyTime) / static_cast<double>(totalTime);
                RC_ASSERT(copyFraction < 0.10);
            }
        });
}

// ===========================================================================
// Concrete Failing Case: 1MB JSON array with 10,000 objects
// This is a deterministic test that demonstrates the bottleneck clearly.
//
// **Validates: Requirements 1.1, 1.2, 1.3**
// ===========================================================================

TEST(ParserPerfBugCondition, ConcreteCase_10KObjects) {
    // Generate a ~1MB JSON array with 10,000 objects
    std::string json = generateLargeJsonArray(10000);

    // Verify size is in the expected range (~1MB)
    ASSERT_GT(json.size(), 500'000u);  // At least 500KB

    // Step 1: Parse via simdjson backend
    auto startParse = std::chrono::high_resolution_clock::now();
    auto arenaResult = parseBuffer(std::string(json),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});
    auto endParse = std::chrono::high_resolution_clock::now();

    ASSERT_TRUE(arenaResult.ok());

    std::size_t nodeCount = countArenaNodes(arenaResult.root);
    ASSERT_GT(nodeCount, 10000u);  // At least 10K nodes (objects + their children)

    // Step 2: Deep-copy via toParseResult()
    auto startCopy = std::chrono::high_resolution_clock::now();
    auto parseResult = arenaResult.toParseResult();
    auto endCopy = std::chrono::high_resolution_clock::now();

    ASSERT_NE(parseResult.root, nullptr);

    auto parseTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        endParse - startParse).count();
    auto copyTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        endCopy - startCopy).count();
    auto totalTimeUs = parseTimeUs + copyTimeUs;

    // Log the timing for documentation
    std::cout << "\n=== Bug Condition Concrete Case ===\n";
    std::cout << "Input size: " << json.size() << " bytes\n";
    std::cout << "Node count: " << nodeCount << "\n";
    std::cout << "parseBuffer() time: " << parseTimeUs << " us\n";
    std::cout << "toParseResult() time: " << copyTimeUs << " us\n";
    std::cout << "Total pipeline time: " << totalTimeUs << " us\n";
    std::cout << "toParseResult() fraction: "
              << (totalTimeUs > 0 ? (100.0 * copyTimeUs / totalTimeUs) : 0.0)
              << "%\n";
    std::cout << "===================================\n";

    // Property: toParseResult() should take less than 10% of total time.
    // On unfixed code, it typically takes 50-80% of total time.
    ASSERT_GT(totalTimeUs, 0);
    double copyFraction = static_cast<double>(copyTimeUs) / static_cast<double>(totalTimeUs);
    EXPECT_LT(copyFraction, 0.10)
        << "toParseResult() took " << (copyFraction * 100.0)
        << "% of total pipeline time (expected < 10%). "
        << "This confirms the deep-copy bottleneck exists.";
}
