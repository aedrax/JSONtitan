#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"

namespace jsontitan::core {

enum class SearchMode { Substring, Regex };

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
    std::shared_ptr<const JsonNode> node;
};

struct FilterResult {
    std::vector<SearchMatch> matches;
    std::optional<SearchError> error;
};

auto filter(const JsonNode& root, const SearchQuery& query) -> FilterResult;

// Arena-aware overload: walks ArenaJsonNode trees directly without deep-copy.
// Matching nodes are converted to JsonNode on-demand (only the matched node, not the full tree).
auto filter(const ArenaJsonNode& root, const SearchQuery& query) -> FilterResult;

} // namespace jsontitan::core
