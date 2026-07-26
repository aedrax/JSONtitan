// ---------------------------------------------------------------------------
// JSONPath Query Engine Tests (parse + evaluate)
// Feature: jsonpath-query-mode (Phase 5c, plan C9)
// ---------------------------------------------------------------------------
// Golden parse-error and evaluation tests for the supported JSONPath subset,
// arena-vs-JsonNode backing equivalence, and RapidCheck properties:
//   * $..* matches exactly every non-root node (countDescendants)
//   * results never contain duplicate paths
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "core/deletion_engine.h"
#include "core/json_node.h"
#include "core/jsonpath.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "core/parser.h"
#include "core/search_engine.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse a complete JSON string via the streaming parser (the test oracle,
// producing a JsonNode tree).
auto parseOracle(const std::string& json) -> ParseResult {
    auto state = makeParserState();
    std::vector<std::byte> bytes(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        bytes[i] = static_cast<std::byte>(json[i]);
    }
    auto result = parseChunk(*state, std::span<const std::byte>(bytes));
    if (result.error) {
        return ParseResult{.root = nullptr, .error = result.error};
    }
    return finalizeParse(*result.nextState);
}

using Paths = std::vector<std::vector<std::size_t>>;

// Extract the (sorted) ancestor-index paths from a FilterResult.
auto sortedPaths(const FilterResult& result) -> Paths {
    Paths paths;
    paths.reserve(result.matches.size());
    for (const auto& match : result.matches) {
        paths.push_back(match.ancestorIndices);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// Fixture document exercising every step kind:
//   root children:  store [0], name [1], list [2]
//   store children: book [0,0], "a b" [0,1], "" [0,2]
//   book children:  {author,title} x2
//   list children:  [1,2] (nested array), {"name":"inner"}
const char* const kFixtureJson = R"({
  "store": {
    "book": [
      {"author": "A1", "title": "T1"},
      {"author": "A2", "title": "T2"}
    ],
    "a b": "spaced",
    "": "empty-key"
  },
  "name": "top",
  "list": [[1, 2], {"name": "inner"}]
})";

// Total non-root nodes in kFixtureJson:
//   3 (store,name,list) + 3 (book,"a b","") + 2 books + 4 (author/title x2)
//   + 2 list elements + 2 nested array scalars + 1 inner name = 17
constexpr std::size_t kFixtureDescendants = 17;

class JsonPathFixtureTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto parsed = parseOracle(kFixtureJson);
        ASSERT_FALSE(parsed.error.has_value());
        ASSERT_NE(parsed.root, nullptr);
        root = parsed.root;
    }

    auto query(const std::string& expr) const -> FilterResult {
        return queryJsonPath(NodeView(*root), expr);
    }

    auto queryPaths(const std::string& expr) const -> Paths {
        auto result = query(expr);
        EXPECT_FALSE(result.error.has_value())
            << expr << ": " << (result.error ? result.error->description : "");
        return sortedPaths(result);
    }

    std::shared_ptr<const JsonNode> root;
};

// ---------------------------------------------------------------------------
// Parse errors (with byte positions)
// ---------------------------------------------------------------------------

TEST(JsonPathParseTest, RejectsMissingDollarPrefix) {
    for (const auto* expr : {"", "store.book", ".store", "@.store"}) {
        auto steps = parseJsonPath(expr);
        ASSERT_FALSE(steps.has_value()) << expr;
        EXPECT_EQ(steps.error().position, 0u) << expr;
        EXPECT_EQ(steps.error().description, "JSONPath must start with '$'");
    }
}

TEST(JsonPathParseTest, RejectsUnterminatedBracket) {
    auto steps = parseJsonPath("$[");
    ASSERT_FALSE(steps.has_value());
    EXPECT_EQ(steps.error().position, 2u);
    EXPECT_EQ(steps.error().description, "Unterminated '['");
}

TEST(JsonPathParseTest, RejectsUnterminatedQuotedName) {
    auto steps = parseJsonPath("$['abc");
    ASSERT_FALSE(steps.has_value());
    EXPECT_EQ(steps.error().position, 6u);
    EXPECT_EQ(steps.error().description, "Unterminated quoted name");

    // Quoted name closed but bracket left open.
    auto noBracket = parseJsonPath("$['abc'");
    ASSERT_FALSE(noBracket.has_value());
    EXPECT_EQ(noBracket.error().position, 7u);
    EXPECT_EQ(noBracket.error().description, "Expected ']' after quoted name");
}

TEST(JsonPathParseTest, RejectsUnterminatedIndex) {
    auto steps = parseJsonPath("$[12");
    ASSERT_FALSE(steps.has_value());
    EXPECT_EQ(steps.error().position, 4u);
    EXPECT_EQ(steps.error().description, "Expected ']' after index");
}

TEST(JsonPathParseTest, RejectsFilterExpressionsExplicitly) {
    auto steps = parseJsonPath("$[?(@.price < 10)]");
    ASSERT_FALSE(steps.has_value());
    EXPECT_EQ(steps.error().position, 1u);
    EXPECT_EQ(steps.error().description,
              "JSONPath filter expressions are not supported");

    auto recursive = parseJsonPath("$..[?(@.x)]");
    ASSERT_FALSE(recursive.has_value());
    EXPECT_EQ(recursive.error().description,
              "JSONPath filter expressions are not supported");
}

TEST(JsonPathParseTest, RejectsMissingNames) {
    auto dot = parseJsonPath("$.");
    ASSERT_FALSE(dot.has_value());
    EXPECT_EQ(dot.error().position, 2u);
    EXPECT_EQ(dot.error().description, "Expected name after '.'");

    auto dotdot = parseJsonPath("$..");
    ASSERT_FALSE(dotdot.has_value());
    EXPECT_EQ(dotdot.error().position, 3u);
    EXPECT_EQ(dotdot.error().description, "Expected name after '..'");
}

TEST(JsonPathParseTest, RejectsEmptyBracketAndStrayCharacters) {
    auto empty = parseJsonPath("$[]");
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().position, 2u);
    EXPECT_EQ(empty.error().description,
              "Expected index, quoted name, or '*' inside '[]'");

    auto stray = parseJsonPath("$x");
    ASSERT_FALSE(stray.has_value());
    EXPECT_EQ(stray.error().position, 1u);
    EXPECT_EQ(stray.error().description, "Unexpected character 'x'");
}

TEST(JsonPathParseTest, RejectsOverflowingIndex) {
    // 2^64 == 18446744073709551616 silently wrapped to 0 in the first draft.
    auto steps = parseJsonPath("$[18446744073709551616]");
    ASSERT_FALSE(steps.has_value());
    EXPECT_EQ(steps.error().position, 2u);
    EXPECT_EQ(steps.error().description, "Array index too large");

    // The maximum representable value still parses.
    auto maxOk = parseJsonPath("$[18446744073709551615]");
    EXPECT_TRUE(maxOk.has_value());
}

TEST(JsonPathParseTest, ParsesQuotedNameEscapes) {
    auto steps = parseJsonPath(R"($['it\'s']["a\"b"]['back\\slash'])");
    ASSERT_TRUE(steps.has_value());
    ASSERT_EQ(steps->size(), 3u);
    EXPECT_EQ((*steps)[0].key, "it's");
    EXPECT_EQ((*steps)[1].key, "a\"b");
    EXPECT_EQ((*steps)[2].key, "back\\slash");
}

TEST(JsonPathParseTest, QueryReportsParseErrorWithPosition) {
    auto root = JsonNode::makeObject("", {});
    auto result = queryJsonPath(NodeView(*root), "$[?(@.x)]");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.matches.empty());
    EXPECT_EQ(result.error->description,
              "JSONPath error at position 1: "
              "JSONPath filter expressions are not supported");
}

// ---------------------------------------------------------------------------
// Golden evaluations (every step kind)
// ---------------------------------------------------------------------------

TEST_F(JsonPathFixtureTest, RootOnlyMatchesRoot) {
    auto result = query("$");
    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_TRUE(result.matches[0].ancestorIndices.empty());
    EXPECT_EQ(result.matches[0].node, nullptr);
}

TEST_F(JsonPathFixtureTest, DottedKeyChain) {
    EXPECT_EQ(queryPaths("$.store.book"), (Paths{{0, 0}}));
    EXPECT_EQ(queryPaths("$.name"), (Paths{{1}}));
    EXPECT_EQ(queryPaths("$.missing"), Paths{});
}

TEST_F(JsonPathFixtureTest, QuotedKeysIncludingSpacesAndEmpty) {
    EXPECT_EQ(queryPaths("$['store']['a b']"), (Paths{{0, 1}}));
    EXPECT_EQ(queryPaths("$.store['']"), (Paths{{0, 2}}));
    EXPECT_EQ(queryPaths("$[\"name\"]"), (Paths{{1}}));
}

TEST_F(JsonPathFixtureTest, ArrayIndices) {
    EXPECT_EQ(queryPaths("$.list[0]"), (Paths{{2, 0}}));
    EXPECT_EQ(queryPaths("$.list[0][1]"), (Paths{{2, 0, 1}}));
    EXPECT_EQ(queryPaths("$.store.book[1].title"), (Paths{{0, 0, 1, 1}}));
    // Out-of-range index and index applied to an object both match nothing.
    EXPECT_EQ(queryPaths("$.list[7]"), Paths{});
    EXPECT_EQ(queryPaths("$[0]"), Paths{});
}

TEST_F(JsonPathFixtureTest, Wildcards) {
    const Paths topLevel{{0}, {1}, {2}};
    EXPECT_EQ(queryPaths("$.*"), topLevel);
    EXPECT_EQ(queryPaths("$[*]"), topLevel);
    EXPECT_EQ(queryPaths("$.store.book[*].author"),
              (Paths{{0, 0, 0, 0}, {0, 0, 1, 0}}));
}

TEST_F(JsonPathFixtureTest, RecursiveKey) {
    const Paths names{{1}, {2, 1, 0}};
    EXPECT_EQ(queryPaths("$..name"), names);
    EXPECT_EQ(queryPaths("$..['name']"), names);
    EXPECT_EQ(queryPaths("$..author"),
              (Paths{{0, 0, 0, 0}, {0, 0, 1, 0}}));
}

TEST_F(JsonPathFixtureTest, RecursiveIndex) {
    // Every array's first element: book[0], list[0], list[0][0].
    EXPECT_EQ(queryPaths("$..[0]"), (Paths{{0, 0, 0}, {2, 0}, {2, 0, 0}}));
}

TEST_F(JsonPathFixtureTest, RecursiveWildcardMatchesEveryNonRootNode) {
    auto starPaths = queryPaths("$..*");
    EXPECT_EQ(starPaths.size(), kFixtureDescendants);
    EXPECT_EQ(countDescendants(NodeView(*root)), kFixtureDescendants);
    EXPECT_EQ(queryPaths("$..[*]"), starPaths);
    // No duplicates.
    EXPECT_EQ(std::set<std::vector<std::size_t>>(starPaths.begin(),
                                                 starPaths.end())
                  .size(),
              starPaths.size());
}

TEST_F(JsonPathFixtureTest, ChainedRecursiveCombos) {
    EXPECT_EQ(queryPaths("$..book..author"),
              (Paths{{0, 0, 0, 0}, {0, 0, 1, 0}}));
    EXPECT_EQ(queryPaths("$..book[0].author"), (Paths{{0, 0, 0, 0}}));
    EXPECT_EQ(queryPaths("$.list..name"), (Paths{{2, 1, 0}}));
    EXPECT_EQ(queryPaths("$..list[*]"), (Paths{{2, 0}, {2, 1}}));
}

TEST_F(JsonPathFixtureTest, MatchNodesAreAlwaysNull) {
    auto result = query("$..*");
    for (const auto& match : result.matches) {
        EXPECT_EQ(match.node, nullptr);
    }
}

// ---------------------------------------------------------------------------
// SearchMode::JsonPath dispatch through filter() — both backings
// ---------------------------------------------------------------------------

TEST_F(JsonPathFixtureTest, FilterDispatchesJsonPathMode) {
    SearchQuery searchQuery{.pattern = "$..name",
                            .mode = SearchMode::JsonPath,
                            .caseSensitive = true};  // ignored for JSONPath
    auto result = filter(*root, searchQuery);
    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(sortedPaths(result), (Paths{{1}, {2, 1, 0}}));
}

TEST(JsonPathArenaTest, ArenaAndJsonNodeBackingsProduceIdenticalPaths) {
    auto arenaResult = parseBuffer(std::string(kFixtureJson));
    ASSERT_TRUE(arenaResult.ok());
    auto oracle = parseOracle(kFixtureJson);
    ASSERT_FALSE(oracle.error.has_value());
    ASSERT_NE(oracle.root, nullptr);

    const char* const queries[] = {
        "$",         "$.store.book[*].author", "$..name", "$..*",
        "$..[0]",    "$[*]",                   "$.store['']",
        "$..book..author",
    };
    for (const auto* expr : queries) {
        auto arenaPaths =
            sortedPaths(queryJsonPath(NodeView(*arenaResult.root), expr));
        auto oraclePaths =
            sortedPaths(queryJsonPath(NodeView(*oracle.root), expr));
        EXPECT_EQ(arenaPaths, oraclePaths) << expr;
    }

    // The arena filter() overload dispatches identically.
    SearchQuery searchQuery{.pattern = "$..[0]", .mode = SearchMode::JsonPath};
    EXPECT_EQ(sortedPaths(filter(*arenaResult.root, searchQuery)),
              sortedPaths(filter(*oracle.root, searchQuery)));
}

// ---------------------------------------------------------------------------
// RapidCheck properties
// ---------------------------------------------------------------------------

// Generate a random JsonNode tree (same shape as deletion_engine_pbt's
// generator: object keys are "k0".."kN", scalars are lowercase strings).
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNode(int maxDepth = 3,
                                                     int maxBreadth = 4) {
    return rc::gen::exec([maxDepth, maxBreadth]() -> std::shared_ptr<const JsonNode> {
        int choice = (maxDepth <= 0) ? *rc::gen::inRange(2, 6) : *rc::gen::inRange(0, 6);
        switch (choice) {
            case 0: {
                int childCount = *rc::gen::inRange(1, maxBreadth + 1);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < childCount; ++i) {
                    auto child = *genJsonNode(maxDepth - 1, maxBreadth);
                    std::string key = "k" + std::to_string(i);
                    switch (child->type) {
                        case NodeType::Object:
                            children.push_back(JsonNode::makeObject(key, child->children));
                            break;
                        case NodeType::Array:
                            children.push_back(JsonNode::makeArray(key, child->children));
                            break;
                        case NodeType::String:
                            children.push_back(JsonNode::makeString(key, child->value));
                            break;
                        case NodeType::Number:
                            children.push_back(JsonNode::makeNumber(key, child->value));
                            break;
                        case NodeType::Boolean:
                            children.push_back(JsonNode::makeBool(key, child->value == "true"));
                            break;
                        case NodeType::Null:
                            children.push_back(JsonNode::makeNull(key));
                            break;
                    }
                }
                return JsonNode::makeObject("", std::move(children));
            }
            case 1: {
                int elemCount = *rc::gen::inRange(1, maxBreadth + 1);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < elemCount; ++i) {
                    children.push_back(*genJsonNode(maxDepth - 1, maxBreadth));
                }
                return JsonNode::makeArray("", std::move(children));
            }
            case 2: {
                int len = *rc::gen::inRange(1, 10);
                std::string val;
                for (int c = 0; c < len; ++c) {
                    val += static_cast<char>(*rc::gen::inRange<int>('a', 'z'));
                }
                return JsonNode::makeString("", val);
            }
            case 3:
                return JsonNode::makeNumber(
                    "", std::to_string(*rc::gen::inRange(-1000, 1000)));
            case 4:
                return JsonNode::makeBool("", *rc::gen::arbitrary<bool>());
            case 5:
            default:
                return JsonNode::makeNull("");
        }
    });
}

TEST(JsonPathPBT, RecursiveWildcardMatchesEveryNodeExactlyOnce) {
    rc::check(
        "Feature: jsonpath, Property: $..* matches every non-root node "
        "exactly once (total node count = countDescendants + 1 incl. root)",
        [] {
            auto root = *genJsonNode(4, 4);
            auto result = queryJsonPath(NodeView(*root), "$..*");
            RC_ASSERT(!result.error.has_value());
            // $..* selects every node except the root itself, so match count
            // is the total node count (countDescendants + 1) minus the root.
            RC_ASSERT(result.matches.size() == countDescendants(NodeView(*root)));
        });
}

TEST(JsonPathPBT, ResultsNeverContainDuplicatePaths) {
    rc::check(
        "Feature: jsonpath, Property: evaluation results are deduplicated",
        [] {
            auto root = *genJsonNode(4, 4);
            for (const auto* expr :
                 {"$..*", "$..[0]", "$..k0", "$..k0..k1", "$..k0..*"}) {
                auto result = queryJsonPath(NodeView(*root), expr);
                RC_ASSERT(!result.error.has_value());
                auto paths = sortedPaths(result);
                RC_ASSERT(std::adjacent_find(paths.begin(), paths.end()) ==
                          paths.end());
            }
        });
}

}  // namespace
