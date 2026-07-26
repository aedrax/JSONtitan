#include "core/search_engine.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace jsontitan::core {

namespace {

// How often (in visited nodes) the cancellation callback is polled.
constexpr std::size_t kCancelCheckInterval = 4096;

// Lowercase one char (byte-wise; matches the previous toLower semantics).
inline auto lowerChar(char c) -> char {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Allocation-free case-insensitive substring scan. `loweredNeedle` must
// already be lowercased (done once per filter() call, not once per node —
// the previous implementation heap-allocated two lowered copies per visited
// node, ~100-200M temporary allocations on a 50M-node tree).
auto containsSubstringCI(std::string_view haystack, std::string_view loweredNeedle) -> bool {
    if (loweredNeedle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        loweredNeedle.begin(), loweredNeedle.end(),
        [](char h, char n) { return lowerChar(h) == n; });
    return it != haystack.end();
}

// Case-sensitive substring scan.
auto containsSubstringCS(std::string_view haystack, std::string_view needle) -> bool {
    if (needle.empty()) return true;
    return haystack.find(needle) != std::string_view::npos;
}

// Query state prepared once per filter() call.
struct CompiledQuery {
    const SearchQuery& query;
    std::string loweredPattern;        // only for case-insensitive substring
    const std::regex* regex = nullptr; // only for regex mode
    const std::function<bool()>* shouldCancel = nullptr;
    std::size_t visited = 0;
    bool cancelled = false;

    // Returns true when the walk should stop.
    auto pollCancel() -> bool {
        if (cancelled) return true;
        if (shouldCancel && *shouldCancel &&
            ++visited % kCancelCheckInterval == 0 && (*shouldCancel)()) {
            cancelled = true;
        }
        return cancelled;
    }

    auto matches(std::string_view key, std::string_view value, NodeType type) const -> bool {
        if (query.mode == SearchMode::Substring) {
            if (query.caseSensitive) {
                if (containsSubstringCS(key, query.pattern)) return true;
                if (type == NodeType::String && containsSubstringCS(value, query.pattern))
                    return true;
            } else {
                if (containsSubstringCI(key, loweredPattern)) return true;
                if (type == NodeType::String && containsSubstringCI(value, loweredPattern))
                    return true;
            }
        } else if (query.mode == SearchMode::Regex && regex) {
            if (std::regex_search(key.begin(), key.end(), *regex)) return true;
            if (type == NodeType::String &&
                std::regex_search(value.begin(), value.end(), *regex))
                return true;
        }
        return false;
    }
};

// Recursive DFS to find all matching nodes and build ancestor index paths.
void searchRecursive(const std::shared_ptr<const JsonNode>& node,
                     CompiledQuery& cq,
                     std::vector<std::size_t>& currentPath,
                     std::vector<SearchMatch>& matches) {
    if (cq.pollCancel()) {
        return;
    }

    if (cq.matches(node->key, node->value, node->type)) {
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = node
        });
    }

    for (std::size_t i = 0; i < node->children.size(); ++i) {
        currentPath.push_back(i);
        searchRecursive(node->children[i], cq, currentPath, matches);
        currentPath.pop_back();
        if (cq.cancelled) return;
    }
}

// Recursive DFS for ArenaJsonNode trees. Matches carry only their index
// path — no node materialization (see SearchMatch::node documentation).
void arenaSearchRecursive(const ArenaJsonNode& node,
                          CompiledQuery& cq,
                          std::vector<std::size_t>& currentPath,
                          std::vector<SearchMatch>& matches) {
    if (cq.pollCancel()) {
        return;
    }

    if (cq.matches(node.keyView(), node.valueView(), node.type)) {
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = nullptr
        });
    }

    for (std::size_t i = 0; i < node.childCount; ++i) {
        currentPath.push_back(i);
        arenaSearchRecursive(*node.children[i], cq, currentPath, matches);
        currentPath.pop_back();
        if (cq.cancelled) return;
    }
}

// Shared per-call setup: compile the regex / lower the pattern.
// Returns an error result if the regex is invalid.
auto prepare(const SearchQuery& query,
             std::regex& regexStorage,
             CompiledQuery& cq) -> std::optional<FilterResult> {
    if (query.mode == SearchMode::Regex) {
        try {
            auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
            if (!query.caseSensitive) {
                flags |= std::regex_constants::icase;
            }
            regexStorage = std::regex(query.pattern, flags);
            cq.regex = &regexStorage;
        } catch (const std::regex_error& e) {
            return FilterResult{
                .matches = {},
                .error = SearchError{.description = std::string("Invalid regex pattern: ") + e.what()}
            };
        }
    } else if (!query.caseSensitive) {
        cq.loweredPattern.reserve(query.pattern.size());
        for (char c : query.pattern) {
            cq.loweredPattern += lowerChar(c);
        }
    }
    return std::nullopt;
}

} // anonymous namespace

auto filter(const JsonNode& root, const SearchQuery& query,
            const std::function<bool()>& shouldCancel) -> FilterResult {
    // Empty pattern matches nothing — return empty results (not an error)
    if (query.pattern.empty()) {
        return FilterResult{.matches = {}, .error = std::nullopt};
    }

    std::regex regexStorage;
    CompiledQuery cq = {.query = query};
    cq.shouldCancel = &shouldCancel;
    if (auto err = prepare(query, regexStorage, cq)) {
        FilterResult errorResult = std::move(*err);
        return errorResult;
    }

    std::vector<SearchMatch> matches;
    std::vector<std::size_t> currentPath;

    // Check root node
    if (cq.matches(root.key, root.value, root.type)) {
        // The caller keeps the tree alive for the duration of any use of the
        // result; a non-owning pointer preserves node identity across calls.
        auto rootPtr = std::shared_ptr<const JsonNode>(&root, [](const JsonNode*) {});
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = rootPtr
        });
    }

    // Recurse into children
    for (std::size_t i = 0; i < root.children.size(); ++i) {
        currentPath.push_back(i);
        searchRecursive(root.children[i], cq, currentPath, matches);
        currentPath.pop_back();
        if (cq.cancelled) break;
    }

    return FilterResult{.matches = std::move(matches), .error = std::nullopt};
}

auto filter(const ArenaJsonNode& root, const SearchQuery& query,
            const std::function<bool()>& shouldCancel) -> FilterResult {
    // Empty pattern matches nothing — return empty results (not an error)
    if (query.pattern.empty()) {
        return FilterResult{.matches = {}, .error = std::nullopt};
    }

    std::regex regexStorage;
    CompiledQuery cq = {.query = query};
    cq.shouldCancel = &shouldCancel;
    if (auto err = prepare(query, regexStorage, cq)) {
        FilterResult errorResult = std::move(*err);
        return errorResult;
    }

    std::vector<SearchMatch> matches;
    std::vector<std::size_t> currentPath;

    // Check root node
    if (cq.matches(root.keyView(), root.valueView(), root.type)) {
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = nullptr
        });
    }

    // Recurse into children
    for (std::size_t i = 0; i < root.childCount; ++i) {
        currentPath.push_back(i);
        arenaSearchRecursive(*root.children[i], cq, currentPath, matches);
        currentPath.pop_back();
        if (cq.cancelled) break;
    }

    return FilterResult{.matches = std::move(matches), .error = std::nullopt};
}

} // namespace jsontitan::core
