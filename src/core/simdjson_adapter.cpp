#include "core/simdjson_adapter.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <simdjson.h>

namespace jsontitan::core {

namespace {

// Map a simdjson error code to a human-readable ParseError.
// Byte offset is included where simdjson provides location information.
auto mapSimdjsonError(simdjson::error_code error) -> ParseError {
    switch (error) {
        case simdjson::EMPTY:
            return ParseError{0, "Empty input"};
        case simdjson::UNCLOSED_STRING:
            return ParseError{0, "Unterminated string"};
        case simdjson::TAPE_ERROR:
            return ParseError{0, "Structural error at byte 0"};
        case simdjson::DEPTH_ERROR:
            return ParseError{0, "Document exceeds maximum nesting depth"};
        case simdjson::CAPACITY:
            return ParseError{0, "Document exceeds maximum size (4 GB)"};
        case simdjson::MEMALLOC:
            return ParseError{0, "Memory allocation failed during parsing"};
        case simdjson::UTF8_ERROR:
            return ParseError{0, "Invalid UTF-8 encoding at byte 0"};
        case simdjson::TRAILING_CONTENT:
            return ParseError{0, "Unexpected trailing content after JSON value"};
        default:
            return ParseError{0, std::string("Parse error: ") + simdjson::error_message(error)};
    }
}

// Convert a simdjson DOM element into an ArenaJsonNode tree, recursively.
// All string data is copied into the arena so that the resulting tree
// outlives the simdjson parser's internal buffers.
auto convertElement(simdjson::dom::element elem,
                    StringRef key,
                    ArenaAllocator& arena) -> ArenaJsonNode* {
    auto* node = arena.construct<ArenaJsonNode>();
    if (!node) {
        return nullptr;
    }
    node->key = key;

    switch (elem.type()) {
        case simdjson::dom::element_type::OBJECT: {
            node->type = NodeType::Object;
            simdjson::dom::object obj;
            if (elem.get(obj) != simdjson::SUCCESS) {
                return nullptr;
            }

            // Count children first to allocate the pointer array.
            std::size_t count = obj.size();
            if (count > 0) {
                auto** children = static_cast<ArenaJsonNode**>(
                    arena.allocate(count * sizeof(ArenaJsonNode*), alignof(ArenaJsonNode*)));
                if (!children) {
                    return nullptr;
                }

                std::size_t idx = 0;
                for (auto [k, v] : obj) {
                    // Copy the key string into the arena.
                    auto keyCopy = arena.copyString(k);
                    StringRef childKey{keyCopy.data(), keyCopy.size(), true};

                    auto* child = convertElement(v, childKey, arena);
                    if (!child) {
                        return nullptr;
                    }
                    children[idx++] = child;
                }

                node->children = children;
                node->childCount = count;
            }
            break;
        }

        case simdjson::dom::element_type::ARRAY: {
            node->type = NodeType::Array;
            simdjson::dom::array arr;
            if (elem.get(arr) != simdjson::SUCCESS) {
                return nullptr;
            }

            std::size_t count = arr.size();
            if (count > 0) {
                auto** children = static_cast<ArenaJsonNode**>(
                    arena.allocate(count * sizeof(ArenaJsonNode*), alignof(ArenaJsonNode*)));
                if (!children) {
                    return nullptr;
                }

                std::size_t idx = 0;
                for (auto v : arr) {
                    StringRef emptyKey{};
                    auto* child = convertElement(v, emptyKey, arena);
                    if (!child) {
                        return nullptr;
                    }
                    children[idx++] = child;
                }

                node->children = children;
                node->childCount = count;
            }
            break;
        }

        case simdjson::dom::element_type::STRING: {
            node->type = NodeType::String;
            std::string_view sv;
            if (elem.get(sv) != simdjson::SUCCESS) {
                return nullptr;
            }
            auto valueCopy = arena.copyString(sv);
            node->value = StringRef{valueCopy.data(), valueCopy.size(), true};
            break;
        }

        case simdjson::dom::element_type::INT64: {
            node->type = NodeType::Number;
            std::int64_t val{};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            // Convert integer to string via std::to_chars.
            std::array<char, 21> buf{}; // max digits for int64 + sign
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            auto valueCopy = arena.copyString(numStr);
            node->value = StringRef{valueCopy.data(), valueCopy.size(), true};
            break;
        }

        case simdjson::dom::element_type::UINT64: {
            node->type = NodeType::Number;
            std::uint64_t val{};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            std::array<char, 20> buf{}; // max digits for uint64
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            auto valueCopy = arena.copyString(numStr);
            node->value = StringRef{valueCopy.data(), valueCopy.size(), true};
            break;
        }

        case simdjson::dom::element_type::DOUBLE: {
            node->type = NodeType::Number;
            double val{};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            // Use std::to_chars with general format for round-trip fidelity.
            std::array<char, 32> buf{};
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val,
                                           std::chars_format::general);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            auto valueCopy = arena.copyString(numStr);
            node->value = StringRef{valueCopy.data(), valueCopy.size(), true};
            break;
        }

        case simdjson::dom::element_type::BOOL: {
            node->type = NodeType::Boolean;
            bool val{};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            std::string_view boolStr = val ? "true" : "false";
            auto valueCopy = arena.copyString(boolStr);
            node->value = StringRef{valueCopy.data(), valueCopy.size(), true};
            break;
        }

        case simdjson::dom::element_type::NULL_VALUE: {
            node->type = NodeType::Null;
            // value remains empty (default StringRef).
            break;
        }
    }

    return node;
}

} // anonymous namespace

auto simdjsonParse(const SourceBuffer& source,
                   ArenaAllocator& arena,
                   SimdjsonParseOptions /*options*/) -> SimdjsonResult {
    // Empty input check.
    if (source.size() == 0) {
        return SimdjsonResult{nullptr, ParseError{0, "Empty input"}};
    }

    // Create the simdjson parser on the stack — RAII ensures its internal
    // buffers are released when this function returns.
    simdjson::dom::parser parser;

    // Parse the input. simdjson requires a padded_string_view for safety.
    // SourceBuffer stores a std::string which is null-terminated, providing
    // the required padding for simdjson (SIMDJSON_PADDING bytes after data).
    // However, std::string only guarantees 1 byte of null termination.
    // Use padded_string to be safe.
    simdjson::padded_string paddedInput(source.data(), source.size());

    simdjson::dom::element doc;
    auto error = parser.parse(paddedInput).get(doc);
    if (error != simdjson::SUCCESS) {
        return SimdjsonResult{nullptr, mapSimdjsonError(error)};
    }

    // Walk the DOM tree and convert to ArenaJsonNode.
    StringRef rootKey{}; // Root node has no key.
    auto* root = convertElement(doc, rootKey, arena);
    if (!root) {
        return SimdjsonResult{nullptr, ParseError{0, "Memory allocation failed during tree construction"}};
    }

    // At this point, the simdjson parser (and its internal buffers) will be
    // destroyed when this function returns, since `parser` and `paddedInput`
    // are stack-local. All string data has been copied into the arena.
    return SimdjsonResult{root, std::nullopt};
}

} // namespace jsontitan::core
