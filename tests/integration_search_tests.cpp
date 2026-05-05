#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <algorithm>

#include "core/json_node.h"
#include "core/search_engine.h"
#include "shell/filter_proxy_model.h"
#include "shell/search_worker.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 6.1: Integration test for full async search flow
// Validates: Requirements 2.1, 2.2, 3.1
// ---------------------------------------------------------------------------

class IntegrationSearchTest : public QObject {
    Q_OBJECT

private:
    // Helper: build a synthetic large tree with many nodes
    static std::shared_ptr<const JsonNode> buildLargeTree(int breadth, int depth);
    static std::shared_ptr<const JsonNode> buildSubtree(const std::string& prefix, int breadth, int depth);

private slots:
    void testFullAsyncSearchFlow();
    void testUIThreadNotBlockedDuringSearch();
    void testDebounceCoalescesRapidInput();
    void testResultsAppearInFilterProxyModel();
    void testCancellationDiscardsStaleResults();
    void testOnlyLatestGenerationApplied();
};

std::shared_ptr<const JsonNode> IntegrationSearchTest::buildSubtree(
    const std::string& prefix, int breadth, int depth) {
    if (depth <= 0) {
        return JsonNode::makeString(prefix + "_leaf", "value_" + prefix);
    }

    std::vector<std::shared_ptr<const JsonNode>> children;
    children.reserve(static_cast<std::size_t>(breadth));
    for (int i = 0; i < breadth; ++i) {
        std::string childPrefix = prefix + "_" + std::to_string(i);
        children.push_back(buildSubtree(childPrefix, breadth, depth - 1));
    }
    return JsonNode::makeObject(prefix, std::move(children));
}

std::shared_ptr<const JsonNode> IntegrationSearchTest::buildLargeTree(int breadth, int depth) {
    // Build a tree with breadth^depth leaf nodes
    // breadth=10, depth=4 gives ~10,000 nodes (enough for integration test)
    // breadth=10, depth=5 gives ~100,000 nodes (for blocking test)
    return buildSubtree("root", breadth, depth);
}

void IntegrationSearchTest::testFullAsyncSearchFlow() {
    // Integration test: simulate the full MainWindow search pipeline
    // 1. Load a synthetic tree into TreeModel + FilterProxyModel
    // 2. Set up debounce timer + SearchWorker on QThread
    // 3. Simulate typing a query
    // 4. Verify debounce fires → worker executes → results applied to FilterProxyModel

    // Build a moderate tree (~1000 nodes)
    auto root = buildLargeTree(10, 3);

    // Set up models (mimics MainWindow)
    TreeModel treeModel;
    treeModel.setRootNode(root);

    FilterProxyModel filterProxy;
    filterProxy.setSourceModel(&treeModel);

    // Set up debounce timer (single-shot, 250ms)
    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    // Set up search worker on dedicated thread
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    uint64_t searchGeneration = 0;
    QString currentText;
    bool searchApplied = false;

    // Connect debounce timer → executeSearch (mimics MainWindow::executeSearch)
    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
        if (currentText.isEmpty() || !root) return;
        ++searchGeneration;

        SearchQuery query;
        query.pattern = currentText.toStdString();
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, searchGeneration));
    });

    // Connect searchComplete → apply results (mimics MainWindow::onSearchComplete)
    connect(&worker, &SearchWorker::searchComplete, this,
            [&](jsontitan::core::FilterResult result, uint64_t generation) {
                if (generation != searchGeneration) return;
                if (!result.error.has_value()) {
                    filterProxy.applyFilter(result);
                    searchApplied = true;
                }
            },
            Qt::QueuedConnection);

    // Simulate typing "leaf" character by character (mimics onSearchTextChanged)
    QStringList keystrokes = {"l", "le", "lea", "leaf"};
    for (const auto& text : keystrokes) {
        currentText = text;
        debounceTimer.start(); // restart on each keystroke
        QTest::qWait(30);     // ~30ms between keystrokes
    }

    // Wait for debounce (250ms) + worker execution + signal delivery
    QVERIFY(completeSpy.wait(5000));

    // Process remaining events to ensure onSearchComplete fires
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    // Verify: exactly one search was dispatched (debounce coalesced keystrokes)
    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(searchGeneration, uint64_t(1));

    // Verify: results were applied to FilterProxyModel
    QVERIFY(searchApplied);
    QVERIFY(filterProxy.isFiltered());

    // Verify: the filter result contains matches (tree has "leaf" in many node keys)
    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());
    QVERIFY(!result.matches.empty());

    // Clean up
    workerThread.quit();
    workerThread.wait();
}

void IntegrationSearchTest::testUIThreadNotBlockedDuringSearch() {
    // Verify that the UI thread can process events while a search is running
    // on the background thread. We use a larger tree to ensure the search
    // takes measurable time, then check that processEvents() returns quickly.

    // Build a larger tree (~100K nodes): breadth=10, depth=5
    auto root = buildLargeTree(10, 5);

    // Set up search worker on dedicated thread
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    // Dispatch search to background worker
    SearchQuery query;
    query.pattern = "leaf";
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    QMetaObject::invokeMethod(&worker, "executeSearch",
                              Qt::QueuedConnection,
                              Q_ARG(jsontitan::core::SearchQuery, query),
                              Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                              Q_ARG(uint64_t, uint64_t(1)));

    // Immediately after dispatching, measure how long processEvents takes.
    // If the UI thread were blocked (synchronous search), this would take
    // as long as the search itself. With async search, it should return quickly.
    QElapsedTimer uiTimer;
    uiTimer.start();

    // Process events multiple times to simulate a responsive UI loop
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }

    qint64 uiElapsed = uiTimer.elapsed();

    // The UI thread should regain control almost immediately (well under 100ms)
    // even though the background search on 100K nodes takes longer.
    // We use a generous threshold of 200ms to avoid flakiness.
    QVERIFY2(uiElapsed < 200,
             qPrintable(QString("UI thread was blocked for %1ms during search dispatch")
                            .arg(uiElapsed)));

    // Wait for the background search to complete
    QVERIFY(completeSpy.wait(30000)); // 30s timeout for large tree

    // Verify search completed successfully
    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());
    QVERIFY(!result.matches.empty());

    // Clean up
    workerThread.quit();
    workerThread.wait();
}

void IntegrationSearchTest::testDebounceCoalescesRapidInput() {
    // Verify that rapid text changes produce only one search execution
    // and that the search uses the final text value.

    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("greeting", "hello"),
        JsonNode::makeString("city", "Helsinki")
    });

    TreeModel treeModel;
    treeModel.setRootNode(root);

    FilterProxyModel filterProxy;
    filterProxy.setSourceModel(&treeModel);

    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    uint64_t generation = 0;
    QString currentText;

    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
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

    // Simulate rapid typing: "h", "he", "hel" — all within debounce window
    QStringList keystrokes = {"h", "he", "hel"};
    for (const auto& text : keystrokes) {
        currentText = text;
        debounceTimer.start();
        QTest::qWait(20);
    }

    // Wait for debounce + execution
    QTest::qWait(400);
    QCoreApplication::processEvents();

    // Only one search should have fired
    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(generation, uint64_t(1));

    // The search should have used the final text "hel"
    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());
    // "hel" matches "hello" and "Helsinki" (case-insensitive substring)
    QCOMPARE(result.matches.size(), std::size_t(2));

    workerThread.quit();
    workerThread.wait();
}

void IntegrationSearchTest::testResultsAppearInFilterProxyModel() {
    // End-to-end: verify that after the full async pipeline completes,
    // FilterProxyModel correctly filters the tree view.

    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("city", "Boston"),
        JsonNode::makeNumber("age", "30"),
        JsonNode::makeObject("address", {
            JsonNode::makeString("street", "Main St"),
            JsonNode::makeString("zip", "02101")
        })
    });

    TreeModel treeModel;
    treeModel.setRootNode(root);

    // Fetch all rows so the model is fully populated (TreeModel uses lazy loading)
    while (treeModel.canFetchMore(QModelIndex())) {
        treeModel.fetchMore(QModelIndex());
    }

    FilterProxyModel filterProxy;
    filterProxy.setSourceModel(&treeModel);

    // Before search: filter is not active
    QVERIFY(!filterProxy.isFiltered());
    int sourceRows = treeModel.rowCount(QModelIndex());
    QCOMPARE(sourceRows, 4); // name, city, age, address

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    // Dispatch search for "Alice"
    SearchQuery query;
    query.pattern = "Alice";
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    QMetaObject::invokeMethod(&worker, "executeSearch",
                              Qt::QueuedConnection,
                              Q_ARG(jsontitan::core::SearchQuery, query),
                              Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                              Q_ARG(uint64_t, uint64_t(1)));

    // Wait for result
    QVERIFY(completeSpy.wait(5000));

    auto args = completeSpy.at(0);
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.matches.size(), std::size_t(1));

    // Apply result to FilterProxyModel (mimics MainWindow::onSearchComplete)
    filterProxy.applyFilter(result);

    // After applying filter: proxy model is filtered
    QVERIFY(filterProxy.isFiltered());

    // The proxy model should show fewer rows than the source model
    // Source has 4 top-level children (name, city, age, address)
    // Only "name" (with value "Alice") should be visible via its ancestor path
    int visibleRows = filterProxy.rowCount(QModelIndex());
    QVERIFY(visibleRows > 0);
    QVERIFY(visibleRows < sourceRows);

    // Clear filter and verify all rows are visible again
    filterProxy.clearFilter();
    QVERIFY(!filterProxy.isFiltered());
    QCOMPARE(filterProxy.rowCount(QModelIndex()), sourceRows);

    workerThread.quit();
    workerThread.wait();
}

// ---------------------------------------------------------------------------
// Task 6.2: Integration test for cancellation (generation counter)
// Validates: Requirements 2.1, 2.2
// ---------------------------------------------------------------------------

void IntegrationSearchTest::testCancellationDiscardsStaleResults() {
    // Test: dispatch two searches in quick succession. The first search uses a
    // large tree (takes longer), the second uses a different query. Only the
    // second result (matching the current generation) should be applied.

    // Build a moderately large tree so the first search takes measurable time
    auto root = buildLargeTree(10, 4); // ~10,000 nodes

    TreeModel treeModel;
    treeModel.setRootNode(root);

    FilterProxyModel filterProxy;
    filterProxy.setSourceModel(&treeModel);

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    // Mimic MainWindow's generation counter logic
    uint64_t currentGeneration = 0;
    FilterResult appliedResult;
    int appliedCount = 0;
    uint64_t appliedGeneration = 0;

    // Connect searchComplete with generation-checking logic (mimics onSearchComplete)
    connect(&worker, &SearchWorker::searchComplete, this,
            [&](jsontitan::core::FilterResult result, uint64_t generation) {
                // Discard stale results — only apply if generation matches current
                if (generation != currentGeneration) {
                    return; // Stale result discarded
                }
                if (!result.error.has_value()) {
                    filterProxy.applyFilter(result);
                    appliedResult = result;
                    appliedGeneration = generation;
                    ++appliedCount;
                }
            },
            Qt::QueuedConnection);

    // Dispatch FIRST search (generation 1) — broad query "0" matches many nodes
    ++currentGeneration; // generation = 1
    {
        SearchQuery query;
        query.pattern = "0";
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, uint64_t(1)));
    }

    // Immediately dispatch SECOND search (generation 2) — different query
    // This simulates the user typing a new query before the first completes
    ++currentGeneration; // generation = 2
    {
        SearchQuery query;
        query.pattern = "root_0_0_0_0";
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, uint64_t(2)));
    }

    // Wait for both searches to complete (both signals will arrive)
    // The worker processes them sequentially on its thread
    QTRY_COMPARE_WITH_TIMEOUT(completeSpy.count(), 2, 30000);

    // Process all pending events to ensure our lambda ran
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    // Verify: only ONE result was applied (the second search, generation 2)
    QCOMPARE(appliedCount, 1);
    QCOMPARE(appliedGeneration, uint64_t(2));

    // Verify: the applied result corresponds to the second query ("root_0_0_0_0")
    // which should match fewer nodes than the broad "0" query
    QVERIFY(!appliedResult.error.has_value());
    QVERIFY(!appliedResult.matches.empty());

    // Verify: FilterProxyModel is filtered with the second result
    QVERIFY(filterProxy.isFiltered());

    workerThread.quit();
    workerThread.wait();
}

void IntegrationSearchTest::testOnlyLatestGenerationApplied() {
    // Test: dispatch three searches rapidly with incrementing generations.
    // Only the third (latest) generation's result should be applied.
    // This tests the scenario where a user types multiple characters quickly
    // and each debounce fires a new search, superseding the previous one.

    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("alpha", "first_value"),
        JsonNode::makeString("beta", "second_value"),
        JsonNode::makeString("gamma", "third_value"),
        JsonNode::makeObject("nested", {
            JsonNode::makeString("alpha_child", "nested_alpha"),
            JsonNode::makeString("beta_child", "nested_beta"),
            JsonNode::makeString("gamma_child", "nested_gamma")
        })
    });

    TreeModel treeModel;
    treeModel.setRootNode(root);

    // Fetch all rows
    while (treeModel.canFetchMore(QModelIndex())) {
        treeModel.fetchMore(QModelIndex());
    }

    FilterProxyModel filterProxy;
    filterProxy.setSourceModel(&treeModel);

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    uint64_t currentGeneration = 0;
    std::vector<uint64_t> appliedGenerations;
    FilterResult lastAppliedResult;

    connect(&worker, &SearchWorker::searchComplete, this,
            [&](jsontitan::core::FilterResult result, uint64_t generation) {
                if (generation != currentGeneration) {
                    return; // Stale — discard
                }
                if (!result.error.has_value()) {
                    filterProxy.applyFilter(result);
                    lastAppliedResult = result;
                    appliedGenerations.push_back(generation);
                }
            },
            Qt::QueuedConnection);

    // Dispatch three searches in rapid succession, each with a new generation
    // Search 1: "alpha" (generation 1)
    ++currentGeneration;
    {
        SearchQuery query;
        query.pattern = "alpha";
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, uint64_t(1)));
    }

    // Search 2: "beta" (generation 2)
    ++currentGeneration;
    {
        SearchQuery query;
        query.pattern = "beta";
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, uint64_t(2)));
    }

    // Search 3: "gamma" (generation 3) — this is the latest
    ++currentGeneration;
    {
        SearchQuery query;
        query.pattern = "gamma";
        query.mode = SearchMode::Substring;
        query.caseSensitive = false;

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, uint64_t(3)));
    }

    // Wait for all three searches to complete
    QTRY_COMPARE_WITH_TIMEOUT(completeSpy.count(), 3, 10000);

    // Process events to ensure all lambdas fire
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    // Verify: only the third generation was applied
    QCOMPARE(appliedGenerations.size(), std::size_t(1));
    QCOMPARE(appliedGenerations[0], uint64_t(3));

    // Verify: the applied result matches "gamma" query
    QVERIFY(!lastAppliedResult.error.has_value());
    QVERIFY(!lastAppliedResult.matches.empty());

    // "gamma" should match "gamma" key and "gamma_child" key and their values
    // containing "gamma" — verify we got the right matches
    for (const auto& match : lastAppliedResult.matches) {
        // Each match node's key or value should contain "gamma"
        bool containsGamma = false;
        if (match.node) {
            std::string key = match.node->key;
            std::string val = match.node->value;
            // Case-insensitive check
            auto toLower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                return s;
            };
            containsGamma = toLower(key).find("gamma") != std::string::npos ||
                            toLower(val).find("gamma") != std::string::npos;
        }
        QVERIFY2(containsGamma,
                 qPrintable(QString("Match node does not contain 'gamma': key='%1', value='%2'")
                                .arg(QString::fromStdString(match.node->key),
                                     QString::fromStdString(match.node->value))));
    }

    // Verify: FilterProxyModel reflects the gamma search results
    QVERIFY(filterProxy.isFiltered());

    workerThread.quit();
    workerThread.wait();
}

QTEST_MAIN(IntegrationSearchTest)
#include "integration_search_tests.moc"
