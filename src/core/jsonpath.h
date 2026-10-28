#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/deletion_engine.h"  // NodePath
#include "core/node_view.h"
#include "core/search_engine.h"

namespace jsontitan::core {

// One evaluation step of the supported JSONPath subset:
//   $           root (consumed by the parser, not a step)
//   .name       object member                       -> Key
//   ['name']    object member (quoted)              -> Key
//   [n]         array element                       -> Index
//   [*] / .*    every child                         -> Wildcard
//   ..name      recursive descent + member          -> RecursiveKey
//   ..[n]       recursive descent + element         -> RecursiveIndex
//   ..* / ..[*] recursive descent + every child     -> RecursiveWildcard
// Filter expressions ([?(...)]) are intentionally unsupported and produce a
// parse error saying so.
struct JsonPathStep {
    enum class Kind {
        Key,
        Index,
        Wildcard,
        RecursiveKey,
        RecursiveIndex,
        RecursiveWildcard,
    };
    Kind kind;
    std::string key;        // Key / RecursiveKey
    std::size_t index = 0;  // Index / RecursiveIndex
};

struct JsonPathError {
    std::size_t position;  // byte offset into the expression
    std::string description;
};

// Parse a JSONPath expression (must start with '$').
auto parseJsonPath(std::string_view expr)
    -> std::expected<std::vector<JsonPathStep>, JsonPathError>;

// Evaluate parsed steps against a tree. Matches carry ancestor child-index
// paths (deduplicated, root = empty path) compatible with FilterProxyModel;
// SearchMatch::node is always nullptr for JSONPath results.
auto evalJsonPath(NodeView root, std::span<const JsonPathStep> steps) -> FilterResult;

// Resolve a definite path (member names and array indices only, no
// wildcards or recursive descent) to the NodePath it names, without touching
// any tree. The leading '$' is optional: "store.book[0]" and "[2].id" are
// read as "$.store.book[0]" and "$[2].id". Used by "Go to Path".
auto parseDefinitePath(std::string_view expr)
    -> std::expected<NodePath, JsonPathError>;

// Convenience: parse + evaluate. A parse error is returned as
// FilterResult::error ("JSONPath error at position N: ...").
auto queryJsonPath(NodeView root, std::string_view expr) -> FilterResult;

} // namespace jsontitan::core
