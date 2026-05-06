// ---------------------------------------------------------------------------
// Preservation Property-Based Tests — Parser Performance Optimization
// Property 2: Tree Display and Search Functional Equivalence
//
// These tests verify that ArenaJsonNode and its corresponding JsonNode
// (produced by toJsonNode()) contain identical data at every node in the tree.
// They MUST PASS on unfixed code — they establish the baseline behavior to preserve.
//
// **Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5**
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "core/source_buffer.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Helper: Generate a display string for a JsonNode (replicates TreeModel logic
// without Qt dependency). This mirrors TreeModel::formatNodeDisplay exactly.
// ---------------------------------------------------------------------------
std::string formatNodeDisplayStr(const JsonNode& node, int arrayIndex = -1) {
    std::string display;

    // Key or array index
    if (arrayIndex >= 0) {
        display = "[" + std::to_string(arrayIndex) + "]";
    } else if (!node.key.empty()) {
        display = node.key;
    }

    // Type indicator and value preview
    switch (node.type) {
    case NodeType::Object:
        if (!display.empty())
            display += " ";
        display += "{Object}";
        if (!node.children.empty()) {
            display += " (" + std::to_string(node.children.size()) + " items)";
        }
        break;

    case NodeType::Array:
        if (!display.empty())
            display += " ";
        display += "[Array]";
        if (!node.children.empty()) {
            display += " (" + std::to_string(node.children.size()) + " items)";
        }
        break;

    case NodeType::String:
        if (!display.empty())
            display += ": ";
        display += "\"";
        {
            const std::string& val = node.value;
            if (val.length() > 50) {
                display += val.substr(0, 50) + "...";
            } else {
                display += val;
            }
        }
        display += "\"";
        break;

    case NodeType::Number:
        if (!display.empty())
            display += ": ";
        display += node.value;
        break;

    case NodeType::Boolean:
        if (!display.empty())
            display += ": ";
        display += node.value;
        break;

    case NodeType::Null:
        if (!display.empty())
            display += ": ";
        display += "null";
        break;
    }

    return display;
}

// ---------------------------------------------------------------------------
// Helper: Generate a display string for an ArenaJsonNode using the same logic.
// This is what the fixed code would produce if formatNodeDisplay worked on
// ArenaJsonNode directly.
// ---------------------------------------------------------------------------
std::string formatArenaNodeDisplayStr(const ArenaJsonNode& node, int arrayIndex = -1) {
    std::string display;

    // Key or array index
    if (arrayIndex >= 0) {
        display = "[" + std::to_string(arrayIndex) + "]";
    } else if (node.key.length > 0) {
        display = node.key.toString();
    }

    // Type indicator and value preview
    switch (node.type) {
    case NodeType::Object:
        if (!display.empty())
            display += " ";
        display += "{Object}";
        if (node.childCount > 0) {
            display += " (" + std::to_string(node.childCount) + " items)";
        }
        break;

    case NodeType::Array:
        if (!display.empty())
            display += " ";
        display += "[Array]";
        if (node.childCount > 0) {
            display += " (" + std::to_string(node.childCount) + " items)";
        }
        break;

    case NodeType::String:
        if (!display.empty())
            display += ": ";
        display += "\"";
        {
            std::string val = node.value.toString();
            if (val.length() > 50) {
                display += val.substr(0, 50) + "...";
            } else {
                display += val;
            }
        }
        display += "\"";
        break;

    case NodeType::Number:
        if (!display.empty())
            display += ": ";
        display += node.value.toString();
        break;

    case NodeType::Boolean:
        if (!display.empty())
            display += ": ";
        display += node.value.toString();
        break;

    case NodeType::Null:
        if (!display.empty())
            display += ": ";
        display += "null";
        break;
    }

    return display;
}

// ---------------------------------------------------------------------------
// Helper: Recursively compare display strings between ArenaJsonNode and JsonNode
// Returns true if all nodes match, false otherwise.
// ---------------------------------------------------------------------------
bool compareDisplayStrings(const ArenaJsonNode& arenaNode,
                           const JsonNode& jsonNode,
                           int arrayIndex = -1) {
    std::string arenaDisplay = formatArenaNodeDisplayStr(arenaNode, arrayIndex);
    std::string jsonDisplay = formatNodeDisplayStr(jsonNode, arrayIndex);

    if (arenaDisplay != jsonDisplay) {
        return false;
    }

    // Recurse into children
    if (arenaNode.childCount != jsonNode.children.size()) {
        return false;
    }

    for (std::size_t i = 0; i < arenaNode.childCount; ++i) {
        // Determine array index for children of arrays
        int childArrayIdx = (arenaNode.type == NodeType::Array)
                                ? static_cast<int>(i)
                                : -1;
        if (!compareDisplayStrings(*arenaNode.children[i],
                                   *jsonNode.children[i],
                                   childArrayIdx)) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helper: Recursively compare child counts between ArenaJsonNode and JsonNode
// Returns true if all levels match, false otherwise.
// ---------------------------------------------------------------------------
bool compareChildCounts(const ArenaJsonNode& arenaNode,
                        const JsonNode& jsonNode) {
    if (arenaNode.childCount != jsonNode.children.size()) {
        return false;
    }

    for (std::size_t i = 0; i < arenaNode.childCount; ++i) {
        if (!compareChildCounts(*arenaNode.children[i], *jsonNode.children[i])) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helper: Recursively compare node types between ArenaJsonNode and JsonNode
// Returns true if all positions match, false otherwise.
// ---------------------------------------------------------------------------
bool compareNodeTypes(const ArenaJsonNode& arenaNode,
                      const JsonNode& jsonNode) {
    if (arenaNode.type != jsonNode.type) {
        return false;
    }

    // Also verify child count matches so we can recurse safely
    if (arenaNode.childCount != jsonNode.children.size()) {
        return false;
    }

    for (std::size_t i = 0; i < arenaNode.childCount; ++i) {
        if (!compareNodeTypes(*arenaNode.children[i], *jsonNode.children[i])) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helper: Count nodes in an ArenaJsonNode tree
// ---------------------------------------------------------------------------
std::size_t countArenaNodes(const ArenaJsonNode* node) {
    if (!node) return 0;
    std::size_t count = 1;
    for (std::size_t i = 0; i < node->childCount; ++i) {
        count += countArenaNodes(node->children[i]);
    }
    return count;
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: Simple JSON values
// ---------------------------------------------------------------------------
std::string genJsonValue() {
    auto choice = *rc::gen::inRange(0, 5);
    switch (choice) {
    case 0: {
        // String value
        auto len = *rc::gen::inRange(0, 20);
        std::string s = "\"";
        for (int i = 0; i < len; ++i) {
            auto c = *rc::gen::inRange<char>('a', 'z' + 1);
            s += c;
        }
        s += "\"";
        return s;
    }
    case 1: {
        // Integer
        auto val = *rc::gen::inRange(-9999, 10000);
        return std::to_string(val);
    }
    case 2: {
        // Float
        auto val = *rc::gen::inRange(-999, 1000);
        return std::to_string(val) + "." + std::to_string(*rc::gen::inRange(0, 100));
    }
    case 3:
        return "true";
    case 4:
        return "false";
    default:
        return "null";
    }
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: Random JSON key (alphanumeric, 1-10 chars)
// ---------------------------------------------------------------------------
std::string genJsonKey() {
    auto len = *rc::gen::inRange(1, 8);
    std::string s;
    for (int i = 0; i < len; ++i) {
        s += *rc::gen::inRange<char>('a', 'z' + 1);
    }
    return s;
}

// ---------------------------------------------------------------------------
// RapidCheck Generator: Random JSON structure (recursive)
// Budget limits total node count to prevent huge inputs.
// ---------------------------------------------------------------------------
std::string genJsonImpl(int maxDepth, int& budget) {
    if (budget <= 0 || maxDepth <= 0) {
        return genJsonValue();
    }

    auto choice = *rc::gen::inRange(0, 3);

    if (choice == 0 && maxDepth > 1) {
        // Object
        auto numKeys = *rc::gen::inRange(1, 6);
        numKeys = std::min(numKeys, budget);
        budget -= numKeys;
        std::string json = "{";
        for (int i = 0; i < numKeys; ++i) {
            if (i > 0) json += ",";
            json += "\"" + genJsonKey() + "\":";
            json += genJsonImpl(maxDepth - 1, budget);
        }
        json += "}";
        return json;
    } else if (choice == 1 && maxDepth > 1) {
        // Array
        auto numElems = *rc::gen::inRange(1, 6);
        numElems = std::min(numElems, budget);
        budget -= numElems;
        std::string json = "[";
        for (int i = 0; i < numElems; ++i) {
            if (i > 0) json += ",";
            json += genJsonImpl(maxDepth - 1, budget);
        }
        json += "]";
        return json;
    } else {
        // Leaf value
        return genJsonValue();
    }
}

rc::Gen<std::string> genRandomJson() {
    return rc::gen::exec([]() -> std::string {
        int budget = *rc::gen::inRange(5, 50);
        int maxDepth = *rc::gen::inRange(2, 5);
        return genJsonImpl(maxDepth, budget);
    });
}

// Generator for larger JSON structures (for more thorough testing)
rc::Gen<std::string> genLargerJson() {
    return rc::gen::exec([]() -> std::string {
        int budget = *rc::gen::inRange(20, 200);
        int maxDepth = *rc::gen::inRange(3, 6);
        return genJsonImpl(maxDepth, budget);
    });
}

} // anonymous namespace

// ===========================================================================
// Property 2a: formatNodeDisplay equivalence
//
// For all valid JSON inputs, ArenaJsonNode and its corresponding JsonNode
// (from toJsonNode()) produce identical formatNodeDisplay() strings for
// every node in the tree.
//
// **Validates: Requirements 3.1, 3.2**
// ===========================================================================

TEST(ParserPerfPreservation, Property2a_FormatNodeDisplayEquivalence) {
    rc::check("Property 2a: ArenaJsonNode and JsonNode produce identical display strings",
        []() {
            const auto json = *genRandomJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare display strings at every node in the tree
            RC_ASSERT(compareDisplayStrings(*arenaResult.root, *jsonNode));
        });
}

TEST(ParserPerfPreservation, Property2a_FormatNodeDisplayEquivalence_LargerInputs) {
    rc::check("Property 2a (larger): ArenaJsonNode and JsonNode produce identical display strings",
        []() {
            const auto json = *genLargerJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare display strings at every node in the tree
            RC_ASSERT(compareDisplayStrings(*arenaResult.root, *jsonNode));
        });
}

// ===========================================================================
// Property 2b: Child count equivalence
//
// For all valid JSON inputs, the child count at every level matches between
// ArenaJsonNode::childCount and JsonNode::children.size().
//
// **Validates: Requirements 3.3, 3.4**
// ===========================================================================

TEST(ParserPerfPreservation, Property2b_ChildCountEquivalence) {
    rc::check("Property 2b: Child counts match at every level between ArenaJsonNode and JsonNode",
        []() {
            const auto json = *genRandomJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare child counts at every level
            RC_ASSERT(compareChildCounts(*arenaResult.root, *jsonNode));
        });
}

TEST(ParserPerfPreservation, Property2b_ChildCountEquivalence_LargerInputs) {
    rc::check("Property 2b (larger): Child counts match at every level",
        []() {
            const auto json = *genLargerJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare child counts at every level
            RC_ASSERT(compareChildCounts(*arenaResult.root, *jsonNode));
        });
}

// ===========================================================================
// Property 2c: Node type equivalence
//
// For all valid JSON inputs, node types match at every position in the tree
// between ArenaJsonNode and JsonNode representations.
//
// **Validates: Requirements 3.4, 3.5**
// ===========================================================================

TEST(ParserPerfPreservation, Property2c_NodeTypeEquivalence) {
    rc::check("Property 2c: Node types match at every position between ArenaJsonNode and JsonNode",
        []() {
            const auto json = *genRandomJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare node types at every position
            RC_ASSERT(compareNodeTypes(*arenaResult.root, *jsonNode));
        });
}

TEST(ParserPerfPreservation, Property2c_NodeTypeEquivalence_LargerInputs) {
    rc::check("Property 2c (larger): Node types match at every position",
        []() {
            const auto json = *genLargerJson();

            // Parse via simdjson backend
            auto arenaResult = parseBuffer(std::string(json),
                ParseBufferOptions{.backend = ParserBackend::Simdjson});

            RC_PRE(arenaResult.ok());
            RC_PRE(arenaResult.root != nullptr);

            // Convert to JsonNode via toJsonNode()
            auto jsonNode = arenaResult.root->toJsonNode();
            RC_PRE(jsonNode != nullptr);

            // Compare node types at every position
            RC_ASSERT(compareNodeTypes(*arenaResult.root, *jsonNode));
        });
}
