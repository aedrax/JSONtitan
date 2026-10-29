#include "core/edit_engine.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/parse_orchestrator.h"

namespace jsontitan::core {

namespace {

auto makeError(EditErrorCode code, std::string description) -> std::unexpected<EditError> {
    return std::unexpected(EditError{code, std::move(description)});
}

// Walk `path` from `root`, collecting every node on the way (ancestors[0] ==
// root, ancestors.back() == the node at `path`). Segment semantics mirror
// deleteNode: string segments match object children by key, index segments
// address array children by position.
auto collectPathNodes(const std::shared_ptr<const JsonNode>& root,
                      const NodePath& path,
                      std::vector<std::shared_ptr<const JsonNode>>& out) -> bool {
    out.clear();
    out.reserve(path.size() + 1);
    out.push_back(root);

    for (const auto& segment : path) {
        const auto& current = out.back();
        std::shared_ptr<const JsonNode> foundChild;

        if (const auto* keyPtr = std::get_if<std::string>(&segment)) {
            if (current->type != NodeType::Object) {
                return false;
            }
            for (const auto& child : current->children) {
                if (child->key == *keyPtr) {
                    foundChild = child;
                    break;
                }
            }
        } else {
            auto idx = std::get<std::size_t>(segment);
            if (current->type != NodeType::Array || idx >= current->children.size()) {
                return false;
            }
            foundChild = current->children[idx];
        }

        if (!foundChild) {
            return false;
        }
        out.push_back(std::move(foundChild));
    }
    return true;
}

// Rebuild the ancestor chain bottom-up with `replacement` substituted for the
// node at the end of the chain. nodes[0] is the root; returns the new root.
auto rebuildWithReplacement(const std::vector<std::shared_ptr<const JsonNode>>& nodes,
                            std::shared_ptr<const JsonNode> replacement)
    -> std::shared_ptr<const JsonNode> {
    auto newNode = std::move(replacement);
    for (auto i = static_cast<int>(nodes.size()) - 2; i >= 0; --i) {
        const auto& ancestor = nodes[static_cast<std::size_t>(i)];
        const auto& oldChild = nodes[static_cast<std::size_t>(i + 1)];

        std::vector<std::shared_ptr<const JsonNode>> updatedChildren;
        updatedChildren.reserve(ancestor->children.size());
        for (const auto& child : ancestor->children) {
            if (child.get() == oldChild.get()) {
                updatedChildren.push_back(newNode);
            } else {
                updatedChildren.push_back(child);  // structural sharing
            }
        }

        newNode = ancestor->type == NodeType::Object
                      ? JsonNode::makeObject(ancestor->key, std::move(updatedChildren))
                      : JsonNode::makeArray(ancestor->key, std::move(updatedChildren));
    }
    return newNode;
}

// A shallow clone of `node` with `key` (type, value and children stay
// shared). Always a new node, even when the key is unchanged: edits find a
// child by pointer, so one node must never sit in two places (duplicating an
// array element inserts the element itself).
auto withKey(const std::shared_ptr<const JsonNode>& node, std::string key)
    -> std::shared_ptr<const JsonNode> {
    return std::make_shared<JsonNode>(
        JsonNode{node->type, std::move(key), node->value, node->children});
}

auto withChildren(const JsonNode& container,
                  std::vector<std::shared_ptr<const JsonNode>> children)
    -> std::shared_ptr<const JsonNode> {
    return container.type == NodeType::Object
               ? JsonNode::makeObject(container.key, std::move(children))
               : JsonNode::makeArray(container.key, std::move(children));
}

auto isContainer(const JsonNode& node) -> bool {
    return node.type == NodeType::Object || node.type == NodeType::Array;
}

// sortKeys worker: returns `node` itself when its subtree is already sorted.
auto sortedSubtree(const std::shared_ptr<const JsonNode>& node)
    -> std::shared_ptr<const JsonNode> {
    if (!isContainer(*node)) {
        return node;
    }
    bool changed = false;
    std::vector<std::shared_ptr<const JsonNode>> children;
    children.reserve(node->children.size());
    for (const auto& child : node->children) {
        auto sorted = sortedSubtree(child);
        changed = changed || sorted.get() != child.get();
        children.push_back(std::move(sorted));
    }
    if (node->type == NodeType::Object) {
        const auto byKey = [](const auto& a, const auto& b) { return a->key < b->key; };
        if (!std::is_sorted(children.begin(), children.end(), byKey)) {
            std::stable_sort(children.begin(), children.end(), byKey);
            changed = true;
        }
    }
    return changed ? withChildren(*node, std::move(children)) : node;
}

auto makeScalar(std::string key, const ScalarValue& value)
    -> std::shared_ptr<const JsonNode> {
    switch (value.type) {
        case NodeType::String:  return JsonNode::makeString(std::move(key), value.text);
        case NodeType::Number:  return JsonNode::makeNumber(std::move(key), value.text);
        case NodeType::Boolean: return JsonNode::makeBool(std::move(key), value.text == "true");
        case NodeType::Null:    return JsonNode::makeNull(std::move(key));
        case NodeType::Object:
        case NodeType::Array:   break;  // rejected by callers
    }
    return nullptr;
}

} // anonymous namespace

auto isRfc8259Number(std::string_view text) -> bool {
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto digit = [&](std::size_t p) { return p < n && text[p] >= '0' && text[p] <= '9'; };

    if (i < n && text[i] == '-') ++i;

    // int part: 0 | [1-9][0-9]*
    if (!digit(i)) return false;
    if (text[i] == '0') {
        ++i;
    } else {
        while (digit(i)) ++i;
    }

    // frac part
    if (i < n && text[i] == '.') {
        ++i;
        if (!digit(i)) return false;
        while (digit(i)) ++i;
    }

    // exp part
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
        if (!digit(i)) return false;
        while (digit(i)) ++i;
    }

    return i == n;
}

auto classifyScalarInput(std::string_view text) -> ScalarValue {
    if (text == "true" || text == "false") {
        return ScalarValue{NodeType::Boolean, std::string(text)};
    }
    if (text == "null") {
        return ScalarValue{NodeType::Null, {}};
    }
    if (isRfc8259Number(text)) {
        return ScalarValue{NodeType::Number, std::string(text)};
    }
    return ScalarValue{NodeType::String, std::string(text)};
}

auto editValue(std::shared_ptr<const JsonNode> root, const NodePath& path,
               ScalarValue value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    if (value.type == NodeType::Object || value.type == NodeType::Array) {
        return makeError(EditErrorCode::NotEditable,
                         "Only scalar values can be set with editValue");
    }
    if (value.type == NodeType::Number && !isRfc8259Number(value.text)) {
        return makeError(EditErrorCode::InvalidNumber,
                         "'" + value.text + "' is not a valid JSON number");
    }

    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }

    const auto& target = nodes.back();
    if (target->type == NodeType::Object || target->type == NodeType::Array) {
        return makeError(EditErrorCode::NotEditable,
                         "Container nodes cannot be edited as scalar values");
    }

    auto replacement = makeScalar(target->key, value);
    return rebuildWithReplacement(nodes, std::move(replacement));
}

auto renameKey(std::shared_ptr<const JsonNode> root, const NodePath& path,
               std::string newKey)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    if (path.empty()) {
        return makeError(EditErrorCode::InvalidPath, "The root node has no key to rename");
    }
    // Only object members carry keys; an array element path ends in an index.
    if (!std::holds_alternative<std::string>(path.back())) {
        return makeError(EditErrorCode::InvalidPath, "Array elements have no key");
    }

    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }

    const auto& target = nodes.back();
    if (target->key == newKey) {
        return root;  // no-op rename
    }

    const auto& parent = nodes[nodes.size() - 2];
    for (const auto& sibling : parent->children) {
        if (sibling.get() != target.get() && sibling->key == newKey) {
            return makeError(EditErrorCode::DuplicateKey,
                             "Key '" + newKey + "' already exists in this object");
        }
    }

    // Clone the target with the new key, keeping type/value/children shared.
    auto renamed = std::make_shared<JsonNode>(
        JsonNode{target->type, std::move(newKey), target->value, target->children});
    return rebuildWithReplacement(nodes, std::move(renamed));
}

auto findNode(const std::shared_ptr<const JsonNode>& root, const NodePath& path)
    -> std::shared_ptr<const JsonNode> {
    if (!root) {
        return nullptr;
    }
    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return nullptr;
    }
    return nodes.back();
}

auto parseValueInput(std::string_view text)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    auto parsed = parseBuffer(std::string(text), ParseBufferOptions{});
    if (parsed.ok()) {
        return parsed.root->toJsonNode();
    }

    const auto first = text.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos && (text[first] == '{' || text[first] == '[')) {
        const std::string detail =
            parsed.error ? parsed.error->description : std::string("parse failed");
        return makeError(EditErrorCode::InvalidJson, "Not valid JSON: " + detail);
    }
    auto scalar = classifyScalarInput(text);
    return makeScalar({}, scalar);
}

auto uniqueChildKey(const JsonNode& object, std::string_view base) -> std::string {
    const auto taken = [&object](const std::string& key) {
        return std::ranges::any_of(object.children,
                                   [&key](const auto& child) { return child->key == key; });
    };
    std::string candidate(base);
    for (int suffix = 2; taken(candidate); ++suffix) {
        candidate = std::string(base) + " " + std::to_string(suffix);
    }
    return candidate;
}

auto insertChild(std::shared_ptr<const JsonNode> root, const NodePath& parentPath,
                 std::size_t position, std::string key,
                 std::shared_ptr<const JsonNode> value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root || !value) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, parentPath, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }
    const auto& parent = nodes.back();
    if (!isContainer(*parent)) {
        return makeError(EditErrorCode::NotContainer,
                         "Only objects and arrays can have children");
    }

    if (parent->type == NodeType::Object) {
        for (const auto& sibling : parent->children) {
            if (sibling->key == key) {
                return makeError(EditErrorCode::DuplicateKey,
                                 "Key '" + key + "' already exists in this object");
            }
        }
    } else {
        key.clear();
    }

    auto children = parent->children;
    position = std::min(position, children.size());
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(position),
                    withKey(std::move(value), std::move(key)));
    return rebuildWithReplacement(nodes, withChildren(*parent, std::move(children)));
}

auto replaceNode(std::shared_ptr<const JsonNode> root, const NodePath& path,
                 std::shared_ptr<const JsonNode> value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root || !value) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }
    return rebuildWithReplacement(nodes, withKey(std::move(value), nodes.back()->key));
}

auto moveChild(std::shared_ptr<const JsonNode> root, const NodePath& path,
               std::size_t newPosition)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    if (path.empty()) {
        return makeError(EditErrorCode::InvalidPath, "The root node cannot be moved");
    }
    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }
    const auto& target = nodes.back();
    const auto& parent = nodes[nodes.size() - 2];

    auto children = parent->children;
    auto from = std::ranges::find_if(
        children, [&target](const auto& child) { return child.get() == target.get(); });
    const auto oldPosition = static_cast<std::size_t>(from - children.begin());
    newPosition = std::min(newPosition, children.size() - 1);
    if (oldPosition == newPosition) {
        return root;
    }
    children.erase(from);
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(newPosition), target);

    nodes.pop_back();  // rebuild from the parent upward
    return rebuildWithReplacement(nodes, withChildren(*parent, std::move(children)));
}

auto sortKeys(std::shared_ptr<const JsonNode> root, const NodePath& path)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError> {
    if (!root) {
        return makeError(EditErrorCode::InvalidPath, "No document");
    }
    std::vector<std::shared_ptr<const JsonNode>> nodes;
    if (!collectPathNodes(root, path, nodes)) {
        return makeError(EditErrorCode::InvalidPath, "Path does not resolve to a node");
    }
    if (!isContainer(*nodes.back())) {
        return makeError(EditErrorCode::NotContainer,
                         "Only objects and arrays have keys to sort");
    }
    auto sorted = sortedSubtree(nodes.back());
    if (sorted.get() == nodes.back().get()) {
        return root;
    }
    return rebuildWithReplacement(nodes, std::move(sorted));
}

} // namespace jsontitan::core
