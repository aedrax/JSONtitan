#pragma once

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/deletion_engine.h"  // NodePath
#include "core/node_view.h"
#include "core/search_engine.h"

namespace jsontitan::core {

// A parsed filter predicate ([?(...)]); defined in jsonpath.cpp.
struct FilterExpr;

// One evaluation step of the supported JSONPath subset:
//   $           root (consumed by the parser, not a step)
//   .name       object member                       -> Key
//   ['name']    object member (quoted)              -> Key
//   [n]         array element                       -> Index
//   [*] / .*    every child                         -> Wildcard
//   [?(expr)]   every child for which expr holds    -> Filter
//   ..name      recursive descent + member          -> RecursiveKey
//   ..[n]       recursive descent + element         -> RecursiveIndex
//   ..* / ..[*] recursive descent + every child     -> RecursiveWildcard
//   ..[?(expr)] recursive descent + filter          -> RecursiveFilter
//
// Filter expressions (the parentheses are optional: [?@.a > 1]):
//   @.a.b[0] / @['k']   path from the child being tested ('@' alone is it)
//   $.a                 path from the document root
//   'text' "text" 42 -1.5e3 true false null      literals
//   == != < <= > >=     comparisons; numbers compare numerically, strings
//                       by bytes, mismatched types are never equal/ordered
//   =~ /regex/i         regex search on a string ('i': ignore case)
//   @.a                 on its own: the path exists
//   ! && || ( )         logic and grouping
// Filter paths take names and indices only (no wildcards or '..').
struct JsonPathStep {
    enum class Kind {
        Key,
        Index,
        Wildcard,
        Filter,
        RecursiveKey,
        RecursiveIndex,
        RecursiveWildcard,
        RecursiveFilter,
    };
    Kind kind;
    std::string key;        // Key / RecursiveKey
    std::size_t index = 0;  // Index / RecursiveIndex
    std::shared_ptr<const FilterExpr> filter = nullptr;  // (Recursive)Filter
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
