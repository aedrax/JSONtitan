#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"

namespace jsontitan::core {

enum class SearchMode { Substring, Regex, JsonPath };

struct SearchQuery {
    std::string pattern;
    SearchMode mode = SearchMode::Substring;
    bool caseSensitive = false;
};

struct SearchError {
    std::string description;
};

struct SearchMatch {
    std::vector<std::size_t> ancestorIndices;
    // Matched node. Populated (cheaply, via shared ownership) for JsonNode
    // searches; ALWAYS nullptr for arena searches — consumers must locate
    // arena matches via ancestorIndices. (Materializing arena matches
    // previously deep-copied each matched subtree for data nothing read.)
    std::shared_ptr<const JsonNode> node;
};

struct FilterResult {
    std::vector<SearchMatch> matches;
    std::optional<SearchError> error = std::nullopt;
};

// shouldCancel (optional) is polled every few thousand nodes; returning true
// aborts the walk and returns the partial result. Callers that cancel are
// expected to discard the result (e.g. via a generation check).
auto filter(const JsonNode& root, const SearchQuery& query,
            const std::function<bool()>& shouldCancel = nullptr) -> FilterResult;

// Arena-aware overload: walks ArenaJsonNode trees directly without deep-copy.
auto filter(const ArenaJsonNode& root, const SearchQuery& query,
            const std::function<bool()>& shouldCancel = nullptr) -> FilterResult;

} // namespace jsontitan::core
