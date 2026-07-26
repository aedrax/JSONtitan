// ---------------------------------------------------------------------------
// Edit Engine Tests (unit + property-based)
// Phase 5a, Commit C6: editValue / renameKey / classifyScalarInput
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/edit_engine.h"
#include "core/json_exporter.h"
#include "core/json_node.h"

using namespace jsontitan::core;

namespace {

// {"a": 1, "b": {"c": "x", "d": true}, "arr": [10, "s", null]}
auto makeFixtureTree() -> std::shared_ptr<const JsonNode> {
    std::vector<std::shared_ptr<const JsonNode>> inner;
    inner.push_back(JsonNode::makeString("c", "x"));
    inner.push_back(JsonNode::makeBool("d", true));

    std::vector<std::shared_ptr<const JsonNode>> arr;
    arr.push_back(JsonNode::makeNumber("", "10"));
    arr.push_back(JsonNode::makeString("", "s"));
    arr.push_back(JsonNode::makeNull(""));

    std::vector<std::shared_ptr<const JsonNode>> rootChildren;
    rootChildren.push_back(JsonNode::makeNumber("a", "1"));
    rootChildren.push_back(JsonNode::makeObject("b", std::move(inner)));
    rootChildren.push_back(JsonNode::makeArray("arr", std::move(arr)));
    return JsonNode::makeObject("", std::move(rootChildren));
}

const JsonNode* childByKey(const JsonNode& parent, const std::string& key) {
    for (const auto& child : parent.children) {
        if (child->key == key) {
            return child.get();
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// editValue — unit tests
// ---------------------------------------------------------------------------

TEST(EditEngine, EditValueReplacesScalarWithStructuralSharing) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("b"), std::string("c")};

    auto result = editValue(root, path, ScalarValue{NodeType::Number, "42"});
    ASSERT_TRUE(result.has_value());
    auto newRoot = *result;
    ASSERT_NE(newRoot, nullptr);
    EXPECT_NE(newRoot.get(), root.get());

    // Edited node has the new value AND the new type; key preserved.
    const auto* newB = childByKey(*newRoot, "b");
    ASSERT_NE(newB, nullptr);
    const auto* newC = childByKey(*newB, "c");
    ASSERT_NE(newC, nullptr);
    EXPECT_EQ(newC->type, NodeType::Number);
    EXPECT_EQ(newC->value, "42");
    EXPECT_EQ(newC->key, "c");

    // Untouched siblings are pointer-identical between old and new roots.
    const auto* oldB = childByKey(*root, "b");
    EXPECT_EQ(childByKey(*newRoot, "a"), childByKey(*root, "a"));
    EXPECT_EQ(childByKey(*newRoot, "arr"), childByKey(*root, "arr"));
    EXPECT_EQ(childByKey(*newB, "d"), childByKey(*oldB, "d"));

    // Original tree untouched.
    const auto* oldC = childByKey(*oldB, "c");
    EXPECT_EQ(oldC->type, NodeType::String);
    EXPECT_EQ(oldC->value, "x");
}

TEST(EditEngine, EditValueOnContainerIsNotEditable) {
    auto root = makeFixtureTree();
    NodePath objectPath = {std::string("b")};
    auto result = editValue(root, objectPath, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, EditErrorCode::NotEditable);

    NodePath arrayPath = {std::string("arr")};
    auto result2 = editValue(root, arrayPath, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error().code, EditErrorCode::NotEditable);
}

TEST(EditEngine, EditValueBadPathIsInvalidPath) {
    auto root = makeFixtureTree();

    NodePath missingKey = {std::string("nope")};
    auto r1 = editValue(root, missingKey, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error().code, EditErrorCode::InvalidPath);

    NodePath badIndex = {std::string("arr"), std::size_t{99}};
    auto r2 = editValue(root, badIndex, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, EditErrorCode::InvalidPath);

    // Key segment into an array is a type mismatch, also InvalidPath.
    NodePath keyIntoArray = {std::string("arr"), std::string("k")};
    auto r3 = editValue(root, keyIntoArray, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(r3.has_value());
    EXPECT_EQ(r3.error().code, EditErrorCode::InvalidPath);

    // Null root.
    auto r4 = editValue(nullptr, {}, ScalarValue{NodeType::String, "x"});
    ASSERT_FALSE(r4.has_value());
    EXPECT_EQ(r4.error().code, EditErrorCode::InvalidPath);
}

TEST(EditEngine, EditValueExplicitInvalidNumberIsRejected) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("a")};
    for (const char* bad : {"", "abc", "0123", "1.", ".5", "1e", "--2", "+1", "NaN", "Infinity"}) {
        auto result = editValue(root, path, ScalarValue{NodeType::Number, bad});
        ASSERT_FALSE(result.has_value()) << "accepted invalid number: " << bad;
        EXPECT_EQ(result.error().code, EditErrorCode::InvalidNumber) << bad;
    }
}

// ---------------------------------------------------------------------------
// classifyScalarInput / isRfc8259Number
// ---------------------------------------------------------------------------

TEST(EditEngine, ClassifyScalarInputTable) {
    struct Case {
        const char* text;
        NodeType expectedType;
    };
    const Case cases[] = {
        {"true", NodeType::Boolean},
        {"false", NodeType::Boolean},
        {"null", NodeType::Null},
        {"42", NodeType::Number},
        {"-1.5e3", NodeType::Number},
        {"0123", NodeType::String},  // leading zero: invalid RFC 8259 number
        {"hello", NodeType::String},
        {"", NodeType::String},
    };
    for (const auto& c : cases) {
        auto result = classifyScalarInput(c.text);
        EXPECT_EQ(result.type, c.expectedType) << "input: '" << c.text << "'";
        if (c.expectedType != NodeType::Null) {
            EXPECT_EQ(result.text, c.text) << "input: '" << c.text << "'";
        }
    }
}

TEST(EditEngine, IsRfc8259NumberCases) {
    // Valid per RFC 8259.
    for (const char* valid :
         {"0", "-0", "42", "-1", "1.5", "-1.5e3", "1e10", "1E+10", "2e-7",
          "0.001", "123456789", "0e0"}) {
        EXPECT_TRUE(isRfc8259Number(valid)) << valid;
    }
    // Invalid per RFC 8259.
    for (const char* invalid :
         {"", "-", "+1", "0123", "1.", ".5", "1e", "1e+", "01", "--1", "1..2",
          "1.2.3", "e5", "NaN", "Infinity", " 1", "1 ", "0x10"}) {
        EXPECT_FALSE(isRfc8259Number(invalid)) << invalid;
    }
}

// ---------------------------------------------------------------------------
// renameKey — unit tests
// ---------------------------------------------------------------------------

TEST(EditEngine, RenameKeySuccessSharesChildrenByPointer) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("b")};

    auto result = renameKey(root, path, "renamed");
    ASSERT_TRUE(result.has_value());
    auto newRoot = *result;
    ASSERT_NE(newRoot, nullptr);
    EXPECT_NE(newRoot.get(), root.get());

    const auto* oldB = childByKey(*root, "b");
    const auto* newB = childByKey(*newRoot, "renamed");
    ASSERT_NE(newB, nullptr);
    EXPECT_EQ(childByKey(*newRoot, "b"), nullptr);
    EXPECT_EQ(newB->type, oldB->type);

    // The renamed node's CHILDREN are shared by pointer with the original.
    ASSERT_EQ(newB->children.size(), oldB->children.size());
    for (std::size_t i = 0; i < newB->children.size(); ++i) {
        EXPECT_EQ(newB->children[i].get(), oldB->children[i].get());
    }

    // Untouched siblings shared too.
    EXPECT_EQ(childByKey(*newRoot, "a"), childByKey(*root, "a"));
    EXPECT_EQ(childByKey(*newRoot, "arr"), childByKey(*root, "arr"));
}

TEST(EditEngine, RenameKeyDuplicateKeyOnSiblingCollision) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("a")};
    auto result = renameKey(root, path, "arr");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, EditErrorCode::DuplicateKey);
}

TEST(EditEngine, RenameKeySameKeyReturnsSameRootPointer) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("b")};
    auto result = renameKey(root, path, "b");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get(), root.get());
}

TEST(EditEngine, RenameKeyArrayElementIsInvalidPath) {
    auto root = makeFixtureTree();
    NodePath path = {std::string("arr"), std::size_t{0}};
    auto result = renameKey(root, path, "newkey");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, EditErrorCode::InvalidPath);
}

TEST(EditEngine, RenameKeyRootIsInvalidPath) {
    auto root = makeFixtureTree();
    auto result = renameKey(root, {}, "newkey");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, EditErrorCode::InvalidPath);
}

// ---------------------------------------------------------------------------
// Property-based tests
// ---------------------------------------------------------------------------

namespace {

// Generate a random JsonNode tree with configurable max depth and breadth.
// (Same shape as the deletion-engine PBT generator.)
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNode(int maxDepth = 3,
                                                     int maxBreadth = 4) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        int choice = (maxDepth <= 0) ? *rc::gen::inRange(2, 6) : *rc::gen::inRange(0, 6);
        switch (choice) {
            case 0: {
                int childCount = *rc::gen::inRange(1, maxBreadth + 1);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < childCount; ++i) {
                    auto child = *genJsonNode(maxDepth - 1, maxBreadth);
                    std::string key = "k" + std::to_string(i);
                    switch (child->type) {
                        case NodeType::Object:
                            children.push_back(JsonNode::makeObject(key, child->children));
                            break;
                        case NodeType::Array:
                            children.push_back(JsonNode::makeArray(key, child->children));
                            break;
                        case NodeType::String:
                            children.push_back(JsonNode::makeString(key, child->value));
                            break;
                        case NodeType::Number:
                            children.push_back(JsonNode::makeNumber(key, child->value));
                            break;
                        case NodeType::Boolean:
                            children.push_back(JsonNode::makeBool(key, child->value == "true"));
                            break;
                        case NodeType::Null:
                            children.push_back(JsonNode::makeNull(key));
                            break;
                    }
                }
                return JsonNode::makeObject("", std::move(children));
            }
            case 1: {
                int elemCount = *rc::gen::inRange(1, maxBreadth + 1);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < elemCount; ++i) {
                    children.push_back(*genJsonNode(maxDepth - 1, maxBreadth));
                }
                return JsonNode::makeArray("", std::move(children));
            }
            case 2: {
                int len = *rc::gen::inRange(1, 12);
                std::string val;
                for (int c = 0; c < len; ++c) {
                    val += static_cast<char>(*rc::gen::inRange<int>('a', 'z'));
                }
                return JsonNode::makeString("", val);
            }
            case 3: {
                int num = *rc::gen::inRange(-1000, 1000);
                return JsonNode::makeNumber("", std::to_string(num));
            }
            case 4: {
                bool b = *rc::gen::arbitrary<bool>();
                return JsonNode::makeBool("", b);
            }
            case 5:
            default:
                return JsonNode::makeNull("");
        }
    });
}

// Root guaranteed to be a container with at least one child.
rc::Gen<std::shared_ptr<const JsonNode>> genContainerNode(int maxDepth = 3,
                                                          int maxBreadth = 4) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        auto node = *genJsonNode(maxDepth, maxBreadth);
        if ((node->type == NodeType::Object || node->type == NodeType::Array) &&
            !node->children.empty()) {
            return node;
        }
        // Wrap scalars into a single-member object.
        std::vector<std::shared_ptr<const JsonNode>> children;
        switch (node->type) {
            case NodeType::String:
                children.push_back(JsonNode::makeString("k0", node->value));
                break;
            case NodeType::Number:
                children.push_back(JsonNode::makeNumber("k0", node->value));
                break;
            case NodeType::Boolean:
                children.push_back(JsonNode::makeBool("k0", node->value == "true"));
                break;
            default:
                children.push_back(JsonNode::makeNull("k0"));
                break;
        }
        return JsonNode::makeObject("", std::move(children));
    });
}

struct PathEntry {
    NodePath path;
    const JsonNode* node;
};

// Collect every non-root node with its full root-relative path.
void collectEntries(const JsonNode* node, const NodePath& prefix,
                    std::vector<PathEntry>& out) {
    for (std::size_t i = 0; i < node->children.size(); ++i) {
        const auto& child = node->children[i];
        NodePath path = prefix;
        if (node->type == NodeType::Object) {
            path.push_back(child->key);
        } else {
            path.push_back(i);
        }
        out.push_back({path, child.get()});
        collectEntries(child.get(), path, out);
    }
}

// Resolve a path over a JsonNode tree; nullptr when unresolvable.
const JsonNode* resolve(const JsonNode* node, const NodePath& path) {
    for (const auto& segment : path) {
        if (!node) return nullptr;
        if (auto* keyPtr = std::get_if<std::string>(&segment)) {
            const JsonNode* found = nullptr;
            for (const auto& child : node->children) {
                if (child->key == *keyPtr) {
                    found = child.get();
                    break;
                }
            }
            node = found;
        } else {
            auto idx = std::get<std::size_t>(segment);
            if (node->type != NodeType::Array || idx >= node->children.size()) {
                return nullptr;
            }
            node = node->children[idx].get();
        }
    }
    return node;
}

// Check that all subtrees NOT on the root->path spine are pointer-shared
// between oldRoot and newRoot.
void assertOffSpineSharing(const std::shared_ptr<const JsonNode>& oldRoot,
                           const std::shared_ptr<const JsonNode>& newRoot,
                           const NodePath& path) {
    const JsonNode* oldNode = oldRoot.get();
    const JsonNode* newNode = newRoot.get();
    for (const auto& segment : path) {
        RC_ASSERT(oldNode->children.size() == newNode->children.size());
        std::size_t spineIdx = oldNode->children.size();
        if (auto* keyPtr = std::get_if<std::string>(&segment)) {
            for (std::size_t i = 0; i < oldNode->children.size(); ++i) {
                if (oldNode->children[i]->key == *keyPtr) {
                    spineIdx = i;
                    break;
                }
            }
        } else {
            spineIdx = std::get<std::size_t>(segment);
        }
        RC_ASSERT(spineIdx < oldNode->children.size());
        for (std::size_t i = 0; i < oldNode->children.size(); ++i) {
            if (i != spineIdx) {
                // Off-spine subtree: pointer identity (full structural sharing).
                RC_ASSERT(newNode->children[i].get() == oldNode->children[i].get());
            }
        }
        oldNode = oldNode->children[spineIdx].get();
        newNode = newNode->children[spineIdx].get();
    }
}

}  // namespace

// For random trees and random scalar-leaf paths: editValue leaves every
// untouched subtree pointer-identical and gives the edited node the new value.
TEST(EditEnginePBT, EditValuePreservesUntouchedSubtreesAndSetsValue) {
    rc::check("editValue: structural sharing off the spine + new value at path",
        []() {
            auto root = *genContainerNode(3, 4);

            std::vector<PathEntry> entries;
            collectEntries(root.get(), {}, entries);
            std::vector<PathEntry> scalars;
            for (const auto& e : entries) {
                if (e.node->type != NodeType::Object &&
                    e.node->type != NodeType::Array) {
                    scalars.push_back(e);
                }
            }
            RC_PRE(!scalars.empty());
            auto pick = *rc::gen::inRange<std::size_t>(0, scalars.size());
            const auto& target = scalars[pick];

            // Random replacement scalar via classifyScalarInput on random text.
            auto textChoice = *rc::gen::inRange(0, 4);
            std::string text;
            switch (textChoice) {
                case 0: text = "true"; break;
                case 1: text = std::to_string(*rc::gen::inRange(-5000, 5000)); break;
                case 2: text = "null"; break;
                default: {
                    int len = *rc::gen::inRange(0, 10);
                    for (int i = 0; i < len; ++i) {
                        text += static_cast<char>(*rc::gen::inRange<int>('a', 'z'));
                    }
                    break;
                }
            }
            auto value = classifyScalarInput(text);

            auto beforeJson = exportJson(*root);
            auto result = editValue(root, target.path, value);
            RC_ASSERT(result.has_value());
            auto newRoot = *result;
            RC_ASSERT(newRoot != nullptr);

            // Edited node: new type + expected value text.
            const JsonNode* edited = resolve(newRoot.get(), target.path);
            RC_ASSERT(edited != nullptr);
            RC_ASSERT(edited->type == value.type);
            if (value.type == NodeType::Null) {
                RC_ASSERT(edited->value == std::string("null"));
            } else {
                RC_ASSERT(edited->value == value.text);
            }
            RC_ASSERT(edited->key == target.node->key);

            // All untouched subtrees are pointer-identical.
            assertOffSpineSharing(root, newRoot, target.path);

            // Original tree not mutated.
            RC_ASSERT(exportJson(*root) == beforeJson);
        });
}

// renameKey then renameKey back yields a tree with identical content.
TEST(EditEnginePBT, RenameKeyRoundTripRestoresContent) {
    rc::check("renameKey there-and-back == original tree content",
        []() {
            auto root = *genContainerNode(3, 4);

            std::vector<PathEntry> entries;
            collectEntries(root.get(), {}, entries);
            std::vector<PathEntry> members;
            for (const auto& e : entries) {
                if (std::holds_alternative<std::string>(e.path.back())) {
                    members.push_back(e);
                }
            }
            RC_PRE(!members.empty());
            auto pick = *rc::gen::inRange<std::size_t>(0, members.size());
            const auto& target = members[pick];
            const std::string oldKey = target.node->key;

            // A fresh key that cannot collide with generated keys ("k<i>").
            std::string newKey =
                "renamed_" + std::to_string(*rc::gen::inRange(0, 1000));

            auto original = exportJson(*root);

            auto renamed = renameKey(root, target.path, newKey);
            RC_ASSERT(renamed.has_value());

            NodePath backPath = target.path;
            backPath.back() = newKey;
            auto restored = renameKey(*renamed, backPath, oldKey);
            RC_ASSERT(restored.has_value());

            RC_ASSERT(exportJson(**restored) == original);
        });
}
