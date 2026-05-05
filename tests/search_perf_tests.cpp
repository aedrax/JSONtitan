// ---------------------------------------------------------------------------
// Bug Condition Exploration Test — Search Performance Fix
// Property 1: Synchronous UI Thread Blocking on Large Tree Search
//
// CRITICAL: This test MUST FAIL on unfixed code — failure confirms the bug exists.
// DO NOT attempt to fix the test or the code when it fails.
//
// This test encodes the EXPECTED behavior: search SHALL NOT block the calling
// thread beyond a responsiveness threshold (16ms UI frame budget).
// When the fix is implemented, this test will PASS.
//
// Requirements: 1.1, 1.2, 2.1, 2.2
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "core/json_node.h"
#include "core/search_engine.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Helper: Build a wide, shallow tree with a target node count.
// Uses a balanced fan-out to reach the desired size efficiently.
// ---------------------------------------------------------------------------
std::shared_ptr<const JsonNode> buildLargeTree(std::size_t targetNodeCount,
                                                std::mt19937& rng) {
    if (targetNodeCount <= 1) {
        return JsonNode::makeString("leaf", "value");
    }

    // Use a fan-out that produces the target count in ~2-3 levels
    // For 100K nodes with fan-out 320: level0=1, level1=320, level2=320*312 ≈ 100K
    // Simpler: build a 2-level tree. Root has N children, each child has M leaves.
    // Total = 1 + N + N*M ≈ targetNodeCount
    // Choose N = sqrt(targetNodeCount), M = targetNodeCount / N

    auto n = static_cast<std::size_t>(std::sqrt(static_cast<double>(targetNodeCount)));
    if (n < 2) n = 2;
    auto m = (targetNodeCount - 1) / n;
    if (m < 1) m = 1;

    std::uniform_int_distribution<int> charDist(97, 122);

    auto randomString = [&](int len) -> std::string {
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            s += static_cast<char>(charDist(rng));
        }
        return s;
    };

    std::vector<std::shared_ptr<const JsonNode>> topChildren;
    topChildren.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::shared_ptr<const JsonNode>> leaves;
        leaves.reserve(m);
        for (std::size_t j = 0; j < m; ++j) {
            // Mix of string nodes with searchable content
            leaves.push_back(JsonNode::makeString(
                "key_" + std::to_string(i) + "_" + std::to_string(j),
                randomString(8)));
        }
        topChildren.push_back(JsonNode::makeObject(
            "section_" + std::to_string(i), std::move(leaves)));
    }

    return JsonNode::makeObject("root", std::move(topChildren));
}

// ---------------------------------------------------------------------------
// Helper: Count total nodes in a tree
// ---------------------------------------------------------------------------
std::size_t countNodes(const JsonNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += countNodes(*child);
    }
    return count;
}

// UI frame budget threshold: 16ms (60 FPS)
constexpr auto UI_FRAME_BUDGET = std::chrono::milliseconds(16);

} // anonymous namespace

// ---------------------------------------------------------------------------
// Property 1: Bug Condition — Synchronous UI Thread Blocking
//
// For any large JsonNode tree (100K+ nodes) and non-empty search query,
// calling filter() synchronously SHALL NOT block the calling thread for
// more than the UI frame budget (16ms).
//
// EXPECTED OUTCOME on unfixed code: FAILS
// The filter() call blocks for hundreds of milliseconds to seconds on large
// trees, proving the bug condition exists.
// ---------------------------------------------------------------------------

TEST(BugConditionExploration, SynchronousFilterBlocksUIThread) {
    rc::check("Property 1: Bug Condition — Synchronous filter() blocks calling thread on large trees",
        [](void) {
    // Generate a tree size between 100K and 200K nodes
    auto nodeCount = *rc::gen::inRange(100000, 200001);

    // Generate a non-empty search pattern (1-5 chars from a-z)
    auto patternLen = *rc::gen::inRange(1, 6);
    std::string pattern;
    for (int i = 0; i < patternLen; ++i) {
        pattern += static_cast<char>(*rc::gen::inRange(97, 123));
    }

    // Generate search mode
    auto useRegex = *rc::gen::arbitrary<bool>();

    // Build the large tree
    std::mt19937 rng(*rc::gen::arbitrary<uint32_t>());
    auto root = buildLargeTree(static_cast<std::size_t>(nodeCount), rng);

    // Verify we actually have a large tree
    auto actualCount = countNodes(*root);
    RC_PRE(actualCount >= 100000);

    // Build the search query (mirrors what onSearchTextChanged does)
    SearchQuery query;
    query.pattern = pattern;
    query.mode = useRegex ? SearchMode::Regex : SearchMode::Substring;
    query.caseSensitive = false;

    // Measure wall-clock time of the synchronous filter() call
    // This is exactly what happens on the UI thread in onSearchTextChanged()
    auto start = std::chrono::steady_clock::now();
    auto result = filter(*root, query);
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // ASSERTION: The calling thread SHALL NOT be blocked beyond the UI frame budget.
    // On unfixed code, this WILL FAIL because filter() runs a full recursive DFS
    // synchronously, taking hundreds of ms to seconds on 100K+ node trees.
    RC_ASSERT(elapsed <= UI_FRAME_BUDGET);
    });
}

// ---------------------------------------------------------------------------
// Deterministic test case: Fixed 100K-node tree with known pattern
// This provides a reproducible baseline measurement.
// ---------------------------------------------------------------------------

TEST(BugConditionExploration, DeterministicLargeTreeBlocking) {
    // Build a 100K-node tree with a fixed seed for reproducibility
    std::mt19937 rng(42);
    auto root = buildLargeTree(100000, rng);

    auto actualCount = countNodes(*root);
    ASSERT_GE(actualCount, 100000u)
        << "Tree must have at least 100K nodes to trigger the bug condition";

    // Search for a common pattern that will require full tree traversal
    SearchQuery query;
    query.pattern = "key";  // Will match many node keys
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    // Measure the blocking time
    auto start = std::chrono::steady_clock::now();
    auto result = filter(*root, query);
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Document the actual blocking time for counterexample reporting
    std::cout << "\n[BUG CONDITION COUNTEREXAMPLE]\n"
              << "  Tree size: " << actualCount << " nodes\n"
              << "  Query: \"" << query.pattern << "\" (Substring, case-insensitive)\n"
              << "  Matches found: " << result.matches.size() << "\n"
              << "  Blocking time: " << elapsed.count() << "ms\n"
              << "  UI frame budget: " << UI_FRAME_BUDGET.count() << "ms\n"
              << "  Exceeds budget: " << (elapsed > UI_FRAME_BUDGET ? "YES" : "NO") << "\n"
              << std::endl;

    // ASSERTION: filter() must complete within the UI frame budget (16ms).
    // On unfixed code this WILL FAIL — the synchronous DFS blocks for much longer.
    EXPECT_LE(elapsed, UI_FRAME_BUDGET)
        << "filter() on " << actualCount << "-node tree blocked the calling thread for "
        << elapsed.count() << "ms, exceeding the " << UI_FRAME_BUDGET.count()
        << "ms UI frame budget. This confirms the bug: synchronous search "
        << "blocks the UI thread.";
}

// ---------------------------------------------------------------------------
// Deterministic test case: Keystroke flooding (no debounce)
// Simulates rapid typing — each character triggers a full DFS with no coalescing.
// ---------------------------------------------------------------------------

TEST(BugConditionExploration, KeystrokeFloodingNoDebounce) {
    // Build a 100K-node tree
    std::mt19937 rng(123);
    auto root = buildLargeTree(100000, rng);

    auto actualCount = countNodes(*root);
    ASSERT_GE(actualCount, 100000u);

    // Simulate typing "name" — 4 keystrokes, each triggering a full search
    std::vector<std::string> keystrokes = {"n", "na", "nam", "name"};

    auto totalStart = std::chrono::steady_clock::now();

    for (const auto& text : keystrokes) {
        SearchQuery query;
        query.pattern = text;
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        // Each keystroke triggers filter() synchronously (no debounce)
        auto result = filter(*root, query);
        (void)result;  // Result is applied to UI, but we're measuring blocking
    }

    auto totalEnd = std::chrono::steady_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        totalEnd - totalStart);

    // With debouncing, only ONE search should execute after the user pauses.
    // The total time for 4 keystrokes should be at most the debounce interval
    // plus one search execution (well under 4x the single-search time).
    // We use 4 * UI_FRAME_BUDGET as the threshold (64ms) — still generous.
    auto keystrokeThreshold = UI_FRAME_BUDGET * 4;

    std::cout << "\n[BUG CONDITION COUNTEREXAMPLE - KEYSTROKE FLOODING]\n"
              << "  Tree size: " << actualCount << " nodes\n"
              << "  Keystrokes: " << keystrokes.size() << " (\"n\", \"na\", \"nam\", \"name\")\n"
              << "  Total blocking time: " << totalElapsed.count() << "ms\n"
              << "  Threshold (4 * frame budget): " << keystrokeThreshold.count() << "ms\n"
              << "  Exceeds threshold: " << (totalElapsed > keystrokeThreshold ? "YES" : "NO") << "\n"
              << "  Average per keystroke: " << (totalElapsed.count() / keystrokes.size()) << "ms\n"
              << std::endl;

    // ASSERTION: Total time for 4 keystrokes must be within threshold.
    // On unfixed code this WILL FAIL — each keystroke triggers a full DFS,
    // compounding the blocking time (no debounce coalescing).
    EXPECT_LE(totalElapsed, keystrokeThreshold)
        << "4 keystrokes on " << actualCount << "-node tree blocked for "
        << totalElapsed.count() << "ms total (avg "
        << (totalElapsed.count() / keystrokes.size())
        << "ms/keystroke). Without debouncing, each keystroke triggers a full "
        << "tree traversal, compounding the UI hang.";
}
