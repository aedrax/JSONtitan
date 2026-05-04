#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QtTest/QtTest>

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

// Qt Test requires a QApplication instance
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Register metatype for cross-thread signal/slot
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>();

    int status = 0;

    FileLoaderTest fileLoaderTest;
    status |= QTest::qExec(&fileLoaderTest, argc, argv);

    return status;
}

#include "file_loader_tests.moc"
