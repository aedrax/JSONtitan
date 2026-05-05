#include <QCoreApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include "core/json_node.h"
#include "core/search_engine.h"
#include "shell/search_worker.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 4.3: Unit test for debounce behavior
// Validates: Requirements 2.2
// ---------------------------------------------------------------------------

class DebounceTest : public QObject {
    Q_OBJECT

private slots:
    void testRapidTextChangesProduceOneSearch();
    void testGenerationCounterIncrementsCorrectly();
    void testEmptyTextClearsImmediatelyNoDebounce();
};

void DebounceTest::testRapidTextChangesProduceOneSearch() {
    // Simulate the debounce mechanism: rapid text changes (5 chars in 100ms)
    // should result in only one search execution after the debounce interval.
    //
    // We replicate the MainWindow debounce logic with a QTimer + SearchWorker
    // to test the behavior in isolation without needing a full GUI.

    // Set up a debounce timer (single-shot, 250ms) like MainWindow
    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    // Set up a search worker on a dedicated thread
    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    // Track how many times executeSearch is actually invoked
    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    // Create a known tree
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("greeting", "hello"),
        JsonNode::makeNumber("count", "42")
    });

    uint64_t generation = 0;
    QString currentText;

    // Connect timer timeout to execute search (mimics MainWindow::executeSearch)
    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
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

    // Simulate rapid text changes: 5 characters typed in ~100ms
    // Each keystroke restarts the debounce timer
    QStringList keystrokes = {"h", "he", "hel", "hell", "hello"};
    for (int i = 0; i < keystrokes.size(); ++i) {
        currentText = keystrokes[i];
        debounceTimer.start(); // restart timer on each keystroke
        // Small delay between keystrokes (~20ms each)
        QTest::qWait(20);
    }

    // Wait for debounce interval to fire (250ms) plus margin for worker execution
    QTest::qWait(400);

    // Process any remaining events
    QCoreApplication::processEvents();

    // Only ONE search should have been executed
    QCOMPARE(completeSpy.count(), 1);

    // Verify the search was for the final text "hello"
    auto args = completeSpy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    auto resultGeneration = args.at(1).value<uint64_t>();

    QCOMPARE(resultGeneration, uint64_t(1)); // Only one generation increment
    QVERIFY(!result.error.has_value());
    // "hello" should match the "greeting" node's value "hello"
    QCOMPARE(result.matches.size(), std::size_t(1));

    // Clean up
    workerThread.quit();
    workerThread.wait();
}

void DebounceTest::testGenerationCounterIncrementsCorrectly() {
    // Verify that each debounce fire increments the generation counter,
    // and that multiple separate search bursts produce correct generations.

    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice"),
        JsonNode::makeString("city", "Boston")
    });

    uint64_t generation = 0;
    QString currentText;

    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
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

    // First burst: type "Al" rapidly
    currentText = "A";
    debounceTimer.start();
    QTest::qWait(20);
    currentText = "Al";
    debounceTimer.start();

    // Wait for first debounce to fire
    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(completeSpy.at(0).at(1).value<uint64_t>(), uint64_t(1));

    // Second burst: type "Bo" rapidly
    currentText = "B";
    debounceTimer.start();
    QTest::qWait(20);
    currentText = "Bo";
    debounceTimer.start();

    // Wait for second debounce to fire
    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 2);
    QCOMPARE(completeSpy.at(1).at(1).value<uint64_t>(), uint64_t(2));

    // Third burst: type "name"
    currentText = "name";
    debounceTimer.start();

    // Wait for third debounce to fire
    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 3);
    QCOMPARE(completeSpy.at(2).at(1).value<uint64_t>(), uint64_t(3));

    // Clean up
    workerThread.quit();
    workerThread.wait();
}

void DebounceTest::testEmptyTextClearsImmediatelyNoDebounce() {
    // Verify that when text becomes empty, the debounce timer is stopped
    // and no search is dispatched (clear happens immediately).

    QTimer debounceTimer;
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(250);

    QThread workerThread;
    SearchWorker worker;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy completeSpy(&worker, &SearchWorker::searchComplete);
    QVERIFY(completeSpy.isValid());

    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("name", "Alice")
    });

    uint64_t generation = 0;
    QString currentText;

    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
        if (currentText.isEmpty()) {
            return; // Should not happen — timer should be stopped
        }
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

    // Type some text to start debounce
    currentText = "A";
    debounceTimer.start();
    QTest::qWait(20);

    // Now clear the text — should stop the timer immediately
    currentText = "";
    debounceTimer.stop(); // Mimics MainWindow::onSearchTextChanged with empty text

    // Wait past the debounce interval
    QTest::qWait(400);
    QCoreApplication::processEvents();

    // No search should have been executed
    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(generation, uint64_t(0));

    // Clean up
    workerThread.quit();
    workerThread.wait();
}

QTEST_MAIN(DebounceTest)
#include "debounce_tests.moc"
