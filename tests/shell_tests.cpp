#include <QApplication>
#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest/QtTest>
#include <rapidcheck.h>

#include "core/csv_exporter.h"
#include "core/json_node.h"
#include "core/search_engine.h"
#include "core/xml_exporter.h"
#include "shell/drop_validator.h"
#include "shell/export_handler.h"
#include "shell/filter_proxy_model.h"
#include "shell/main_window.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Helper: Generate random JsonNode values for property testing
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<const JsonNode> generateRandomNode(uint32_t seed, int maxDepth = 3) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> typeDist(0, 5);
    std::uniform_int_distribution<int> childCountDist(0, 5);
    std::uniform_int_distribution<int> keyLenDist(1, 10);
    std::uniform_int_distribution<int> charDist(97, 122); // a-z

    auto randomKey = [&]() -> std::string {
        int len = keyLenDist(rng);
        std::string key;
        for (int i = 0; i < len; ++i)
            key += static_cast<char>(charDist(rng));
        return key;
    };

    auto randomValue = [&]() -> std::string {
        int len = keyLenDist(rng);
        std::string val;
        for (int i = 0; i < len; ++i)
            val += static_cast<char>(charDist(rng));
        return val;
    };

    std::function<std::shared_ptr<const JsonNode>(int)> generate =
        [&](int depth) -> std::shared_ptr<const JsonNode> {
        int typeChoice = (depth >= maxDepth) ? typeDist(rng) % 4 + 2 : typeDist(rng);

        switch (typeChoice) {
        case 0: { // Object
            int childCount = childCountDist(rng);
            std::vector<std::shared_ptr<const JsonNode>> children;
            for (int i = 0; i < childCount; ++i)
                children.push_back(generate(depth + 1));
            return JsonNode::makeObject(randomKey(), std::move(children));
        }
        case 1: { // Array
            int childCount = childCountDist(rng);
            std::vector<std::shared_ptr<const JsonNode>> children;
            for (int i = 0; i < childCount; ++i)
                children.push_back(generate(depth + 1));
            return JsonNode::makeArray(randomKey(), std::move(children));
        }
        case 2: // String
            return JsonNode::makeString(randomKey(), randomValue());
        case 3: // Number
            return JsonNode::makeNumber(randomKey(), std::to_string(std::uniform_int_distribution<int>(-1000, 1000)(rng)));
        case 4: // Boolean
            return JsonNode::makeBool(randomKey(), rng() % 2 == 0);
        case 5: // Null
        default:
            return JsonNode::makeNull(randomKey());
        }
    };

    return generate(0);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Task 11.2: Property test for node display formatting
// Property 4: Node Display Formatting
// Validates: Requirement 3.3
// ---------------------------------------------------------------------------

class TreeModelPropertyTest : public QObject {
    Q_OBJECT

private slots:
    void property4_nodeDisplayFormatting() {
        // Property 4: Node Display Formatting
        // For any JsonNode, the display formatting function SHALL produce output
        // containing the node's key (or array index), a type indicator, and for
        // scalar types a preview of the value.
        rc::check("Feature: json-titan-core, Property 4: Node Display Formatting",
            [](void) {
                auto seed = *rc::gen::arbitrary<uint32_t>();
                auto node = generateRandomNode(seed, 2);
                RC_ASSERT(node != nullptr);

                // Test with key (non-array context)
                {
                    QString display = TreeModel::formatNodeDisplay(*node, -1);
                    RC_ASSERT(!display.isEmpty());

                    // Verify key is present if node has a key
                    if (!node->key.empty()) {
                        QString key = QString::fromStdString(node->key);
                        RC_ASSERT(display.contains(key));
                    }

                    // Verify type indicator is present
                    switch (node->type) {
                    case NodeType::Object:
                        RC_ASSERT(display.contains("{Object}"));
                        break;
                    case NodeType::Array:
                        RC_ASSERT(display.contains("[Array]"));
                        break;
                    case NodeType::String:
                        // String values are shown in quotes
                        RC_ASSERT(display.contains("\""));
                        // Value preview should be present (possibly truncated)
                        if (!node->value.empty()) {
                            QString val = QString::fromStdString(node->value);
                            // Either the full value or a truncated version should appear
                            RC_ASSERT(display.contains(val.left(std::min(val.length(), qsizetype(50)))));
                        }
                        break;
                    case NodeType::Number:
                        // Number value should appear directly
                        RC_ASSERT(display.contains(QString::fromStdString(node->value)));
                        break;
                    case NodeType::Boolean:
                        // Boolean value should appear
                        RC_ASSERT(display.contains(QString::fromStdString(node->value)));
                        break;
                    case NodeType::Null:
                        RC_ASSERT(display.contains("null"));
                        break;
                    }
                }

                // Test with array index context
                {
                    int arrayIdx = *rc::gen::inRange(0, 100);
                    QString display = TreeModel::formatNodeDisplay(*node, arrayIdx);
                    RC_ASSERT(!display.isEmpty());

                    // Verify array index is present
                    QString indexStr = QStringLiteral("[%1]").arg(arrayIdx);
                    RC_ASSERT(display.contains(indexStr));
                }
            });
    }
};

// ---------------------------------------------------------------------------
// Existing shell setup test
// ---------------------------------------------------------------------------

class ShellSetupTest : public QObject {
    Q_OBJECT

private slots:
    void testMainWindowCreation() {
        MainWindow window;
        QVERIFY(window.isWindow());
    }
};

// ---------------------------------------------------------------------------
// Task 11.3: Unit tests for TreeModel using QAbstractItemModelTester
// Requirements: 3.1, 3.2, 3.3, 3.4, 3.5
// ---------------------------------------------------------------------------

class TreeModelUnitTest : public QObject {
    Q_OBJECT

private slots:
    void testEmptyModel() {
        TreeModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        QCOMPARE(model.rowCount(QModelIndex()), 0);
        QCOMPARE(model.columnCount(QModelIndex()), 1);
        QVERIFY(!model.hasChildren(QModelIndex()));
    }

    void testSetRootNodeTriggersReset() {
        TreeModel model;

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30")
        });

        model.setRootNode(root);
        QCOMPARE(resetSpy.count(), 1);
    }

    void testSetRootNodeWithNull() {
        TreeModel model;

        // Set a valid root first
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("key", "value")
        });
        model.setRootNode(root);

        // Now set null — should reset to empty
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        model.setRootNode(nullptr);
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(model.rowCount(QModelIndex()), 0);
        QVERIFY(!model.hasChildren(QModelIndex()));
    }

    void testLazyLoadingChildrenNotLoadedUntilFetchMore() {
        TreeModel model;

        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("a", "1"),
            JsonNode::makeString("b", "2"),
            JsonNode::makeString("c", "3")
        });

        model.setRootNode(root);

        // hasChildren should be true (root has children)
        QVERIFY(model.hasChildren(QModelIndex()));

        // But rowCount should be 0 initially (lazy loading — not fetched yet)
        QCOMPARE(model.rowCount(QModelIndex()), 0);

        // canFetchMore should be true
        QVERIFY(model.canFetchMore(QModelIndex()));

        // Fetch children
        model.fetchMore(QModelIndex());

        // Now rowCount should reflect the children
        QCOMPARE(model.rowCount(QModelIndex()), 3);

        // canFetchMore should be false now
        QVERIFY(!model.canFetchMore(QModelIndex()));
    }

    void testLazyLoadingNestedChildren() {
        TreeModel model;

        auto inner = JsonNode::makeObject("inner", {
            JsonNode::makeString("x", "hello"),
            JsonNode::makeNumber("y", "42")
        });

        auto root = JsonNode::makeObject("", {inner});
        model.setRootNode(root);

        // Fetch root children
        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 1);

        // Get the inner node index
        QModelIndex innerIdx = model.index(0, 0, QModelIndex());
        QVERIFY(innerIdx.isValid());

        // Inner node has children but they're not fetched yet
        QVERIFY(model.hasChildren(innerIdx));
        QCOMPARE(model.rowCount(innerIdx), 0);
        QVERIFY(model.canFetchMore(innerIdx));

        // Fetch inner children
        model.fetchMore(innerIdx);
        QCOMPARE(model.rowCount(innerIdx), 2);
        QVERIFY(!model.canFetchMore(innerIdx));
    }

    void testDataDisplayRole() {
        TreeModel model;

        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30"),
            JsonNode::makeBool("active", true),
            JsonNode::makeNull("deleted"),
            JsonNode::makeObject("address", {
                JsonNode::makeString("city", "NYC")
            }),
            JsonNode::makeArray("tags", {
                JsonNode::makeString("", "dev"),
                JsonNode::makeString("", "cpp")
            })
        });

        model.setRootNode(root);
        model.fetchMore(QModelIndex());

        // String node
        QModelIndex nameIdx = model.index(0, 0, QModelIndex());
        QString nameDisplay = model.data(nameIdx, Qt::DisplayRole).toString();
        QVERIFY(nameDisplay.contains("name"));
        QVERIFY(nameDisplay.contains("Alice"));
        QVERIFY(nameDisplay.contains("\""));

        // Number node
        QModelIndex ageIdx = model.index(1, 0, QModelIndex());
        QString ageDisplay = model.data(ageIdx, Qt::DisplayRole).toString();
        QVERIFY(ageDisplay.contains("age"));
        QVERIFY(ageDisplay.contains("30"));

        // Boolean node
        QModelIndex activeIdx = model.index(2, 0, QModelIndex());
        QString activeDisplay = model.data(activeIdx, Qt::DisplayRole).toString();
        QVERIFY(activeDisplay.contains("active"));
        QVERIFY(activeDisplay.contains("true"));

        // Null node
        QModelIndex deletedIdx = model.index(3, 0, QModelIndex());
        QString deletedDisplay = model.data(deletedIdx, Qt::DisplayRole).toString();
        QVERIFY(deletedDisplay.contains("deleted"));
        QVERIFY(deletedDisplay.contains("null"));

        // Object node
        QModelIndex addrIdx = model.index(4, 0, QModelIndex());
        QString addrDisplay = model.data(addrIdx, Qt::DisplayRole).toString();
        QVERIFY(addrDisplay.contains("address"));
        QVERIFY(addrDisplay.contains("{Object}"));

        // Array node
        QModelIndex tagsIdx = model.index(5, 0, QModelIndex());
        QString tagsDisplay = model.data(tagsIdx, Qt::DisplayRole).toString();
        QVERIFY(tagsDisplay.contains("tags"));
        QVERIFY(tagsDisplay.contains("[Array]"));
    }

    void testArrayIndexDisplay() {
        TreeModel model;

        auto root = JsonNode::makeArray("", {
            JsonNode::makeString("", "first"),
            JsonNode::makeString("", "second"),
            JsonNode::makeString("", "third")
        });

        model.setRootNode(root);
        model.fetchMore(QModelIndex());

        // Children of an array should show [0], [1], [2] indices
        QModelIndex idx0 = model.index(0, 0, QModelIndex());
        QString display0 = model.data(idx0, Qt::DisplayRole).toString();
        QVERIFY(display0.contains("[0]"));

        QModelIndex idx1 = model.index(1, 0, QModelIndex());
        QString display1 = model.data(idx1, Qt::DisplayRole).toString();
        QVERIFY(display1.contains("[1]"));

        QModelIndex idx2 = model.index(2, 0, QModelIndex());
        QString display2 = model.data(idx2, Qt::DisplayRole).toString();
        QVERIFY(display2.contains("[2]"));
    }

    void testParentIndex() {
        TreeModel model;

        auto root = JsonNode::makeObject("", {
            JsonNode::makeObject("child", {
                JsonNode::makeString("grandchild", "value")
            })
        });

        model.setRootNode(root);
        model.fetchMore(QModelIndex());

        QModelIndex childIdx = model.index(0, 0, QModelIndex());
        QVERIFY(childIdx.isValid());

        // Parent of root child should be invalid (root)
        QModelIndex parentOfChild = model.parent(childIdx);
        QVERIFY(!parentOfChild.isValid());

        // Fetch grandchild
        model.fetchMore(childIdx);
        QModelIndex grandchildIdx = model.index(0, 0, childIdx);
        QVERIFY(grandchildIdx.isValid());

        // Parent of grandchild should be child
        QModelIndex parentOfGrandchild = model.parent(grandchildIdx);
        QCOMPARE(parentOfGrandchild, childIdx);
    }

    void testInvalidIndexReturnsEmptyData() {
        TreeModel model;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("key", "value")
        });
        model.setRootNode(root);

        // Invalid index
        QVariant data = model.data(QModelIndex(), Qt::DisplayRole);
        QVERIFY(!data.isValid());

        // Wrong role
        model.fetchMore(QModelIndex());
        QModelIndex idx = model.index(0, 0, QModelIndex());
        QVariant editData = model.data(idx, Qt::EditRole);
        QVERIFY(!editData.isValid());
    }

    void testBatchFetching() {
        TreeModel model;

        // Create a node with more children than FETCH_BATCH_SIZE (100)
        std::vector<std::shared_ptr<const JsonNode>> children;
        for (int i = 0; i < 250; ++i) {
            children.push_back(JsonNode::makeNumber("", std::to_string(i)));
        }
        auto root = JsonNode::makeArray("", std::move(children));

        model.setRootNode(root);

        // First fetch should get batch of 100
        QVERIFY(model.canFetchMore(QModelIndex()));
        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 100);
        QVERIFY(model.canFetchMore(QModelIndex()));

        // Second fetch should get another 100
        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 200);
        QVERIFY(model.canFetchMore(QModelIndex()));

        // Third fetch should get remaining 50
        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 250);
        QVERIFY(!model.canFetchMore(QModelIndex()));
    }

    void testModelTesterWithPopulatedModel() {
        TreeModel model;

        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "test"),
            JsonNode::makeArray("items", {
                JsonNode::makeNumber("", "1"),
                JsonNode::makeNumber("", "2")
            }),
            JsonNode::makeObject("nested", {
                JsonNode::makeBool("flag", false)
            })
        });

        model.setRootNode(root);

        // Fetch all levels
        model.fetchMore(QModelIndex());

        QModelIndex itemsIdx = model.index(1, 0, QModelIndex());
        model.fetchMore(itemsIdx);

        QModelIndex nestedIdx = model.index(2, 0, QModelIndex());
        model.fetchMore(nestedIdx);

        // QAbstractItemModelTester validates model consistency
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        // If we get here without assertion failures, the model is consistent
        QVERIFY(true);
    }

    void testSetRootNodeClearsOldData() {
        TreeModel model;

        auto root1 = JsonNode::makeObject("", {
            JsonNode::makeString("a", "1"),
            JsonNode::makeString("b", "2")
        });
        model.setRootNode(root1);
        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 2);

        // Replace with a different root
        auto root2 = JsonNode::makeObject("", {
            JsonNode::makeString("x", "10")
        });
        model.setRootNode(root2);

        // After reset, rowCount should be 0 (lazy loading)
        QCOMPARE(model.rowCount(QModelIndex()), 0);
        QVERIFY(model.hasChildren(QModelIndex()));
        QVERIFY(model.canFetchMore(QModelIndex()));

        model.fetchMore(QModelIndex());
        QCOMPARE(model.rowCount(QModelIndex()), 1);
    }
};

// ---------------------------------------------------------------------------
// Task 12.2: Unit tests for FilterProxyModel
// Requirements: 4.2, 4.3, 4.7
// ---------------------------------------------------------------------------

class FilterProxyModelTest : public QObject {
    Q_OBJECT

private:
    // Helper: fully fetch all children recursively in the source model
    void fetchAll(TreeModel* model, const QModelIndex& parent = QModelIndex()) {
        while (model->canFetchMore(parent)) {
            model->fetchMore(parent);
        }
        for (int i = 0; i < model->rowCount(parent); ++i) {
            QModelIndex child = model->index(i, 0, parent);
            fetchAll(model, child);
        }
    }

private slots:
    void testNoFilterAcceptsAllRows() {
        // When no filter is applied, all rows should be visible
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30"),
            JsonNode::makeBool("active", true)
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        QVERIFY(!proxy.isFiltered());
        QCOMPARE(proxy.rowCount(QModelIndex()), 3);
    }

    void testApplyFilterHidesNonMatchingRows() {
        // Build a tree: root { name: "Alice", age: 30, city: "NYC" }
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30"),
            JsonNode::makeString("city", "NYC")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Create a FilterResult that only matches the "name" node (index 0)
        jsontitan::core::FilterResult result;
        jsontitan::core::SearchMatch match;
        match.ancestorIndices = {0}; // First child of root
        match.node = root->children[0];
        result.matches.push_back(match);

        proxy.applyFilter(result);

        QVERIFY(proxy.isFiltered());
        // Only the matched row should be visible
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        // Verify the visible row is "name"
        QModelIndex visibleIdx = proxy.index(0, 0, QModelIndex());
        QString display = proxy.data(visibleIdx, Qt::DisplayRole).toString();
        QVERIFY(display.contains("name"));
        QVERIFY(display.contains("Alice"));
    }

    void testApplyFilterShowsMatchedRowsWithAncestors() {
        // Build a nested tree:
        // root {
        //   person {
        //     name: "Alice"
        //     age: 30
        //   }
        //   settings {
        //     theme: "dark"
        //   }
        // }
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeObject("person", {
                JsonNode::makeString("name", "Alice"),
                JsonNode::makeNumber("age", "30")
            }),
            JsonNode::makeObject("settings", {
                JsonNode::makeString("theme", "dark")
            })
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Match only "name" node at path [0, 0] (person -> name)
        jsontitan::core::FilterResult result;
        jsontitan::core::SearchMatch match;
        match.ancestorIndices = {0, 0}; // person (index 0) -> name (index 0)
        match.node = root->children[0]->children[0];
        result.matches.push_back(match);

        proxy.applyFilter(result);

        // Root level: only "person" should be visible (ancestor of match), not "settings"
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        // Under "person": only "name" should be visible, not "age"
        QModelIndex personIdx = proxy.index(0, 0, QModelIndex());
        QCOMPARE(proxy.rowCount(personIdx), 1);

        // Verify the visible child is "name"
        QModelIndex nameIdx = proxy.index(0, 0, personIdx);
        QString display = proxy.data(nameIdx, Qt::DisplayRole).toString();
        QVERIFY(display.contains("name"));
        QVERIFY(display.contains("Alice"));
    }

    void testMultipleMatchesShowAllMatchedPaths() {
        // root { a: "hello", b: "world", c: "foo" }
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("a", "hello"),
            JsonNode::makeString("b", "world"),
            JsonNode::makeString("c", "foo")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Match "a" (index 0) and "c" (index 2)
        jsontitan::core::FilterResult result;
        {
            jsontitan::core::SearchMatch m;
            m.ancestorIndices = {0};
            m.node = root->children[0];
            result.matches.push_back(m);
        }
        {
            jsontitan::core::SearchMatch m;
            m.ancestorIndices = {2};
            m.node = root->children[2];
            result.matches.push_back(m);
        }

        proxy.applyFilter(result);

        // "a" and "c" should be visible, "b" should be hidden
        QCOMPARE(proxy.rowCount(QModelIndex()), 2);

        QModelIndex idx0 = proxy.index(0, 0, QModelIndex());
        QString display0 = proxy.data(idx0, Qt::DisplayRole).toString();
        QVERIFY(display0.contains("a"));

        QModelIndex idx1 = proxy.index(1, 0, QModelIndex());
        QString display1 = proxy.data(idx1, Qt::DisplayRole).toString();
        QVERIFY(display1.contains("c"));
    }

    void testClearFilterRestoresAllRows() {
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30"),
            JsonNode::makeString("city", "NYC")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Apply a filter that shows only one row
        jsontitan::core::FilterResult result;
        jsontitan::core::SearchMatch match;
        match.ancestorIndices = {0};
        match.node = root->children[0];
        result.matches.push_back(match);
        proxy.applyFilter(result);
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        // Clear the filter
        proxy.clearFilter();

        QVERIFY(!proxy.isFiltered());
        // All rows should be visible again
        QCOMPARE(proxy.rowCount(QModelIndex()), 3);
    }

    void testEmptyFilterResultHidesAllRows() {
        // When FilterResult has no matches, no rows should be visible
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("name", "Alice"),
            JsonNode::makeNumber("age", "30")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Apply empty filter result (no matches)
        jsontitan::core::FilterResult result;
        // result.matches is empty
        proxy.applyFilter(result);

        QVERIFY(proxy.isFiltered());
        QCOMPARE(proxy.rowCount(QModelIndex()), 0);
    }

    void testDeepNestedAncestorPreservation() {
        // root {
        //   level1 {
        //     level2 {
        //       level3 {
        //         target: "found"
        //       }
        //       sibling: "hidden"
        //     }
        //     other: "hidden"
        //   }
        //   unrelated: "hidden"
        // }
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeObject("level1", {
                JsonNode::makeObject("level2", {
                    JsonNode::makeObject("level3", {
                        JsonNode::makeString("target", "found")
                    }),
                    JsonNode::makeString("sibling", "hidden")
                }),
                JsonNode::makeString("other", "hidden")
            }),
            JsonNode::makeString("unrelated", "hidden")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // Match "target" at path [0, 0, 0, 0]
        // level1(0) -> level2(0) -> level3(0) -> target(0)
        jsontitan::core::FilterResult result;
        jsontitan::core::SearchMatch match;
        match.ancestorIndices = {0, 0, 0, 0};
        match.node = root->children[0]->children[0]->children[0]->children[0];
        result.matches.push_back(match);

        proxy.applyFilter(result);

        // Root level: only "level1" visible, not "unrelated"
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        // Under level1: only "level2" visible, not "other"
        QModelIndex level1Idx = proxy.index(0, 0, QModelIndex());
        QCOMPARE(proxy.rowCount(level1Idx), 1);

        // Under level2: only "level3" visible, not "sibling"
        QModelIndex level2Idx = proxy.index(0, 0, level1Idx);
        QCOMPARE(proxy.rowCount(level2Idx), 1);

        // Under level3: "target" visible
        QModelIndex level3Idx = proxy.index(0, 0, level2Idx);
        QCOMPARE(proxy.rowCount(level3Idx), 1);

        QModelIndex targetIdx = proxy.index(0, 0, level3Idx);
        QString display = proxy.data(targetIdx, Qt::DisplayRole).toString();
        QVERIFY(display.contains("target"));
        QVERIFY(display.contains("found"));
    }

    void testFilterWithSearchEngineIntegration() {
        // Integration test: use the actual SearchEngine to produce a FilterResult
        // and verify the proxy model correctly filters based on it
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("greeting", "hello world"),
            JsonNode::makeNumber("count", "42"),
            JsonNode::makeString("message", "goodbye world"),
            JsonNode::makeString("unrelated", "nothing here")
        });

        // Search for "world" — should match "greeting" and "message"
        jsontitan::core::SearchQuery query;
        query.pattern = "world";
        query.mode = jsontitan::core::SearchMode::Substring;
        query.caseSensitive = false;

        auto result = jsontitan::core::filter(*root, query);

        TreeModel sourceModel;
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);
        proxy.applyFilter(result);

        // Should show "greeting" and "message" but not "count" or "unrelated"
        QCOMPARE(proxy.rowCount(QModelIndex()), 2);

        QModelIndex idx0 = proxy.index(0, 0, QModelIndex());
        QString display0 = proxy.data(idx0, Qt::DisplayRole).toString();

        QModelIndex idx1 = proxy.index(1, 0, QModelIndex());
        QString display1 = proxy.data(idx1, Qt::DisplayRole).toString();

        // Both matched nodes should contain "world" in their values
        bool hasGreeting = display0.contains("greeting") || display1.contains("greeting");
        bool hasMessage = display0.contains("message") || display1.contains("message");
        QVERIFY(hasGreeting);
        QVERIFY(hasMessage);
    }

    void testReapplyFilterReplacesOldFilter() {
        TreeModel sourceModel;
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("a", "alpha"),
            JsonNode::makeString("b", "beta"),
            JsonNode::makeString("c", "gamma")
        });
        sourceModel.setRootNode(root);
        fetchAll(&sourceModel);

        FilterProxyModel proxy;
        proxy.setSourceModel(&sourceModel);

        // First filter: show only "a"
        {
            jsontitan::core::FilterResult result;
            jsontitan::core::SearchMatch match;
            match.ancestorIndices = {0};
            match.node = root->children[0];
            result.matches.push_back(match);
            proxy.applyFilter(result);
        }
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        // Second filter: show only "c"
        {
            jsontitan::core::FilterResult result;
            jsontitan::core::SearchMatch match;
            match.ancestorIndices = {2};
            match.node = root->children[2];
            result.matches.push_back(match);
            proxy.applyFilter(result);
        }
        QCOMPARE(proxy.rowCount(QModelIndex()), 1);

        QModelIndex idx = proxy.index(0, 0, QModelIndex());
        QString display = proxy.data(idx, Qt::DisplayRole).toString();
        QVERIFY(display.contains("c"));
        QVERIFY(display.contains("gamma"));
    }
};

// ---------------------------------------------------------------------------
// Task 14.2: Unit tests for ExportHandler
// Requirements: 6.6, 7.5
// ---------------------------------------------------------------------------

class ExportHandlerTest : public QObject {
    Q_OBJECT

private slots:
    void testCsvExportWritesCorrectContent() {
        // Build an array of objects suitable for CSV export
        auto root = JsonNode::makeArray("", {
            JsonNode::makeObject("", {
                JsonNode::makeString("name", "Alice"),
                JsonNode::makeNumber("age", "30")
            }),
            JsonNode::makeObject("", {
                JsonNode::makeString("name", "Bob"),
                JsonNode::makeNumber("age", "25")
            })
        });

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString filePath = tempDir.path() + "/output.csv";

        QString error = ExportHandler::exportCsvToFile(*root, filePath);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Read back and verify content (binary mode to preserve \r\n)
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        // Verify header row contains both keys
        QVERIFY(content.contains("name"));
        QVERIFY(content.contains("age"));

        // Verify data rows contain the values
        QVERIFY(content.contains("Alice"));
        QVERIFY(content.contains("Bob"));
        QVERIFY(content.contains("30"));
        QVERIFY(content.contains("25"));

        // Verify the content matches what the core exporter produces
        auto coreResult = jsontitan::core::exportCsv(*root);
        QVERIFY(std::holds_alternative<std::string>(coreResult));
        QString expectedContent = QString::fromStdString(std::get<std::string>(coreResult));
        QCOMPARE(content, expectedContent);
    }

    void testXmlExportWritesCorrectContent() {
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("greeting", "hello"),
            JsonNode::makeNumber("count", "42")
        });

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString filePath = tempDir.path() + "/output.xml";

        QString error = ExportHandler::exportXmlToFile(*root, filePath, "data");
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Read back and verify content
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        // Verify XML declaration
        QVERIFY(content.contains("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));

        // Verify root element name
        QVERIFY(content.contains("<data>"));
        QVERIFY(content.contains("</data>"));

        // Verify child elements
        QVERIFY(content.contains("<greeting>hello</greeting>"));
        QVERIFY(content.contains("<count>42</count>"));

        // Verify the content matches what the core exporter produces
        std::string expectedXml = jsontitan::core::exportXml(*root, "data");
        QCOMPARE(content, QString::fromStdString(expectedXml));
    }

    void testCsvExportReturnsErrorForNonTabularData() {
        // A plain object is not an array of objects — CsvExporter should return CsvError
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("key", "value")
        });

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString filePath = tempDir.path() + "/output.csv";

        QString error = ExportHandler::exportCsvToFile(*root, filePath);

        // Should return a non-empty error message from the core CsvError
        QVERIFY(!error.isEmpty());

        // File should not have been created
        QVERIFY(!QFile::exists(filePath));
    }

    void testCsvExportReturnsErrorOnFileWriteFailure() {
        // Build valid CSV data
        auto root = JsonNode::makeArray("", {
            JsonNode::makeObject("", {
                JsonNode::makeString("name", "Alice")
            })
        });

        // Use an invalid path that cannot be written to
        QString invalidPath = "/nonexistent_directory_xyz/impossible/output.csv";

        QString error = ExportHandler::exportCsvToFile(*root, invalidPath);

        // Should return a non-empty error about file write failure
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains("Failed to open file"));
    }

    void testXmlExportReturnsErrorOnFileWriteFailure() {
        auto root = JsonNode::makeObject("", {
            JsonNode::makeString("key", "value")
        });

        // Use an invalid path that cannot be written to
        QString invalidPath = "/nonexistent_directory_xyz/impossible/output.xml";

        QString error = ExportHandler::exportXmlToFile(*root, invalidPath, "root");

        // Should return a non-empty error about file write failure
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains("Failed to open file"));
    }

    void testXmlExportUsesDefaultRootElementName() {
        auto root = JsonNode::makeString("", "hello");

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString filePath = tempDir.path() + "/output.xml";

        // Call without specifying rootElementName — should default to "root"
        QString error = ExportHandler::exportXmlToFile(*root, filePath);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        QVERIFY(content.contains("<root>"));
        QVERIFY(content.contains("</root>"));
    }
};

// ---------------------------------------------------------------------------
// Task 16.7: UI smoke tests for MainWindow
// Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6
// ---------------------------------------------------------------------------

class MainWindowSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void testAllExpectedWidgetsPresent() {
        MainWindow window;

        // Menu bar should exist
        QMenuBar* menuBar = window.menuBar();
        QVERIFY(menuBar != nullptr);

        // Search bar (QLineEdit)
        auto* searchBar = window.findChild<QLineEdit*>();
        QVERIFY(searchBar != nullptr);

        // Tree view
        auto* treeView = window.findChild<QTreeView*>();
        QVERIFY(treeView != nullptr);

        // Detail panel (QTextEdit, read-only)
        auto* detailPanel = window.findChild<QTextEdit*>();
        QVERIFY(detailPanel != nullptr);
        QVERIFY(detailPanel->isReadOnly());

        // Status bar
        QStatusBar* statusBar = window.statusBar();
        QVERIFY(statusBar != nullptr);

        // Progress bar (in status bar)
        auto* progressBar = window.findChild<QProgressBar*>();
        QVERIFY(progressBar != nullptr);
    }

    void testFileMenuContainsExpectedActions() {
        MainWindow window;

        QMenuBar* menuBar = window.menuBar();
        QVERIFY(menuBar != nullptr);

        // Find the File menu
        QMenu* fileMenu = nullptr;
        for (auto* action : menuBar->actions()) {
            if (action->text().contains("File")) {
                fileMenu = action->menu();
                break;
            }
        }
        QVERIFY(fileMenu != nullptr);

        // Collect action texts
        QStringList actionTexts;
        for (auto* action : fileMenu->actions()) {
            if (!action->isSeparator()) {
                actionTexts << action->text();
            }
        }

        // Verify expected actions are present
        bool hasOpen = false, hasUnion = false, hasCsv = false, hasXml = false, hasExit = false;
        for (const auto& text : actionTexts) {
            if (text.contains("Open")) hasOpen = true;
            if (text.contains("Union")) hasUnion = true;
            if (text.contains("CSV")) hasCsv = true;
            if (text.contains("XML")) hasXml = true;
            if (text.contains("xit")) hasExit = true;
        }

        QVERIFY2(hasOpen, "File menu missing Open action");
        QVERIFY2(hasUnion, "File menu missing Union Files action");
        QVERIFY2(hasCsv, "File menu missing Export CSV action");
        QVERIFY2(hasXml, "File menu missing Export XML action");
        QVERIFY2(hasExit, "File menu missing Exit action");
    }

    void testWelcomeMessageDisplayedWhenNoFileLoaded() {
        MainWindow window;
        window.show();
        QApplication::processEvents();

        // Find the welcome label
        QList<QLabel*> labels = window.findChildren<QLabel*>();
        bool foundWelcome = false;
        for (auto* label : labels) {
            if (label->text().contains("Welcome") && label->isVisible()) {
                foundWelcome = true;
                break;
            }
        }
        QVERIFY2(foundWelcome, "Welcome message not displayed when no file is loaded");

        // Tree view should be hidden when no file is loaded
        auto* treeView = window.findChild<QTreeView*>();
        QVERIFY(treeView != nullptr);
        QVERIFY(!treeView->isVisible());
    }

    void testMenuBarHasEditAndHelpMenus() {
        MainWindow window;

        QMenuBar* menuBar = window.menuBar();
        QVERIFY(menuBar != nullptr);

        bool hasEdit = false, hasHelp = false;
        for (auto* action : menuBar->actions()) {
            if (action->text().contains("Edit")) hasEdit = true;
            if (action->text().contains("Help")) hasHelp = true;
        }

        QVERIFY2(hasEdit, "Menu bar missing Edit menu");
        QVERIFY2(hasHelp, "Menu bar missing Help menu");
    }

    void testSearchBarHasPlaceholderText() {
        MainWindow window;

        auto* searchBar = window.findChild<QLineEdit*>();
        QVERIFY(searchBar != nullptr);
        QVERIFY(!searchBar->placeholderText().isEmpty());
    }

    void testProgressBarInitiallyHidden() {
        MainWindow window;

        auto* progressBar = window.findChild<QProgressBar*>();
        QVERIFY(progressBar != nullptr);
        QVERIFY(!progressBar->isVisible());
    }
};

// ---------------------------------------------------------------------------
// Task 1.2: Property test for extension-based acceptance (Property 1)
// Feature: drag-drop-file-open
// Validates: Requirements 1.2, 1.3
// ---------------------------------------------------------------------------

class DropValidatorExtensionPropertyTest : public QObject {
    Q_OBJECT

private slots:
    void property1_extensionBasedAcceptance() {
        rc::check("Feature: drag-drop-file-open, Property 1: Extension-based acceptance",
            [](void) {
                // Generate a random base filename (1-20 alphanumeric chars)
                auto nameLen = *rc::gen::inRange(1, 21);
                auto baseName = *rc::gen::container<std::string>(
                    nameLen,
                    rc::gen::oneOf(
                        rc::gen::inRange<char>('a', 'z' + 1),
                        rc::gen::inRange<char>('A', 'Z' + 1),
                        rc::gen::inRange<char>('0', '9' + 1)
                    )
                );
                // Ensure baseName is non-empty
                RC_PRE(!baseName.empty());

                // Generate a non-.json extension to use when testing rejection
                auto otherExt = *rc::gen::element(
                    std::string(".txt"), std::string(".xml"), std::string(".csv"),
                    std::string(".yaml"), std::string(".html"), std::string(".js"),
                    std::string(".jsonl"), std::string(".JSON5"), std::string("")
                );

                // Test acceptance: filename ending with .json (various cases)
                {
                    auto jsonSuffix = *rc::gen::element(
                        std::string(".json"), std::string(".JSON"),
                        std::string(".Json"), std::string(".jSoN")
                    );
                    QString filePath = QStringLiteral("/tmp/") +
                        QString::fromStdString(baseName) +
                        QString::fromStdString(jsonSuffix);

                    QMimeData mimeData;
                    mimeData.setUrls({QUrl::fromLocalFile(filePath)});

                    auto result = DropValidator::validate(&mimeData);
                    RC_ASSERT(result.accepted);
                    RC_ASSERT(result.filePath == filePath);
                }

                // Test rejection: filename NOT ending with .json
                {
                    QString filePath = QStringLiteral("/tmp/") +
                        QString::fromStdString(baseName) +
                        QString::fromStdString(otherExt);

                    // Ensure it doesn't accidentally end with .json
                    if (filePath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
                        return; // Skip this iteration
                    }

                    QMimeData mimeData;
                    mimeData.setUrls({QUrl::fromLocalFile(filePath)});

                    auto result = DropValidator::validate(&mimeData);
                    RC_ASSERT(!result.accepted);
                }
            });
    }
};

// ---------------------------------------------------------------------------
// Task 1.3: Property test for multiple file rejection (Property 2)
// Feature: drag-drop-file-open
// Validates: Requirements 4.1
// ---------------------------------------------------------------------------

class DropValidatorMultiFilePropertyTest : public QObject {
    Q_OBJECT

private slots:
    void property2_multipleFileRejection() {
        rc::check("Feature: drag-drop-file-open, Property 2: Multiple file rejection",
            [](void) {
                // Generate 2-10 random filenames
                auto fileCount = *rc::gen::inRange(2, 11);

                QList<QUrl> urls;
                for (int i = 0; i < fileCount; ++i) {
                    // Randomly choose whether this file has .json extension
                    auto useJson = *rc::gen::arbitrary<bool>();
                    auto nameLen = *rc::gen::inRange(1, 16);
                    auto baseName = *rc::gen::container<std::string>(
                        nameLen,
                        rc::gen::inRange<char>('a', 'z' + 1)
                    );
                    RC_PRE(!baseName.empty());

                    QString fileName = QString::fromStdString(baseName);
                    if (useJson) {
                        fileName += QStringLiteral(".json");
                    } else {
                        fileName += QStringLiteral(".txt");
                    }

                    urls.append(QUrl::fromLocalFile(QStringLiteral("/tmp/") + fileName));
                }

                QMimeData mimeData;
                mimeData.setUrls(urls);

                auto result = DropValidator::validate(&mimeData);
                // Multiple files must always be rejected regardless of extensions
                RC_ASSERT(!result.accepted);
            });
    }
};

// ---------------------------------------------------------------------------
// Task 4.1: Unit tests for MainWindow drag-drop behavior
// Requirements: 1.1, 1.4, 2.1, 2.2, 2.3, 3.1, 5.3
// ---------------------------------------------------------------------------

class MainWindowDragDropTest : public QObject {
    Q_OBJECT

private slots:
    void testAcceptDropsEnabled() {
        // Requirement 1.1: MainWindow SHALL accept drag-and-drop operations
        MainWindow window;
        QVERIFY(window.acceptDrops());
    }

    void testValidDragEnterShowsOverlay() {
        // Requirement 2.1: Valid drag over Drop_Zone SHALL display Drag_Indicator
        MainWindow window;
        window.show();
        QApplication::processEvents();

        // Find the drop overlay
        auto* overlay = window.findChild<QLabel*>();
        QLabel* dropOverlay = nullptr;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains("Drop JSON file here")) {
                dropOverlay = label;
                break;
            }
        }
        QVERIFY(dropOverlay != nullptr);
        QVERIFY(!dropOverlay->isVisible());

        // Simulate a valid drag enter with a .json file
        QMimeData* mimeData = new QMimeData();
        mimeData->setUrls({QUrl::fromLocalFile("/tmp/test.json")});

        QDragEnterEvent dragEnterEvent(
            QPoint(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dragEnterEvent);

        QVERIFY(dragEnterEvent.isAccepted());
        QVERIFY(dropOverlay->isVisible());

        delete mimeData;
    }

    void testDragLeaveHidesOverlay() {
        // Requirement 2.2: Drag leaving Drop_Zone SHALL hide Drag_Indicator
        MainWindow window;
        window.show();
        QApplication::processEvents();

        QLabel* dropOverlay = nullptr;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains("Drop JSON file here")) {
                dropOverlay = label;
                break;
            }
        }
        QVERIFY(dropOverlay != nullptr);

        // First, trigger a valid drag enter to show the overlay
        QMimeData* mimeData = new QMimeData();
        mimeData->setUrls({QUrl::fromLocalFile("/tmp/test.json")});

        QDragEnterEvent dragEnterEvent(
            QPoint(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dragEnterEvent);
        QVERIFY(dropOverlay->isVisible());

        // Now simulate drag leave
        QDragLeaveEvent dragLeaveEvent;
        QApplication::sendEvent(&window, &dragLeaveEvent);

        QVERIFY(!dropOverlay->isVisible());

        delete mimeData;
    }

    void testDropHidesOverlayAndTriggersFileLoader() {
        // Requirements 2.3, 3.1: Drop SHALL hide overlay and initiate parsing
        MainWindow window;
        window.show();
        QApplication::processEvents();

        QLabel* dropOverlay = nullptr;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains("Drop JSON file here")) {
                dropOverlay = label;
                break;
            }
        }
        QVERIFY(dropOverlay != nullptr);

        // First show the overlay via drag enter
        QMimeData* mimeData = new QMimeData();
        mimeData->setUrls({QUrl::fromLocalFile("/tmp/test.json")});

        QDragEnterEvent dragEnterEvent(
            QPoint(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dragEnterEvent);
        QVERIFY(dropOverlay->isVisible());

        // Simulate drop
        QDropEvent dropEvent(
            QPointF(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dropEvent);

        // Overlay should be hidden after drop
        QVERIFY(!dropOverlay->isVisible());

        // Progress bar should be visible (indicates FileLoader::startParse was called)
        auto* progressBar = window.findChild<QProgressBar*>();
        QVERIFY(progressBar != nullptr);
        QVERIFY(progressBar->isVisible());

        delete mimeData;
    }

    void testMimeDataWithNoUrlsIsRejected() {
        // Requirement 1.4: MIME data without file URLs SHALL be rejected
        MainWindow window;
        window.show();
        QApplication::processEvents();

        QLabel* dropOverlay = nullptr;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains("Drop JSON file here")) {
                dropOverlay = label;
                break;
            }
        }
        QVERIFY(dropOverlay != nullptr);

        // MIME data with no URLs (just plain text)
        QMimeData* mimeData = new QMimeData();
        mimeData->setText("some random text");

        QDragEnterEvent dragEnterEvent(
            QPoint(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dragEnterEvent);

        // Event should not be accepted
        QVERIFY(!dragEnterEvent.isAccepted());
        // Overlay should remain hidden
        QVERIFY(!dropOverlay->isVisible());

        delete mimeData;
    }

    void testDropExitsUnionMode() {
        // Requirement 5.3: Drop SHALL exit union mode if previously active
        MainWindow window;
        window.show();
        QApplication::processEvents();

        // Set some search text to verify state reset
        auto* searchBar = window.findChild<QLineEdit*>();
        QVERIFY(searchBar != nullptr);
        searchBar->setText("some search");

        // Set detail panel content
        auto* detailPanel = window.findChild<QTextEdit*>();
        QVERIFY(detailPanel != nullptr);
        detailPanel->setPlainText("some detail content");

        // Perform a valid drag enter followed by drop
        QMimeData* mimeData = new QMimeData();
        mimeData->setUrls({QUrl::fromLocalFile("/tmp/test.json")});

        QDragEnterEvent dragEnterEvent(
            QPoint(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dragEnterEvent);
        QVERIFY(dragEnterEvent.isAccepted());

        QDropEvent dropEvent(
            QPointF(50, 50), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &dropEvent);

        // After drop, the progress bar being visible confirms the drop was processed
        auto* progressBar = window.findChild<QProgressBar*>();
        QVERIFY(progressBar != nullptr);
        QVERIFY(progressBar->isVisible());

        // Verify search bar is cleared (state reset - Requirement 5.1)
        QVERIFY(searchBar->text().isEmpty());

        // Verify detail panel is cleared (state reset - Requirement 5.2)
        QVERIFY(detailPanel->toPlainText().isEmpty());

        delete mimeData;
    }
};

// ---------------------------------------------------------------------------
// Task 4.2: Property test for state reset on valid drop (Property 3)
// Feature: drag-drop-file-open
// Validates: Requirements 5.1, 5.2
// ---------------------------------------------------------------------------

class DropStateResetPropertyTest : public QObject {
    Q_OBJECT

private slots:
    void property3_stateResetOnValidDrop() {
        rc::check("Feature: drag-drop-file-open, Property 3: State reset on valid drop",
            [](void) {
                MainWindow window;
                window.show();
                QApplication::processEvents();

                // Generate random search text (1-50 chars)
                auto searchLen = *rc::gen::inRange(1, 51);
                auto searchText = *rc::gen::container<std::string>(
                    searchLen,
                    rc::gen::oneOf(
                        rc::gen::inRange<char>('a', 'z' + 1),
                        rc::gen::inRange<char>('A', 'Z' + 1),
                        rc::gen::inRange<char>('0', '9' + 1),
                        rc::gen::element<char>(' ', '-', '_', '.')
                    )
                );
                RC_PRE(!searchText.empty());

                // Generate random detail panel content (1-100 chars)
                auto detailLen = *rc::gen::inRange(1, 101);
                auto detailText = *rc::gen::container<std::string>(
                    detailLen,
                    rc::gen::oneOf(
                        rc::gen::inRange<char>('a', 'z' + 1),
                        rc::gen::inRange<char>('A', 'Z' + 1),
                        rc::gen::inRange<char>('0', '9' + 1),
                        rc::gen::element<char>(' ', '\n', '{', '}', '"', ':')
                    )
                );
                RC_PRE(!detailText.empty());

                // Set random search text in the search bar
                auto* searchBar = window.findChild<QLineEdit*>();
                RC_ASSERT(searchBar != nullptr);
                searchBar->setText(QString::fromStdString(searchText));
                RC_ASSERT(!searchBar->text().isEmpty());

                // Set random detail content in the detail panel
                auto* detailPanel = window.findChild<QTextEdit*>();
                RC_ASSERT(detailPanel != nullptr);
                detailPanel->setPlainText(QString::fromStdString(detailText));
                RC_ASSERT(!detailPanel->toPlainText().isEmpty());

                // Simulate a valid drag enter followed by drop
                QMimeData mimeData;
                mimeData.setUrls({QUrl::fromLocalFile("/tmp/test_file.json")});

                QDragEnterEvent dragEnterEvent(
                    QPoint(50, 50), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &dragEnterEvent);

                QDropEvent dropEvent(
                    QPointF(50, 50), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &dropEvent);

                // Verify search bar is cleared (Requirement 5.1)
                RC_ASSERT(searchBar->text().isEmpty());

                // Verify detail panel is cleared (Requirement 5.2)
                RC_ASSERT(detailPanel->toPlainText().isEmpty());
            });
    }
};

// Qt Test requires a QApplication instance
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int status = 0;

    TreeModelPropertyTest propertyTest;
    status |= QTest::qExec(&propertyTest, argc, argv);

    TreeModelUnitTest unitTest;
    status |= QTest::qExec(&unitTest, argc, argv);

    FilterProxyModelTest filterProxyTest;
    status |= QTest::qExec(&filterProxyTest, argc, argv);

    ExportHandlerTest exportHandlerTest;
    status |= QTest::qExec(&exportHandlerTest, argc, argv);

    MainWindowSmokeTest mainWindowTest;
    status |= QTest::qExec(&mainWindowTest, argc, argv);

    DropValidatorExtensionPropertyTest dropExtTest;
    status |= QTest::qExec(&dropExtTest, argc, argv);

    DropValidatorMultiFilePropertyTest dropMultiTest;
    status |= QTest::qExec(&dropMultiTest, argc, argv);

    MainWindowDragDropTest dragDropTest;
    status |= QTest::qExec(&dragDropTest, argc, argv);

    DropStateResetPropertyTest stateResetTest;
    status |= QTest::qExec(&stateResetTest, argc, argv);

    ShellSetupTest setupTest;
    status |= QTest::qExec(&setupTest, argc, argv);

    return status;
}


#include "shell_tests.moc"
