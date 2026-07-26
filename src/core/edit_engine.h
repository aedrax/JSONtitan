#pragma once

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

} // namespace jsontitan::core
