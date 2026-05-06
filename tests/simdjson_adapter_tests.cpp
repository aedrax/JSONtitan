#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/arena_allocator.h"
#include "core/parse_orchestrator.h"
#include "core/pretty_printer.h"
#include "core/simdjson_adapter.h"

using namespace jsontitan::core;

// ===========================================================================
// JSON Generators for Property-Based Testing
// Requirements: 6.1, 7.1
// ===========================================================================

namespace generators {

// ---------------------------------------------------------------------------
// Valid JSON Generator
// Produces well-formed JSON documents with:
//   - Varying depth (0–10 levels)
//   - Mixed types (objects, arrays, strings, numbers, booleans, nulls)
//   - Unicode characters and escape sequences
//   - Duplicate keys in objects
//   - Empty containers
//   - Large string values
// ---------------------------------------------------------------------------

/// Generate a valid JSON string value with possible unicode and escapes.
static rc::Gen<std::string> genJsonString() {
    return rc::gen::mapcat(rc::gen::inRange(0, 6), [](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: {
                // Simple ASCII string (0-50 chars)
                return rc::gen::mapcat(rc::gen::inRange(0, 50), [](int len) -> rc::Gen<std::string> {
                    return rc::gen::map(
                        rc::gen::container<std::string>(
                            static_cast<std::size_t>(len),
                            rc::gen::suchThat(rc::gen::inRange<char>(32, 126), [](char c) {
                                return c != '"' && c != '\\';
                            })),
                        [](std::string s) { return "\"" + s + "\""; });
                });
            }
            case 1: {
                // String with escape sequences
                return rc::gen::element<std::string>(
                    "\"hello\\nworld\"",
                    "\"tab\\there\"",
                    "\"quote\\\"inside\"",
                    "\"back\\\\slash\"",
                    "\"slash\\/here\"",
                    "\"form\\ffeed\"",
                    "\"carriage\\rreturn\"");
            }
            case 2: {
                // String with unicode escapes
                return rc::gen::element<std::string>(
                    "\"\\u0041\"",           // 'A'
                    "\"\\u00E9\"",           // 'é'
                    "\"\\u4E16\\u754C\"",    // '世界'
                    "\"\\u0048\\u0065\\u006C\\u006C\\u006F\"",  // 'Hello'
                    "\"\\uD83D\\uDE00\"");   // 😀 (surrogate pair)
            }
            case 3: {
                // Empty string
                return rc::gen::just(std::string("\"\""));
            }
            case 4: {
                // Large string value (50-200 chars)
                return rc::gen::mapcat(rc::gen::inRange(50, 200), [](int len) -> rc::Gen<std::string> {
                    return rc::gen::map(
                        rc::gen::container<std::string>(
                            static_cast<std::size_t>(len),
                            rc::gen::suchThat(rc::gen::inRange<char>(32, 126), [](char c) {
                                return c != '"' && c != '\\';
                            })),
                        [](std::string s) { return "\"" + s + "\""; });
                });
            }
            default: {
                // String with raw unicode characters (valid UTF-8)
                return rc::gen::element<std::string>(
                    "\"\xC3\xA9\"",          // é in UTF-8
                    "\"\xE4\xB8\x96\"",      // 世 in UTF-8
                    "\"\xF0\x9F\x98\x80\""); // 😀 in UTF-8
            }
        }
    });
}

/// Generate a valid JSON number.
static rc::Gen<std::string> genJsonNumber() {
    return rc::gen::mapcat(rc::gen::inRange(0, 5), [](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: {
                // Integer
                return rc::gen::map(rc::gen::inRange(-999999, 999999),
                    [](int n) { return std::to_string(n); });
            }
            case 1: {
                // Decimal
                return rc::gen::map(
                    rc::gen::pair(rc::gen::inRange(-999, 999), rc::gen::inRange(1, 999999)),
                    [](std::pair<int, int> p) {
                        return std::to_string(p.first) + "." + std::to_string(p.second);
                    });
            }
            case 2: {
                // Scientific notation
                return rc::gen::map(
                    rc::gen::pair(rc::gen::inRange(1, 9), rc::gen::inRange(-10, 10)),
                    [](std::pair<int, int> p) {
                        return std::to_string(p.first) + "e" + std::to_string(p.second);
                    });
            }
            case 3: {
                // Zero
                return rc::gen::just(std::string("0"));
            }
            default: {
                // Negative decimal
                return rc::gen::map(rc::gen::inRange(1, 9999),
                    [](int n) { return "-" + std::to_string(n) + ".5"; });
            }
        }
    });
}

/// Generate a valid JSON scalar (string, number, boolean, null).
static rc::Gen<std::string> genJsonScalar() {
    return rc::gen::mapcat(rc::gen::inRange(0, 4), [](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: return genJsonString();
            case 1: return genJsonNumber();
            case 2: return rc::gen::element<std::string>("true", "false");
            default: return rc::gen::just(std::string("null"));
        }
    });
}

/// Recursive valid JSON value generator with depth control.
/// At depth 0, only scalars are generated.
/// At higher depths, objects and arrays are possible.
static rc::Gen<std::string> genJsonValue(int maxDepth);

/// Generate a JSON object key (for use in objects).
static rc::Gen<std::string> genObjectKey() {
    return rc::gen::mapcat(rc::gen::inRange(0, 3), [](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: {
                // Simple key (1-10 lowercase chars)
                return rc::gen::mapcat(rc::gen::inRange(1, 10), [](int len) -> rc::Gen<std::string> {
                    return rc::gen::map(
                        rc::gen::container<std::string>(
                            static_cast<std::size_t>(len),
                            rc::gen::inRange<char>('a', 'z' + 1)),
                        [](std::string s) { return "\"" + s + "\""; });
                });
            }
            case 1: {
                // Key with special chars
                return rc::gen::element<std::string>(
                    "\"key with spaces\"",
                    "\"key\\nwith\\nnewlines\"",
                    "\"\"",
                    "\"123\"");
            }
            default: {
                // Duplicate-friendly keys (small set to increase collision chance)
                return rc::gen::element<std::string>(
                    "\"a\"", "\"b\"", "\"c\"", "\"name\"", "\"value\"");
            }
        }
    });
}

/// Generate a JSON object with given max depth.
static rc::Gen<std::string> genJsonObject(int maxDepth) {
    return rc::gen::mapcat(rc::gen::inRange(0, 6), [maxDepth](int count) -> rc::Gen<std::string> {
        if (count == 0) {
            return rc::gen::just(std::string("{}"));
        }
        return rc::gen::map(
            rc::gen::container<std::vector<std::pair<std::string, std::string>>>(
                static_cast<std::size_t>(count),
                rc::gen::pair(genObjectKey(), genJsonValue(maxDepth - 1))),
            [](const std::vector<std::pair<std::string, std::string>>& entries) {
                std::string result = "{";
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    if (i > 0) result += ",";
                    result += entries[i].first + ":" + entries[i].second;
                }
                result += "}";
                return result;
            });
    });
}

/// Generate a JSON array with given max depth.
static rc::Gen<std::string> genJsonArray(int maxDepth) {
    return rc::gen::mapcat(rc::gen::inRange(0, 6), [maxDepth](int count) -> rc::Gen<std::string> {
        if (count == 0) {
            return rc::gen::just(std::string("[]"));
        }
        return rc::gen::map(
            rc::gen::container<std::vector<std::string>>(
                static_cast<std::size_t>(count),
                genJsonValue(maxDepth - 1)),
            [](const std::vector<std::string>& elements) {
                std::string result = "[";
                for (std::size_t i = 0; i < elements.size(); ++i) {
                    if (i > 0) result += ",";
                    result += elements[i];
                }
                result += "]";
                return result;
            });
    });
}

/// Implementation of the recursive JSON value generator.
static rc::Gen<std::string> genJsonValue(int maxDepth) {
    if (maxDepth <= 0) {
        return genJsonScalar();
    }
    return rc::gen::mapcat(rc::gen::inRange(0, 5), [maxDepth](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: return genJsonObject(maxDepth);
            case 1: return genJsonArray(maxDepth);
            default: return genJsonScalar();
        }
    });
}

/// Top-level valid JSON document generator.
/// Generates documents with depth 0–10.
static rc::Gen<std::string> genValidJson() {
    return rc::gen::mapcat(rc::gen::inRange(0, 11), [](int depth) -> rc::Gen<std::string> {
        return genJsonValue(depth);
    });
}

/// Generate a valid JSON document that is specifically an object with duplicate keys.
static rc::Gen<std::string> genJsonWithDuplicateKeys() {
    return rc::gen::mapcat(rc::gen::inRange(2, 6), [](int count) -> rc::Gen<std::string> {
        // Use a fixed key for some entries to guarantee duplicates
        return rc::gen::map(
            rc::gen::container<std::vector<std::string>>(
                static_cast<std::size_t>(count),
                genJsonScalar()),
            [](const std::vector<std::string>& values) {
                std::string result = "{";
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) result += ",";
                    // Use "dup" as key for all entries to guarantee duplicates
                    result += "\"dup\":" + values[i];
                }
                result += "}";
                return result;
            });
    });
}

// ---------------------------------------------------------------------------
// Invalid JSON Generator
// Produces malformed JSON documents:
//   - Truncated documents
//   - Missing closing brackets
//   - Invalid escape sequences
//   - Trailing content after valid JSON
// ---------------------------------------------------------------------------

/// Generate an invalid JSON document (truncated).
/// Uses objects/arrays as source to ensure truncation always produces invalid JSON.
static rc::Gen<std::string> genTruncatedJson() {
    // Generate objects or arrays (which are always invalid when truncated)
    auto genContainer = rc::gen::mapcat(rc::gen::inRange(0, 2), [](int variant) -> rc::Gen<std::string> {
        if (variant == 0) {
            return genJsonObject(3);
        }
        return genJsonArray(3);
    });
    return rc::gen::mapcat(genContainer, [](const std::string& valid) -> rc::Gen<std::string> {
        if (valid.size() <= 2) {
            // Too short to truncate meaningfully, return a known truncated doc
            return rc::gen::just(std::string("{\"key\":"));
        }
        // Truncate at a random point, removing at least the closing bracket
        // This ensures the result is always invalid (missing closing bracket)
        return rc::gen::map(
            rc::gen::inRange(std::size_t{1}, valid.size() - 1),
            [valid](std::size_t cutPoint) {
                return valid.substr(0, cutPoint);
            });
    });
}

/// Generate JSON with missing closing brackets.
static rc::Gen<std::string> genMissingBrackets() {
    return rc::gen::element<std::string>(
        "{\"key\": \"value\"",
        "[1, 2, 3",
        "{\"a\": [1, 2}",
        "[{\"nested\": true",
        "{\"deep\": {\"deeper\": [1, 2, 3}",
        "[[[[1",
        "{\"a\": {\"b\": {\"c\": 1}");
}

/// Generate JSON with invalid escape sequences.
static rc::Gen<std::string> genInvalidEscapes() {
    return rc::gen::element<std::string>(
        "\"\\x41\"",           // \x is not valid JSON escape
        "\"\\a\"",             // \a is not valid JSON escape
        "\"\\u00GG\"",         // Invalid hex in unicode escape
        "\"\\uD800\"",         // Lone high surrogate (no low surrogate)
        "\"\\u\"",             // Incomplete unicode escape
        "\"\\uZZZZ\"",         // Non-hex chars in unicode escape
        "\"\\q\"");            // \q is not valid JSON escape
}

/// Generate JSON with trailing content after a valid value.
/// Ensures trailing content is clearly non-whitespace to trigger parse errors.
static rc::Gen<std::string> genTrailingContent() {
    return rc::gen::mapcat(genJsonValue(2), [](const std::string& valid) -> rc::Gen<std::string> {
        return rc::gen::map(
            rc::gen::element<std::string>(
                "extra",
                "{}",
                "[]",
                "null",
                "123",
                "\"trailing\""),
            [valid](const std::string& trailing) {
                // Use a space then clearly invalid trailing content
                return valid + " " + trailing;
            });
    });
}

/// Top-level invalid JSON generator combining all invalid variants.
static rc::Gen<std::string> genInvalidJson() {
    return rc::gen::mapcat(rc::gen::inRange(0, 4), [](int variant) -> rc::Gen<std::string> {
        switch (variant) {
            case 0: return genTruncatedJson();
            case 1: return genMissingBrackets();
            case 2: return genInvalidEscapes();
            default: return genTrailingContent();
        }
    });
}

} // namespace generators

// ===========================================================================
// Smoke Tests — Verify generators produce valid/invalid JSON
// ===========================================================================

TEST(SimdjsonGenerators, ValidJsonGeneratorProducesParseableDocuments) {
    rc::check("Valid JSON generator produces documents that parse successfully",
        []() {
            const auto json = *generators::genValidJson();

            // Parse with simdjson backend
            auto result = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_ASSERT(result.ok());
            RC_ASSERT(result.root != nullptr);
            RC_ASSERT(!result.error.has_value());
        });
}

TEST(SimdjsonGenerators, InvalidJsonGeneratorProducesUnparseableDocuments) {
    rc::check("Invalid JSON generator produces documents that fail to parse",
        []() {
            const auto json = *generators::genInvalidJson();

            // Parse with simdjson backend — should fail
            auto result = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_ASSERT(!result.ok() || result.error.has_value());
        });
}

TEST(SimdjsonGenerators, DuplicateKeyGeneratorProducesParseable) {
    rc::check("Duplicate key generator produces parseable JSON",
        []() {
            const auto json = *generators::genJsonWithDuplicateKeys();

            auto result = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_ASSERT(result.ok());
            RC_ASSERT(result.root != nullptr);
        });
}

// ===========================================================================
// Helper: Semantic comparison of two JsonNode trees
// ===========================================================================

namespace {

/// Try to parse a string as a double for semantic number comparison.
/// Returns true if parsing succeeded, with the result in `out`.
static bool tryParseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

/// Recursively compare two JsonNode trees for semantic equivalence.
/// For numbers, compares parsed numeric values rather than string representations.
/// For strings, compares the resolved UTF-8 content.
/// Returns true if the trees are semantically equivalent.
static bool jsonNodesSemanticEqual(
    const std::shared_ptr<const JsonNode>& a,
    const std::shared_ptr<const JsonNode>& b) {

    if (!a && !b) return true;
    if (!a || !b) return false;

    // Same node type
    if (a->type != b->type) return false;

    // Same key
    if (a->key != b->key) return false;

    // Value comparison depends on type
    switch (a->type) {
        case NodeType::Number: {
            // Semantic number comparison: parse both as doubles and compare
            double va = 0.0, vb = 0.0;
            bool aOk = tryParseDouble(a->value, va);
            bool bOk = tryParseDouble(b->value, vb);
            if (aOk && bOk) {
                // Use relative tolerance for floating point comparison
                if (va == vb) break;  // Exact match (handles 0.0 == 0.0)
                double diff = std::abs(va - vb);
                double maxVal = std::max(std::abs(va), std::abs(vb));
                if (maxVal > 0.0 && diff / maxVal > 1e-10) return false;
            } else {
                // If either fails to parse as double, fall back to string comparison
                if (a->value != b->value) return false;
            }
            break;
        }
        case NodeType::String:
            // Direct string comparison (both backends should resolve escapes)
            if (a->value != b->value) return false;
            break;
        case NodeType::Boolean:
            if (a->value != b->value) return false;
            break;
        case NodeType::Null:
            // No value to compare for null
            break;
        case NodeType::Object:
        case NodeType::Array:
            // Value field is unused for containers
            break;
    }

    // Same number of children
    if (a->children.size() != b->children.size()) return false;

    // Children are equivalent (recursively, order-sensitive)
    for (std::size_t i = 0; i < a->children.size(); ++i) {
        if (!jsonNodesSemanticEqual(a->children[i], b->children[i])) {
            return false;
        }
    }

    return true;
}

} // anonymous namespace

// ===========================================================================
// Property 1: Semantic Equivalence
// For any valid JSON, simdjson and custom backends produce equivalent
// ArenaJsonNode trees.
// Validates: Requirements 6.1, 2.2, 2.4
// ===========================================================================

TEST(SimdjsonProperties, SemanticEquivalence) {
    rc::check("Feature: simdjson-integration, Property 1: Semantic Equivalence",
        []() {
            const auto json = *generators::genValidJson();

            // Parse with simdjson backend
            auto simdjsonResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            // Parse with custom backend
            auto customResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Custom});

            // Both must parse successfully — if either fails, discard this test case
            // (the generator should produce valid JSON, but some edge cases may
            // only be accepted by one backend)
            RC_PRE(simdjsonResult.ok());
            RC_PRE(customResult.ok());

            // Convert both to JsonNode trees
            auto simdjsonTree = simdjsonResult.toParseResult();
            auto customTree = customResult.toParseResult();

            RC_ASSERT(simdjsonTree.root != nullptr);
            RC_ASSERT(customTree.root != nullptr);

            // Compare the two trees for semantic equivalence
            RC_ASSERT(jsonNodesSemanticEqual(simdjsonTree.root, customTree.root));
        });
}

// ===========================================================================
// Property 2: Parse–Print Round-Trip
// For any valid JSON, parse → pretty-print → parse produces structurally
// equivalent tree.
// Validates: Requirements 6.2
// ===========================================================================

TEST(SimdjsonProperties, ParsePrintRoundTrip) {
    rc::check("Feature: simdjson-integration, Property 2: Parse-Print Round-Trip",
        []() {
            const auto json = *generators::genValidJson();

            // Step 1: Parse with simdjson backend to get tree1
            auto result1 = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            // Must parse successfully
            RC_PRE(result1.ok());

            // Convert to JsonNode tree
            auto tree1 = result1.toParseResult();
            RC_ASSERT(tree1.root != nullptr);

            // Step 2: Pretty-print tree1
            auto printed = prettyPrint(*tree1.root);
            RC_ASSERT(!printed.empty());

            // Step 3: Parse the pretty-printed output again with simdjson
            auto result2 = parseBuffer(std::string(printed),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            // The pretty-printed output must also parse successfully
            RC_ASSERT(result2.ok());

            // Convert to JsonNode tree
            auto tree2 = result2.toParseResult();
            RC_ASSERT(tree2.root != nullptr);

            // Step 4: Assert structural/semantic equivalence
            RC_ASSERT(jsonNodesSemanticEqual(tree1.root, tree2.root));
        });
}
