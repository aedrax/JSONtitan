#include "core/chunk_parser.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jsontitan::core {

namespace {

// ---------------------------------------------------------------------------
// Internal recursive-descent parser operating on a byte range of SourceBuffer.
// Allocates all nodes from the provided ArenaAllocator.
// Uses zero-copy StringRef for strings without escape sequences.
// ---------------------------------------------------------------------------

struct ChunkInternalParser {
    const SourceBuffer& source;
    ArenaAllocator& arena;
    std::size_t pos;          // Current position in the source buffer
    std::size_t endOffset;    // End of the parseable range (exclusive)
    std::size_t baseOffset;   // Start offset for error reporting

    ChunkInternalParser(const SourceBuffer& src, ArenaAllocator& alloc,
                        std::size_t start, std::size_t end)
        : source(src), arena(alloc), pos(start), endOffset(end), baseOffset(start) {}

    // -- Error helpers ------------------------------------------------------

    auto makeError(const std::string& desc) const -> ParseError {
        return ParseError{pos, desc};
    }

    // -- Character access ---------------------------------------------------

    [[nodiscard]] auto peek() const -> char {
        return (pos < endOffset) ? source.data()[pos] : '\0';
    }

    auto advance() -> char {
        return (pos < endOffset) ? source.data()[pos++] : '\0';
    }

    [[nodiscard]] auto atEnd() const -> bool {
        return pos >= endOffset;
    }

    // -- Whitespace ---------------------------------------------------------

    void skipWhitespace() {
        const char* data = source.data();
        while (pos < endOffset) {
            char c = data[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }

    // -- String parsing (zero-copy when no escapes) -------------------------

    struct StringParseResult {
        StringRef ref;
        std::optional<ParseError> error;
    };

    auto parseString() -> StringParseResult {
        if (peek() != '"') {
            return {{}, makeError("Expected '\"' at start of string")};
        }
        advance(); // consume opening quote

        const char* data = source.data();
        std::size_t start = pos;
        bool hasEscapes = false;

        // First pass: scan for end of string, check for escapes
        while (pos < endOffset) {
            char c = data[pos];
            if (c == '"') {
                // End of string found
                if (!hasEscapes) {
                    // Zero-copy: reference directly into source buffer
                    StringRef ref{data + start, pos - start, false};
                    ++pos; // consume closing quote
                    return {ref, std::nullopt};
                } else {
                    // Has escapes: need to resolve them
                    // Reset pos to start and do full escape resolution
                    std::size_t endPos = pos;
                    pos = start;
                    auto resolved = resolveEscapes(endPos);
                    if (!resolved.error) {
                        // Copy resolved string into arena
                        auto arenaStr = arena.copyString(resolved.value);
                        StringRef ref{arenaStr.data(), arenaStr.size(), true};
                        return {ref, std::nullopt};
                    }
                    return {{}, resolved.error};
                }
            }
            if (c == '\\') {
                hasEscapes = true;
                ++pos;
                if (pos < endOffset) {
                    ++pos; // skip escaped character
                }
            } else {
                ++pos;
            }
        }

        return {{}, makeError("Unterminated string")};
    }

    struct ResolveResult {
        std::string value;
        std::optional<ParseError> error;
    };

    // Resolve escape sequences from current pos to endPos
    auto resolveEscapes(std::size_t endPos) -> ResolveResult {
        const char* data = source.data();
        std::string result;
        result.reserve(endPos - pos);

        while (pos < endPos) {
            char c = data[pos];
            if (c == '\\') {
                ++pos;
                if (pos >= endPos) {
                    return {"", makeError("Unexpected end of input in string escape")};
                }
                char esc = data[pos++];
                switch (esc) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        auto [codepoint, err] = parseUnicodeEscape();
                        if (err) return {"", *err};

                        // Handle surrogate pairs
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            // High surrogate — expect \uXXXX low surrogate
                            if (pos + 1 < endPos &&
                                data[pos] == '\\' && data[pos + 1] == 'u') {
                                pos += 2; // skip \u
                                auto [low, err2] = parseUnicodeEscape();
                                if (err2) return {"", *err2};
                                if (low < 0xDC00 || low > 0xDFFF) {
                                    return {"", makeError("Invalid low surrogate in Unicode escape")};
                                }
                                codepoint = 0x10000 +
                                    ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                return {"", makeError("Expected low surrogate after high surrogate")};
                            }
                        }

                        appendCodepoint(result, codepoint);
                        break;
                    }
                    default:
                        return {"", makeError(
                            std::string("Invalid escape character: '\\") + esc + "'")};
                }
            } else {
                result += c;
                ++pos;
            }
        }

        ++pos; // consume closing quote
        return {std::move(result), std::nullopt};
    }

    auto parseUnicodeEscape() -> std::pair<uint32_t, std::optional<ParseError>> {
        if (pos + 4 > endOffset) {
            return {0, makeError("Incomplete Unicode escape sequence")};
        }
        const char* data = source.data();
        uint32_t codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            char h = data[pos++];
            codepoint <<= 4;
            if (h >= '0' && h <= '9') {
                codepoint |= static_cast<uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                codepoint |= static_cast<uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                codepoint |= static_cast<uint32_t>(h - 'A' + 10);
            } else {
                return {0, makeError(
                    std::string("Invalid hex digit in Unicode escape: '") + h + "'")};
            }
        }
        return {codepoint, std::nullopt};
    }

    static void appendCodepoint(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // -- Number parsing -----------------------------------------------------

    struct NumberParseResult {
        StringRef ref;
        std::optional<ParseError> error;
    };

    auto parseNumber() -> NumberParseResult {
        std::size_t start = pos;
        const char* data = source.data();

        // Optional minus
        if (peek() == '-') ++pos;

        // Integer part
        if (pos < endOffset && data[pos] == '0') {
            ++pos;
        } else if (pos < endOffset && data[pos] >= '1' && data[pos] <= '9') {
            ++pos;
            while (pos < endOffset && data[pos] >= '0' && data[pos] <= '9') ++pos;
        } else {
            return {{}, makeError("Invalid number: expected digit")};
        }

        // Fractional part
        if (pos < endOffset && data[pos] == '.') {
            ++pos;
            if (pos >= endOffset || data[pos] < '0' || data[pos] > '9') {
                return {{}, makeError("Invalid number: expected digit after decimal point")};
            }
            while (pos < endOffset && data[pos] >= '0' && data[pos] <= '9') ++pos;
        }

        // Exponent part
        if (pos < endOffset && (data[pos] == 'e' || data[pos] == 'E')) {
            ++pos;
            if (pos < endOffset && (data[pos] == '+' || data[pos] == '-')) ++pos;
            if (pos >= endOffset || data[pos] < '0' || data[pos] > '9') {
                return {{}, makeError("Invalid number: expected digit in exponent")};
            }
            while (pos < endOffset && data[pos] >= '0' && data[pos] <= '9') ++pos;
        }

        // Zero-copy: numbers never need escape resolution
        StringRef ref{data + start, pos - start, false};
        return {ref, std::nullopt};
    }

    // -- Literal parsing (true, false, null) --------------------------------

    auto expectLiteral(const char* lit, std::size_t len)
        -> std::optional<ParseError>
    {
        const char* data = source.data();
        for (std::size_t i = 0; i < len; ++i) {
            if (pos >= endOffset || data[pos] != lit[i]) {
                return makeError(std::string("Expected '") + lit + "'");
            }
            ++pos;
        }
        return std::nullopt;
    }

    // -- Value parsing (recursive) ------------------------------------------

    auto parseValue(StringRef key) -> ChunkParseResult {
        skipWhitespace();
        if (atEnd()) {
            return {nullptr, makeError("Unexpected end of input")};
        }

        char c = peek();

        if (c == '{') return parseObject(key);
        if (c == '[') return parseArray(key);
        if (c == '"') {
            auto [ref, err] = parseString();
            if (err) return {nullptr, err};
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::String;
            node->key = key;
            node->value = ref;
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            auto [ref, err] = parseNumber();
            if (err) return {nullptr, err};
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Number;
            node->key = key;
            node->value = ref;
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }
        if (c == 't') {
            std::size_t start = pos;
            auto err = expectLiteral("true", 4);
            if (err) return {nullptr, err};
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Boolean;
            node->key = key;
            node->value = StringRef{source.data() + start, 4, false};
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }
        if (c == 'f') {
            std::size_t start = pos;
            auto err = expectLiteral("false", 5);
            if (err) return {nullptr, err};
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Boolean;
            node->key = key;
            node->value = StringRef{source.data() + start, 5, false};
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }
        if (c == 'n') {
            std::size_t start = pos;
            auto err = expectLiteral("null", 4);
            if (err) return {nullptr, err};
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Null;
            node->key = key;
            node->value = StringRef{source.data() + start, 4, false};
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }

        return {nullptr, makeError(std::string("Unexpected character: '") + c + "'")};
    }

    // -- Object parsing -----------------------------------------------------

    auto parseObject(StringRef key) -> ChunkParseResult {
        ++pos; // consume '{'
        skipWhitespace();

        if (peek() == '}') {
            ++pos;
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Object;
            node->key = key;
            node->value = StringRef{};
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }

        // Collect children in a temporary vector, then copy to arena
        std::vector<ArenaJsonNode*> childVec;

        while (true) {
            skipWhitespace();

            // Parse key
            if (peek() != '"') {
                return {nullptr, makeError("Expected '\"' for object key")};
            }
            auto [childKey, keyErr] = parseString();
            if (keyErr) return {nullptr, keyErr};

            skipWhitespace();

            // Expect colon
            if (peek() != ':') {
                return {nullptr, makeError("Expected ':' after object key")};
            }
            ++pos;

            // Parse value
            auto [child, valErr] = parseValue(childKey);
            if (valErr) return {nullptr, valErr};
            childVec.push_back(child);

            skipWhitespace();

            char next = peek();
            if (next == '}') {
                ++pos;
                break;
            }
            if (next == ',') {
                ++pos;
                continue;
            }

            return {nullptr, makeError("Expected ',' or '}' in object")};
        }

        // Allocate children array in arena
        auto** children = static_cast<ArenaJsonNode**>(
            arena.allocate(childVec.size() * sizeof(ArenaJsonNode*),
                           alignof(ArenaJsonNode*)));
        for (std::size_t i = 0; i < childVec.size(); ++i) {
            children[i] = childVec[i];
        }

        auto* node = arena.construct<ArenaJsonNode>();
        node->type = NodeType::Object;
        node->key = key;
        node->value = StringRef{};
        node->children = children;
        node->childCount = childVec.size();
        return {node, std::nullopt};
    }

    // -- Array parsing ------------------------------------------------------

    auto parseArray(StringRef key) -> ChunkParseResult {
        ++pos; // consume '['
        skipWhitespace();

        if (peek() == ']') {
            ++pos;
            auto* node = arena.construct<ArenaJsonNode>();
            node->type = NodeType::Array;
            node->key = key;
            node->value = StringRef{};
            node->children = nullptr;
            node->childCount = 0;
            return {node, std::nullopt};
        }

        // Collect children in a temporary vector, then copy to arena
        std::vector<ArenaJsonNode*> childVec;

        while (true) {
            // Array elements have empty keys
            auto [child, err] = parseValue(StringRef{});
            if (err) return {nullptr, err};
            childVec.push_back(child);

            skipWhitespace();

            char next = peek();
            if (next == ']') {
                ++pos;
                break;
            }
            if (next == ',') {
                ++pos;
                continue;
            }

            return {nullptr, makeError("Expected ',' or ']' in array")};
        }

        // Allocate children array in arena
        auto** children = static_cast<ArenaJsonNode**>(
            arena.allocate(childVec.size() * sizeof(ArenaJsonNode*),
                           alignof(ArenaJsonNode*)));
        for (std::size_t i = 0; i < childVec.size(); ++i) {
            children[i] = childVec[i];
        }

        auto* node = arena.construct<ArenaJsonNode>();
        node->type = NodeType::Array;
        node->key = key;
        node->value = StringRef{};
        node->children = children;
        node->childCount = childVec.size();
        return {node, std::nullopt};
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto parseChunkRange(const SourceBuffer& source,
                     std::size_t startOffset,
                     std::size_t endOffset,
                     ArenaAllocator& arena,
                     StringRef key) -> ChunkParseResult
{
    if (startOffset >= endOffset) {
        return {nullptr, ParseError{startOffset, "Empty input range"}};
    }

    if (endOffset > source.size()) {
        endOffset = source.size();
    }

    ChunkInternalParser parser(source, arena, startOffset, endOffset);
    auto result = parser.parseValue(key);

    if (result.error) {
        return result;
    }

    // Check for trailing non-whitespace content
    parser.skipWhitespace();
    if (!parser.atEnd()) {
        return {nullptr, parser.makeError("Unexpected content after JSON value")};
    }

    return result;
}

} // namespace jsontitan::core
