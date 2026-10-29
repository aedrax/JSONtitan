#include "core/deletion_engine.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace jsontitan::core {

namespace {

// `root` rebuilt with the children `segments` name removed from the node at
// `parentPath`, in one copy of that node and its ancestors. Segments that do
// not resolve are ignored; `root` itself is returned when nothing resolves.
// A key removes only the first member with it: paths resolve a duplicated
// key to its first occurrence everywhere else, so deleting must too.
auto removeChildren(const std::shared_ptr<const JsonNode>& root,
                    const NodePath& parentPath,
                    std::span<const PathSegment> segments)
    -> std::shared_ptr<const JsonNode> {
    // ancestors[i] is the node at depth i (ancestors[0] = root).
    std::vector<std::shared_ptr<const JsonNode>> ancestors{root};
    ancestors.reserve(parentPath.size() + 1);
    for (const auto& segment : parentPath) {
        const auto& current = ancestors.back();
        std::shared_ptr<const JsonNode> found;
        if (const auto* key = std::get_if<std::string>(&segment)) {
            if (current->type == NodeType::Object) {
                auto it = std::ranges::find_if(current->children, [key](const auto& child) {
                    return child->key == *key;
                });
                if (it != current->children.end()) {
                    found = *it;
                }
            }
        } else if (const auto idx = std::get<std::size_t>(segment);
                   current->type == NodeType::Array && idx < current->children.size()) {
            found = current->children[idx];
        }
        if (!found) {
            return root;  // invalid path
        }
        ancestors.push_back(std::move(found));
    }

    const auto& parent = ancestors.back();
    std::unordered_set<std::size_t> indices;
    std::unordered_map<std::string_view, std::size_t> keys;  // key -> members to drop
    for (const auto& segment : segments) {
        if (const auto* key = std::get_if<std::string>(&segment)) {
            if (parent->type == NodeType::Object) {
                keys[*key] = 1;
            }
        } else if (parent->type == NodeType::Array) {
            indices.insert(std::get<std::size_t>(segment));
        }
    }

    std::vector<std::shared_ptr<const JsonNode>> newChildren;
    newChildren.reserve(parent->children.size());
    for (std::size_t i = 0; i < parent->children.size(); ++i) {
        const auto& child = parent->children[i];
        if (indices.contains(i)) {
            continue;
        }
        if (auto it = keys.find(child->key); it != keys.end() && it->second > 0) {
            --it->second;
            continue;
        }
        newChildren.push_back(child);  // structural sharing
    }
    if (newChildren.size() == parent->children.size()) {
        return root;  // nothing resolved
    }

    auto newNode = parent->type == NodeType::Object
                       ? JsonNode::makeObject(parent->key, std::move(newChildren))
                       : JsonNode::makeArray(parent->key, std::move(newChildren));

    // Walk back up the ancestor chain, cloning each ancestor with the
    // updated child replacing the old one.
    for (auto i = static_cast<int>(ancestors.size()) - 2; i >= 0; --i) {
        const auto& ancestor = ancestors[static_cast<std::size_t>(i)];
        const auto& oldChild = ancestors[static_cast<std::size_t>(i + 1)];

        std::vector<std::shared_ptr<const JsonNode>> updatedChildren;
        updatedChildren.reserve(ancestor->children.size());
        for (const auto& child : ancestor->children) {
            updatedChildren.push_back(child.get() == oldChild.get() ? newNode : child);
        }
        newNode = ancestor->type == NodeType::Object
                      ? JsonNode::makeObject(ancestor->key, std::move(updatedChildren))
                      : JsonNode::makeArray(ancestor->key, std::move(updatedChildren));
    }
    return newNode;
}

}  // namespace

auto deleteNode(std::shared_ptr<const JsonNode> root, const NodePath& targetPath)
    -> std::shared_ptr<const JsonNode> {
    // Empty path = root node itself -> return nullptr
    if (targetPath.empty()) {
        return nullptr;
    }
    if (!root) {
        return root;
    }
    const NodePath parentPath(targetPath.begin(), targetPath.end() - 1);
    return removeChildren(root, parentPath, std::span(&targetPath.back(), 1));
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

auto deleteNodes(std::shared_ptr<const JsonNode> root, std::vector<NodePath> paths)
    -> std::shared_ptr<const JsonNode> {
    if (!root) {
        return root;
    }
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (!paths.empty() && paths.front().empty()) {
        return nullptr;  // the root itself
    }

    // A path inside another deleted path goes with it. Sorted, an ancestor
    // comes before its descendants. The rest are grouped by parent so each
    // parent is rebuilt once (deleting rows one by one copied the parent's
    // child list per row).
    std::set<NodePath> deleted;
    std::map<NodePath, std::vector<PathSegment>, std::greater<>> byParent;
    for (auto& path : paths) {
        bool insideDeleted = false;
        for (std::size_t length = 1; length < path.size() && !insideDeleted; ++length) {
            insideDeleted = deleted.contains(NodePath(path.begin(), path.begin() + length));
        }
        if (insideDeleted) {
            continue;
        }
        deleted.insert(path);
        PathSegment last = path.back();
        path.pop_back();
        byParent[std::move(path)].push_back(std::move(last));
    }

    // Parents in reverse order: removing children of a parent only shifts
    // the indices of later siblings' subtrees, whose paths sort after it
    // and so were already handled.
    for (const auto& [parentPath, segments] : byParent) {
        root = removeChildren(root, parentPath, segments);
    }
    return root;
}

} // namespace jsontitan::core
