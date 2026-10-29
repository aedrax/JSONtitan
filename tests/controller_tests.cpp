#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QSignalSpy>
#include <QString>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "shell/document_session.h"
#include "shell/model_paths.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// Phase 3 refactor: DocumentSession state holder + model_paths helpers
// ---------------------------------------------------------------------------

// DocumentSession::ensureEditable must switch the model backing exactly once
// (a single model reset) and release the arena.
TEST(DocumentSessionTest, EnsureEditableSwitchesBackingOnceAndReleasesArena) {
    auto arenaResult = parseBuffer(R"({"a": [1, 2, 3], "b": {"c": "x"}})");
    ASSERT_TRUE(arenaResult.ok());
    auto shared = std::make_shared<ArenaParseResult>(std::move(arenaResult));
    std::weak_ptr<ArenaParseResult> weakArena = shared;

    TreeModel model;
    DocumentSession session(&model);
    session.setArenaRoot(shared, QStringLiteral("/tmp/x.json"),
                         QStringLiteral("x.json"));
    shared.reset();

    ASSERT_NE(session.arenaResult(), nullptr);
    ASSERT_EQ(session.currentRoot(), nullptr);
    ASSERT_NE(model.arenaResult(), nullptr);
    ASSERT_EQ(model.rootNode(), nullptr);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    ASSERT_TRUE(resetSpy.isValid());

    ASSERT_TRUE(session.ensureEditable());

    // Backing switched exactly once...
    EXPECT_EQ(resetSpy.count(), 1);
    EXPECT_NE(session.currentRoot(), nullptr);
    EXPECT_EQ(session.arenaResult(), nullptr);
    EXPECT_EQ(model.rootNode(), session.currentRoot());
    EXPECT_EQ(model.arenaResult(), nullptr);
    // ...and the arena is released (no owner left anywhere).
    EXPECT_TRUE(weakArena.expired());

    // A second call is a no-op: already editable, no further model reset.
    ASSERT_TRUE(session.ensureEditable());
    EXPECT_EQ(resetSpy.count(), 1);
}

TEST(DocumentSessionTest, EnsureEditableFailsWithNothingLoaded) {
    TreeModel model;
    DocumentSession session(&model);
    EXPECT_FALSE(session.ensureEditable());
}

// ---------------------------------------------------------------------------
// model_paths properties over a fetched JsonNode-backed tree, including
// levels larger than the model's lazy-fetch batch size.
//
// NOTE (pre-existing behavior, moved verbatim from MainWindow::onDeleteNode):
// nodePathForIndex stops its upward walk at nodes whose parent index is
// invalid, i.e. the TOP-LEVEL segment of the path is dropped. The full
// round-trip nodePathForIndex(indexForPath(path)) == path therefore only
// holds with the first segment removed. The tests below codify the actual
// behavior; the truncation itself is noted as a pre-existing bug in the
// delete flow (deleteNode expects full root-relative paths).
// ---------------------------------------------------------------------------

namespace {

// Collect the NodePath of every node in the tree (excluding the root, whose
// path is empty).
void collectPaths(const std::shared_ptr<const JsonNode>& node,
                  const NodePath& prefix, std::vector<NodePath>& out) {
    for (std::size_t i = 0; i < node->children.size(); ++i) {
        const auto& child = node->children[i];
        NodePath path = prefix;
        if (node->type == NodeType::Array) {
            path.push_back(i);
        } else {
            path.push_back(child->key);
        }
        out.push_back(path);
        collectPaths(child, path, out);
    }
}

auto makeRoundTripTree() -> std::shared_ptr<const JsonNode> {
    // A wide array (250 > FETCH_BATCH_SIZE = 100) forces indexForPath to
    // actually loop on fetchMore.
    std::vector<std::shared_ptr<const JsonNode>> wide;
    for (int i = 0; i < 250; ++i) {
        wide.push_back(JsonNode::makeNumber("", std::to_string(i)));
    }

    std::vector<std::shared_ptr<const JsonNode>> inner;
    inner.push_back(JsonNode::makeString("x", "1"));
    inner.push_back(JsonNode::makeBool("y", true));

    std::vector<std::shared_ptr<const JsonNode>> arrayOfObjects;
    arrayOfObjects.push_back(JsonNode::makeObject("", std::move(inner)));
    arrayOfObjects.push_back(JsonNode::makeNull(""));

    std::vector<std::shared_ptr<const JsonNode>> rootChildren;
    rootChildren.push_back(JsonNode::makeArray("wide", std::move(wide)));
    rootChildren.push_back(JsonNode::makeArray("beta", std::move(arrayOfObjects)));
    std::vector<std::shared_ptr<const JsonNode>> alpha;
    alpha.push_back(JsonNode::makeString("deep", "value"));
    rootChildren.push_back(JsonNode::makeObject("alpha", std::move(alpha)));

    return JsonNode::makeObject("", std::move(rootChildren));
}

}  // namespace

// indexForPath descends the full root-relative path and must resolve every
// node of the tree to the exact JsonNode the path denotes.
TEST(ModelPathsTest, IndexForPathResolvesEveryNode) {
    auto root = makeRoundTripTree();

    TreeModel model;
    model.setRootNode(root);

    std::vector<NodePath> paths;
    collectPaths(root, {}, paths);
    ASSERT_FALSE(paths.empty());

    for (const auto& path : paths) {
        QModelIndex index = jsontitan::shell::indexForPath(model, path);
        ASSERT_TRUE(index.isValid());

        // Resolve the same path over the raw tree and compare node identity.
        const JsonNode* expected = root.get();
        for (const auto& segment : path) {
            if (auto* key = std::get_if<std::string>(&segment)) {
                const JsonNode* found = nullptr;
                for (const auto& child : expected->children) {
                    if (child->key == *key) {
                        found = child.get();
                        break;
                    }
                }
                expected = found;
            } else {
                expected = expected->children[std::get<std::size_t>(segment)].get();
            }
            ASSERT_NE(expected, nullptr);
        }
        EXPECT_EQ(model.jsonNodeForIndex(index), expected);
    }
}

// Regression: a later member with a duplicated key got the same path as the
// first ("a"), so Delete and inline edits on it acted on the first member.
TEST(ModelPathsTest, DuplicateKeyMembersGetDistinctPaths) {
    auto root = JsonNode::makeObject("", {JsonNode::makeNumber("a", "1"),
                                          JsonNode::makeNumber("b", "2"),
                                          JsonNode::makeNumber("a", "3")});
    TreeModel model;
    model.setRootNode(root);

    const NodePath first = {std::string("a")};
    const NodePath later = {std::size_t{2}};
    auto laterIndex = jsontitan::shell::indexForPath(model, later);
    ASSERT_TRUE(laterIndex.isValid());
    EXPECT_EQ(jsontitan::shell::nodePathForIndex(model, laterIndex), later);
    EXPECT_EQ(model.jsonNodeForIndex(laterIndex)->value, "3");
    EXPECT_EQ(jsontitan::shell::nodePathForIndex(
                  model, jsontitan::shell::indexForPath(model, first)),
              first);

    // Repeated asks (a multi-selection) switch from scanning the earlier
    // siblings to keys hashed once per object; both must agree.
    const auto bIndex = jsontitan::shell::indexForPath(model, {std::string("b")});
    for (int ask = 0; ask < 20; ++ask) {
        EXPECT_TRUE(model.isLaterDuplicateKey(laterIndex)) << ask;
        EXPECT_FALSE(model.isLaterDuplicateKey(bIndex)) << ask;
    }
    EXPECT_EQ(jsontitan::shell::nodePathForIndex(model, laterIndex), later);
}

// Regression: indexForPath fetched every row of each level it passed, so
// one edit in a multi-million-row array inserted every row into the view.
TEST(ModelPathsTest, IndexForPathFetchesOnlyWhatThePathNeeds) {
    std::vector<std::shared_ptr<const JsonNode>> elements;
    std::vector<std::shared_ptr<const JsonNode>> members;
    for (int i = 0; i < 1000; ++i) {
        elements.push_back(JsonNode::makeNumber("", std::to_string(i)));
        members.push_back(JsonNode::makeNumber("k" + std::to_string(i), "0"));
    }
    std::vector<std::shared_ptr<const JsonNode>> rootChildren;
    rootChildren.push_back(JsonNode::makeArray("list", std::move(elements)));
    rootChildren.push_back(JsonNode::makeObject("map", std::move(members)));
    auto root = JsonNode::makeObject("", std::move(rootChildren));

    TreeModel model;
    model.setRootNode(root);

    auto byIndex = jsontitan::shell::indexForPath(model, {std::string("list"), std::size_t{150}});
    ASSERT_TRUE(byIndex.isValid());
    EXPECT_EQ(model.jsonNodeForIndex(byIndex)->value, "150");
    EXPECT_EQ(model.rowCount(byIndex.parent()), 200);  // two batches of 100

    auto byKey = jsontitan::shell::indexForPath(model, {std::string("map"), std::string("k5")});
    ASSERT_TRUE(byKey.isValid());
    EXPECT_EQ(model.rowCount(byKey.parent()), 100);  // found in the first batch

    // A key that does not exist still searches every batch.
    EXPECT_FALSE(jsontitan::shell::indexForPath(model, {std::string("map"), std::string("nope")})
                     .isValid());
    EXPECT_EQ(model.rowCount(byKey.parent()), 1000);
}

// Round-trip property as it actually holds today: the walk drops the
// top-level segment (see note above), so
//   nodePathForIndex(indexForPath(path)) == path minus its first segment.
TEST(ModelPathsTest, PathIndexRoundTrip) {
    auto root = makeRoundTripTree();

    TreeModel model;
    model.setRootNode(root);

    std::vector<NodePath> paths;
    collectPaths(root, {}, paths);
    ASSERT_FALSE(paths.empty());

    for (const auto& path : paths) {
        QModelIndex index = jsontitan::shell::indexForPath(model, path);
        ASSERT_TRUE(index.isValid());
        NodePath roundTripped =
            jsontitan::shell::nodePathForIndex(model, index);
        EXPECT_EQ(roundTripped, path);
    }
}

TEST(ModelPathsTest, IndexForPathFailsForMissingKey) {
    auto root = makeRoundTripTree();
    TreeModel model;
    model.setRootNode(root);

    NodePath missing;
    missing.push_back(std::string("no-such-key"));
    EXPECT_FALSE(jsontitan::shell::indexForPath(model, missing).isValid());
}
