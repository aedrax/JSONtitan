// ---------------------------------------------------------------------------
// Unit and Integration Tests for CLI Parser
// Feature: cli-file-open
// ---------------------------------------------------------------------------

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTimer>
#include <QtTest/QtTest>

#include <gtest/gtest.h>

#include "core/cli_parser.h"
#include "shell/file_loader.h"
#include "shell/main_window.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

// ===========================================================================
// Task 5.1: Example-based unit tests for parseCli
// ===========================================================================

TEST(CliParserUnit, EmptyArgsProducesNoAction) {
    std::vector<std::string> args;
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::NoAction);
    EXPECT_TRUE(result.filePaths.empty());
    EXPECT_FALSE(result.error.has_value());
}

TEST(CliParserUnit, SingleFileProducesOpenFiles) {
    std::vector<std::string> args = {"data.json"};
    auto result = parseCli(args, "/home/user");

    EXPECT_EQ(result.action, CliAction::OpenFiles);
    EXPECT_EQ(result.filePaths.size(), 1u);
    EXPECT_FALSE(result.error.has_value());
}

TEST(CliParserUnit, MultipleFilesProducesOpenFiles) {
    std::vector<std::string> args = {"a.json", "b.json", "c.json"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::OpenFiles);
    EXPECT_EQ(result.filePaths.size(), 3u);
    EXPECT_FALSE(result.error.has_value());
}

TEST(CliParserUnit, HelpFlagProducesShowHelp) {
    std::vector<std::string> args = {"--help"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::ShowHelp);
    EXPECT_TRUE(result.filePaths.empty());
    EXPECT_FALSE(result.error.has_value());
}

TEST(CliParserUnit, VersionFlagProducesShowVersion) {
    std::vector<std::string> args = {"--version"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::ShowVersion);
    EXPECT_TRUE(result.filePaths.empty());
    EXPECT_FALSE(result.error.has_value());
}

TEST(CliParserUnit, UnknownFlagProducesError) {
    std::vector<std::string> args = {"--foo"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::NoAction);
    EXPECT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->find("--foo"), std::string::npos);
}

TEST(CliParserUnit, HelpWithOtherArgsStillShowsHelp) {
    std::vector<std::string> args = {"file.json", "--help", "--version"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::ShowHelp);
}

TEST(CliParserUnit, VersionWithOtherArgsButNoHelpShowsVersion) {
    std::vector<std::string> args = {"file.json", "--version"};
    auto result = parseCli(args, "/tmp");

    EXPECT_EQ(result.action, CliAction::ShowVersion);
}

TEST(CliParserUnit, RelativePathResolvedAgainstCwd) {
    std::vector<std::string> args = {"subdir/file.json"};
    auto result = parseCli(args, "/home/user");

    EXPECT_EQ(result.action, CliAction::OpenFiles);
    EXPECT_EQ(result.filePaths.size(), 1u);

    std::filesystem::path expected =
        std::filesystem::weakly_canonical(
            std::filesystem::path("/home/user") / "subdir/file.json");
    EXPECT_EQ(result.filePaths[0], expected.string());
}

TEST(CliParserUnit, AbsolutePathPreserved) {
    std::vector<std::string> args = {"/absolute/path/file.json"};
    auto result = parseCli(args, "/home/user");

    EXPECT_EQ(result.action, CliAction::OpenFiles);
    EXPECT_EQ(result.filePaths.size(), 1u);

    std::filesystem::path expected =
        std::filesystem::weakly_canonical(std::filesystem::path("/absolute/path/file.json"));
    EXPECT_EQ(result.filePaths[0], expected.string());
}

TEST(CliParserUnit, HelpTextContainsUsage) {
    auto text = helpText("jsontitan");
    EXPECT_NE(text.find("Usage:"), std::string::npos);
    EXPECT_NE(text.find("--help"), std::string::npos);
    EXPECT_NE(text.find("--version"), std::string::npos);
}

TEST(CliParserUnit, VersionTextContainsVersion) {
    auto text = versionText("1.0.0");
    EXPECT_NE(text.find("1.0.0"), std::string::npos);
    EXPECT_NE(text.find("JSONTitan"), std::string::npos);
}

// ===========================================================================
// Task 5.2-5.4: Integration tests for MainWindow::openFromCliArgs
// ===========================================================================

class CliIntegrationTest : public QObject {
    Q_OBJECT

private:
    // Helper: write content to a temporary file and return its path
    QString writeTempFile(const QByteArray& content, const QString& suffix = ".json") {
        auto* tempFile = new QTemporaryFile(
            QDir::tempPath() + "/jsontitan_cli_test_XXXXXX" + suffix, this);
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
    // Task 5.2: Single valid JSON file triggers parse and populates tree model
    // Validates: Requirements 1.1, 1.2, 4.1
    // -----------------------------------------------------------------------
    void testSingleFilePopulatesTreeModel() {
        QByteArray json = R"({"name": "Alice", "age": 30, "active": true})";
        QString filePath = writeTempFile(json);
        QVERIFY(!filePath.isEmpty());

        MainWindow window;
        window.show();
        QCoreApplication::processEvents();

        // Get the FileLoader to spy on parseComplete
        auto* fileLoader = window.findChild<FileLoader*>();
        QVERIFY(fileLoader != nullptr);

        QSignalSpy completeSpy(fileLoader, &FileLoader::arenaParseComplete);

        std::vector<std::string> paths = {filePath.toStdString()};
        window.openFromCliArgs(paths);

        // Wait for async parse to complete
        QVERIFY(completeSpy.wait(5000));

        // Verify the tree model is populated (welcome label hidden, tree visible)
        auto* welcomeLabel = window.findChild<QLabel*>("welcomeLabel");
        QVERIFY(welcomeLabel != nullptr);
        QVERIFY(welcomeLabel->isHidden());

        auto* treeView = window.findChild<QTreeView*>();
        QVERIFY(treeView != nullptr);
        QVERIFY(!treeView->isHidden());

        // Verify the model has data (root has children)
        auto* model = treeView->model();
        QVERIFY(model != nullptr);
        QVERIFY(model->hasChildren(QModelIndex()));
    }

    // -----------------------------------------------------------------------
    // Task 5.3: Multiple valid JSON files triggers union mode
    // Validates: Requirements 2.1, 2.2, 4.2
    // -----------------------------------------------------------------------
    void testMultipleFilesTriggersUnionMode() {
        QByteArray json1 = R"({"name": "Alice"})";
        QByteArray json2 = R"({"name": "Bob"})";
        QString filePath1 = writeTempFile(json1);
        QString filePath2 = writeTempFile(json2);
        QVERIFY(!filePath1.isEmpty());
        QVERIFY(!filePath2.isEmpty());

        MainWindow window;
        window.show();
        QCoreApplication::processEvents();

        std::vector<std::string> paths = {
            filePath1.toStdString(),
            filePath2.toStdString()
        };
        window.openFromCliArgs(paths);

        // Union mode is synchronous, so the tree should be populated immediately
        auto* welcomeLabel = window.findChild<QLabel*>("welcomeLabel");
        QVERIFY(welcomeLabel != nullptr);
        QVERIFY(welcomeLabel->isHidden());

        auto* treeView = window.findChild<QTreeView*>();
        QVERIFY(treeView != nullptr);
        QVERIFY(!treeView->isHidden());

        // Verify the model has data (union root with children)
        auto* model = treeView->model();
        QVERIFY(model != nullptr);
        QVERIFY(model->hasChildren(QModelIndex()));
    }

    // -----------------------------------------------------------------------
    // Task 5.4: Non-existent file shows error and leaves welcome screen visible
    // Validates: Requirements 1.3
    // -----------------------------------------------------------------------
    void testNonExistentFileShowsErrorKeepsWelcome() {
        MainWindow window;
        window.show();
        QCoreApplication::processEvents();

        // Use a path that definitely doesn't exist
        std::vector<std::string> paths = {
            "/tmp/jsontitan_nonexistent_cli_test_99999.json"
        };

        // Note: QMessageBox::warning will be shown as a modal dialog.
        // We use a timer to dismiss it before calling openFromCliArgs.
        QTimer::singleShot(100, []() {
            // Dismiss any message box that appears
            for (auto* widget : qApp->topLevelWidgets()) {
                if (auto* mb = qobject_cast<QMessageBox*>(widget)) {
                    mb->accept();
                }
            }
        });

        window.openFromCliArgs(paths);

        // Process events to handle the message box dismissal
        QCoreApplication::processEvents();
        QTest::qWait(200);
        QCoreApplication::processEvents();

        // Welcome screen should still be visible (not hidden)
        auto* welcomeLabel = window.findChild<QLabel*>("welcomeLabel");
        QVERIFY(welcomeLabel != nullptr);
        QVERIFY(!welcomeLabel->isHidden());

        // Tree view should be hidden
        auto* treeView = window.findChild<QTreeView*>();
        QVERIFY(treeView != nullptr);
        QVERIFY(treeView->isHidden());
    }
};

// ===========================================================================
// Main: Run both GTest and Qt Test
// ===========================================================================

// Qt Test adapter to run alongside GTest
static CliIntegrationTest s_integrationTest;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Run GTest unit tests
    ::testing::InitGoogleTest(&argc, argv);
    int gtestResult = RUN_ALL_TESTS();

    // Run Qt integration tests
    int qtResult = QTest::qExec(&s_integrationTest, argc, argv);

    return gtestResult + qtResult;
}

#include "cli_parser_tests.moc"
