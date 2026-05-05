// ---------------------------------------------------------------------------
// Preservation Property Tests — Search Result Accuracy
// Property 2: For all query/tree combinations, filter() produces correct results
//
// These tests verify the EXISTING behavior of the pure filter() function.
// They MUST PASS on unfixed code — the fix does not change filter() itself.
//
// **Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5**
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "core/json_node.h"
#include "core/search_engine.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// RapidCheck Generators for JsonNode trees
// ---------------------------------------------------------------------------

// Generate a random string of lowercase ASCII characters (length 1-7)
auto genKey() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto len = *rc::gen::inRange(1, 8);
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            s += *rc::gen::inRange<char>('a', 'z' + 1);
        }
        return s;
    });
}

// Generate a random value string (may contain mixed case for testing)
auto genValue() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto len = *rc::gen::inRange(0, 12);
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            auto choice = *rc::gen::inRange(0, 3);
            if (choice == 0) {
                s += *rc::gen::inRange<char>('a', 'z' + 1);
            } else if (choice == 1) {
                s += *rc::gen::inRange<char>('A', 'Z' + 1);
            } else {
                s += *rc::gen::inRange<char>('0', '9' + 1);
            }
        }
        return s;
    });
}

// Forward declaration for recursive generation
std::shared_ptr<const JsonNode> genTreeImpl(int maxDepth, int& budget);

std::shared_ptr<const JsonNode> genLeafNode(int& budget) {
    if (budget <= 0) {
        return JsonNode::makeNull("leaf");
    }
    --budget;

    auto key = *genKey();
    // Pick a random leaf type
    auto typeChoice = *rc::gen::inRange(0, 4);
    switch (typeChoice) {
        case 0: return JsonNode::makeString(key, *genValue());
        case 1: return JsonNode::makeNumber(key, std::to_string(*rc::gen::inRange(-1000, 1000)));
        case 2: return JsonNode::makeBool(key, *rc::gen::arbitrary<bool>());
        default: return JsonNode::makeNull(key);
    }
}

std::shared_ptr<const JsonNode> genTreeImpl(int maxDepth, int& budget) {
    if (budget <= 0 || maxDepth <= 0) {
        return genLeafNode(budget);
    }

    --budget;
    auto key = *genKey();

    // Decide: leaf or container
    auto isContainer = *rc::gen::inRange(0, 3); // 2/3 chance of container at depth > 0
    if (isContainer == 0 || maxDepth <= 1) {
        ++budget; // give back since genLeafNode will decrement
        return genLeafNode(budget);
    }

    // Generate children (1-6 children)
    auto numChildren = *rc::gen::inRange(1, std::min(7, budget + 1));
    std::vector<std::shared_ptr<const JsonNode>> children;
    children.reserve(numChildren);
    for (int i = 0; i < numChildren && budget > 0; ++i) {
        children.push_back(genTreeImpl(maxDepth - 1, budget));
    }

    // Object or Array
    if (*rc::gen::arbitrary<bool>()) {
        return JsonNode::makeObject(key, std::move(children));
    } else {
        return JsonNode::makeArray(key, std::move(children));
    }
}

// Generate a random JsonNode tree with moderate size (up to ~100 nodes)
auto genTree() -> rc::Gen<std::shared_ptr<const JsonNode>> {
    return rc::gen::exec([]() -> std::shared_ptr<const JsonNode> {
        int budget = *rc::gen::inRange(5, 100);
        int maxDepth = *rc::gen::inRange(2, 6);
        return genTreeImpl(maxDepth, budget);
    });
}

// Generate a non-empty substring search pattern (1-4 lowercase chars)
auto genSubstringPattern() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto len = *rc::gen::inRange(1, 5);
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            s += *rc::gen::inRange<char>('a', 'z' + 1);
        }
        return s;
    });
}

// Generate a SearchQuery with Substring mode
auto genSubstringQuery() -> rc::Gen<SearchQuery> {
    return rc::gen::exec([]() -> SearchQuery {
        SearchQuery q;
        q.pattern = *genSubstringPattern();
        q.mode = SearchMode::Substring;
        q.caseSensitive = *rc::gen::arbitrary<bool>();
        return q;
    });
}

// Generate a SearchQuery (either Substring or Regex with valid pattern)
auto genValidQuery() -> rc::Gen<SearchQuery> {
    return rc::gen::exec([]() -> SearchQuery {
        SearchQuery q;
        q.pattern = *genSubstringPattern();
        q.caseSensitive = *rc::gen::arbitrary<bool>();
        // Use substring mode most of the time (regex can be slow with random patterns)
        q.mode = *rc::gen::arbitrary<bool>() ? SearchMode::Regex : SearchMode::Substring;
        return q;
    });
}

// Generate known-invalid regex patterns
auto genInvalidRegexPattern() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        std::vector<std::string> patterns = {
            "[unclosed",
            "(unmatched",
            "*invalid",
            "+invalid",
            "?invalid",
            "[z-a]",
            "(?P<bad",
            "\\"
        };
        auto idx = *rc::gen::inRange(std::size_t{0}, patterns.size());
        return patterns[idx];
    });
}

// ---------------------------------------------------------------------------
// Helper: case-insensitive substring check (mirrors search_engine logic)
// ---------------------------------------------------------------------------
auto toLower(const std::string& s) -> std::string {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        result += static_cast<char>(std::tolower(c));
    }
    return result;
}

auto containsCI(const std::string& haystack, const std::string& needle) -> bool {
    if (needle.empty()) return true;
    auto lh = toLower(haystack);
    auto ln = toLower(needle);
    return lh.find(ln) != std::string::npos;
}

auto containsCS(const std::string& haystack, const std::string& needle) -> bool {
    if (needle.empty()) return true;
    return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Helper: Verify ancestor path correctness by walking the tree
// ---------------------------------------------------------------------------
auto resolveAncestorPath(const JsonNode& root,
                         const std::vector<std::size_t>& ancestorIndices)
    -> const JsonNode* {
    const JsonNode* current = &root;
    for (auto idx : ancestorIndices) {
        if (idx >= current->children.size()) {
            return nullptr; // invalid path
        }
        current = current->children[idx].get();
    }
    return current;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Property 2.1: Determinism
// For random trees and queries, calling filter() twice produces identical results.
//
// **Validates: Requirements 3.1**
// ---------------------------------------------------------------------------
TEST(SearchPreservation, DeterministicResults) {
    rc::check("filter() is deterministic: same input always produces same output",
        [](void) {
        auto tree = *genTree();
        auto query = *genValidQuery();

        auto result1 = filter(*tree, query);
        auto result2 = filter(*tree, query);

        // Same number of matches
        RC_ASSERT(result1.matches.size() == result2.matches.size());

        // Same error state
        RC_ASSERT(result1.error.has_value() == result2.error.has_value());
        if (result1.error.has_value()) {
            RC_ASSERT(result1.error->description == result2.error->description);
        }

        // Same matches in same order
        for (std::size_t i = 0; i < result1.matches.size(); ++i) {
            RC_ASSERT(result1.matches[i].ancestorIndices == result2.matches[i].ancestorIndices);
            RC_ASSERT(result1.matches[i].node.get() == result2.matches[i].node.get());
        }
    });
}

// ---------------------------------------------------------------------------
// Property 2.2: Empty Pattern
// For any tree, empty pattern returns zero matches and no error.
//
// **Validates: Requirements 3.2**
// ---------------------------------------------------------------------------
TEST(SearchPreservation, EmptyPatternReturnsNoMatches) {
    rc::check("Empty pattern returns zero matches and no error for any tree",
        [](void) {
        auto tree = *genTree();

        SearchQuery query;
        query.pattern = "";
        query.mode = *rc::gen::arbitrary<bool>() ? SearchMode::Regex : SearchMode::Substring;
        query.caseSensitive = *rc::gen::arbitrary<bool>();

        auto result = filter(*tree, query);

        RC_ASSERT(result.matches.empty());
        RC_ASSERT(!result.error.has_value());
    });
}

// ---------------------------------------------------------------------------
// Property 2.3: Invalid Regex
// For invalid regex patterns in Regex mode, result has error with non-empty description.
//
// **Validates: Requirements 3.3**
// ---------------------------------------------------------------------------
TEST(SearchPreservation, InvalidRegexReturnsError) {
    rc::check("Invalid regex pattern returns error with non-empty description",
        [](void) {
        auto tree = *genTree();
        auto invalidPattern = *genInvalidRegexPattern();

        SearchQuery query;
        query.pattern = invalidPattern;
        query.mode = SearchMode::Regex;
        query.caseSensitive = *rc::gen::arbitrary<bool>();

        auto result = filter(*tree, query);

        RC_ASSERT(result.error.has_value());
        RC_ASSERT(!result.error->description.empty());
        RC_ASSERT(result.matches.empty());
    });
}

// ---------------------------------------------------------------------------
// Property 2.4: Substring Match Correctness
// For substring queries, every match node's key or string value contains the
// pattern (case-insensitive by default, case-sensitive if caseSensitive=true).
//
// **Validates: Requirements 3.4**
// ---------------------------------------------------------------------------
TEST(SearchPreservation, SubstringMatchCorrectness) {
    rc::check("Every substring match node's key or value contains the pattern",
        [](void) {
        auto tree = *genTree();
        auto query = *genSubstringQuery();

        auto result = filter(*tree, query);

        RC_ASSERT(!result.error.has_value());

        for (const auto& match : result.matches) {
            const auto& node = *match.node;
            bool keyMatches = false;
            bool valueMatches = false;

            if (query.caseSensitive) {
                keyMatches = containsCS(node.key, query.pattern);
                if (node.type == NodeType::String) {
                    valueMatches = containsCS(node.value, query.pattern);
                }
            } else {
                keyMatches = containsCI(node.key, query.pattern);
                if (node.type == NodeType::String) {
                    valueMatches = containsCI(node.value, query.pattern);
                }
            }

            RC_ASSERT(keyMatches || valueMatches);
        }
    });
}

// ---------------------------------------------------------------------------
// Property 2.5: Ancestor Path Correctness
// For all matches, ancestorIndices correctly traces the path from root to the
// matched node (following children indices at each level).
//
// **Validates: Requirements 3.5**
// ---------------------------------------------------------------------------
TEST(SearchPreservation, AncestorPathCorrectness) {
    rc::check("ancestorIndices correctly traces path from root to matched node",
        [](void) {
        auto tree = *genTree();
        auto query = *genValidQuery();

        auto result = filter(*tree, query);

        // Skip if we got an error (invalid regex)
        RC_PRE(!result.error.has_value());

        for (const auto& match : result.matches) {
            // Resolve the ancestor path by walking the tree
            const JsonNode* resolved = resolveAncestorPath(*tree, match.ancestorIndices);

            // The path must resolve to a valid node
            RC_ASSERT(resolved != nullptr);

            // The resolved node must be the same as the match node
            RC_ASSERT(resolved == match.node.get());
        }
    });
}
