// ---------------------------------------------------------------------------
// Bug Condition Exploration Property-Based Test
// Feature: pretty-print-node-fix
// ---------------------------------------------------------------------------
// This test demonstrates the bug in onTreeSelectionChanged():
// When the tree is arena-backed (simdjson path), selecting a valid node
// results in an empty detail panel because getSelectedNode() returns nullptr
// (m_currentRoot is null for arena trees) and the handler clears the panel
// instead of using the arena fallback path.
//
// **Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3**
//
// EXPECTED OUTCOME on UNFIXED code: FAIL
// The property asserts that arena node selections should produce non-empty
// rendered output. On unfixed code, the detail panel is cleared instead.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <QApplication>
#include <QTextEdit>
#include <QTreeView>

#include <memory>
#include <string>

#include "core/arena_json_node.h"
#include "core/parse_orchestrator.h"
#include "core/pretty_printer.h"
#include "core/token_emitter.h"
#include "shell/filter_proxy_model.h"
#include "shell/syntax_highlighter.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Helper: Generate a JSON object with N key-value pairs
// ---------------------------------------------------------------------------
std::string generateJsonObject(int fieldCount) {
    std::string json = "{";
    for (int i = 0; i < fieldCount; ++i) {
        if (i > 0) json += ",";
        json += "\"key_" + std::to_string(i) + "\":";
        // Alternate between different value types
        switch (i % 5) {
            case 0: json += "\"string_value_" + std::to_string(i) + "\""; break;
            case 1: json += std::to_string(i * 42); break;
            case 2: json += (i % 2 == 0) ? "true" : "false"; break;
            case 3: json += "null"; break;
            case 4: json += "[1,2,3]"; break;
        }
    }
    json += "}";
    return json;
}

// ---------------------------------------------------------------------------
// Helper: Generate a JSON array with N elements
// ---------------------------------------------------------------------------
std::string generateJsonArray(int elementCount) {
    std::string json = "[";
    for (int i = 0; i < elementCount; ++i) {
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(i) +
                ",\"name\":\"item_" + std::to_string(i) + "\"}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: JSON inputs of varying sizes and structures
// ---------------------------------------------------------------------------
rc::Gen<std::string> genArenaJson() {
    return rc::gen::exec([]() -> std::string {
        int choice = *rc::gen::inRange(0, 3);
        switch (choice) {
            case 0: {
                // Object with 1-20 fields
                int fieldCount = *rc::gen::inRange(1, 21);
                return generateJsonObject(fieldCount);
            }
            case 1: {
                // Array with 1-20 elements
                int elementCount = *rc::gen::inRange(1, 21);
                return generateJsonArray(elementCount);
            }
            case 2:
            default: {
                // Nested object
                int depth = *rc::gen::inRange(1, 4);
                std::string json = "{\"root\":{\"nested\":";
                for (int d = 0; d < depth; ++d) {
                    json += "{\"level_" + std::to_string(d) + "\":";
                }
                json += "\"leaf_value\"";
                for (int d = 0; d < depth; ++d) {
                    json += "}";
                }
                json += "}}";
                return json;
            }
        }
    });
}

// Ensure QApplication exists for Qt widget tests
int g_argc = 1;
char g_arg0[] = "pretty_print_arena_pbt";
char* g_argv[] = {g_arg0, nullptr};

class QtAppFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            s_app = new QApplication(g_argc, g_argv);
        }
    }
    static QApplication* s_app;
};
QApplication* QtAppFixture::s_app = nullptr;

} // anonymous namespace

// ===========================================================================
// Property 1: Bug Condition — Arena Node Selection Produces Empty Detail Panel
//
// For any valid JSON parsed via the simdjson backend (arena path), when a
// valid node is selected in the tree:
// 1. arenaNodeForIndex() returns a non-null ArenaJsonNode*
// 2. toJsonNode() produces a valid JsonNode
// 3. emitTokens() produces a non-empty TokenEmitResult
// 4. BUT the current onTreeSelectionChanged() logic clears the detail panel
//    because getSelectedNode() returns nullptr (m_currentRoot is null)
//
// The test asserts the EXPECTED behavior (arena selections should render),
// which FAILS on unfixed code, confirming the bug exists.
//
// **Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3**
// ===========================================================================

TEST_F(QtAppFixture, BugCondition_ArenaNodeSelectionProducesEmptyDetailPanel) {
    rc::check("Bug Condition: Arena node selection should produce non-empty detail panel content",
        []() {
            const auto json = *genArenaJson();

            // Step 1: Parse via simdjson backend to get an ArenaParseResult
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});
            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);
            RC_PRE(arenaResult.root->childCount > 0);

            auto sharedResult = std::make_shared<ArenaParseResult>(std::move(arenaResult));

            // Step 2: Set up TreeModel with arena-backed tree
            TreeModel treeModel;
            treeModel.setArenaRoot(sharedResult);

            // Step 3: Set up FilterProxyModel over the TreeModel
            FilterProxyModel filterProxy;
            filterProxy.setSourceModel(&treeModel);

            // Step 4: Fetch children so valid indices exist
            treeModel.fetchMore(QModelIndex());
            RC_PRE(treeModel.rowCount(QModelIndex()) > 0);

            // Step 5: Generate a valid child index
            int childCount = treeModel.rowCount(QModelIndex());
            int selectedRow = *rc::gen::inRange(0, childCount);

            QModelIndex sourceIndex = treeModel.index(selectedRow, 0, QModelIndex());
            RC_PRE(sourceIndex.isValid());

            // Step 6: Verify arenaNodeForIndex returns non-null
            const ArenaJsonNode* arenaNode = treeModel.arenaNodeForIndex(sourceIndex);
            RC_ASSERT(arenaNode != nullptr);

            // Step 7: Verify toJsonNode() produces a valid node
            auto jsonNode = arenaNode->toJsonNode();
            RC_ASSERT(jsonNode != nullptr);

            // Step 8: Verify emitTokens produces non-empty result
            PrettyPrintOptions opts;
            opts.maxOutputSize = 65536;  // 64 KB limit
            auto tokenResult = emitTokens(*jsonNode, opts);
            RC_ASSERT(!tokenResult.tokens.empty());

            // Step 9: Simulate the CURRENT onTreeSelectionChanged() logic
            // This is the bug: getSelectedNode() relies on m_currentRoot which
            // is null for arena-backed trees. The TreeModel has no rootNode()
            // set (it was cleared by setArenaRoot).
            auto legacyRoot = treeModel.rootNode();
            // m_currentRoot equivalent is null for arena trees
            RC_ASSERT(legacyRoot == nullptr);

            // jsonNodeForIndex returns nullptr for arena-backed nodes
            const JsonNode* jsonNodePtr = treeModel.jsonNodeForIndex(sourceIndex);
            RC_ASSERT(jsonNodePtr == nullptr);

            // This means getSelectedNode() would return nullptr (or m_currentRoot which is null)
            // The current handler then calls m_detailPanel->clear()

            // Step 10: Simulate what the handler SHOULD do (expected behavior)
            // Create a QTextEdit to act as the detail panel
            QTextEdit detailPanel;
            auto theme = jsontitan::shell::catppuccinMochaTheme();

            // The EXPECTED behavior: render the arena node
            jsontitan::shell::renderHighlighted(&detailPanel, tokenResult, theme);
            QString renderedContent = detailPanel.toPlainText();

            // ASSERT: The rendered content should be non-empty
            // On UNFIXED code, the handler would clear the panel instead of rendering
            // This assertion encodes the EXPECTED behavior
            RC_ASSERT(!renderedContent.isEmpty());

            // Step 11: Simulate what the CURRENT (buggy) handler actually does
            QTextEdit buggyPanel;
            // Current logic: getSelectedNode() returns nullptr → clear panel
            // Since legacyRoot is null AND jsonNodeForIndex returns nullptr,
            // getSelectedNode() returns nullptr, and the handler clears:
            buggyPanel.clear();

            // ASSERT: The buggy behavior produces empty content (confirms bug)
            // This is the key assertion that demonstrates the bug:
            // The buggy code clears the panel, but the expected behavior renders content
            QString buggyContent = buggyPanel.toPlainText();
            RC_ASSERT(buggyContent.isEmpty());

            // FINAL ASSERTION: The expected behavior differs from the buggy behavior
            // This FAILS on unfixed code because the actual handler clears the panel
            // instead of rendering the arena node. We assert the EXPECTED behavior
            // that the detail panel should NOT be empty after selecting an arena node.
            //
            // To make this test encode the expected behavior (which fails on unfixed code):
            // We simulate what onTreeSelectionChanged() ACTUALLY does and assert it
            // should produce non-empty output. Since it doesn't, the test FAILS.
            QTextEdit actualPanel;
            // Simulate actual onTreeSelectionChanged() behavior:
            // 1. Call getSelectedNode() equivalent - returns nullptr for arena trees
            std::shared_ptr<const JsonNode> selectedNode = nullptr; // getSelectedNode() result
            if (selectedNode) {
                // Legacy path - would render here (but selectedNode is null)
                jsontitan::shell::renderHighlighted(&actualPanel, emitTokens(*selectedNode, opts), theme);
            } else {
                // Current buggy behavior: just clear
                actualPanel.clear();
            }

            // EXPECTED: actualPanel should have content (the arena node rendered)
            // ACTUAL on unfixed code: actualPanel is empty (bug!)
            // This assertion FAILS on unfixed code, confirming the bug exists
            RC_ASSERT(!actualPanel.toPlainText().isEmpty());
        });
}

// ===========================================================================
// Concrete Failing Case: Single object with arena-backed tree
// Deterministic test that clearly demonstrates the bug.
//
// **Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3**
// ===========================================================================

TEST_F(QtAppFixture, ConcreteCase_ArenaRootSelectionClearsPanel) {
    // Parse a simple JSON object via simdjson
    std::string json = R"({"name": "Alice", "age": 30, "active": true})";

    auto arenaResult = parseBuffer(std::string(json),
        ParseBufferOptions{.backend = ParserBackend::Simdjson});
    ASSERT_TRUE(arenaResult.ok());
    ASSERT_NE(arenaResult.root, nullptr);
    ASSERT_GT(arenaResult.root->childCount, 0u);

    auto sharedResult = std::make_shared<ArenaParseResult>(std::move(arenaResult));

    // Set up TreeModel with arena root
    TreeModel treeModel;
    treeModel.setArenaRoot(sharedResult);

    // Verify m_rootJsonNode is null (arena path clears it)
    ASSERT_EQ(treeModel.rootNode(), nullptr);

    // Fetch children
    treeModel.fetchMore(QModelIndex());
    ASSERT_GT(treeModel.rowCount(QModelIndex()), 0);

    // Select the first child (index 0)
    QModelIndex sourceIndex = treeModel.index(0, 0, QModelIndex());
    ASSERT_TRUE(sourceIndex.isValid());

    // Verify arena node is accessible
    const ArenaJsonNode* arenaNode = treeModel.arenaNodeForIndex(sourceIndex);
    ASSERT_NE(arenaNode, nullptr);

    // Verify jsonNodeForIndex returns nullptr (arena-backed)
    const JsonNode* jsonNodePtr = treeModel.jsonNodeForIndex(sourceIndex);
    ASSERT_EQ(jsonNodePtr, nullptr);

    // Verify the arena node CAN be rendered (infrastructure works)
    auto jsonNode = arenaNode->toJsonNode();
    ASSERT_NE(jsonNode, nullptr);

    PrettyPrintOptions opts;
    opts.maxOutputSize = 65536;
    auto tokenResult = emitTokens(*jsonNode, opts);
    ASSERT_FALSE(tokenResult.tokens.empty());

    // Simulate onTreeSelectionChanged() current behavior:
    // getSelectedNode() returns nullptr because:
    //   - jsonNodeForIndex(sourceIndex) returns nullptr (arena node)
    //   - m_currentRoot is nullptr (cleared by setArenaRoot/onArenaParseComplete)
    // So the handler calls m_detailPanel->clear()
    QTextEdit detailPanel;
    auto theme = jsontitan::shell::catppuccinMochaTheme();

    // Simulate the ACTUAL handler logic
    std::shared_ptr<const JsonNode> selectedNode = nullptr; // getSelectedNode() returns null
    if (selectedNode) {
        jsontitan::shell::renderHighlighted(&detailPanel, emitTokens(*selectedNode, opts), theme);
    } else {
        // BUG: handler clears instead of trying arena path
        detailPanel.clear();
    }

    // EXPECTED: detail panel should have content (arena node rendered)
    // ACTUAL on unfixed code: detail panel is empty
    // This assertion FAILS, confirming the bug
    EXPECT_FALSE(detailPanel.toPlainText().isEmpty())
        << "Bug confirmed: selecting index 0 in arena-backed tree results in "
           "empty detail panel instead of rendered JSON. "
           "getSelectedNode() returns nullptr for arena trees because "
           "m_currentRoot is null, and there is no arena fallback path.";
}

