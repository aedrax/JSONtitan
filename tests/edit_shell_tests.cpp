// ---------------------------------------------------------------------------
// Shell Editing Tests
// Phase 5a, Commit C7: inline value editing, key rename, undo/redo
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QToolButton>
#include <QTreeView>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/edit_engine.h"
#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "shell/document_session.h"
#include "shell/edit_controller.h"
#include "shell/filter_proxy_model.h"
#include "shell/model_paths.h"
#include "shell/search_controller.h"
#include "shell/tree_model.h"

using namespace jsontitan::core;

namespace {

// Drain the event loop so queued installs (EditController::queueInstall)
// actually run.
void pumpEvents() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

void fetchAll(TreeModel& model, const QModelIndex& parent = {}) {
    while (model.canFetchMore(parent)) {
        model.fetchMore(parent);
    }
    for (int r = 0; r < model.rowCount(parent); ++r) {
        fetchAll(model, model.index(r, 0, parent));
    }
}

// {"a": 1, "b": {"c": "x"}, "flag": true, "nil": null}
auto makeJsonFixture() -> std::shared_ptr<const JsonNode> {
    std::vector<std::shared_ptr<const JsonNode>> inner;
    inner.push_back(JsonNode::makeString("c", "x"));

    std::vector<std::shared_ptr<const JsonNode>> rootChildren;
    rootChildren.push_back(JsonNode::makeNumber("a", "1"));
    rootChildren.push_back(JsonNode::makeObject("b", std::move(inner)));
    rootChildren.push_back(JsonNode::makeBool("flag", true));
    rootChildren.push_back(JsonNode::makeNull("nil"));
    return JsonNode::makeObject("", std::move(rootChildren));
}

const JsonNode* resolveKey(const JsonNode* node, const std::string& key) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->key == key) return child.get();
    }
    return nullptr;
}

// Full controller stack as MainWindow wires it, minus the window itself.
// Member order gives a safe destruction order (reverse of declaration).
struct Harness {
    TreeModel model;
    FilterProxyModel proxy;
    DocumentSession session{&model};
    QTreeView tree;
    QLineEdit searchBar;
    QToolButton caseToggle;
    QToolButton regexToggle;
    QLabel errorLabel;
    QLabel noResultsLabel;
    QLabel matchCountLabel;
    std::unique_ptr<SearchController> search;
    std::unique_ptr<EditController> edit;

    Harness() {
        proxy.setSourceModel(&model);
        tree.setModel(&proxy);
        search = std::make_unique<SearchController>(
            SearchController::Ui{&searchBar, &caseToggle, &regexToggle,
                                 &errorLabel, &noResultsLabel, &tree,
                                 &matchCountLabel},
            &model, &proxy, &session);
        edit = std::make_unique<EditController>(nullptr, &tree, &model, &proxy,
                                                &session, search.get());
    }

    // Select the node at `path` in the tree view (via the proxy).
    void selectPath(const NodePath& path) {
        QModelIndex sourceIndex = jsontitan::shell::indexForPath(model, path);
        ASSERT_TRUE(sourceIndex.isValid());
        QModelIndex proxyIndex = proxy.mapFromSource(sourceIndex);
        ASSERT_TRUE(proxyIndex.isValid());
        tree.setCurrentIndex(proxyIndex);
    }

    NodePath currentSelection() {
        QModelIndex proxyIndex = tree.currentIndex();
        if (!proxyIndex.isValid()) return {};
        QModelIndex sourceIndex = proxy.mapToSource(proxyIndex);
        if (!sourceIndex.isValid()) return {};
        return jsontitan::shell::nodePathForIndex(model, sourceIndex);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// TreeModel editing contract: flags / EditRole / setData on both backings
// ---------------------------------------------------------------------------

TEST(TreeModelEditing, FlagsAndEditRoleOnJsonBacking) {
    TreeModel model;
    model.setRootNode(makeJsonFixture());
    fetchAll(model);

    auto idx = [&](const NodePath& p) {
        return jsontitan::shell::indexForPath(model, p);
    };

    QModelIndex a = idx({std::string("a")});
    QModelIndex b = idx({std::string("b")});
    QModelIndex c = idx({std::string("b"), std::string("c")});
    QModelIndex flag = idx({std::string("flag")});
    QModelIndex nil = idx({std::string("nil")});
    ASSERT_TRUE(a.isValid() && b.isValid() && c.isValid() && flag.isValid() &&
                nil.isValid());

    // Scalars editable, containers not.
    EXPECT_TRUE(model.flags(a) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(c) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(flag) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(nil) & Qt::ItemIsEditable);
    EXPECT_FALSE(model.flags(b) & Qt::ItemIsEditable);

    // EditRole is the RAW value text, not the composite display string.
    EXPECT_EQ(model.data(a, Qt::EditRole).toString(), QStringLiteral("1"));
    EXPECT_EQ(model.data(c, Qt::EditRole).toString(), QStringLiteral("x"));
    EXPECT_EQ(model.data(flag, Qt::EditRole).toString(),
              QStringLiteral("true"));
    EXPECT_EQ(model.data(nil, Qt::EditRole).toString(),
              QStringLiteral("null"));
    EXPECT_FALSE(model.data(b, Qt::EditRole).isValid());
}

TEST(TreeModelEditing, FlagsAndEditRoleOnArenaBacking) {
    // Note: the arena pipeline canonicalizes number text (e.g. -1.5e3 would
    // become -1500), so the fixture uses an already-canonical number.
    auto arena = parseBuffer(
        R"({"num": -1500, "str": "hello", "yes": true, "no": false, "nil": null, "obj": {"k": 1}, "arr": [1]})");
    ASSERT_TRUE(arena.ok());

    TreeModel model;
    model.setArenaRoot(std::make_shared<ArenaParseResult>(std::move(arena)));
    fetchAll(model);

    auto idx = [&](const char* key) {
        return jsontitan::shell::indexForPath(model, {std::string(key)});
    };

    // Arena scalars are editable too (conversion happens on first commit).
    EXPECT_TRUE(model.flags(idx("num")) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(idx("str")) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(idx("yes")) & Qt::ItemIsEditable);
    EXPECT_TRUE(model.flags(idx("nil")) & Qt::ItemIsEditable);
    EXPECT_FALSE(model.flags(idx("obj")) & Qt::ItemIsEditable);
    EXPECT_FALSE(model.flags(idx("arr")) & Qt::ItemIsEditable);

    EXPECT_EQ(model.data(idx("num"), Qt::EditRole).toString(),
              QStringLiteral("-1500"));
    EXPECT_EQ(model.data(idx("str"), Qt::EditRole).toString(),
              QStringLiteral("hello"));
    EXPECT_EQ(model.data(idx("yes"), Qt::EditRole).toString(),
              QStringLiteral("true"));
    EXPECT_EQ(model.data(idx("no"), Qt::EditRole).toString(),
              QStringLiteral("false"));
    EXPECT_EQ(model.data(idx("nil"), Qt::EditRole).toString(),
              QStringLiteral("null"));
    EXPECT_FALSE(model.data(idx("obj"), Qt::EditRole).isValid());
}

TEST(TreeModelEditing, SetDataDelegatesToHandlerAndEmitsNothing) {
    TreeModel model;
    model.setRootNode(makeJsonFixture());
    fetchAll(model);

    QModelIndex a = jsontitan::shell::indexForPath(model, {std::string("a")});
    ASSERT_TRUE(a.isValid());

    // Without a handler, setData rejects the commit.
    EXPECT_FALSE(model.setData(a, QStringLiteral("2"), Qt::EditRole));

    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    QModelIndex seenIndex;
    QString seenText;
    bool handlerResult = true;
    model.setEditCommitHandler(
        [&](const QModelIndex& index, const QString& text) {
            seenIndex = index;
            seenText = text;
            return handlerResult;
        });

    // Handler round-trip: index and raw text arrive; result is forwarded.
    EXPECT_TRUE(model.setData(a, QStringLiteral("42"), Qt::EditRole));
    EXPECT_EQ(seenIndex, a);
    EXPECT_EQ(seenText, QStringLiteral("42"));

    handlerResult = false;
    EXPECT_FALSE(model.setData(a, QStringLiteral("x"), Qt::EditRole));

    // Non-EditRole commits are rejected without consulting the handler.
    seenText.clear();
    EXPECT_FALSE(model.setData(a, QStringLiteral("y"), Qt::DisplayRole));
    EXPECT_TRUE(seenText.isEmpty());

    // The model itself emits nothing: the handler owns installation.
    EXPECT_EQ(dataChangedSpy.count(), 0);
    EXPECT_EQ(resetSpy.count(), 0);
}

// ---------------------------------------------------------------------------
// EditController: applyEdit on an arena-backed tree converts + installs;
// undo restores the pre-edit tree and selection; redo re-applies.
// ---------------------------------------------------------------------------

TEST(EditControllerEditing, ApplyEditOnArenaConvertsInstallsUndoesRedoes) {
    Harness h;

    auto arena = parseBuffer(R"({"a": 1, "b": {"c": "x"}})");
    ASSERT_TRUE(arena.ok());
    h.session.setArenaRoot(std::make_shared<ArenaParseResult>(std::move(arena)),
                           QStringLiteral("/tmp/t.json"),
                           QStringLiteral("t.json"));
    fetchAll(h.model);
    ASSERT_NE(h.model.arenaResult(), nullptr);
    ASSERT_EQ(h.model.rootNode(), nullptr);
    ASSERT_FALSE(h.session.canUndo());

    QModelIndex a = jsontitan::shell::indexForPath(h.model, {std::string("a")});
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(h.model.flags(a) & Qt::ItemIsEditable);

    // Commit "hello" on the arena scalar. The commit is accepted
    // synchronously; the install (and thus the backing switch) is queued.
    ASSERT_TRUE(h.edit->applyEdit(a, QStringLiteral("hello")));
    EXPECT_NE(h.model.arenaResult(), nullptr);  // not reset synchronously
    pumpEvents();

    // Backing switched to JsonNode; value updated; document modified.
    EXPECT_EQ(h.model.arenaResult(), nullptr);
    ASSERT_NE(h.model.rootNode(), nullptr);
    EXPECT_EQ(h.session.arenaResult(), nullptr);
    ASSERT_NE(h.session.currentRoot(), nullptr);
    const auto* edited = resolveKey(h.session.currentRoot().get(), "a");
    ASSERT_NE(edited, nullptr);
    EXPECT_EQ(edited->type, NodeType::String);
    EXPECT_EQ(edited->value, "hello");
    EXPECT_TRUE(h.session.modified());
    EXPECT_TRUE(h.session.canUndo());
    EXPECT_EQ(h.currentSelection(), (NodePath{std::string("a")}));

    // Undo restores the (converted) pre-edit tree and reselects the node.
    h.edit->undo();
    pumpEvents();
    const auto* restored = resolveKey(h.session.currentRoot().get(), "a");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->type, NodeType::Number);
    EXPECT_EQ(restored->value, "1");
    EXPECT_EQ(h.currentSelection(), (NodePath{std::string("a")}));
    EXPECT_FALSE(h.session.canUndo());
    EXPECT_TRUE(h.session.canRedo());
    EXPECT_TRUE(h.session.modified());  // undo keeps modified set

    // Redo re-applies the edit.
    h.edit->redo();
    pumpEvents();
    const auto* redone = resolveKey(h.session.currentRoot().get(), "a");
    ASSERT_NE(redone, nullptr);
    EXPECT_EQ(redone->type, NodeType::String);
    EXPECT_EQ(redone->value, "hello");
    EXPECT_TRUE(h.session.canUndo());
    EXPECT_FALSE(h.session.canRedo());
}

TEST(EditControllerEditing, NoOpCommitDoesNotPushUndo) {
    Harness h;
    h.session.setJsonRoot(makeJsonFixture(), QStringLiteral("/tmp/t.json"),
                          QStringLiteral("t.json"), false);
    fetchAll(h.model);

    QModelIndex a = jsontitan::shell::indexForPath(h.model, {std::string("a")});
    ASSERT_TRUE(a.isValid());
    auto rootBefore = h.session.currentRoot();

    // Committing the unchanged raw text is accepted but mutates nothing.
    EXPECT_TRUE(h.edit->applyEdit(a, QStringLiteral("1")));
    pumpEvents();
    EXPECT_EQ(h.session.currentRoot().get(), rootBefore.get());
    EXPECT_FALSE(h.session.canUndo());
    EXPECT_FALSE(h.session.modified());
}

// ---------------------------------------------------------------------------
// Delete then undo restores rowCount and content; redo re-applies.
// ---------------------------------------------------------------------------

TEST(EditControllerEditing, DeleteThenUndoRestoresThenRedoReapplies) {
    Harness h;
    h.session.setJsonRoot(makeJsonFixture(), QStringLiteral("/tmp/t.json"),
                          QStringLiteral("t.json"), false);
    fetchAll(h.model);
    ASSERT_EQ(h.model.rowCount(QModelIndex()), 4);

    h.selectPath({std::string("flag")});
    h.edit->deleteSelectedNode();  // synchronous install
    fetchAll(h.model);

    EXPECT_EQ(h.model.rowCount(QModelIndex()), 3);
    EXPECT_EQ(resolveKey(h.session.currentRoot().get(), "flag"), nullptr);
    EXPECT_TRUE(h.session.modified());
    EXPECT_TRUE(h.session.canUndo());

    // Undo restores the deleted node (count and content) and reselects it.
    h.edit->undo();
    pumpEvents();
    fetchAll(h.model);
    EXPECT_EQ(h.model.rowCount(QModelIndex()), 4);
    const auto* flag = resolveKey(h.session.currentRoot().get(), "flag");
    ASSERT_NE(flag, nullptr);
    EXPECT_EQ(flag->type, NodeType::Boolean);
    EXPECT_EQ(flag->value, "true");
    EXPECT_EQ(h.currentSelection(), (NodePath{std::string("flag")}));
    EXPECT_TRUE(h.session.canRedo());

    // Redo re-applies the deletion.
    h.edit->redo();
    pumpEvents();
    fetchAll(h.model);
    EXPECT_EQ(h.model.rowCount(QModelIndex()), 3);
    EXPECT_EQ(resolveKey(h.session.currentRoot().get(), "flag"), nullptr);
}

// ---------------------------------------------------------------------------
// Rename key via the engine path + duplicate rejection surfaces the error.
// ---------------------------------------------------------------------------

TEST(EditControllerEditing, ApplyRenameSucceedsAndDuplicateIsRejected) {
    Harness h;
    h.session.setJsonRoot(makeJsonFixture(), QStringLiteral("/tmp/t.json"),
                          QStringLiteral("t.json"), false);
    fetchAll(h.model);

    // Successful rename: a -> z, undoable, selection follows the new key.
    auto error =
        h.edit->applyRename({std::string("a")}, QStringLiteral("z"));
    EXPECT_FALSE(error.has_value());
    pumpEvents();
    EXPECT_EQ(resolveKey(h.session.currentRoot().get(), "a"), nullptr);
    const auto* z = resolveKey(h.session.currentRoot().get(), "z");
    ASSERT_NE(z, nullptr);
    EXPECT_EQ(z->type, NodeType::Number);
    EXPECT_EQ(z->value, "1");
    EXPECT_TRUE(h.session.canUndo());
    EXPECT_EQ(h.currentSelection(), (NodePath{std::string("z")}));

    // Duplicate key: error surfaces, nothing installs, nothing is pushed.
    auto rootBefore = h.session.currentRoot();
    auto dupError =
        h.edit->applyRename({std::string("z")}, QStringLiteral("flag"));
    ASSERT_TRUE(dupError.has_value());
    EXPECT_EQ(dupError->code, EditErrorCode::DuplicateKey);
    pumpEvents();
    EXPECT_EQ(h.session.currentRoot().get(), rootBefore.get());

    // Same-key rename is a no-op success (nothing installed, nothing pushed).
    auto noopError = h.edit->applyRename({std::string("nil")},
                                         QStringLiteral("nil"));
    EXPECT_FALSE(noopError.has_value());
    pumpEvents();
    EXPECT_EQ(h.session.currentRoot().get(), rootBefore.get());

    // Undo restores the original key.
    h.edit->undo();
    pumpEvents();
    EXPECT_NE(resolveKey(h.session.currentRoot().get(), "a"), nullptr);
    EXPECT_EQ(resolveKey(h.session.currentRoot().get(), "z"), nullptr);
}

// ---------------------------------------------------------------------------
// Undo history is cleared when a new document is installed.
// ---------------------------------------------------------------------------

TEST(EditControllerEditing, UndoStackClearedOnNewDocumentLoad) {
    Harness h;
    h.session.setJsonRoot(makeJsonFixture(), QStringLiteral("/tmp/t.json"),
                          QStringLiteral("t.json"), false);
    fetchAll(h.model);

    QSignalSpy availabilitySpy(&h.session,
                               &DocumentSession::undoAvailabilityChanged);

    h.selectPath({std::string("a")});
    h.edit->deleteSelectedNode();
    ASSERT_TRUE(h.session.canUndo());

    // New JsonNode-backed document (e.g. a union load) clears the history…
    h.session.setJsonRoot(makeJsonFixture(), QStringLiteral("/tmp/u.json"),
                          QStringLiteral("u.json"), false);
    EXPECT_FALSE(h.session.canUndo());
    EXPECT_FALSE(h.session.canRedo());

    // …and so does an arena-backed one (single-file parse completion).
    fetchAll(h.model);
    h.selectPath({std::string("a")});
    h.edit->deleteSelectedNode();
    ASSERT_TRUE(h.session.canUndo());
    auto arena = parseBuffer(R"({"fresh": 1})");
    ASSERT_TRUE(arena.ok());
    h.session.setArenaRoot(std::make_shared<ArenaParseResult>(std::move(arena)),
                           QStringLiteral("/tmp/f.json"),
                           QStringLiteral("f.json"));
    EXPECT_FALSE(h.session.canUndo());
    EXPECT_FALSE(h.session.canRedo());

    // The availability signal fired along the way (menu enablement hook).
    EXPECT_GT(availabilitySpy.count(), 0);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
