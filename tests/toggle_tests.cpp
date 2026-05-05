#include <QCoreApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QtTest>

#include "core/json_node.h"
#include "core/search_engine.h"
#include "shell/search_worker.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 5.3: Unit tests for toggle buttons
// Validates: Requirements 2.3, 2.4
// ---------------------------------------------------------------------------

class ToggleButtonTest : public QObject {
    Q_OBJECT

private slots:
    void testCaseSensitiveToggleSetsQueryCorrectly();
    void testRegexToggleSetsQueryModeCorrectly();
    void testRegexFallbackWithSlashConvention();
    void testTogglingCaseSensitiveRetriggersSearch();
    void testTogglingRegexRetriggersSearch();
    void testDefaultStatesMatchPreviousHardcodedBehavior();
    void testBothTogglesOnProducesCorrectQuery();
};

void ToggleButtonTest::testCaseSensitiveToggleSetsQueryCorrectly() {
    // Replicate the MainWindow logic for building a SearchQuery from toggle state.
    // When m_caseSensitiveToggle is checked, SearchQuery.caseSensitive == true.
    // When unchecked (default), SearchQuery.caseSensitive == false.

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(false);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    QString searchText = "Alice";

    // Default state: case-insensitive
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.caseSensitive, false);
    }

    // Toggle ON: case-sensitive
    caseSensitiveToggle.setChecked(true);
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.caseSensitive, true);
    }

    // Toggle OFF again: case-insensitive
    caseSensitiveToggle.setChecked(false);
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.caseSensitive, false);
    }
}

void ToggleButtonTest::testRegexToggleSetsQueryModeCorrectly() {
    // When m_regexToggle is checked, SearchQuery.mode == SearchMode::Regex.
    // When unchecked (default), SearchQuery.mode == SearchMode::Substring.

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(false);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    QString searchText = "hello";

    // Default state: substring mode
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.mode, SearchMode::Substring);
    }

    // Toggle ON: regex mode
    regexToggle.setChecked(true);
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.mode, SearchMode::Regex);
    }

    // Toggle OFF: back to substring mode
    regexToggle.setChecked(false);
    {
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();
        query.pattern = searchText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;

        QCOMPARE(query.mode, SearchMode::Substring);
    }
}

void ToggleButtonTest::testRegexFallbackWithSlashConvention() {
    // When regex toggle is OFF but /pattern/ convention is used,
    // mode should still be Regex (fallback for discoverability).
    // This replicates the logic in MainWindow::executeSearch().

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    QString searchText = "/hello.*/";

    // Replicate MainWindow::executeSearch logic
    SearchQuery query;
    query.caseSensitive = false;

    if (regexToggle.isChecked()) {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Regex;
    } else if (searchText.startsWith('/') && searchText.endsWith('/') && searchText.length() > 2) {
        // Fallback: /pattern/ convention
        query.pattern = searchText.mid(1, searchText.length() - 2).toStdString();
        query.mode = SearchMode::Regex;
    } else {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Substring;
    }

    QCOMPARE(query.mode, SearchMode::Regex);
    QCOMPARE(query.pattern, std::string("hello.*"));
}

void ToggleButtonTest::testTogglingCaseSensitiveRetriggersSearch() {
    // Verify that toggling the case-sensitive button restarts the debounce timer,
    // which re-triggers a search. This mimics the MainWindow connection:
    // connect(m_caseSensitiveToggle, &QToolButton::toggled, ..., [this]() {
    //     if (!m_searchBar->text().isEmpty()) m_debounceTimer->start();
    // });

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
        JsonNode::makeString("Name", "Alice"),
        JsonNode::makeString("name", "bob")
    });

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(false);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    QString currentText = "name";
    uint64_t generation = 0;

    // Connect timer to execute search (mimics MainWindow::executeSearch)
    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
        ++generation;
        SearchQuery query;
        query.pattern = currentText.toStdString();
        query.mode = regexToggle.isChecked() ? SearchMode::Regex : SearchMode::Substring;
        query.caseSensitive = caseSensitiveToggle.isChecked();

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, generation));
    });

    // Connect toggle to restart debounce (mimics MainWindow connection)
    connect(&caseSensitiveToggle, &QToolButton::toggled, this, [&]() {
        if (!currentText.isEmpty()) {
            debounceTimer.start();
        }
    });

    // Toggle the case-sensitive button ON — should trigger a search
    caseSensitiveToggle.setChecked(true);

    // Wait for debounce to fire + worker to complete
    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 1);

    auto args = completeSpy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());

    // With caseSensitive=true, "name" should match key "name" but not "Name"
    // (depends on search engine implementation — we verify the query was built correctly)
    QCOMPARE(generation, uint64_t(1));

    // Toggle OFF — should trigger another search
    caseSensitiveToggle.setChecked(false);

    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 1); // one more result
    QCOMPARE(generation, uint64_t(2));

    workerThread.quit();
    workerThread.wait();
}

void ToggleButtonTest::testTogglingRegexRetriggersSearch() {
    // Verify that toggling the regex button restarts the debounce timer.

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
        JsonNode::makeString("greeting", "hello world"),
        JsonNode::makeString("farewell", "goodbye")
    });

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(false);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    QString currentText = "hello.*";
    uint64_t generation = 0;

    connect(&debounceTimer, &QTimer::timeout, this, [&]() {
        ++generation;
        SearchQuery query;
        query.caseSensitive = caseSensitiveToggle.isChecked();

        if (regexToggle.isChecked()) {
            query.pattern = currentText.toStdString();
            query.mode = SearchMode::Regex;
        } else if (currentText.startsWith('/') && currentText.endsWith('/') && currentText.length() > 2) {
            query.pattern = currentText.mid(1, currentText.length() - 2).toStdString();
            query.mode = SearchMode::Regex;
        } else {
            query.pattern = currentText.toStdString();
            query.mode = SearchMode::Substring;
        }

        QMetaObject::invokeMethod(&worker, "executeSearch",
                                  Qt::QueuedConnection,
                                  Q_ARG(jsontitan::core::SearchQuery, query),
                                  Q_ARG(std::shared_ptr<const jsontitan::core::JsonNode>, root),
                                  Q_ARG(uint64_t, generation));
    });

    // Connect toggle to restart debounce
    connect(&regexToggle, &QToolButton::toggled, this, [&]() {
        if (!currentText.isEmpty()) {
            debounceTimer.start();
        }
    });

    // Toggle regex ON — should trigger a search with Regex mode
    regexToggle.setChecked(true);

    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(generation, uint64_t(1));

    // Verify the search used Regex mode (hello.* should match "hello world")
    auto args = completeSpy.takeFirst();
    auto result = args.at(0).value<FilterResult>();
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.matches.size(), std::size_t(1));

    // Toggle regex OFF — should trigger another search with Substring mode
    regexToggle.setChecked(false);

    QTest::qWait(400);
    QCoreApplication::processEvents();

    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(generation, uint64_t(2));

    // With substring mode, "hello.*" is a literal substring — won't match "hello world"
    auto args2 = completeSpy.takeFirst();
    auto result2 = args2.at(0).value<FilterResult>();
    QVERIFY(!result2.error.has_value());
    QCOMPARE(result2.matches.size(), std::size_t(0));

    workerThread.quit();
    workerThread.wait();
}

void ToggleButtonTest::testDefaultStatesMatchPreviousHardcodedBehavior() {
    // The default state of both toggles (unchecked) should produce the same
    // SearchQuery as the previous hardcoded behavior:
    // - caseSensitive = false
    // - mode = SearchMode::Substring (for non-/pattern/ text)
    // This ensures no regression from the original behavior.

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(false);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(false);

    // Verify default states
    QCOMPARE(caseSensitiveToggle.isChecked(), false);
    QCOMPARE(regexToggle.isChecked(), false);

    // Build query with defaults — should match previous hardcoded behavior
    QString searchText = "test query";

    SearchQuery query;
    query.caseSensitive = caseSensitiveToggle.isChecked();

    if (regexToggle.isChecked()) {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Regex;
    } else if (searchText.startsWith('/') && searchText.endsWith('/') && searchText.length() > 2) {
        query.pattern = searchText.mid(1, searchText.length() - 2).toStdString();
        query.mode = SearchMode::Regex;
    } else {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Substring;
    }

    // Previous hardcoded behavior: caseSensitive=false, mode=Substring
    QCOMPARE(query.caseSensitive, false);
    QCOMPARE(query.mode, SearchMode::Substring);
    QCOMPARE(query.pattern, std::string("test query"));

    // Verify with actual search engine to confirm behavior matches
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("key", "Test Query"),
        JsonNode::makeString("other", "unrelated")
    });

    auto result = filter(*root, query);
    QVERIFY(!result.error.has_value());
    // Case-insensitive substring "test query" should match "Test Query"
    QCOMPARE(result.matches.size(), std::size_t(1));
}

void ToggleButtonTest::testBothTogglesOnProducesCorrectQuery() {
    // When both toggles are ON, the query should be case-sensitive regex.

    QToolButton caseSensitiveToggle;
    caseSensitiveToggle.setCheckable(true);
    caseSensitiveToggle.setChecked(true);

    QToolButton regexToggle;
    regexToggle.setCheckable(true);
    regexToggle.setChecked(true);

    QString searchText = "Hello.*";

    SearchQuery query;
    query.caseSensitive = caseSensitiveToggle.isChecked();

    if (regexToggle.isChecked()) {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Regex;
    } else if (searchText.startsWith('/') && searchText.endsWith('/') && searchText.length() > 2) {
        query.pattern = searchText.mid(1, searchText.length() - 2).toStdString();
        query.mode = SearchMode::Regex;
    } else {
        query.pattern = searchText.toStdString();
        query.mode = SearchMode::Substring;
    }

    QCOMPARE(query.caseSensitive, true);
    QCOMPARE(query.mode, SearchMode::Regex);
    QCOMPARE(query.pattern, std::string("Hello.*"));

    // Verify with actual search engine
    auto root = JsonNode::makeObject("root", {
        JsonNode::makeString("greeting", "Hello World"),
        JsonNode::makeString("other", "hello world")  // lowercase — should NOT match
    });

    auto result = filter(*root, query);
    QVERIFY(!result.error.has_value());
    // Case-sensitive regex "Hello.*" should match "Hello World" but not "hello world"
    QCOMPARE(result.matches.size(), std::size_t(1));
    QCOMPARE(result.matches[0].node->value, std::string("Hello World"));
}

QTEST_MAIN(ToggleButtonTest)
#include "toggle_tests.moc"
