// ---------------------------------------------------------------------------
// Bug Condition Exploration Test — Search Performance Fix
// Property 1: Synchronous UI Thread Blocking on Large Tree Search
//
// These tests validate that the FIXED search architecture does NOT block the
// UI thread. The fix moves filter() to a background thread via SearchWorker
// with debounce coalescing, so the UI thread dispatches work and returns
// immediately.
//
// The tests validate the fix by using the async SearchWorker path and
// measuring UI thread responsiveness.
//
// Requirements: 1.1, 1.2, 2.1, 2.2
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>

#include "core/json_node.h"
#include "core/search_engine.h"
#include "shell/search_worker.h"

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

// Dispatching must return long before the search itself would (~70 ms on
// these trees). A frame (16 ms) is the goal; a busy CI machine can miss it
// on scheduling alone, so the bound leaves room.
constexpr auto kDispatchBound = std::chrono::milliseconds(50);

// QSignalSpy::wait() only sees emissions made after it starts waiting, and
// the worker can finish before then: poll the count instead.
bool waitForSignal(QSignalSpy& spy, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (spy.count() == 0 && std::chrono::steady_clock::now() < deadline) {
        spy.wait(50);
    }
    return spy.count() > 0;
}

// Stops the worker thread on every exit path. A failed assertion leaves the
// test early, and destroying a running QThread aborts the whole binary.
struct ThreadStopper {
    QThread& thread;
    ~ThreadStopper() {
        thread.quit();
        thread.wait();
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Deterministic test case: Fixed 100K-node tree with known pattern
// Validates the FIX: dispatching search via SearchWorker does NOT block
// the calling thread, even though filter() itself takes ~70ms.
// ---------------------------------------------------------------------------

TEST(BugConditionExploration, DeterministicLargeTreeBlocking) {
    // Build a 100K-node tree with a fixed seed for reproducibility
    std::mt19937 rng(42);
    auto root = buildLargeTree(100000, rng);

    auto actualCount = countNodes(*root);
    ASSERT_GE(actualCount, 100000u)
        << "Tree must have at least 100K nodes to trigger the bug condition";

    // Set up SearchWorker on a background thread (the fix architecture)
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    ThreadStopper stopper{workerThread};  // after the spy: stops first

    // Search for a common pattern that will require full tree traversal
    SearchQuery query;
    query.pattern = "key";  // Will match many node keys
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    // Measure how long the UI thread is blocked when DISPATCHING the search
    // (not when filter() runs — that happens on the worker thread)
    auto start = std::chrono::steady_clock::now();

    QMetaObject::invokeMethod(&worker, "executeSearch",
                              Qt::QueuedConnection,
                              Q_ARG(jsontitan::core::SearchQuery, query),
                              Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                              Q_ARG(uint64_t, uint64_t(1)));

    // Process events briefly to simulate UI responsiveness check
    QCoreApplication::processEvents();

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\n[FIX VALIDATION - ASYNC DISPATCH]\n"
              << "  Tree size: " << actualCount << " nodes\n"
              << "  Query: \"" << query.pattern << "\" (Substring, case-insensitive)\n"
              << "  UI thread dispatch time: " << elapsed.count() << "ms\n"
              << "  Dispatch bound: " << kDispatchBound.count() << "ms\n"
              << "  Within bound: " << (elapsed <= kDispatchBound ? "YES" : "NO") << "\n"
              << std::endl;

    // ASSERTION: Dispatching search to background thread must not block UI thread
    EXPECT_LE(elapsed, kDispatchBound)
        << "Dispatching search via SearchWorker blocked the UI thread for "
        << elapsed.count() << "ms. The async dispatch should be near-instant.";

    // Wait for the background search to actually complete (validates correctness)
    ASSERT_TRUE(waitForSignal(completeSpy, std::chrono::seconds(30)))
        << "SearchWorker did not complete within timeout";

    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    EXPECT_FALSE(result.error.has_value());
    EXPECT_FALSE(result.matches.empty())
        << "Search for 'key' on 100K-node tree should produce matches";

    std::cout << "  Matches found: " << result.matches.size() << "\n"
              << "  Search completed successfully on background thread.\n"
              << std::endl;

}

// ---------------------------------------------------------------------------
// Deterministic test case: Keystroke flooding WITH debounce
// Validates the FIX: rapid keystrokes are coalesced by debounce timer,
// resulting in only ONE search execution instead of one per keystroke.
// The UI thread is never blocked.
// ---------------------------------------------------------------------------

TEST(BugConditionExploration, KeystrokeFloodingNoDebounce) {
    // Build a 100K-node tree
    std::mt19937 rng(123);
    auto root = buildLargeTree(100000, rng);

    auto actualCount = countNodes(*root);
    ASSERT_GE(actualCount, 100000u);

    // Set up SearchWorker on a background thread
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    ThreadStopper stopper{workerThread};  // after the spy: stops first

    // Set up debounce timer (mimics MainWindow fix)
    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    uint64_t generation = 0;
    QString currentText;

    QObject::connect(&debounceTimer, &QTimer::timeout, [&]() {
        if (currentText.isEmpty()) return;
        ++generation;

        SearchQuery query;
        query.pattern = currentText.toStdString();
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, generation));
    });

    // Simulate typing "name" — 4 keystrokes, each restarting the debounce timer
    std::vector<QString> keystrokes = {"n", "na", "nam", "name"};

    // Measure UI thread time during the keystroke simulation
    auto uiStart = std::chrono::steady_clock::now();

    for (const auto& text : keystrokes) {
        currentText = text;
        debounceTimer.start(); // Restart debounce on each keystroke
        QCoreApplication::processEvents();
        // Small delay between keystrokes (simulates typing speed)
        QThread::msleep(20);
        QCoreApplication::processEvents();
    }

    auto uiEnd = std::chrono::steady_clock::now();
    auto uiElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(uiEnd - uiStart);

    // UI thread should only be blocked for the typing simulation time (~80ms)
    // NOT for 4 full filter() executions (~280ms+)
    // The typing itself takes ~80ms (4 * 20ms sleep), so threshold is generous
    auto typingThreshold = std::chrono::milliseconds(200);

    std::cout << "\n[FIX VALIDATION - DEBOUNCE COALESCING]\n"
              << "  Tree size: " << actualCount << " nodes\n"
              << "  Keystrokes: " << keystrokes.size() << " (\"n\", \"na\", \"nam\", \"name\")\n"
              << "  UI thread time during typing: " << uiElapsed.count() << "ms\n"
              << "  Threshold: " << typingThreshold.count() << "ms\n"
              << "  Within threshold: " << (uiElapsed <= typingThreshold ? "YES" : "NO") << "\n"
              << std::endl;

    // ASSERTION: UI thread is not blocked during keystroke simulation
    EXPECT_LE(uiElapsed, typingThreshold)
        << "UI thread was blocked for " << uiElapsed.count()
        << "ms during keystroke simulation. With debounce, the UI thread "
        << "should only spend time restarting the timer, not running filter().";

    // Now wait for the debounce to fire and the single search to complete
    // Debounce is 250ms, search takes ~70ms on 100K nodes
    ASSERT_TRUE(waitForSignal(completeSpy, std::chrono::seconds(10)))
        << "Debounced search did not complete within timeout";

    // ASSERTION: Only ONE search was executed (debounce coalesced 4 keystrokes)
    EXPECT_EQ(completeSpy.count(), 1)
        << "Expected exactly 1 search execution after debounce, got "
        << completeSpy.count() << ". Debounce should coalesce rapid keystrokes.";

    EXPECT_EQ(generation, uint64_t(1))
        << "Generation counter should be 1 (one search dispatched)";

    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    EXPECT_FALSE(result.error.has_value());

    std::cout << "  Searches executed: " << completeSpy.count() << " (expected 1)\n"
              << "  Final query: \"" << currentText.toStdString() << "\"\n"
              << "  Matches found: " << result.matches.size() << "\n"
              << "  Debounce successfully coalesced " << keystrokes.size()
              << " keystrokes into 1 search.\n"
              << std::endl;

}
// Custom main: GTest needs a QCoreApplication for Qt event loop support
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
