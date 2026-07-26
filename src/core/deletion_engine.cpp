#include "core/deletion_engine.h"

#include <algorithm>
#include <utility>

namespace jsontitan::core {

auto deleteNode(std::shared_ptr<const JsonNode> root, const NodePath& targetPath)
    -> std::shared_ptr<const JsonNode> {
    // Empty path = root node itself -> return nullptr
    if (targetPath.empty()) {
        return nullptr;
    }

    if (!root) {
        return root;
    }

    // Walk the path, collecting ancestor nodes for path-copying.
    // ancestors[i] is the node at depth i (ancestors[0] = root).
    std::vector<std::shared_ptr<const JsonNode>> ancestors;
    ancestors.reserve(targetPath.size());
    ancestors.push_back(root);

    for (std::size_t i = 0; i + 1 < targetPath.size(); ++i) {
        const auto& current = ancestors.back();
        const auto& segment = targetPath[i];

        // Find the child matching this segment
        std::shared_ptr<const JsonNode> foundChild;
        if (auto* keyPtr = std::get_if<std::string>(&segment)) {
            // Object child: match by key
            if (current->type != NodeType::Object) {
                return root; // invalid path
            }
            for (const auto& child : current->children) {
                if (child->key == *keyPtr) {
                    foundChild = child;
                    break;
                }
            }
        } else {
            // Array child: match by index
            auto idx = std::get<std::size_t>(segment);
            if (current->type != NodeType::Array) {
                return root; // invalid path
            }
            if (idx >= current->children.size()) {
                return root; // invalid path
            }
            foundChild = current->children[idx];
        }

        if (!foundChild) {
            return root; // invalid path
        }

        ancestors.push_back(foundChild);
    }

    // ancestors.back() is the parent of the target node.
    // The last segment tells us which child to remove.
    const auto& parent = ancestors.back();
    const auto& lastSegment = targetPath.back();

    // Build new children for the parent with the target removed
    std::vector<std::shared_ptr<const JsonNode>> newChildren;

    if (auto* keyPtr = std::get_if<std::string>(&lastSegment)) {
        // Object parent: remove child with matching key
        if (parent->type != NodeType::Object) {
            return root; // invalid path
        }
        bool found = false;
        for (const auto& child : parent->children) {
            if (child->key == *keyPtr) {
                found = true;
            } else {
                newChildren.push_back(child); // structural sharing
            }
        }
        if (!found) {
            return root; // invalid path - key doesn't exist
        }
    } else {
        // Array parent: remove child at index
        auto idx = std::get<std::size_t>(lastSegment);
        if (parent->type != NodeType::Array) {
            return root; // invalid path
        }
        if (idx >= parent->children.size()) {
            return root; // invalid path - index out of bounds
        }
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (i != idx) {
                newChildren.push_back(parent->children[i]); // structural sharing
            }
        }
    }

    // Clone the parent with the new children
    std::shared_ptr<const JsonNode> newNode;
    if (parent->type == NodeType::Object) {
        newNode = JsonNode::makeObject(parent->key, std::move(newChildren));
    } else {
        newNode = JsonNode::makeArray(parent->key, std::move(newChildren));
    }

    // Walk back up the ancestor chain, cloning each ancestor with the
    // updated child replacing the old one.
    for (auto i = static_cast<int>(ancestors.size()) - 2; i >= 0; --i) {
        const auto& ancestor = ancestors[static_cast<std::size_t>(i)];
        const auto& oldChild = ancestors[static_cast<std::size_t>(i + 1)];

        std::vector<std::shared_ptr<const JsonNode>> updatedChildren;
        updatedChildren.reserve(ancestor->children.size());

        for (const auto& child : ancestor->children) {
            if (child.get() == oldChild.get()) {
                updatedChildren.push_back(newNode);
            } else {
                updatedChildren.push_back(child); // structural sharing
            }
        }

        if (ancestor->type == NodeType::Object) {
            newNode = JsonNode::makeObject(ancestor->key, std::move(updatedChildren));
        } else {
            newNode = JsonNode::makeArray(ancestor->key, std::move(updatedChildren));
        }
    }

    return newNode;
}

auto computePath(const std::shared_ptr<const JsonNode>& root,
                 const JsonNode* target)
    -> std::optional<NodePath> {
    if (!root || !target) {
        return std::nullopt;
    }

    // If target is the root itself, return empty path
    if (root.get() == target) {
        return NodePath{};
    }

    // DFS to find the target, building the path as we go
    struct Frame {
        const JsonNode* node;
        std::size_t childIdx;
    };

    std::vector<Frame> stack;
    stack.push_back({root.get(), 0});

    NodePath currentPath;

    while (!stack.empty()) {
        auto& frame = stack.back();

        if (frame.childIdx >= frame.node->children.size()) {
            // Exhausted all children at this level, backtrack
            stack.pop_back();
            if (!currentPath.empty()) {
                currentPath.pop_back();
            }
            continue;
        }

        const auto& child = frame.node->children[frame.childIdx];
        frame.childIdx++;

        // Build the path segment for this child
        if (frame.node->type == NodeType::Object) {
            currentPath.push_back(child->key);
        } else {
            // Array: the index is (childIdx - 1) since we already incremented
            currentPath.push_back(frame.childIdx - 1);
        }

        if (child.get() == target) {
            return currentPath;
        }

        // If this child has children, descend into it
        if (!child->children.empty()) {
            stack.push_back({child.get(), 0});
        } else {
            // Leaf node, not the target, pop the segment
            currentPath.pop_back();
        }
    }

    return std::nullopt;
}

auto countDescendants(NodeView node) -> std::size_t {
    std::size_t count = 0;
    const std::size_t childCount = node.childCount();
    for (std::size_t i = 0; i < childCount; ++i) {
        count += 1 + countDescendants(node.child(i));
    }
    return count;
}

auto countDescendants(const JsonNode& node) -> std::size_t {
    return countDescendants(NodeView(node));
}

} // namespace jsontitan::core
