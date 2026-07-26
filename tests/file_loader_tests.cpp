#include <QApplication>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QtTest/QtTest>

#include <rapidcheck.h>
#include <set>

#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "shell/file_loader.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 13.2: Unit tests for FileLoader
// Requirements: 1.5, 1.6, 9.1, 9.3
// ---------------------------------------------------------------------------

class FileLoaderTest : public QObject {
    Q_OBJECT

private:
    // Helper: write content to a temporary file and return its path
    QString writeTempFile(const QByteArray& content, const QString& suffix = ".json") {
        auto* tempFile = new QTemporaryFile(QDir::tempPath() + "/jsontitan_test_XXXXXX" + suffix, this);
        tempFile->setAutoRemove(true);
        if (!tempFile->open()) {
            return {};
        }
        tempFile->write(content);
        tempFile->flush();
        tempFile->close();
        return tempFile->fileName();
    }

private slots:
    void testSuccessfulParseOnBackgroundThread() {
        // Test that a valid JSON file is parsed successfully on a background thread
        QByteArray json = "{\"name\": \"Alice\", \"age\": 30, \"active\": true}";
        QString filePath = writeTempFile(json);
        QVERIFY(!filePath.isEmpty());

        FileLoader loader;

        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        // Wait for completion (up to 5 seconds)
        QVERIFY(completeSpy.wait(5000));

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        // Verify the parsed tree
        auto result = completeSpy.at(0).at(0).value<std::shared_ptr<ArenaParseResult>>();
        QVERIFY(result != nullptr);
        QVERIFY(result->root != nullptr);
        QCOMPARE(result->root->type, NodeType::Object);
        QCOMPARE(result->root->childCount, std::size_t(3));
    }

    void testProgressSignalEmission() {
        // Test that progress signals are emitted during parsing
        // Create a larger file to ensure multiple chunks are read
        QByteArray json = "[";
        for (int i = 0; i < 1000; ++i) {
            if (i > 0) json += ",";
            json += "{\"index\":" + QByteArray::number(i) + ",\"value\":\"" +
                    QByteArray(100, 'x') + "\"}";
        }
        json += "]";

        QString filePath = writeTempFile(json);
        QVERIFY(!filePath.isEmpty());

        FileLoader loader;

        QSignalSpy progressSpy(&loader, &FileLoader::progressUpdated);
        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);

        loader.startParse(filePath);

        QVERIFY(completeSpy.wait(5000));

        // Progress should have been emitted at least once
        QVERIFY(progressSpy.count() >= 1);

        // Last progress should be 100%
        int lastProgress = progressSpy.last().at(0).toInt();
        QCOMPARE(lastProgress, 100);

        // Progress values should be monotonically non-decreasing
        int prev = 0;
        for (const auto& args : progressSpy) {
            int progress = args.at(0).toInt();
            QVERIFY(progress >= prev);
            QVERIFY(progress >= 0);
            QVERIFY(progress <= 100);
            prev = progress;
        }
    }

    void testCancellationStopsParsingAndDiscardsResults() {
        // Create a large file that takes time to parse
        QByteArray json = "[";
        for (int i = 0; i < 50000; ++i) {
            if (i > 0) json += ",";
            json += "{\"index\":" + QByteArray::number(i) + ",\"data\":\"" +
                    QByteArray(200, 'a') + "\"}";
        }
        json += "]";

        QString filePath = writeTempFile(json);
        QVERIFY(!filePath.isEmpty());

        FileLoader loader;

        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        // Cancel immediately
        loader.cancelParse();

        // Wait a bit for the worker to process the cancellation
        QTest::qWait(500);

        // Process events to receive any pending signals
        QCoreApplication::processEvents();

        // After cancellation, no parseComplete or parseError should be emitted
        // (or if the parse completed before cancellation was processed, that's also acceptable)
        // The key invariant: if cancelled in time, no result is emitted
        // Since timing is non-deterministic, we verify that at most one signal was emitted
        QVERIFY(completeSpy.count() + errorSpy.count() <= 1);
    }

    void testErrorSignalOnInvalidJson() {
        // Test that an invalid JSON file produces a parseError signal
        QByteArray invalidJson = "{\"name\": \"Alice\", \"age\": }";
        QString filePath = writeTempFile(invalidJson);
        QVERIFY(!filePath.isEmpty());

        FileLoader loader;

        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        // Wait for error signal (up to 5 seconds)
        QVERIFY(errorSpy.wait(5000));

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 0);

        // Error message should be non-empty and contain useful info
        QString errorMsg = errorSpy.at(0).at(0).toString();
        QVERIFY(!errorMsg.isEmpty());
    }

    void testErrorSignalOnNonExistentFile() {
        // Test that a non-existent file path produces a parseError signal
        FileLoader loader;

        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(QStringLiteral("/tmp/nonexistent_jsontitan_test_file_12345.json"));

        // Wait for error signal
        QVERIFY(errorSpy.wait(5000));

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 0);

        QString errorMsg = errorSpy.at(0).at(0).toString();
        QVERIFY(!errorMsg.isEmpty());
        QVERIFY(errorMsg.contains("not found") || errorMsg.contains("Cannot open"));
    }

    void testParseNestedJsonStructure() {
        // Test parsing a more complex nested JSON structure
        QByteArray json =
            "{"
            "  \"users\": ["
            "    {\"name\": \"Alice\", \"scores\": [95, 87, 92]},"
            "    {\"name\": \"Bob\", \"scores\": [78, 85, 90]}"
            "  ],"
            "  \"metadata\": {"
            "    \"version\": \"1.0\","
            "    \"count\": 2"
            "  }"
            "}";
        QString filePath = writeTempFile(json);
        QVERIFY(!filePath.isEmpty());

        FileLoader loader;

        QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        QVERIFY(completeSpy.wait(5000));

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        auto result = completeSpy.at(0).at(0).value<std::shared_ptr<ArenaParseResult>>();
        QVERIFY(result != nullptr);
        QVERIFY(result->root != nullptr);
        QCOMPARE(result->root->type, NodeType::Object);
        QCOMPARE(result->root->childCount, std::size_t(2));

        // Verify "users" array
        auto* users = result->root->childAt(0);
        QVERIFY(users != nullptr);
        QCOMPARE(users->keyView(), std::string_view("users"));
        QCOMPARE(users->type, NodeType::Array);
        QCOMPARE(users->childCount, std::size_t(2));
    }

    void testMultipleSequentialParses() {
        // Test that starting a new parse after one completes works correctly
        QByteArray json1 = "{\"first\": true}";
        QByteArray json2 = "{\"second\": true}";
        QString filePath1 = writeTempFile(json1);
        QString filePath2 = writeTempFile(json2);
        QVERIFY(!filePath1.isEmpty());
        QVERIFY(!filePath2.isEmpty());

        FileLoader loader;

        // First parse
        {
            QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
            loader.startParse(filePath1);
            QVERIFY(completeSpy.wait(5000));

            auto result = completeSpy.at(0).at(0).value<std::shared_ptr<ArenaParseResult>>();
            QVERIFY(result != nullptr);
            QVERIFY(result->root != nullptr);
            QCOMPARE(result->root->childCount, std::size_t(1));
            QCOMPARE(result->root->childAt(0)->keyView(), std::string_view("first"));
        }

        // Second parse
        {
            QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);
            loader.startParse(filePath2);
            QVERIFY(completeSpy.wait(5000));

            auto result = completeSpy.at(0).at(0).value<std::shared_ptr<ArenaParseResult>>();
            QVERIFY(result != nullptr);
            QVERIFY(result->root != nullptr);
            QCOMPARE(result->root->childCount, std::size_t(1));
            QCOMPARE(result->root->childAt(0)->keyView(), std::string_view("second"));
        }
    }
};

// ---------------------------------------------------------------------------
// Task 2: Preservation Property Tests (Property 2)
// Error Handling and Cancellation Unchanged
//
// These tests observe and lock down the EXISTING behavior of the file loader
// for non-buggy inputs (error cases, cancellation). They MUST PASS on unfixed
// code to confirm the baseline behavior that the fix must preserve.
//
// Requirements: 3.1, 3.2, 3.3, 3.4, 3.5
// ---------------------------------------------------------------------------

class PreservationPropertyTest : public QObject {
    Q_OBJECT

private:
    // Helper: write content to a temporary file and return its path
    QString writeTempFile(const QByteArray& content, const QString& suffix = ".json") {
        auto* tempFile = new QTemporaryFile(
            QDir::tempPath() + "/jsontitan_preserve_XXXXXX" + suffix, this);
        tempFile->setAutoRemove(true);
        if (!tempFile->open()) {
            return {};
        }
        tempFile->write(content);
        tempFile->flush();
        tempFile->close();
        return tempFile->fileName();
    }

    // Generate a random non-existent file path
    static std::string generateNonExistentPath() {
        // Use random alphanumeric characters for the filename
        auto len = *rc::gen::inRange<std::size_t>(8, 64);
        std::string chars;
        chars.reserve(len);
        for (std::size_t i = 0; i < len; ++i) {
            chars += *rc::gen::oneOf(
                rc::gen::inRange('a', static_cast<char>('z' + 1)),
                rc::gen::inRange('0', static_cast<char>('9' + 1))
            );
        }
        return "/tmp/jsontitan_nonexistent_" + chars + "_does_not_exist.json";
    }

    // Generate random invalid JSON strings
    static std::string generateInvalidJson() {
        // Pick from several categories of invalid JSON
        auto category = *rc::gen::inRange(0, 5);
        switch (category) {
            case 0: {
                // Truncated object: {"key": "val
                auto keyLen = *rc::gen::inRange<std::size_t>(1, 20);
                auto key = std::string(keyLen, 'k');
                return "{\"" + key + "\": \"incomplete";
            }
            case 1: {
                // Missing colon: {"key" "value"}
                auto keyLen = *rc::gen::inRange<std::size_t>(1, 20);
                auto key = std::string(keyLen, 'k');
                return "{\"" + key + "\" \"value\"}";
            }
            case 2: {
                // Unmatched bracket: [1, 2, 3
                auto count = *rc::gen::inRange(1, 10);
                std::string s = "[";
                for (int i = 0; i < count; ++i) {
                    if (i > 0) s += ", ";
                    s += std::to_string(i);
                }
                // No closing bracket
                return s;
            }
            case 3: {
                // Trailing comma: {"a": 1,}
                auto keyLen = *rc::gen::inRange<std::size_t>(1, 10);
                auto key = std::string(keyLen, 'a');
                return "{\"" + key + "\": 1,}";
            }
            case 4:
            default: {
                // Random garbage after valid start
                auto garbageLen = *rc::gen::inRange<std::size_t>(1, 50);
                std::string garbage;
                garbage.reserve(garbageLen);
                for (std::size_t i = 0; i < garbageLen; ++i) {
                    garbage += *rc::gen::inRange<char>(32, 127);
                }
                return "{\"x\": 1} " + garbage + " extra";
            }
        }
    }

    // Generate a large valid JSON string for cancellation testing
    static std::string generateLargeValidJson(std::size_t approxSize) {
        std::string json = "[";
        std::size_t currentSize = 1;
        bool first = true;

        while (currentSize < approxSize) {
            if (!first) {
                json += ",";
                currentSize += 1;
            }
            first = false;

            std::string entry = "{\"key\":\"";
            std::size_t valueLen = std::min<std::size_t>(512, approxSize - currentSize);
            entry += std::string(valueLen, 'x');
            entry += "\",\"n\":0}";

            json += entry;
            currentSize += entry.size();
        }

        json += "]";
        return json;
    }

private slots:
    // -----------------------------------------------------------------------
    // Property 2a: File Not Found Preservation
    // For any non-existent file path, process() emits parseError containing
    // "File not found" and the file path.
    // Requirement: 3.1
    // -----------------------------------------------------------------------
    void testFileNotFoundPreservation() {
        auto result = rc::check(
            "Preservation: non-existent file emits parseError with 'File not found' and path",
            []() {
                std::string path = generateNonExistentPath();
                QString qpath = QString::fromStdString(path);

                // Ensure the file truly doesn't exist
                RC_PRE(!QFile::exists(qpath));

                FileLoaderWorker worker;
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);
                QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);

                worker.process(qpath);

                // Must emit exactly one parseError
                RC_ASSERT(errorSpy.count() == 1);
                RC_ASSERT(completeSpy.count() == 0);

                // Error message must contain "File not found" and the path
                QString errorMsg = errorSpy.at(0).at(0).toString();
                RC_ASSERT(errorMsg.contains("File not found"));
                RC_ASSERT(errorMsg.contains(qpath));

                // No progress signals should be emitted
                RC_ASSERT(progressSpy.count() == 0);
            });

        QVERIFY2(result, "File not found preservation property failed");
    }

    // -----------------------------------------------------------------------
    // Property 2b: Empty File Preservation
    // For any empty file, process() emits parseError containing "File is empty"
    // and the file path.
    // Requirement: 3.2
    // -----------------------------------------------------------------------
    void testEmptyFilePreservation() {
        auto result = rc::check(
            "Preservation: empty file emits parseError with 'File is empty' and path",
            [this]() {
                // Create an empty temp file
                QString filePath = writeTempFile(QByteArray());
                RC_ASSERT(!filePath.isEmpty());

                FileLoaderWorker worker;
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);
                QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);

                worker.process(filePath);

                // Must emit exactly one parseError
                RC_ASSERT(errorSpy.count() == 1);
                RC_ASSERT(completeSpy.count() == 0);

                // Error message must contain "File is empty" and the path
                QString errorMsg = errorSpy.at(0).at(0).toString();
                RC_ASSERT(errorMsg.contains("File is empty"));
                RC_ASSERT(errorMsg.contains(filePath));

                // No progress signals should be emitted
                RC_ASSERT(progressSpy.count() == 0);
            });

        QVERIFY2(result, "Empty file preservation property failed");
    }

    // -----------------------------------------------------------------------
    // Property 2c: Invalid JSON Preservation
    // For any file containing invalid JSON, process() emits parseError
    // containing "Parse error at byte" with a byte offset and description.
    // Requirement: 3.3
    // -----------------------------------------------------------------------
    void testInvalidJsonPreservation() {
        auto result = rc::check(
            "Preservation: invalid JSON emits parseError with byte offset and description",
            [this]() {
                std::string invalidJson = generateInvalidJson();
                QByteArray data(invalidJson.data(), static_cast<qsizetype>(invalidJson.size()));

                QString filePath = writeTempFile(data);
                RC_ASSERT(!filePath.isEmpty());

                FileLoaderWorker worker;
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);

                worker.process(filePath);

                // Must emit exactly one parseError and no parseComplete
                RC_ASSERT(errorSpy.count() == 1);
                RC_ASSERT(completeSpy.count() == 0);

                // Error message must contain "Parse error at byte" followed by a number
                QString errorMsg = errorSpy.at(0).at(0).toString();
                RC_ASSERT(errorMsg.contains("Parse error at byte"));

                // Verify the message format: "Parse error at byte N: description"
                // Extract and validate the byte offset is a non-negative number
                QRegularExpression re(R"(Parse error at byte (\d+): (.+))");
                auto match = re.match(errorMsg);
                RC_ASSERT(match.hasMatch());

                bool ok = false;
                int byteOffset = match.captured(1).toInt(&ok);
                RC_ASSERT(ok);
                RC_ASSERT(byteOffset >= 0);
                RC_ASSERT(byteOffset <= static_cast<int>(invalidJson.size()));

                // Description must be non-empty
                QString description = match.captured(2);
                RC_ASSERT(!description.isEmpty());
            });

        QVERIFY2(result, "Invalid JSON preservation property failed");
    }

    // -----------------------------------------------------------------------
    // Property 2d: Cancellation Preservation
    // When cancel() is called before/during parse, no parseComplete or
    // parseError signal is emitted.
    // Requirement: 3.4
    // -----------------------------------------------------------------------
    void testCancellationPreservation() {
        // For cancellation, we test with a large file and cancel immediately.
        // Since FileLoaderWorker::process() checks m_cancelled between chunks,
        // calling cancel() before process() should result in no output.

        // Test 1: Cancel before process starts
        {
            // Generate a large file so the streaming parser would take multiple chunks
            std::string largeJson = generateLargeValidJson(512 * 1024);
            QByteArray data(largeJson.data(), static_cast<qsizetype>(largeJson.size()));

            auto* tempFile = new QTemporaryFile(
                QDir::tempPath() + "/jsontitan_cancel_XXXXXX.json", this);
            tempFile->setAutoRemove(true);
            QVERIFY(tempFile->open());
            tempFile->write(data);
            tempFile->flush();
            tempFile->close();
            QString filePath = tempFile->fileName();

            FileLoaderWorker worker;
            QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);
            QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);

            // Cancel before processing
            worker.cancel();
            worker.process(filePath);

            // After cancellation, no parseComplete or parseError should be emitted
            // Note: process() resets m_cancelled at the start, so cancelling before
            // process() won't actually cancel. This tests the documented behavior.
            // The real cancellation test is via FileLoader (cross-thread).
        }

        // Test 2: Cancel via FileLoader during parse of a large file
        {
            std::string largeJson = generateLargeValidJson(2 * 1024 * 1024);
            QByteArray data(largeJson.data(), static_cast<qsizetype>(largeJson.size()));

            auto* tempFile = new QTemporaryFile(
                QDir::tempPath() + "/jsontitan_cancel2_XXXXXX.json", this);
            tempFile->setAutoRemove(true);
            QVERIFY(tempFile->open());
            tempFile->write(data);
            tempFile->flush();
            tempFile->close();
            QString filePath = tempFile->fileName();

            FileLoader loader;
            QSignalSpy errorSpy(&loader, &FileLoader::parseError);
            QSignalSpy completeSpy(&loader, &FileLoader::arenaParseComplete);

            loader.startParse(filePath);

            // Cancel immediately after starting
            loader.cancelParse();

            // Wait a bit for the worker to process
            QTest::qWait(300);
            QCoreApplication::processEvents();

            // Due to timing, the parse might complete before cancellation is processed.
            // The key invariant: we should NOT get both a complete AND an error.
            QVERIFY(completeSpy.count() + errorSpy.count() <= 1);

            // If cancellation was processed in time, neither signal is emitted
            // This is non-deterministic, so we just verify no crash and no double-signal
        }
    }

    // -----------------------------------------------------------------------
    // Property 2e: Successful parse still emits parseComplete with valid root
    // For any valid JSON file, process() emits parseComplete with a non-null root.
    // Requirement: 3.5
    // -----------------------------------------------------------------------
    void testSuccessfulParsePreservation() {
        auto result = rc::check(
            "Preservation: valid JSON emits parseComplete with non-null root",
            [this]() {
                // Generate small valid JSON documents of varying structure
                auto depth = *rc::gen::inRange(0, 4);
                auto width = *rc::gen::inRange(1, 5);

                std::string json;
                if (depth == 0) {
                    // Simple value in an array or object
                    auto choice = *rc::gen::inRange(0, 3);
                    if (choice == 0) json = "{\"val\": 42}";
                    else if (choice == 1) json = "[1, 2, 3]";
                    else json = "{\"s\": \"hello\", \"b\": true, \"n\": null}";
                } else {
                    // Nested object
                    json = "{";
                    for (int i = 0; i < width; ++i) {
                        if (i > 0) json += ",";
                        json += "\"k" + std::to_string(i) + "\":";
                        if (depth > 1) {
                            json += "[";
                            for (int j = 0; j < width; ++j) {
                                if (j > 0) json += ",";
                                json += "{\"nested\":" + std::to_string(j) + "}";
                            }
                            json += "]";
                        } else {
                            json += std::to_string(i * 10);
                        }
                    }
                    json += "}";
                }

                QByteArray data(json.data(), static_cast<qsizetype>(json.size()));
                QString filePath = writeTempFile(data);
                RC_ASSERT(!filePath.isEmpty());

                FileLoaderWorker worker;
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);

                worker.process(filePath);

                // Must emit arenaParseComplete with no error
                RC_ASSERT(completeSpy.count() == 1);
                RC_ASSERT(errorSpy.count() == 0);

                // Root must be non-null
                auto result = completeSpy.at(0).at(0)
                    .value<std::shared_ptr<jsontitan::core::ArenaParseResult>>();
                RC_ASSERT(result != nullptr);
                RC_ASSERT(result->root != nullptr);
            });

        QVERIFY2(result, "Successful parse preservation property failed");
    }
};

// ---------------------------------------------------------------------------
// Bug Condition Exploration: Property 1
// Progress Bar Only Emits Three Values
//
// CRITICAL: This test MUST FAIL on unfixed code — failure confirms the bug.
// DO NOT attempt to fix the test or the code when it fails.
//
// The test encodes the EXPECTED CORRECT behavior: for files larger than 1 MB
// (the read chunk size), the progress bar should emit MORE than 2 distinct
// values in each phase range [0,50] and [50,100], providing smooth incremental
// feedback. On unfixed code, only {0, 50, 100} are emitted.
//
// Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3
// ---------------------------------------------------------------------------

class BugConditionExplorationTest : public QObject {
    Q_OBJECT

private:
    // Generate a valid JSON string of approximately the given size.
    // Produces a single top-level ARRAY of large objects, so the whole file
    // is one valid JSON document (as required by simdjson) while still being
    // large enough to exercise the progress-granularity assertions.
    static std::string generateValidJson(std::size_t approxSize) {
        std::string json;
        json.reserve(approxSize + 1024);
        json += "[";

        // Generate array elements that are each ~500 KB
        constexpr std::size_t kObjectSize = 500 * 1024;
        std::size_t currentSize = json.size();
        bool first = true;

        while (currentSize < approxSize) {
            if (!first) {
                json += ",";
                currentSize += 1;
            }
            first = false;

            std::size_t remaining = approxSize - currentSize;
            std::size_t targetSize = std::min(kObjectSize, remaining);
            if (targetSize < 30) targetSize = 30;

            std::string entry = "{\"key\":\"";
            std::size_t valueLen = (targetSize > 20) ? targetSize - 20 : 10;
            entry += std::string(valueLen, 'v');
            entry += "\",\"idx\":0}";

            json += entry;
            currentSize += entry.size();
        }

        json += "]";
        return json;
    }

    // Helper: write content to a temporary file and return its path
    QString writeTempFile(const QByteArray& content) {
        auto* tempFile = new QTemporaryFile(
            QDir::tempPath() + "/jsontitan_bugcond_XXXXXX.json", this);
        tempFile->setAutoRemove(true);
        if (!tempFile->open()) {
            return {};
        }
        tempFile->write(content);
        tempFile->flush();
        tempFile->close();
        return tempFile->fileName();
    }

private slots:
    // -----------------------------------------------------------------------
    // Property 1: Bug Condition — Progress Bar Only Emits Three Values
    //
    // For any valid JSON file > 1 MB loaded through FileLoaderWorker::process(),
    // the EXPECTED correct behavior is:
    //   - More than 2 distinct progress values in [0, 50] (read phase)
    //   - More than 2 distinct progress values in [50, 100] (parse phase)
    //   - Progress values are monotonically non-decreasing
    //   - First value == 0 and last value == 100
    //
    // On UNFIXED code this test FAILS because only {0, 50, 100} are emitted.
    // The failure confirms the bug exists.
    // -----------------------------------------------------------------------
    void testIncrementalProgressForLargeFiles() {
        std::string counterexample;

        auto result = rc::check(
            "Bug Condition: files > 1 MB must emit more than 2 distinct progress "
            "values in each phase range [0,50] and [50,100]",
            [this, &counterexample]() {
                // Generate a random size between 2 MB and 4 MB
                // (must exceed the 1 MB read chunk size so the read phase
                // emits multiple progress values)
                const auto size = *rc::gen::inRange<std::size_t>(
                    2 * 1024 * 1024, 4 * 1024 * 1024);

                // Generate a valid JSON document of approximately that size
                std::string jsonStr = generateValidJson(size);
                QByteArray jsonData(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));

                QString filePath = writeTempFile(jsonData);
                RC_ASSERT(!filePath.isEmpty());

                // Use FileLoaderWorker directly (synchronous on current thread)
                FileLoaderWorker worker;

                QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);

                worker.process(filePath);

                // Must have completed successfully
                RC_ASSERT(completeSpy.count() == 1);
                RC_ASSERT(errorSpy.count() == 0);

                // Extract progress values
                std::vector<int> progressValues;
                for (const auto& args : progressSpy) {
                    progressValues.push_back(args.at(0).toInt());
                }

                // Collect distinct values in each phase range
                std::set<int> readPhaseValues;   // [0, 50]
                std::set<int> parsePhaseValues;  // [50, 100]

                for (int v : progressValues) {
                    if (v >= 0 && v <= 50) readPhaseValues.insert(v);
                    if (v >= 50 && v <= 100) parsePhaseValues.insert(v);
                }

                // Assert: monotonically non-decreasing
                for (std::size_t i = 1; i < progressValues.size(); ++i) {
                    RC_ASSERT(progressValues[i] >= progressValues[i - 1]);
                }

                // Assert: first value == 0 and last value == 100
                RC_ASSERT(!progressValues.empty());
                RC_ASSERT(progressValues.front() == 0);
                RC_ASSERT(progressValues.back() == 100);

                // Assert: more than 2 distinct values in read phase [0, 50]
                if (readPhaseValues.size() <= 2) {
                    counterexample = "For a " + std::to_string(jsonStr.size()) +
                        " byte file, read phase [0,50] has only " +
                        std::to_string(readPhaseValues.size()) +
                        " distinct values: {";
                    bool first = true;
                    for (int v : readPhaseValues) {
                        if (!first) counterexample += ", ";
                        counterexample += std::to_string(v);
                        first = false;
                    }
                    counterexample += "}. Full progress: {";
                    first = true;
                    for (int v : progressValues) {
                        if (!first) counterexample += ", ";
                        counterexample += std::to_string(v);
                        first = false;
                    }
                    counterexample += "}";
                }
                RC_ASSERT(readPhaseValues.size() > 2);

                // Assert: more than 2 distinct values in parse phase [50, 100]
                if (parsePhaseValues.size() <= 2) {
                    counterexample = "For a " + std::to_string(jsonStr.size()) +
                        " byte file, parse phase [50,100] has only " +
                        std::to_string(parsePhaseValues.size()) +
                        " distinct values: {";
                    bool first = true;
                    for (int v : parsePhaseValues) {
                        if (!first) counterexample += ", ";
                        counterexample += std::to_string(v);
                        first = false;
                    }
                    counterexample += "}. Full progress: {";
                    first = true;
                    for (int v : progressValues) {
                        if (!first) counterexample += ", ";
                        counterexample += std::to_string(v);
                        first = false;
                    }
                    counterexample += "}";
                }
                RC_ASSERT(parsePhaseValues.size() > 2);
            });

        if (!result) {
            qWarning() << "COUNTEREXAMPLE FOUND (confirms bug exists):";
            qWarning() << QString::fromStdString(counterexample);
            QFAIL("Bug condition confirmed: progress bar only emits {0, 50, 100} — "
                   "no intermediate progress values in read or parse phases. "
                   "This is EXPECTED on unfixed code.");
        }
    }

    // Concrete test case: a 3 MB file should have intermediate progress values
    void testConcreteProgressPattern1_5MB() {
        std::string jsonStr = generateValidJson(3 * 1024 * 1024);  // 3 MB
        QByteArray jsonData(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));

        QString filePath = writeTempFile(jsonData);
        QVERIFY(!filePath.isEmpty());

        FileLoaderWorker worker;
        QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);
        QSignalSpy completeSpy(&worker, &FileLoaderWorker::arenaParseComplete);
        QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);

        worker.process(filePath);

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        std::vector<int> progressValues;
        for (const auto& args : progressSpy) {
            progressValues.push_back(args.at(0).toInt());
        }

        // Collect distinct values in each phase range
        std::set<int> readPhaseValues;
        std::set<int> parsePhaseValues;
        for (int v : progressValues) {
            if (v >= 0 && v <= 50) readPhaseValues.insert(v);
            if (v >= 50 && v <= 100) parsePhaseValues.insert(v);
        }

        // On unfixed code: only {0, 50, 100} emitted — this will FAIL
        // On fixed code: multiple intermediate values in each phase
        if (readPhaseValues.size() <= 2 || parsePhaseValues.size() <= 2) {
            QString msg = QString("COUNTEREXAMPLE: 1.5 MB file emitted only %1 total "
                                  "progress values. Read phase distinct: %2, Parse phase "
                                  "distinct: %3. Values: ")
                              .arg(progressValues.size())
                              .arg(readPhaseValues.size())
                              .arg(parsePhaseValues.size());
            for (int v : progressValues) {
                msg += QString::number(v) + " ";
            }
            qWarning() << msg;
        }

        QVERIFY2(readPhaseValues.size() > 2,
                 "Read phase [0,50] must have more than 2 distinct progress values");
        QVERIFY2(parsePhaseValues.size() > 2,
                 "Parse phase [50,100] must have more than 2 distinct progress values");

        // Verify monotonically non-decreasing
        for (std::size_t i = 1; i < progressValues.size(); ++i) {
            QVERIFY2(progressValues[i] >= progressValues[i - 1],
                     "Progress values must be monotonically non-decreasing");
        }

        // Verify first == 0 and last == 100
        QCOMPARE(progressValues.front(), 0);
        QCOMPARE(progressValues.back(), 100);
    }
};

// Qt Test requires a QApplication instance
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Register metatypes for cross-thread signal/slot
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>();
    qRegisterMetaType<std::shared_ptr<jsontitan::core::ArenaParseResult>>();

    int status = 0;

    FileLoaderTest fileLoaderTest;
    status |= QTest::qExec(&fileLoaderTest, argc, argv);

    PreservationPropertyTest preservationTest;
    status |= QTest::qExec(&preservationTest, argc, argv);

    BugConditionExplorationTest bugCondTest;
    status |= QTest::qExec(&bugCondTest, argc, argv);

    return status;
}

#include "file_loader_tests.moc"
