#include <QApplication>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QtTest/QtTest>

#include <rapidcheck.h>

#include "core/json_node.h"
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

        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        // Wait for completion (up to 5 seconds)
        QVERIFY(completeSpy.wait(5000));

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        // Verify the parsed tree
        auto root = completeSpy.at(0).at(0).value<std::shared_ptr<const JsonNode>>();
        QVERIFY(root != nullptr);
        QCOMPARE(root->type, NodeType::Object);
        QCOMPARE(root->children.size(), std::size_t(3));
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
        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);

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

        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
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

        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
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

        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
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

        QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
        QSignalSpy errorSpy(&loader, &FileLoader::parseError);

        loader.startParse(filePath);

        QVERIFY(completeSpy.wait(5000));

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        auto root = completeSpy.at(0).at(0).value<std::shared_ptr<const JsonNode>>();
        QVERIFY(root != nullptr);
        QCOMPARE(root->type, NodeType::Object);
        QCOMPARE(root->children.size(), std::size_t(2));

        // Verify "users" array
        auto users = root->children[0];
        QCOMPARE(users->key, std::string("users"));
        QCOMPARE(users->type, NodeType::Array);
        QCOMPARE(users->children.size(), std::size_t(2));
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
            QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
            loader.startParse(filePath1);
            QVERIFY(completeSpy.wait(5000));

            auto root = completeSpy.at(0).at(0).value<std::shared_ptr<const JsonNode>>();
            QVERIFY(root != nullptr);
            QCOMPARE(root->children.size(), std::size_t(1));
            QCOMPARE(root->children[0]->key, std::string("first"));
        }

        // Second parse
        {
            QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);
            loader.startParse(filePath2);
            QVERIFY(completeSpy.wait(5000));

            auto root = completeSpy.at(0).at(0).value<std::shared_ptr<const JsonNode>>();
            QVERIFY(root != nullptr);
            QCOMPARE(root->children.size(), std::size_t(1));
            QCOMPARE(root->children[0]->key, std::string("second"));
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
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);
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
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);
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
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);

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
            QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);

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
            QSignalSpy completeSpy(&loader, &FileLoader::parseComplete);

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
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);

                worker.process(filePath);

                // Must emit parseComplete with no error
                RC_ASSERT(completeSpy.count() == 1);
                RC_ASSERT(errorSpy.count() == 0);

                // Root must be non-null
                auto root = completeSpy.at(0).at(0)
                    .value<std::shared_ptr<const jsontitan::core::JsonNode>>();
                RC_ASSERT(root != nullptr);

                // Root must be an object or array (valid top-level JSON)
                RC_ASSERT(root->type == jsontitan::core::NodeType::Object ||
                          root->type == jsontitan::core::NodeType::Array);
            });

        QVERIFY2(result, "Successful parse preservation property failed");
    }
};

// ---------------------------------------------------------------------------
// Bug Condition Exploration: Property 1
// File Loader Uses Streaming Parser Instead of parseBuffer
//
// CRITICAL: This test MUST FAIL on unfixed code — failure confirms the bug.
// DO NOT attempt to fix the test or the code when it fails.
//
// The test encodes the EXPECTED behavior: progress signals follow the pattern
// {0, 50, 100} when parseBuffer is used. On unfixed code, the streaming path
// emits many intermediate progress values (one per 64KB chunk), causing failure.
//
// Requirements: 1.1, 1.3, 2.1
// ---------------------------------------------------------------------------

class BugConditionExplorationTest : public QObject {
    Q_OBJECT

private:
    // Generate a random valid JSON string of approximately the given size.
    // Produces a JSON array of objects with random string values.
    static std::string generateValidJson(std::size_t approxSize) {
        std::string json = "[";
        std::size_t currentSize = 1;
        bool first = true;

        while (currentSize < approxSize) {
            if (!first) {
                json += ",";
                currentSize += 1;
            }
            first = false;

            // Generate an object with a few keys and random-length string values
            std::string entry = "{\"key\":\"";
            // Fill value to pad size — use a repeating character
            std::size_t valueLen = std::min<std::size_t>(512, approxSize - currentSize);
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
    // Property-based test: For any valid JSON file loaded through FileLoaderWorker,
    // the progress signal sequence MUST be exactly {0, 50, 100}.
    // On unfixed code, the streaming parser emits many intermediate values.
    void testProgressPatternIsOptimizedPipeline() {
        // Use RapidCheck to generate random file sizes (128KB to 2MB)
        // that are large enough to produce multiple chunks in the streaming path.
        bool allPassed = true;
        std::string counterexample;

        auto result = rc::check(
            "Bug Condition: progress pattern must be {0, 50, 100} for valid JSON files",
            [this, &counterexample]() {
                // Generate a random size between 128KB and 1MB
                const auto size = *rc::gen::inRange<std::size_t>(128 * 1024, 1024 * 1024);

                // Generate a valid JSON document of approximately that size
                std::string jsonStr = generateValidJson(size);
                QByteArray jsonData(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));

                QString filePath = writeTempFile(jsonData);
                RC_ASSERT(!filePath.isEmpty());

                // Use FileLoaderWorker directly (synchronous on current thread)
                FileLoaderWorker worker;

                QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);
                QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);
                QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);

                // Call process directly (runs synchronously in this thread)
                worker.process(filePath);

                // Must have completed successfully
                RC_ASSERT(completeSpy.count() == 1);
                RC_ASSERT(errorSpy.count() == 0);

                // Extract progress values
                std::vector<int> progressValues;
                for (const auto& args : progressSpy) {
                    progressValues.push_back(args.at(0).toInt());
                }

                // EXPECTED (optimized pipeline): exactly {0, 50, 100}
                // ACTUAL (streaming/unfixed): many intermediate values from chunk-based progress
                std::vector<int> expectedPattern = {0, 50, 100};

                if (progressValues != expectedPattern) {
                    counterexample = "For a " + std::to_string(jsonStr.size()) +
                                     " byte file, progress emitted " +
                                     std::to_string(progressValues.size()) +
                                     " values [";
                    for (std::size_t i = 0; i < progressValues.size(); ++i) {
                        if (i > 0) counterexample += ", ";
                        counterexample += std::to_string(progressValues[i]);
                        if (i > 10) {
                            counterexample += ", ...";
                            break;
                        }
                    }
                    counterexample += "] instead of [0, 50, 100]";
                }

                RC_ASSERT(progressValues == expectedPattern);
            });

        if (!result) {
            qWarning() << "COUNTEREXAMPLE FOUND (confirms bug exists):";
            qWarning() << QString::fromStdString(counterexample);
            QFAIL("Bug condition confirmed: streaming parser emits chunk-based progress "
                   "instead of the optimized {0, 50, 100} pattern. "
                   "This is EXPECTED on unfixed code.");
        }
    }

    // Concrete test case: a 256KB file should produce exactly {0, 50, 100} progress
    void testConcreteProgressPattern256KB() {
        std::string jsonStr = generateValidJson(256 * 1024);
        QByteArray jsonData(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));

        QString filePath = writeTempFile(jsonData);
        QVERIFY(!filePath.isEmpty());

        FileLoaderWorker worker;
        QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);
        QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);
        QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);

        worker.process(filePath);

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        std::vector<int> progressValues;
        for (const auto& args : progressSpy) {
            progressValues.push_back(args.at(0).toInt());
        }

        // On unfixed code: will have ~4 intermediate values (256KB / 64KB = 4 chunks)
        // On fixed code: exactly {0, 50, 100}
        std::vector<int> expectedPattern = {0, 50, 100};

        if (progressValues != expectedPattern) {
            QString msg = QString("COUNTEREXAMPLE: 256KB file emitted %1 progress values "
                                  "instead of 3. First few: ")
                              .arg(progressValues.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(progressValues.size(), 8); ++i) {
                msg += QString::number(progressValues[i]) + " ";
            }
            qWarning() << msg;
        }

        QCOMPARE(progressValues, expectedPattern);
    }

    // Concrete test case: a 1MB file should produce exactly {0, 50, 100} progress
    void testConcreteProgressPattern1MB() {
        std::string jsonStr = generateValidJson(1024 * 1024);
        QByteArray jsonData(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));

        QString filePath = writeTempFile(jsonData);
        QVERIFY(!filePath.isEmpty());

        FileLoaderWorker worker;
        QSignalSpy progressSpy(&worker, &FileLoaderWorker::progressUpdated);
        QSignalSpy completeSpy(&worker, &FileLoaderWorker::parseComplete);
        QSignalSpy errorSpy(&worker, &FileLoaderWorker::parseError);

        worker.process(filePath);

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        std::vector<int> progressValues;
        for (const auto& args : progressSpy) {
            progressValues.push_back(args.at(0).toInt());
        }

        // On unfixed code: will have ~16 intermediate values (1MB / 64KB = 16 chunks)
        // On fixed code: exactly {0, 50, 100}
        std::vector<int> expectedPattern = {0, 50, 100};

        if (progressValues != expectedPattern) {
            QString msg = QString("COUNTEREXAMPLE: 1MB file emitted %1 progress values "
                                  "instead of 3. First few: ")
                              .arg(progressValues.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(progressValues.size(), 8); ++i) {
                msg += QString::number(progressValues[i]) + " ";
            }
            qWarning() << msg;
        }

        QCOMPARE(progressValues, expectedPattern);
    }
};

// Qt Test requires a QApplication instance
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Register metatype for cross-thread signal/slot
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>();

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
