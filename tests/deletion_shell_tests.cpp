#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/json_exporter.h"
#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "shell/export_handler.h"
#include "shell/save_handler.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Task 5.3: Unit tests for Save Handler
// Requirements: 5.2, 5.3
// ---------------------------------------------------------------------------

// Helper: build a simple JSON tree: {"name": "Alice", "age": 30}
static auto makeSimpleTree() -> std::shared_ptr<const JsonNode> {
    std::vector<std::shared_ptr<const JsonNode>> children;
    children.push_back(JsonNode::makeString("name", "Alice"));
    children.push_back(JsonNode::makeNumber("age", "30"));
    return JsonNode::makeObject("", std::move(children));
}

// Test 1: Successful atomic write — file is created with valid JSON content
TEST(SaveHandler, SuccessfulWrite) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString filePath = tempDir.path() + "/output.json";
    auto tree = makeSimpleTree();

    QString error = SaveHandler::saveToFile(*tree, filePath);
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();

    // Verify the file exists
    EXPECT_TRUE(QFile::exists(filePath));

    // Verify the file contains valid JSON with expected content
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content = file.readAll();
    file.close();

    // Should contain the key "name" and value "Alice"
    EXPECT_TRUE(content.contains("\"name\""));
    EXPECT_TRUE(content.contains("\"Alice\""));
    EXPECT_TRUE(content.contains("\"age\""));
    EXPECT_TRUE(content.contains("30"));

    // Should end with a newline (trailing newline option is true by default)
    EXPECT_TRUE(content.endsWith('\n'));
}

// Test 2: Read-only directory — save should fail with an error
TEST(SaveHandler, ReadOnlyDirectory) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // Create a subdirectory and make it read-only
    QString roDir = tempDir.path() + "/readonly";
    ASSERT_TRUE(QDir().mkpath(roDir));
    ASSERT_TRUE(QFile::setPermissions(roDir, QFileDevice::ReadUser | QFileDevice::ExeUser));

    QString filePath = roDir + "/output.json";
    auto tree = makeSimpleTree();

    QString error = SaveHandler::saveToFile(*tree, filePath);
    EXPECT_FALSE(error.isEmpty()) << "Expected an error for read-only directory";

    // File should not exist
    EXPECT_FALSE(QFile::exists(filePath));

    // Restore permissions for cleanup
    QFile::setPermissions(roDir, QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
}

// Test 3: Original file unchanged on failure — write an initial file, make dir
// read-only, attempt overwrite, verify original content is preserved
TEST(SaveHandler, OriginalUnchangedOnFailure) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString filePath = tempDir.path() + "/data.json";

    // Write an initial file with known content
    {
        QFile file(filePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("{\"original\": true}\n");
        file.close();
    }

    // Make the directory read-only so the save (temp file creation) fails
    ASSERT_TRUE(QFile::setPermissions(tempDir.path(),
        QFileDevice::ReadUser | QFileDevice::ExeUser));

    auto tree = makeSimpleTree();
    QString error = SaveHandler::saveToFile(*tree, filePath);
    EXPECT_FALSE(error.isEmpty()) << "Expected an error when directory is read-only";

    // Restore permissions before reading
    QFile::setPermissions(tempDir.path(),
        QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);

    // Verify original content is preserved
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content = file.readAll();
    file.close();

    EXPECT_EQ(content, QByteArray("{\"original\": true}\n"));
}

// Test 4: Saving an arena-backed tree writes byte-identical output to saving
// the equivalent JsonNode tree (SaveHandler now streams over NodeView).
TEST(SaveHandler, ArenaBackedSaveMatchesJsonNodeSave) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::string json =
        R"({"name":"Alice","age":30,"tags":["a","b"],"meta":{"ok":true,"n":null}})";

    auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
    ASSERT_TRUE(arenaResult.ok());
    auto jsonRoot = arenaResult.root->toJsonNode();
    ASSERT_NE(jsonRoot, nullptr);

    QString arenaPath = tempDir.path() + "/arena.json";
    QString legacyPath = tempDir.path() + "/legacy.json";

    ASSERT_TRUE(SaveHandler::saveToFile(NodeView(*arenaResult.root), arenaPath).isEmpty());
    ASSERT_TRUE(SaveHandler::saveToFile(*jsonRoot, legacyPath).isEmpty());

    QFile arenaFile(arenaPath);
    QFile legacyFile(legacyPath);
    ASSERT_TRUE(arenaFile.open(QIODevice::ReadOnly));
    ASSERT_TRUE(legacyFile.open(QIODevice::ReadOnly));
    QByteArray arenaBytes = arenaFile.readAll();
    QByteArray legacyBytes = legacyFile.readAll();

    EXPECT_FALSE(arenaBytes.isEmpty());
    EXPECT_EQ(arenaBytes, legacyBytes);
}

// ---------------------------------------------------------------------------
// Phase 5b commit 4: export selected subtree as JSON
// ---------------------------------------------------------------------------

// Exporting a subtree NodeView to a file matches the string-returning
// exportJson API (same defaults: pretty print, 2-space indent, trailing
// newline).
TEST(ExportJson, SubtreeMatchesStringExporter) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    auto meta = JsonNode::makeObject("meta", {
        JsonNode::makeBool("ok", true),
        JsonNode::makeNull("n"),
        JsonNode::makeArray("items", {
            JsonNode::makeNumber("", "1"),
            JsonNode::makeString("", "two")
        })
    });
    auto root = JsonNode::makeObject("", {
        meta,
        JsonNode::makeString("other", "ignored")
    });
    (void)root;  // the export targets the subtree, not the root

    QString filePath = tempDir.path() + "/subtree.json";
    QString error = ExportHandler::exportJsonToFile(NodeView(*meta), filePath);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();

    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content = file.readAll();

    std::string expected = exportJson(*meta);
    EXPECT_EQ(content.toStdString(), expected);
    EXPECT_TRUE(content.endsWith('\n'));
}

// Arena-backed subtrees export byte-identically to their JsonNode
// conversion (exportJsonToFile streams over NodeView for either backing).
TEST(ExportJson, ArenaSubtreeMatchesJsonNodeSubtree) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::string json =
        R"({"meta":{"ok":true,"n":null},"tags":["a","b",3]})";
    auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
    ASSERT_TRUE(arenaResult.ok());
    ASSERT_GE(arenaResult.root->childCount, std::size_t(2));

    const auto* arenaSubtree = arenaResult.root->children[1];  // "tags"
    QString filePath = tempDir.path() + "/arena_subtree.json";
    QString error =
        ExportHandler::exportJsonToFile(NodeView(*arenaSubtree), filePath);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();

    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content = file.readAll();

    auto jsonRoot = arenaResult.root->toJsonNode();
    ASSERT_NE(jsonRoot, nullptr);
    std::string expected = exportJson(*jsonRoot->children[1]);
    EXPECT_EQ(content.toStdString(), expected);
}

// Write failures surface as error strings, mirroring the CSV/XML handlers.
TEST(ExportJson, ReturnsErrorOnFileWriteFailure) {
    auto tree = makeSimpleTree();
    QString error = ExportHandler::exportJsonToFile(
        NodeView(*tree), "/nonexistent_directory_xyz/impossible/out.json");
    EXPECT_FALSE(error.isEmpty());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
