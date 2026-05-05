#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QtTest>

#include "core/json_node.h"
#include "core/search_engine.h"
#include "shell/search_worker.h"

using namespace jsontitan::core;

class SearchWorkerTest : public QObject {
    Q_OBJECT

private slots:
    void testSearchWorkerEmitsCorrectResult();
    void testSearchWorkerOnDedicatedThread();
    void testSearchWorkerWithEmptyPattern();
    void testSearchWorkerWithRegexError();
    void testStaleGenerationCanBeIdentified();
};

void SearchWorkerTest::testSearchWorkerEmitsCorrectResult() {
    // Create a known tree: root { name: "Alice", age: 30, city: "NYC" }
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeNumber("age", "30"),
        JsonNode::makeString("city", "NYC")
    });

    SearchQuery query;
    query.pattern = "Alice";
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    SearchWorker worker;
    QSignalSpy spy(&worker, &SearchWorker::searchComplete);
    QVERIFY(spy.isValid());

    // Execute search directly (same thread, for basic correctness)
    worker.executeSearch(query, root, 42);

    QCOMPARE(spy.count(), 1);

    auto args = spy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    auto generation = args.at(1).value<uint64_t>();

    QCOMPARE(generation, uint64_t(42));
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.matches.size(), std::size_t(1));
    QCOMPARE(result.matches[0].node->value, std::string("Alice"));
}

void SearchWorkerTest::testSearchWorkerOnDedicatedThread() {
    // Create a known tree
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("greeting", "hello world"),
        JsonNode::makeString("farewell", "goodbye world"),
        JsonNode::makeNumber("count", "99")
    });

    SearchQuery query;
    query.pattern = "world";
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    // Set up worker on a dedicated QThread
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);

    QSignalSpy spy(&worker, &SearchWorker::searchComplete);
    QVERIFY(spy.isValid());

    // Connect signal to invoke executeSearch on the worker thread
    connect(this, &SearchWorkerTest::destroyed, &worker, &QObject::deleteLater);

    workerThread.start();

    // Invoke the slot on the worker's thread via queued connection
    QMetaObject::invokeMethod(&worker, "executeSearch",
                              Qt::QueuedConnection,
                              Q_ARG(jsontitan::core::SearchQuery, query),
                              Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                              Q_ARG(uint64_t, 7));

    // Wait for the signal (up to 5 seconds)
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.count(), 1);

    auto args = spy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    auto generation = args.at(1).value<uint64_t>();

    QCOMPARE(generation, uint64_t(7));
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.matches.size(), std::size_t(2));

    // Clean up thread
    workerThread.quit();
    workerThread.wait();
}

void SearchWorkerTest::testSearchWorkerWithEmptyPattern() {
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("key", "value")
    });

    SearchQuery query;
    query.pattern = "";
    query.mode = SearchMode::Substring;
    query.caseSensitive = false;

    SearchWorker worker;
    QSignalSpy spy(&worker, &SearchWorker::searchComplete);

    worker.executeSearch(query, root, 1);

    QCOMPARE(spy.count(), 1);
    auto args = spy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    auto generation = args.at(1).value<uint64_t>();

    QCOMPARE(generation, uint64_t(1));
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.matches.size(), std::size_t(0));
}

void SearchWorkerTest::testSearchWorkerWithRegexError() {
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("key", "value")
    });

    SearchQuery query;
    query.pattern = "[invalid(";
    query.mode = SearchMode::Regex;
    query.caseSensitive = false;

    SearchWorker worker;
    QSignalSpy spy(&worker, &SearchWorker::searchComplete);

    worker.executeSearch(query, root, 5);

    QCOMPARE(spy.count(), 1);
    auto args = spy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    auto generation = args.at(1).value<uint64_t>();

    QCOMPARE(generation, uint64_t(5));
    QVERIFY(result.error.has_value());
    QVERIFY(!result.error->description.empty());
    QCOMPARE(result.matches.size(), std::size_t(0));
}

void SearchWorkerTest::testStaleGenerationCanBeIdentified() {
    // Simulate multiple searches with different generations.
    // The consumer should only use the result matching the latest generation.
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("city", "Boston")
    });

    SearchWorker worker;
    QSignalSpy spy(&worker, &SearchWorker::searchComplete);

    // Simulate three searches with increasing generations
    SearchQuery query1{.pattern = "Alice", .mode = SearchMode::Substring, .caseSensitive = false};
    SearchQuery query2{.pattern = "Boston", .mode = SearchMode::Substring, .caseSensitive = false};
    SearchQuery query3{.pattern = "name", .mode = SearchMode::Substring, .caseSensitive = false};

    worker.executeSearch(query1, root, 1);
    worker.executeSearch(query2, root, 2);
    worker.executeSearch(query3, root, 3);

    QCOMPARE(spy.count(), 3);

    // Simulate consumer logic: only accept the latest generation (3)
    uint64_t currentGeneration = 3;

    for (int i = 0; i < spy.count(); ++i) {
        auto args = spy.at(i);
        auto generation = args.at(1).value<uint64_t>();

        if (generation < currentGeneration) {
            // Stale result — would be discarded by consumer
            continue;
        }

        // Only generation 3 should be accepted
        QCOMPARE(generation, uint64_t(3));
        auto result = args.at(0).value<FilterResult>();
        // "name" matches the key "name" in the tree
        QVERIFY(!result.matches.empty());
    }
}

QTEST_MAIN(SearchWorkerTest)
#include "search_worker_tests.moc"
