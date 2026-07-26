#include "core/edit_engine.h"

#include <utility>
#include <vector>

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

} // namespace jsontitan::core
