// ---------------------------------------------------------------------------
// Property-Based Tests for Deletion Engine
// Feature: element-deletion
// ---------------------------------------------------------------------------
// Tests validate the core deletion engine's correctness properties using
// RapidCheck for property-based testing with Google Test as the framework.
//
// **Validates: Requirements 1.2, 3.1, 3.2, 3.3, 3.4, 3.5, 3.7**
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <memory>
#include <string>
#include <vector>

#include "core/deletion_engine.h"
#include "core/json_exporter.h"
#include "core/json_node.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

// Generate a random JsonNode tree with configurable max depth and breadth.
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNode(int maxDepth = 3, int maxBreadth = 5) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        // At depth 0 or below, only generate scalars
        int choice = (maxDepth <= 0) ? *rc::gen::inRange(2, 6) : *rc::gen::inRange(0, 6);
        switch (choice) {
            case 0: {
                // Object with 1-maxBreadth children
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
                // Array with 1-maxBreadth elements
                int elemCount = *rc::gen::inRange(1, maxBreadth + 1);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < elemCount; ++i) {
                    children.push_back(*genJsonNode(maxDepth - 1, maxBreadth));
                }
                return JsonNode::makeArray("", std::move(children));
            }
            case 2: {
                // String value
                int len = *rc::gen::inRange(1, 20);
                std::string val;
                for (int c = 0; c < len; ++c) {
                    val += static_cast<char>(*rc::gen::inRange<int>('a', 'z'));
                }
                return JsonNode::makeString("", val);
            }
            case 3: {
                // Number value
                int num = *rc::gen::inRange(-1000, 1000);
                return JsonNode::makeNumber("", std::to_string(num));
            }
            case 4: {
                // Boolean
                bool b = *rc::gen::arbitrary<bool>();
                return JsonNode::makeBool("", b);
            }
            case 5:
            default: {
                // Null
                return JsonNode::makeNull("");
            }
        }
    });
}

// Generate a tree that is guaranteed to be an Object or Array (has children).
rc::Gen<std::shared_ptr<const JsonNode>> genContainerNode(int maxDepth = 3, int maxBreadth = 5) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        int choice = *rc::gen::inRange(0, 2);
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
        if (choice == 0) {
            return JsonNode::makeObject("", std::move(children));
        } else {
            return JsonNode::makeArray("", std::move(children));
        }
    });
}

// Generate a tree that is guaranteed to be an Array with at least 2 elements.
rc::Gen<std::shared_ptr<const JsonNode>> genArrayNode(int maxDepth = 2, int maxBreadth = 5) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        int elemCount = *rc::gen::inRange(2, maxBreadth + 1);
        std::vector<std::shared_ptr<const JsonNode>> children;
        for (int i = 0; i < elemCount; ++i) {
            children.push_back(*genJsonNode(maxDepth - 1, maxBreadth));
        }
        return JsonNode::makeArray("", std::move(children));
    });
}

// Given a tree, generate a valid path to an existing node within it.
// Returns a pair of (path, pointer to the target node).
struct PathAndTarget {
    NodePath path;
    const JsonNode* target;
};

PathAndTarget pickRandomPath(const std::shared_ptr<const JsonNode>& root) {
    // Collect all valid paths by walking the tree
    struct Entry {
        NodePath path;
        const JsonNode* node;
    };
    std::vector<Entry> allPaths;

    // BFS to collect all non-root nodes with their paths
    struct Frame {
        const JsonNode* node;
        NodePath pathSoFar;
    };
    std::vector<Frame> stack;
    stack.push_back({root.get(), {}});

    while (!stack.empty()) {
        auto frame = stack.back();
        stack.pop_back();

        for (std::size_t i = 0; i < frame.node->children.size(); ++i) {
            const auto& child = frame.node->children[i];
            NodePath childPath = frame.pathSoFar;
            if (frame.node->type == NodeType::Object) {
                childPath.push_back(child->key);
            } else {
                childPath.push_back(i);
            }
            allPaths.push_back({childPath, child.get()});
            if (!child->children.empty()) {
                stack.push_back({child.get(), childPath});
            }
        }
    }

    // Pick a random path from the collected ones
    int idx = *rc::gen::inRange(0, static_cast<int>(allPaths.size()));
    return {allPaths[static_cast<std::size_t>(idx)].path,
            allPaths[static_cast<std::size_t>(idx)].node};
}

rc::Gen<NodePath> genValidPath(const std::shared_ptr<const JsonNode>& root) {
    return rc::gen::exec([root]() -> NodePath {
        return pickRandomPath(root).path;
    });
}

// Generate a path that is unlikely to exist in any tree.
rc::Gen<NodePath> genInvalidPath() {
    return rc::gen::exec([]() -> NodePath {
        int segCount = *rc::gen::inRange(1, 5);
        NodePath path;
        for (int i = 0; i < segCount; ++i) {
            int choice = *rc::gen::inRange(0, 2);
            if (choice == 0) {
                // Use a key that's very unlikely to exist
                std::string key = "__nonexistent_" + std::to_string(*rc::gen::inRange(10000, 99999));
                path.push_back(key);
            } else {
                // Use an index that's very large
                std::size_t idx = static_cast<std::size_t>(*rc::gen::inRange(10000, 99999));
                path.push_back(idx);
            }
        }
        return path;
    });
}

// Helper: check if a node exists at a given path in a tree
bool nodeExistsAtPath(const std::shared_ptr<const JsonNode>& root, const NodePath& path) {
    if (!root) return false;
    const JsonNode* current = root.get();
    for (const auto& segment : path) {
        if (auto* keyPtr = std::get_if<std::string>(&segment)) {
            if (current->type != NodeType::Object) return false;
            bool found = false;
            for (const auto& child : current->children) {
                if (child->key == *keyPtr) {
                    current = child.get();
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        } else {
            auto idx = std::get<std::size_t>(segment);
            if (current->type != NodeType::Array) return false;
            if (idx >= current->children.size()) return false;
            current = current->children[idx].get();
        }
    }
    return true;
}

// Helper: collect all node pointers in a tree
void collectAllNodes(const JsonNode* node, std::vector<const JsonNode*>& out) {
    out.push_back(node);
    for (const auto& child : node->children) {
        collectAllNodes(child.get(), out);
    }
}

// Helper: collect all shared_ptrs in a tree (for structural sharing checks)
void collectAllSharedPtrs(const std::shared_ptr<const JsonNode>& node,
                          std::vector<std::shared_ptr<const JsonNode>>& out) {
    out.push_back(node);
    for (const auto& child : node->children) {
        collectAllSharedPtrs(child, out);
    }
}

// Helper: check if a node is on the ancestor path from root to target
bool isOnAncestorPath(const std::shared_ptr<const JsonNode>& root,
                      const NodePath& targetPath,
                      const JsonNode* candidate) {
    // The root itself is on the ancestor path
    if (candidate == root.get()) return true;

    // Walk the path and check each ancestor
    const JsonNode* current = root.get();
    for (std::size_t i = 0; i + 1 < targetPath.size(); ++i) {
        const auto& segment = targetPath[i];
        if (auto* keyPtr = std::get_if<std::string>(&segment)) {
            for (const auto& child : current->children) {
                if (child->key == *keyPtr) {
                    current = child.get();
                    break;
                }
            }
        } else {
            auto idx = std::get<std::size_t>(segment);
            if (idx < current->children.size()) {
                current = current->children[idx].get();
            }
        }
        if (current == candidate) return true;
    }
    return false;
}

} // anonymous namespace

// ===========================================================================
// Property 1: Deletion removes target node
//
// For any valid JsonNode tree and for any valid path within that tree,
// calling deleteNode(root, path) produces a new tree that does not contain
// the node previously at that path.
//
// Feature: element-deletion, Property 1: Deletion removes target node
// **Validates: Requirements 1.2, 3.1, 3.2**
// ===========================================================================

TEST(DeletionEnginePBT, Property1_DeletionRemovesTargetNode) {
    rc::check("Feature: element-deletion, Property 1: Deletion removes target node",
        []() {
            auto root = *genContainerNode(3, 4);
            RC_PRE(!root->children.empty());

            auto pathAndTarget = pickRandomPath(root);
            auto path = pathAndTarget.path;
            RC_PRE(!path.empty());

            // Navigate to the parent of the target node
            const JsonNode* parent = root.get();
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                const auto& segment = path[i];
                if (auto* keyPtr = std::get_if<std::string>(&segment)) {
                    for (const auto& child : parent->children) {
                        if (child->key == *keyPtr) {
                            parent = child.get();
                            break;
                        }
                    }
                } else {
                    auto idx = std::get<std::size_t>(segment);
                    parent = parent->children[idx].get();
                }
            }

            std::size_t originalParentChildCount = parent->children.size();

            // Perform deletion
            auto result = deleteNode(root, path);

            // The result should be non-null (we're not deleting root)
            RC_ASSERT(result != nullptr);

            // Navigate to the parent in the NEW tree
            const JsonNode* newParent = result.get();
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                const auto& segment = path[i];
                if (auto* keyPtr = std::get_if<std::string>(&segment)) {
                    for (const auto& child : newParent->children) {
                        if (child->key == *keyPtr) {
                            newParent = child.get();
                            break;
                        }
                    }
                } else {
                    auto idx = std::get<std::size_t>(segment);
                    newParent = newParent->children[idx].get();
                }
            }

            // The parent should have one fewer child
            RC_ASSERT(newParent->children.size() == originalParentChildCount - 1);

            // For object parents: the key should be absent
            const auto& lastSegment = path.back();
            if (auto* keyPtr = std::get_if<std::string>(&lastSegment)) {
                for (const auto& child : newParent->children) {
                    RC_ASSERT(child->key != *keyPtr);
                }
            }
            // For array parents: the element count reduction is already verified above
        });
}

// ===========================================================================
// Property 2: Array re-indexing after deletion
//
// For any JsonNode tree containing an array node, and for any valid index
// within that array, deleting the element at that index produces a new array
// where the remaining elements are contiguously indexed from 0 to
// (original_length - 2), preserving relative order.
//
// Feature: element-deletion, Property 2: Array re-indexing after deletion
// **Validates: Requirements 1.2, 3.3**
// ===========================================================================

TEST(DeletionEnginePBT, Property2_ArrayReIndexingAfterDeletion) {
    rc::check("Feature: element-deletion, Property 2: Array re-indexing after deletion",
        []() {
            // Generate a root that IS an array with at least 2 elements
            auto arrayNode = *genArrayNode(2, 5);
            RC_PRE(arrayNode->type == NodeType::Array);
            RC_PRE(arrayNode->children.size() >= 2);

            std::size_t originalSize = arrayNode->children.size();

            // Pick a random valid index to delete
            std::size_t deleteIdx = static_cast<std::size_t>(
                *rc::gen::inRange(0, static_cast<int>(originalSize)));

            NodePath path = {deleteIdx};

            // Capture original elements (excluding the one to delete)
            std::vector<const JsonNode*> expectedOrder;
            for (std::size_t i = 0; i < originalSize; ++i) {
                if (i != deleteIdx) {
                    expectedOrder.push_back(arrayNode->children[i].get());
                }
            }

            // Perform deletion
            auto result = deleteNode(arrayNode, path);
            RC_ASSERT(result != nullptr);
            RC_ASSERT(result->type == NodeType::Array);

            // New array should have one fewer element
            RC_ASSERT(result->children.size() == originalSize - 1);

            // Elements should be contiguously indexed 0..(original_size-2)
            // and preserve relative order (pointer identity for structural sharing)
            for (std::size_t i = 0; i < result->children.size(); ++i) {
                RC_ASSERT(result->children[i].get() == expectedOrder[i]);
            }
        });
}

// ===========================================================================
// Property 3: Structural sharing
//
// For any JsonNode tree and for any valid deletion path, all nodes in the
// resulting tree that are NOT on the ancestor path from root to the deleted
// node's parent are pointer-identical (shared_ptr address equality) to the
// corresponding nodes in the original tree.
//
// Feature: element-deletion, Property 3: Structural sharing
// **Validates: Requirements 3.4**
// ===========================================================================

TEST(DeletionEnginePBT, Property3_StructuralSharing) {
    rc::check("Feature: element-deletion, Property 3: Structural sharing",
        []() {
            auto root = *genContainerNode(3, 4);
            RC_PRE(!root->children.empty());

            auto pathAndTarget = pickRandomPath(root);
            auto path = pathAndTarget.path;
            RC_PRE(!path.empty());

            // Perform deletion
            auto result = deleteNode(root, path);
            RC_ASSERT(result != nullptr);

            // For each child of the root that is NOT on the ancestor path,
            // it should be pointer-identical in the result.
            // We check at the top level: children of root not on the path
            // should be shared.
            const auto& firstSegment = path[0];

            for (std::size_t i = 0; i < root->children.size(); ++i) {
                bool isOnPath = false;
                if (auto* keyPtr = std::get_if<std::string>(&firstSegment)) {
                    isOnPath = (root->children[i]->key == *keyPtr);
                } else {
                    auto idx = std::get<std::size_t>(firstSegment);
                    isOnPath = (i == idx);
                }

                if (!isOnPath) {
                    // This child should be pointer-identical in the result
                    // Find it in the result tree
                    bool foundInResult = false;
                    for (const auto& resultChild : result->children) {
                        if (resultChild.get() == root->children[i].get()) {
                            foundInResult = true;
                            break;
                        }
                    }
                    RC_ASSERT(foundInResult);
                }
            }
        });
}

// ===========================================================================
// Property 4: Invalid path returns identical tree
//
// For any JsonNode tree and for any path that does not correspond to an
// existing node, deleteNode(root, path) returns a shared_ptr that is
// pointer-identical to the input root (result.get() == root.get()).
//
// Feature: element-deletion, Property 4: Invalid path returns identical tree
// **Validates: Requirements 3.5**
// ===========================================================================

TEST(DeletionEnginePBT, Property4_InvalidPathReturnsIdenticalTree) {
    rc::check("Feature: element-deletion, Property 4: Invalid path returns identical tree",
        []() {
            auto root = *genContainerNode(3, 4);

            auto invalidPath = *genInvalidPath();

            // Ensure the path is actually invalid for this tree
            RC_PRE(!nodeExistsAtPath(root, invalidPath));

            // Perform deletion with invalid path
            auto result = deleteNode(root, invalidPath);

            // Result should be pointer-identical to the input root
            RC_ASSERT(result.get() == root.get());
        });
}

// ===========================================================================
// Property 5: Original tree immutability
//
// For any JsonNode tree, after calling deleteNode(root, path) with any path,
// serializing the original root tree produces byte-identical output to
// serializing it before the deletion call.
//
// Feature: element-deletion, Property 5: Original tree immutability
// **Validates: Requirements 3.7**
// ===========================================================================

TEST(DeletionEnginePBT, Property5_OriginalTreeImmutability) {
    rc::check("Feature: element-deletion, Property 5: Original tree immutability",
        []() {
            auto root = *genContainerNode(3, 4);
            RC_PRE(!root->children.empty());

            // Serialize the tree BEFORE deletion
            auto beforeJson = exportJson(*root);

            // Pick a valid path and delete
            auto path = *genValidPath(root);
            RC_PRE(!path.empty());

            // Perform deletion
            auto result = deleteNode(root, path);

            // Serialize the ORIGINAL tree AFTER deletion
            auto afterJson = exportJson(*root);

            // The original tree serialization should be byte-identical
            RC_ASSERT(beforeJson == afterJson);
        });
}

