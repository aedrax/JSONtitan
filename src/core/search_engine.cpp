#include "core/search_engine.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace jsontitan::core {

namespace {

// Convert a string to lowercase for case-insensitive matching
auto toLower(const std::string& s) -> std::string {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        result += static_cast<char>(std::tolower(c));
    }
    return result;
}

// Check if haystack contains needle (case-insensitive)
auto containsSubstringCI(const std::string& haystack, const std::string& needle) -> bool {
    if (needle.empty()) return true;
    auto lowerHaystack = toLower(haystack);
    auto lowerNeedle = toLower(needle);
    return lowerHaystack.find(lowerNeedle) != std::string::npos;
}

// Check if haystack contains needle (case-sensitive)
auto containsSubstringCS(const std::string& haystack, const std::string& needle) -> bool {
    if (needle.empty()) return true;
    return haystack.find(needle) != std::string::npos;
}

// Check if a node's key or string value matches the query
auto nodeMatches(const JsonNode& node, const SearchQuery& query,
                 const std::regex* compiledRegex) -> bool {
    if (query.mode == SearchMode::Substring) {
        if (query.caseSensitive) {
            if (containsSubstringCS(node.key, query.pattern)) return true;
            if (node.type == NodeType::String && containsSubstringCS(node.value, query.pattern))
                return true;
        } else {
            if (containsSubstringCI(node.key, query.pattern)) return true;
            if (node.type == NodeType::String && containsSubstringCI(node.value, query.pattern))
                return true;
        }
    } else if (query.mode == SearchMode::Regex && compiledRegex) {
        if (std::regex_search(node.key, *compiledRegex)) return true;
        if (node.type == NodeType::String && std::regex_search(node.value, *compiledRegex))
            return true;
    }
    return false;
}

// Recursive DFS to find all matching nodes and build ancestor index paths.
void searchRecursive(const std::shared_ptr<const JsonNode>& node,
                     const SearchQuery& query,
                     const std::regex* compiledRegex,
                     std::vector<std::size_t>& currentPath,
                     std::vector<SearchMatch>& matches) {
    // Check if this node itself matches
    if (nodeMatches(*node, query, compiledRegex)) {
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = node
        });
    }

    // Recurse into children
    for (std::size_t i = 0; i < node->children.size(); ++i) {
        currentPath.push_back(i);
        searchRecursive(node->children[i], query, compiledRegex, currentPath, matches);
        currentPath.pop_back();
    }
}

} // anonymous namespace

auto filter(const JsonNode& root, const SearchQuery& query) -> FilterResult {
    // Empty pattern matches nothing — return empty results (not an error)
    if (query.pattern.empty()) {
        return FilterResult{.matches = {}, .error = std::nullopt};
    }

    // For regex mode, try to compile the pattern first
    std::regex compiledRegex;
    const std::regex* regexPtr = nullptr;

    if (query.mode == SearchMode::Regex) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!query.caseSensitive) {
                flags |= std::regex_constants::icase;
            }
            compiledRegex = std::regex(query.pattern, flags);
            regexPtr = &compiledRegex;
        } catch (const std::regex_error& e) {
            return FilterResult{
                .matches = {},
                .error = SearchError{.description = std::string("Invalid regex pattern: ") + e.what()}
            };
        }
    }

    // We need a shared_ptr to the root for the recursive search.
    // Since filter takes a const reference, we create a temporary shared_ptr
    // that wraps the root without owning it (using a no-op deleter).
    // However, for child nodes we already have shared_ptrs from the tree.
    // For the root itself, we'll handle it specially.
    std::vector<SearchMatch> matches;
    std::vector<std::size_t> currentPath;

    // Check root node
    if (nodeMatches(root, query, regexPtr)) {
        // For the root match, we need a shared_ptr. Since we don't own the root,
        // we create a non-owning shared_ptr.
        auto rootPtr = std::shared_ptr<const JsonNode>(&root, [](const JsonNode*) {});
        matches.push_back(SearchMatch{
            .ancestorIndices = currentPath,
            .node = rootPtr
        });
    }

    // Recurse into children
    for (std::size_t i = 0; i < root.children.size(); ++i) {
        currentPath.push_back(i);
        searchRecursive(root.children[i], query, regexPtr, currentPath, matches);
        currentPath.pop_back();
    }

    return FilterResult{.matches = std::move(matches), .error = std::nullopt};
}

} // namespace jsontitan::core
