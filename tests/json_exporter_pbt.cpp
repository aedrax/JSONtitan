// ---------------------------------------------------------------------------
// Property-Based Tests for JSON Exporter
// Feature: element-deletion
// ---------------------------------------------------------------------------
// Tests validate correctness properties of the exportJson function:
// - Serialization round-trip (Property 6)
// - String escaping correctness (Property 7)
// - Serialization determinism (Property 8)
// - Compact mode no extraneous whitespace (Property 9)
// - Pretty-print indentation correctness (Property 10)
//
// **Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5, 7.6**
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/json_exporter.h"
#include "core/json_node.h"
#include "core/parser.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

// Generate a safe string key (alphanumeric + underscore, non-empty)
rc::Gen<std::string> genSafeKey() {
    return rc::gen::exec([]() -> std::string {
        int len = *rc::gen::inRange(1, 10);
        std::string key;
        for (int i = 0; i < len; ++i) {
            int choice = *rc::gen::inRange(0, 36);
            if (choice < 26) {
                key += static_cast<char>('a' + choice);
            } else {
                key += static_cast<char>('0' + (choice - 26));
            }
        }
        return key;
    });
}

// Generate a safe string value (printable ASCII, no special chars)
rc::Gen<std::string> genSafeStringValue() {
    return rc::gen::exec([]() -> std::string {
        int len = *rc::gen::inRange(0, 20);
        std::string val;
        for (int i = 0; i < len; ++i) {
            // Printable ASCII excluding " and backslash for simplicity in round-trip
            int c = *rc::gen::inRange(0x20, 0x7E);
            if (c == '"' || c == '\\') {
                c = 'x'; // Replace to keep it simple for the basic generator
            }
            val += static_cast<char>(c);
        }
        return val;
    });
}

// Generate strings containing special characters (control chars, quotes, backslashes)
rc::Gen<std::string> genStringWithSpecialChars() {
    return rc::gen::exec([]() -> std::string {
        int len = *rc::gen::inRange(1, 15);
        std::string val;
        for (int i = 0; i < len; ++i) {
            int choice = *rc::gen::inRange(0, 5);
            switch (choice) {
                case 0:
                    // Control character U+0000 through U+001F
                    val += static_cast<char>(*rc::gen::inRange(0, 0x20));
                    break;
                case 1:
                    // Quotation mark
                    val += '"';
                    break;
                case 2:
                    // Backslash
                    val += '\\';
                    break;
                case 3:
                    // Normal printable ASCII
                    val += static_cast<char>(*rc::gen::inRange(0x20, 0x7F));
                    break;
                case 4:
                default:
                    // Mix of specific control chars
                    {
                        int ctrl = *rc::gen::inRange(0, 6);
                        switch (ctrl) {
                            case 0: val += '\n'; break;
                            case 1: val += '\t'; break;
                            case 2: val += '\r'; break;
                            case 3: val += '\b'; break;
                            case 4: val += '\f'; break;
                            default: val += static_cast<char>(*rc::gen::inRange(1, 0x20)); break;
                        }
                    }
                    break;
            }
        }
        return val;
    });
}

// Generate indent width in [1, 8]
rc::Gen<int> genIndentWidth() {
    return rc::gen::inRange(1, 9); // inRange is [low, high)
}

// Generate a random JsonNode tree with configurable max depth
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNode(int maxDepth = 3) {
    return rc::gen::exec([maxDepth]() -> std::shared_ptr<const JsonNode> {
        // At max depth, only generate scalars
        int choice = (maxDepth <= 0) ? *rc::gen::inRange(2, 6) : *rc::gen::inRange(0, 6);
        switch (choice) {
            case 0: {
                // Object with 0-5 children
                int childCount = *rc::gen::inRange(0, 6);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < childCount; ++i) {
                    auto child = *genJsonNode(maxDepth - 1);
                    std::string key = *genSafeKey();
                    // Re-create child with the generated key
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
                // Array with 0-5 elements
                int elemCount = *rc::gen::inRange(0, 6);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < elemCount; ++i) {
                    auto child = *genJsonNode(maxDepth - 1);
                    // Array elements have empty keys
                    children.push_back(child);
                }
                return JsonNode::makeArray("", std::move(children));
            }
            case 2: {
                // String value
                std::string val = *genSafeStringValue();
                return JsonNode::makeString("", val);
            }
            case 3: {
                // Number value
                int num = *rc::gen::inRange(-999, 1000);
                return JsonNode::makeNumber("", std::to_string(num));
            }
            case 4: {
                // Boolean
                bool b = *rc::gen::arbitrary<bool>();
                return JsonNode::makeBool("", b);
            }
            case 5:
            default: {
                // Null
                return JsonNode::makeNull("");
            }
        }
    });
}

// Generate a JsonNode tree that is guaranteed to be an Object or Array (root-level)
// Ensures array children have empty keys (matching parser behavior for round-trip)
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNodeTree() {
    return rc::gen::exec([]() -> std::shared_ptr<const JsonNode> {
        int choice = *rc::gen::inRange(0, 2);
        int childCount = *rc::gen::inRange(0, 6);
        std::vector<std::shared_ptr<const JsonNode>> children;
        for (int i = 0; i < childCount; ++i) {
            auto child = *genJsonNode(2);
            if (choice == 0) {
                // Object: children need keys
                std::string key = *genSafeKey();
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
            } else {
                // Array: children have empty keys (parser produces empty keys for array elements)
                children.push_back(child);
            }
        }
        if (choice == 0) {
            return JsonNode::makeObject("", std::move(children));
        } else {
            return JsonNode::makeArray("", std::move(children));
        }
    });
}

// Generate valid JsonExportOptions
rc::Gen<JsonExportOptions> genExportOptions() {
    return rc::gen::exec([]() -> JsonExportOptions {
        JsonExportOptions opts;
        int modeChoice = *rc::gen::inRange(0, 2);
        opts.mode = (modeChoice == 0) ? IndentMode::Compact : IndentMode::PrettyPrint;
        opts.indentWidth = *genIndentWidth();
        opts.trailingNewline = *rc::gen::arbitrary<bool>();
        return opts;
    });
}

// ---------------------------------------------------------------------------
// Helper: Compare two JsonNode trees for structural equivalence
// ---------------------------------------------------------------------------
bool structurallyEqual(const JsonNode& a, const JsonNode& b) {
    if (a.type != b.type) return false;
    if (a.key != b.key) return false;

    switch (a.type) {
        case NodeType::String:
        case NodeType::Number:
        case NodeType::Boolean:
            return a.value == b.value;
        case NodeType::Null:
            return true;
        case NodeType::Object:
        case NodeType::Array:
            if (a.children.size() != b.children.size()) return false;
            for (std::size_t i = 0; i < a.children.size(); ++i) {
                if (!structurallyEqual(*a.children[i], *b.children[i])) return false;
            }
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helper: Parse a JSON string using the project's parser
// ---------------------------------------------------------------------------
std::shared_ptr<const JsonNode> parseJsonString(const std::string& json) {
    auto state = makeParserState();
    std::span<const std::byte> chunk(
        reinterpret_cast<const std::byte*>(json.data()), json.size());
    auto chunkResult = parseChunk(*state, chunk);
    if (chunkResult.error) return nullptr;
    auto finalResult = finalizeParse(*chunkResult.nextState);
    if (finalResult.error) return nullptr;
    return finalResult.root;
}

} // anonymous namespace

// ===========================================================================
// Property 6: Serialization round-trip
//
// For any valid JsonNode tree, serializing it with exportJson and then parsing
// the resulting string with the existing parser produces a tree that is
// structurally equivalent to the original.
//
// **Validates: Requirements 7.1, 7.2**
// ===========================================================================

TEST(JsonExporterPBT, Property6_SerializationRoundTrip) {
    rc::check("Feature: element-deletion, Property 6: Serialization round-trip",
        []() {
            auto tree = *genJsonNodeTree();

            // Serialize with default pretty-print options (no trailing newline for clean parse)
            JsonExportOptions opts;
            opts.mode = IndentMode::PrettyPrint;
            opts.indentWidth = 2;
            opts.trailingNewline = false;

            std::string json = exportJson(*tree, opts);
            RC_ASSERT(!json.empty());

            // Parse the serialized output back
            auto parsed = parseJsonString(json);
            RC_ASSERT(parsed != nullptr);

            // Verify structural equivalence
            RC_ASSERT(structurallyEqual(*tree, *parsed));
        });
}

// ===========================================================================
// Property 7: String escaping correctness
//
// For any string value containing characters in U+0000–U+001F, or containing
// `"` or `\`, the exportJson output contains the properly escaped
// representation, and parsing the escaped output recovers the original string
// value.
//
// **Validates: Requirements 7.3**
// ===========================================================================

TEST(JsonExporterPBT, Property7_StringEscapingCorrectness) {
    rc::check("Feature: element-deletion, Property 7: String escaping correctness",
        []() {
            std::string specialStr = *genStringWithSpecialChars();

            // Create a simple object containing the special string
            auto node = JsonNode::makeObject("", {
                JsonNode::makeString("val", specialStr)
            });

            // Serialize
            JsonExportOptions opts;
            opts.mode = IndentMode::Compact;
            opts.trailingNewline = false;

            std::string json = exportJson(*node, opts);

            // The output should be valid JSON - parse it back
            auto parsed = parseJsonString(json);
            RC_ASSERT(parsed != nullptr);
            RC_ASSERT(parsed->type == NodeType::Object);
            RC_ASSERT(parsed->children.size() == 1);
            RC_ASSERT(parsed->children[0]->type == NodeType::String);

            // The recovered string value must match the original
            RC_ASSERT(parsed->children[0]->value == specialStr);
        });
}

// ===========================================================================
// Property 8: Serialization determinism
//
// For any JsonNode tree and for any valid JsonExportOptions, calling
// exportJson(tree, options) twice with the same arguments produces
// byte-identical output.
//
// **Validates: Requirements 7.4**
// ===========================================================================

TEST(JsonExporterPBT, Property8_SerializationDeterminism) {
    rc::check("Feature: element-deletion, Property 8: Serialization determinism",
        []() {
            auto tree = *genJsonNodeTree();
            auto opts = *genExportOptions();

            std::string output1 = exportJson(*tree, opts);
            std::string output2 = exportJson(*tree, opts);

            RC_ASSERT(output1 == output2);
        });
}

// ===========================================================================
// Property 9: Compact mode produces no extraneous whitespace
//
// For any JsonNode tree, serializing with IndentMode::Compact produces output
// where no whitespace characters (space, tab, newline, carriage return) appear
// outside of JSON string value literals.
//
// **Validates: Requirements 7.5**
// ===========================================================================

TEST(JsonExporterPBT, Property9_CompactModeNoExtraneousWhitespace) {
    rc::check("Feature: element-deletion, Property 9: Compact mode no extraneous whitespace",
        []() {
            auto tree = *genJsonNodeTree();

            JsonExportOptions opts;
            opts.mode = IndentMode::Compact;
            opts.trailingNewline = false;

            std::string json = exportJson(*tree, opts);

            // Scan through the output checking for whitespace outside string literals
            bool inString = false;
            for (std::size_t i = 0; i < json.size(); ++i) {
                char c = json[i];
                if (inString) {
                    if (c == '\\' && i + 1 < json.size()) {
                        // Skip escaped character
                        ++i;
                        continue;
                    }
                    if (c == '"') {
                        inString = false;
                    }
                    // Inside a string, whitespace is allowed
                } else {
                    if (c == '"') {
                        inString = true;
                    } else {
                        // Outside a string, no whitespace should appear
                        RC_ASSERT(c != ' ');
                        RC_ASSERT(c != '\t');
                        RC_ASSERT(c != '\n');
                        RC_ASSERT(c != '\r');
                    }
                }
            }
        });
}

// ===========================================================================
// Property 10: Pretty-print indentation correctness
//
// For any JsonNode tree and for any indent width w in [1, 8], serializing with
// IndentMode::PrettyPrint and width w produces output where each line
// containing a nested element is indented by exactly depth * w spaces.
//
// **Validates: Requirements 7.6**
// ===========================================================================

TEST(JsonExporterPBT, Property10_PrettyPrintIndentationCorrectness) {
    rc::check("Feature: element-deletion, Property 10: Pretty-print indentation correctness",
        []() {
            auto tree = *genJsonNodeTree();
            int indentWidth = *genIndentWidth();

            JsonExportOptions opts;
            opts.mode = IndentMode::PrettyPrint;
            opts.indentWidth = indentWidth;
            opts.trailingNewline = false;

            std::string json = exportJson(*tree, opts);

            // Verify indentation using depth tracking:
            // - If a line's first non-space char is } or ]: decrement depth
            // - Verify leading spaces == depth * indentWidth
            // - If the line's last char is { or [: increment depth
            std::istringstream stream(json);
            std::string line;
            int depth = 0;

            while (std::getline(stream, line)) {
                if (line.empty()) continue;

                // Count leading spaces
                std::size_t leadingSpaces = 0;
                while (leadingSpaces < line.size() && line[leadingSpaces] == ' ') {
                    ++leadingSpaces;
                }

                // Skip all-whitespace lines
                if (leadingSpaces >= line.size()) continue;

                char firstChar = line[leadingSpaces];
                char lastChar = line.back();

                // Closing brace/bracket: depth decreases before checking this line
                if (firstChar == '}' || firstChar == ']') {
                    --depth;
                    RC_ASSERT(depth >= 0);
                }

                // Verify indentation matches expected depth
                std::size_t expectedIndent = static_cast<std::size_t>(depth * indentWidth);
                RC_ASSERT(leadingSpaces == expectedIndent);

                // Opening brace/bracket at end of line: depth increases for next line
                if (lastChar == '{' || lastChar == '[') {
                    ++depth;
                }
            }

            // After all lines, depth should return to 0
            RC_ASSERT(depth == 0);
        });
}
