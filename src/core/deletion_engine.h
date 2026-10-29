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

// Delete several nodes at once (multi-selection delete). Paths address the
// tree as it is before any deletion; each parent is rebuilt once, and a
// path nested inside another deleted path goes with it. Paths that do not
// resolve are skipped; an empty path (the root) yields nullptr like
// deleteNode.
auto deleteNodes(std::shared_ptr<const JsonNode> root, std::vector<NodePath> paths)
    -> std::shared_ptr<const JsonNode>;

// Utility: count all descendants of a node (recursive), over either tree
// backing. The JsonNode overload forwards to the NodeView implementation.
auto countDescendants(NodeView node) -> std::size_t;
auto countDescendants(const JsonNode& node) -> std::size_t;

} // namespace jsontitan::core
