// ---------------------------------------------------------------------------
// Tests for NodeView (uniform view over JsonNode and ArenaJsonNode) and the
// budget-enforcing token emitter, plus the parse-time node counter.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "core/parser.h"
#include "core/token_emitter.h"

using namespace jsontitan::core;

namespace {

// Parse via the legacy (oracle) chunk parser to obtain a JsonNode tree.
auto oracleParse(const std::string& json) -> std::shared_ptr<const JsonNode> {
    auto state = makeParserState();
    auto chunk = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(json.data()), json.size());
    auto chunkResult = parseChunk(*state, chunk);
    if (chunkResult.error) {
        return nullptr;
    }
    auto result = finalizeParse(*chunkResult.nextState);
    if (result.error) {
        return nullptr;
    }
    return result.root;
}

// Recursively assert two NodeViews describe identical trees.
// Null nodes' value is representation-defined (the oracle parser stores
// "null", the arena backing leaves it empty) and no consumer reads it, so
// it is excluded from the comparison.
void assertSameTree(NodeView a, NodeView b) {
    ASSERT_EQ(a.type(), b.type());
    ASSERT_EQ(a.key(), b.key());
    if (a.type() != NodeType::Null) {
        ASSERT_EQ(a.value(), b.value());
    }
    ASSERT_EQ(a.childCount(), b.childCount());
    for (std::size_t i = 0; i < a.childCount(); ++i) {
        assertSameTree(a.child(i), b.child(i));
    }
}

// rc-friendly variant returning false on the first mismatch.
bool sameTree(NodeView a, NodeView b) {
    if (a.type() != b.type() || a.key() != b.key() ||
        a.childCount() != b.childCount()) {
        return false;
    }
    if (a.type() != NodeType::Null && a.value() != b.value()) {
        return false;
    }
    for (std::size_t i = 0; i < a.childCount(); ++i) {
        if (!sameTree(a.child(i), b.child(i))) {
            return false;
        }
    }
    return true;
}

bool sameTokens(const TokenEmitResult& a, const TokenEmitResult& b) {
    if (a.truncated != b.truncated || a.tokens.size() != b.tokens.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.tokens.size(); ++i) {
        if (a.tokens[i].type != b.tokens[i].type ||
            a.tokens[i].text != b.tokens[i].text ||
            a.tokens[i].depth != b.tokens[i].depth) {
            return false;
        }
    }
    return true;
}

std::size_t countArenaNodes(const ArenaJsonNode* node) {
    if (!node) return 0;
    std::size_t count = 1;
    for (std::size_t i = 0; i < node->childCount; ++i) {
        count += countArenaNodes(node->children[i]);
    }
    return count;
}

// Validate that a byte string is well-formed UTF-8.
bool isValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        if (c < 0x80) {
            len = 1;
        } else if ((c & 0xE0U) == 0xC0U) {
            len = 2;
        } else if ((c & 0xF0U) == 0xE0U) {
            len = 3;
        } else if ((c & 0xF8U) == 0xF0U) {
            len = 4;
        } else {
            return false;  // continuation byte or invalid lead byte
        }
        if (i + len > s.size()) {
            return false;  // sequence runs past the end
        }
        for (std::size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0U) != 0x80U) {
                return false;
            }
        }
        i += len;
    }
    return true;
}

// Generate JSON documents whose values round-trip identically through both
// parsers (integer numbers only; safe ASCII keys/strings).
rc::Gen<std::string> genRoundTrippableJson() {
    return rc::gen::exec([]() -> std::string {
        // Recursive lambda building a JSON value string.
        std::function<std::string(int)> genValue = [&](int depth) -> std::string {
            int choice = (depth <= 0) ? *rc::gen::inRange(0, 4)
                                      : *rc::gen::inRange(0, 6);
            switch (choice) {
                case 0: return std::to_string(*rc::gen::inRange(-10000, 10000));
                case 1: return *rc::gen::arbitrary<bool>() ? "true" : "false";
                case 2: return "null";
                case 3: {
                    int len = *rc::gen::inRange(0, 12);
                    std::string s = "\"";
                    for (int i = 0; i < len; ++i) {
                        s += static_cast<char>('a' + *rc::gen::inRange(0, 26));
                    }
                    s += '"';
                    return s;
                }
                case 4: {  // object
                    int n = *rc::gen::inRange(0, 5);
                    std::string s = "{";
                    for (int i = 0; i < n; ++i) {
                        if (i > 0) s += ',';
                        s += "\"k" + std::to_string(i) + "\":" + genValue(depth - 1);
                    }
                    s += '}';
                    return s;
                }
                default: {  // array
                    int n = *rc::gen::inRange(0, 5);
                    std::string s = "[";
                    for (int i = 0; i < n; ++i) {
                        if (i > 0) s += ',';
                        s += genValue(depth - 1);
                    }
                    s += ']';
                    return s;
                }
            }
        };
        return genValue(*rc::gen::inRange(1, 4));
    });
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// NodeView accessors agree across both backings
// ---------------------------------------------------------------------------

TEST(NodeViewAccessors, ArenaAndJsonNodeBackingsAgree) {
    const std::string json =
        R"({"name":"Alice","age":30,"active":true,"tags":["a","b"],)"
        R"("address":{"city":"Rome","zip":"00100"},"nothing":null})";

    auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
    ASSERT_TRUE(arenaResult.ok());

    auto oracleRoot = oracleParse(json);
    ASSERT_NE(oracleRoot, nullptr);

    NodeView arenaView(*arenaResult.root);
    NodeView jsonView(*oracleRoot);

    EXPECT_TRUE(arenaView.isArena());
    EXPECT_FALSE(jsonView.isArena());
    EXPECT_EQ(arenaView.asJsonNode(), nullptr);
    EXPECT_EQ(jsonView.asJsonNode(), oracleRoot.get());

    assertSameTree(arenaView, jsonView);
}

TEST(NodeViewAccessors, PropertyArenaAndOracleAgree) {
    rc::check("NodeView exposes identical shape/keys/values over both backings",
        []() {
            const auto json = *genRoundTrippableJson();

            auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
            RC_PRE(arenaResult.ok());

            auto oracleRoot = oracleParse(json);
            RC_PRE(oracleRoot != nullptr);

            RC_ASSERT(sameTree(NodeView(*arenaResult.root), NodeView(*oracleRoot)));
        });
}

// ---------------------------------------------------------------------------
// emitTokens over an arena backing matches emitTokens over JsonNode
// ---------------------------------------------------------------------------

TEST(NodeViewTokenEmission, ArenaTokensEqualJsonNodeTokens) {
    rc::check("emitTokens(NodeView-arena) == emitTokens(JsonNode) token-for-token",
        []() {
            const auto json = *genRoundTrippableJson();

            auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
            RC_PRE(arenaResult.ok());

            // Deep-copy path gives the equivalent JsonNode tree.
            auto jsonRoot = arenaResult.root->toJsonNode();
            RC_PRE(jsonRoot != nullptr);

            PrettyPrintOptions opts;
            opts.indentWidth = *rc::gen::inRange(1, 9);
            opts.sortKeys = *rc::gen::arbitrary<bool>();

            auto arenaTokens = emitTokens(NodeView(*arenaResult.root), opts);
            auto jsonTokens = emitTokens(*jsonRoot, opts);

            RC_ASSERT(sameTokens(arenaTokens, jsonTokens));
        });
}

// ---------------------------------------------------------------------------
// Byte budget: a single huge string token must not blow past maxOutputSize
// ---------------------------------------------------------------------------

TEST(TokenEmitterBudget, HugeSingleStringRespectsByteBudget) {
    // 1 MB string value with embedded multi-byte UTF-8 so the cut point is
    // very likely to land inside a sequence unless the emitter is careful.
    std::string big;
    big.reserve(1024 * 1024 + 4);
    while (big.size() < 1024 * 1024) {
        big += "ab\xC3\xA9";  // "abé" — 4 bytes per repetition
    }
    auto node = JsonNode::makeString("payload", big);

    PrettyPrintOptions opts;
    opts.maxOutputSize = 65536;
    auto result = emitTokens(*node, opts);

    EXPECT_TRUE(result.truncated);

    std::string concatenated;
    for (const auto& token : result.tokens) {
        concatenated += token.text;
    }

    // Hard budget: at most maxOutputSize bytes (allow a small epsilon in case
    // a truncation marker is ever appended).
    EXPECT_LE(concatenated.size(), 65536u + 16u);
    EXPECT_GT(concatenated.size(), 0u);

    // The truncation boundary must not split a UTF-8 sequence.
    EXPECT_TRUE(isValidUtf8(concatenated));
}

TEST(TokenEmitterBudget, TruncationNeverSplitsEscapeSequence) {
    // String full of characters that escape to multi-byte escape sequences
    // ("\uXXXX" and two-byte escapes); sweep budgets to hit every boundary.
    std::string value;
    for (int i = 0; i < 40; ++i) {
        value += '\x01';  // escapes to a 6-byte backslash-u sequence
        value += '\n';    // escapes to \n (2 bytes)
        value += 'x';
    }
    auto node = JsonNode::makeString("", value);

    for (std::size_t budget = 1; budget <= 64; ++budget) {
        PrettyPrintOptions opts;
        opts.maxOutputSize = budget;
        auto result = emitTokens(*node, opts);

        std::string concatenated;
        for (const auto& token : result.tokens) {
            concatenated += token.text;
        }
        ASSERT_LE(concatenated.size(), budget);

        // No trailing partial escape: count trailing backslashes — a lone
        // (odd) trailing backslash or a partial \uXXXX means a split escape.
        if (!concatenated.empty()) {
            // Walk from the start using escape-aware steps; every unit must
            // be complete.
            std::size_t i = 0;
            bool wellFormed = true;
            while (i < concatenated.size()) {
                if (concatenated[i] == '\\') {
                    if (i + 1 >= concatenated.size()) { wellFormed = false; break; }
                    std::size_t len = (concatenated[i + 1] == 'u') ? 6 : 2;
                    if (i + len > concatenated.size()) { wellFormed = false; break; }
                    i += len;
                } else {
                    ++i;
                }
            }
            ASSERT_TRUE(wellFormed) << "budget=" << budget
                                    << " output=" << concatenated;
        }
    }
}

// ---------------------------------------------------------------------------
// Node counting during parse
// ---------------------------------------------------------------------------

TEST(ParseNodeCount, MatchesHandCountedWalk) {
    // Nodes: root object (1), "a" (2), "b" array (3), true (4), null (5),
    // "x" (6), "c" object (7), "d" (8).
    const std::string json = R"({"a":1,"b":[true,null,"x"],"c":{"d":2}})";

    auto result = parseBuffer(std::string(json), ParseBufferOptions{});
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.nodeCount, 8u);
    EXPECT_EQ(result.nodeCount, countArenaNodes(result.root));
}

TEST(ParseNodeCount, PropertyMatchesTreeWalk) {
    rc::check("ArenaParseResult::nodeCount equals a full tree walk",
        []() {
            const auto json = *genRoundTrippableJson();
            auto result = parseBuffer(std::string(json), ParseBufferOptions{});
            RC_PRE(result.ok());
            RC_ASSERT(result.nodeCount == countArenaNodes(result.root));
        });
}
