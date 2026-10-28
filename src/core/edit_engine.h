#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "core/deletion_engine.h"  // NodePath / PathSegment
#include "core/json_node.h"

namespace jsontitan::core {

enum class EditErrorCode {
    InvalidPath,   // path does not resolve to a node in this tree
    DuplicateKey,  // renameKey target's parent already has a child with newKey
    InvalidNumber, // explicit Number edit whose text is not RFC 8259 valid
    NotEditable,   // editValue on a container (Object/Array) node
    NotContainer,  // insertChild / sortKeys target is not an Object/Array
    InvalidJson,   // parseValueInput text looks like JSON but does not parse
};

struct EditError {
    EditErrorCode code;
    std::string description;
};

// A scalar replacement value. `type` must be String, Number, Boolean, or
// Null; `text` is the raw value text ("true", "42", "hello", ...; ignored
// for Null).
struct ScalarValue {
    NodeType type;
    std::string text;
};

// Classify user-typed text the way JSON would: "true"/"false" -> Boolean,
// "null" -> Null, an RFC 8259 number -> Number, anything else -> String.
auto classifyScalarInput(std::string_view text) -> ScalarValue;

// True when `text` matches the RFC 8259 number grammar.
auto isRfc8259Number(std::string_view text) -> bool;

// Replace the scalar value (and possibly type) of the node at `path`.
// Returns a new root sharing all untouched structure with the old tree; the
// original tree is never modified. Container nodes are NotEditable.
auto editValue(std::shared_ptr<const JsonNode> root, const NodePath& path,
               ScalarValue value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// Rename an object member's key. Rejects when a sibling already has newKey
// (DuplicateKey) or when the target is not an object member (InvalidPath).
// Renaming to the identical key returns the original root (no-op).
auto renameKey(std::shared_ptr<const JsonNode> root, const NodePath& path,
               std::string newKey)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// Resolve `path` to its node, or nullptr when it does not resolve.
auto findNode(const std::shared_ptr<const JsonNode>& root, const NodePath& path)
    -> std::shared_ptr<const JsonNode>;

// Turn text the user typed or pasted into a value node (key empty). Valid
// JSON of any kind is taken as such ("{\"a\": 1}", "[1, 2]", "\"quoted\"",
// "42"); anything else is read like an inline edit (classifyScalarInput),
// so bare words become strings. Text that starts with '{' or '[' but does
// not parse is rejected (InvalidJson) rather than silently stringified.
auto parseValueInput(std::string_view text)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// A key not yet used by any member of `object`: `base` itself when free,
// otherwise "base 2", "base 3", ...
auto uniqueChildKey(const JsonNode& object, std::string_view base) -> std::string;

// Insert `value` as a child of the container at `parentPath`, at `position`
// (clamped to the end, so SIZE_MAX appends). Object members take `key`
// (DuplicateKey when a sibling already has it); array elements ignore it.
// The value's own key is replaced, so parsed fragments can be inserted
// as-is.
auto insertChild(std::shared_ptr<const JsonNode> root, const NodePath& parentPath,
                 std::size_t position, std::string key,
                 std::shared_ptr<const JsonNode> value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// Replace the node at `path` with `value` (any type, containers included),
// keeping the target's key. An empty path replaces the whole document.
auto replaceNode(std::shared_ptr<const JsonNode> root, const NodePath& path,
                 std::shared_ptr<const JsonNode> value)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// Move the node at `path` to `newPosition` among its siblings (clamped to
// the last position). Works for array elements and object members alike.
auto moveChild(std::shared_ptr<const JsonNode> root, const NodePath& path,
               std::size_t newPosition)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

// Sort object members by key (byte order, stable) in the subtree at `path`:
// every object in it, including objects nested in arrays. Returns the
// original root when nothing was out of order.
auto sortKeys(std::shared_ptr<const JsonNode> root, const NodePath& path)
    -> std::expected<std::shared_ptr<const JsonNode>, EditError>;

} // namespace jsontitan::core
