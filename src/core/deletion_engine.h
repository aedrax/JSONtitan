#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/json_node.h"
#include "core/node_view.h"

namespace jsontitan::core {

// A path segment is either a string key (for object children)
// or an integer index (for array children).
using PathSegment = std::variant<std::string, std::size_t>;
using NodePath = std::vector<PathSegment>;

// Result of a deletion operation.
// On success: a new tree root (nullptr if root itself was deleted).
// The original tree is never modified.
auto deleteNode(std::shared_ptr<const JsonNode> root, const NodePath& targetPath)
    -> std::shared_ptr<const JsonNode>;

// Utility: compute the NodePath for a given node within a tree.
// Returns std::nullopt if the node is not found in the tree.
auto computePath(const std::shared_ptr<const JsonNode>& root,
                 const JsonNode* target)
    -> std::optional<NodePath>;

// Utility: count all descendants of a node (recursive), over either tree
// backing. The JsonNode overload forwards to the NodeView implementation.
auto countDescendants(NodeView node) -> std::size_t;
auto countDescendants(const JsonNode& node) -> std::size_t;

} // namespace jsontitan::core
