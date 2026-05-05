#include "core/parser.h"

#include <cctype>
#include <cstdint>
#include <utility>

namespace jsontitan::core {

// ---------------------------------------------------------------------------
// ParserState — accumulates bytes across chunks for streaming parse
// ---------------------------------------------------------------------------

struct ParserState {
    std::string buffer;            // Residual byte buffer accumulated across chunks
    std::size_t bytesConsumed = 0; // Total bytes processed so far
    int currentDepth = 0;          // Current nesting depth (informational)
    std::size_t emittedCount = 0;  // Number of top-level values emitted so far
    std::shared_ptr<const JsonNode> lastEmitted; // Last emitted node (for finalizeParse)
};

// ---------------------------------------------------------------------------
// ParserStateDeleter (allows unique_ptr<ParserState> in incomplete-type contexts)
// ---------------------------------------------------------------------------

void ParserStateDeleter::operator()(ParserState* p) const noexcept {
    delete p;
}

// ---------------------------------------------------------------------------
// Internal recursive-descent parser
// ---------------------------------------------------------------------------

namespace {

struct InternalParser {
    const std::string& input;
    std::size_t pos;
    std::size_t baseOffset; // Added to pos for error byte offsets

    explicit InternalParser(const std::string& src, std::size_t base = 0)
        : input(src), pos(0), baseOffset(base) {}

    // -- Error helpers ------------------------------------------------------

    auto makeError(const std::string& desc) const -> ParseError {
        return ParseError{baseOffset + pos, desc};
    }

    // -- Whitespace ---------------------------------------------------------

    void skipWhitespace() {
        while (pos < input.size()) {
            char c = input[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }

    // -- Peek / advance -----------------------------------------------------

    [[nodiscard]] auto peek() const -> char {
        return (pos < input.size()) ? input[pos] : '\0';
    }

    auto advance() -> char {
        return (pos < input.size()) ? input[pos++] : '\0';
    }

    [[nodiscard]] auto atEnd() const -> bool {
        return pos >= input.size();
    }

    // -- String parsing -----------------------------------------------------

    auto parseString() -> std::pair<std::string, std::optional<ParseError>> {
        if (peek() != '"') {
            return {"", makeError("Expected '\"' at start of string")};
        }
        advance(); // consume opening quote

        std::string result;
        while (!atEnd()) {
            char c = advance();
            if (c == '"') {
                return {result, std::nullopt};
            }
            if (c == '\\') {
                if (atEnd()) {
                    return {"", makeError("Unexpected end of input in string escape")};
                }
                char esc = advance();
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
                            if (pos + 1 < input.size() && input[pos] == '\\' && input[pos + 1] == 'u') {
                                pos += 2; // skip \u
                                auto [low, err2] = parseUnicodeEscape();
                                if (err2) return {"", *err2};
                                if (low < 0xDC00 || low > 0xDFFF) {
                                    return {"", makeError("Invalid low surrogate in Unicode escape")};
                                }
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                return {"", makeError("Expected low surrogate after high surrogate")};
                            }
                        }

                        appendCodepoint(result, codepoint);
                        break;
                    }
                    default:
                        return {"", makeError(std::string("Invalid escape character: '\\") + esc + "'")};
                }
            } else {
                result += c;
            }
        }
        return {"", makeError("Unterminated string")};
    }

    auto parseUnicodeEscape() -> std::pair<uint32_t, std::optional<ParseError>> {
        if (pos + 4 > input.size()) {
            return {0, makeError("Incomplete Unicode escape sequence")};
        }
        uint32_t codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            char h = advance();
            codepoint <<= 4;
            if (h >= '0' && h <= '9') {
                codepoint |= static_cast<uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                codepoint |= static_cast<uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                codepoint |= static_cast<uint32_t>(h - 'A' + 10);
            } else {
                return {0, makeError(std::string("Invalid hex digit in Unicode escape: '") + h + "'")};
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

    auto parseNumber() -> std::pair<std::string, std::optional<ParseError>> {
        std::size_t start = pos;

        // Optional minus
        if (peek() == '-') advance();

        // Integer part
        if (peek() == '0') {
            advance();
        } else if (peek() >= '1' && peek() <= '9') {
            advance();
            while (peek() >= '0' && peek() <= '9') advance();
        } else {
            return {"", makeError("Invalid number: expected digit")};
        }

        // Fractional part
        if (peek() == '.') {
            advance();
            if (peek() < '0' || peek() > '9') {
                return {"", makeError("Invalid number: expected digit after decimal point")};
            }
            while (peek() >= '0' && peek() <= '9') advance();
        }

        // Exponent part
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (peek() < '0' || peek() > '9') {
                return {"", makeError("Invalid number: expected digit in exponent")};
            }
            while (peek() >= '0' && peek() <= '9') advance();
        }

        return {input.substr(start, pos - start), std::nullopt};
    }

    // -- Literal parsing (true, false, null) --------------------------------

    auto expectLiteral(const std::string& lit) -> std::optional<ParseError> {
        for (std::size_t i = 0; i < lit.size(); ++i) {
            if (atEnd() || peek() != lit[i]) {
                return makeError("Expected '" + lit + "'");
            }
            advance();
        }
        return std::nullopt;
    }

    // -- Value parsing (recursive) ------------------------------------------

    auto parseValue(const std::string& key)
        -> std::pair<std::shared_ptr<const JsonNode>, std::optional<ParseError>>
    {
        skipWhitespace();
        if (atEnd()) {
            return {nullptr, makeError("Unexpected end of input")};
        }

        char c = peek();

        if (c == '{') return parseObject(key);
        if (c == '[') return parseArray(key);
        if (c == '"') {
            auto [str, err] = parseString();
            if (err) return {nullptr, err};
            return {JsonNode::makeString(key, std::move(str)), std::nullopt};
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            auto [num, err] = parseNumber();
            if (err) return {nullptr, err};
            return {JsonNode::makeNumber(key, std::move(num)), std::nullopt};
        }
        if (c == 't') {
            auto err = expectLiteral("true");
            if (err) return {nullptr, err};
            return {JsonNode::makeBool(key, true), std::nullopt};
        }
        if (c == 'f') {
            auto err = expectLiteral("false");
            if (err) return {nullptr, err};
            return {JsonNode::makeBool(key, false), std::nullopt};
        }
        if (c == 'n') {
            auto err = expectLiteral("null");
            if (err) return {nullptr, err};
            return {JsonNode::makeNull(key), std::nullopt};
        }

        return {nullptr, makeError(std::string("Unexpected character: '") + c + "'")};
    }

    // -- Object parsing -----------------------------------------------------

    auto parseObject(const std::string& key)
        -> std::pair<std::shared_ptr<const JsonNode>, std::optional<ParseError>>
    {
        advance(); // consume '{'
        skipWhitespace();

        std::vector<std::shared_ptr<const JsonNode>> children;

        if (peek() == '}') {
            advance();
            return {JsonNode::makeObject(key, std::move(children)), std::nullopt};
        }

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
            advance();

            // Parse value
            auto [child, valErr] = parseValue(childKey);
            if (valErr) return {nullptr, valErr};
            children.push_back(std::move(child));

            skipWhitespace();

            char next = peek();
            if (next == '}') {
                advance();
                return {JsonNode::makeObject(key, std::move(children)), std::nullopt};
            }
            if (next == ',') {
                advance();
                continue;
            }

            return {nullptr, makeError("Expected ',' or '}' in object")};
        }
    }

    // -- Array parsing ------------------------------------------------------

    auto parseArray(const std::string& key)
        -> std::pair<std::shared_ptr<const JsonNode>, std::optional<ParseError>>
    {
        advance(); // consume '['
        skipWhitespace();

        std::vector<std::shared_ptr<const JsonNode>> children;

        if (peek() == ']') {
            advance();
            return {JsonNode::makeArray(key, std::move(children)), std::nullopt};
        }

        while (true) {
            // Array elements have empty keys
            auto [child, err] = parseValue("");
            if (err) return {nullptr, err};
            children.push_back(std::move(child));

            skipWhitespace();

            char next = peek();
            if (next == ']') {
                advance();
                return {JsonNode::makeArray(key, std::move(children)), std::nullopt};
            }
            if (next == ',') {
                advance();
                continue;
            }

            return {nullptr, makeError("Expected ',' or ']' in array")};
        }
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto makeParserState() -> std::unique_ptr<ParserState, ParserStateDeleter> {
    return std::unique_ptr<ParserState, ParserStateDeleter>(new ParserState());
}

auto parseChunk(const ParserState& state, std::span<const std::byte> chunk)
    -> ParseChunkResult
{
    // Build new buffer by appending chunk bytes to the accumulated buffer
    std::string newBuffer = state.buffer;
    newBuffer.append(
        reinterpret_cast<const char*>(chunk.data()),
        chunk.size()
    );

    // Try to parse complete top-level values from the buffer.
    // Strategy: parse values sequentially. Only EMIT a value if there is
    // clearly more non-whitespace content after it. If after parsing a value
    // we're at end of buffer (or only whitespace remains), keep that value
    // in the buffer — more bytes might arrive that extend it (e.g., numbers).
    std::vector<std::shared_ptr<const JsonNode>> emitted;
    std::size_t cursor = 0;
    std::size_t emittedCount = state.emittedCount;
    std::shared_ptr<const JsonNode> lastEmitted = state.lastEmitted;

    while (cursor < newBuffer.size()) {
        // Skip leading whitespace
        std::size_t valueStart = cursor;
        while (cursor < newBuffer.size()) {
            char c = newBuffer[cursor];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++cursor;
            } else {
                break;
            }
        }

        if (cursor >= newBuffer.size()) {
            // Only whitespace remains - discard it, buffer is effectively empty
            cursor = newBuffer.size();
            break;
        }

        // Remember where this value starts (after whitespace)
        valueStart = cursor;

        // Attempt to parse one complete value starting at cursor
        std::string remaining = newBuffer.substr(cursor);
        InternalParser parser(remaining);
        auto [node, err] = parser.parseValue("");

        if (err) {
            // Parse failed — could be incomplete data or syntax error.
            // In parseChunk, keep the bytes in the buffer for later.
            // The cursor stays at valueStart so these bytes are retained.
            cursor = valueStart;
            break;
        }

        // Successfully parsed a value. Check if there's more non-whitespace
        // content after it in the buffer.
        std::size_t afterValue = cursor + parser.pos;
        std::size_t checkPos = afterValue;
        while (checkPos < newBuffer.size()) {
            char c = newBuffer[checkPos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++checkPos;
            } else {
                break;
            }
        }

        if (checkPos >= newBuffer.size()) {
            // No more non-whitespace content after this value.
            // Don't emit — keep it in the buffer in case more data arrives
            // that extends this value (e.g., number `3` might become `3.14`).
            cursor = valueStart;
            break;
        }

        // There IS more content after this value — safe to emit it.
        emitted.push_back(node);
        lastEmitted = node;
        emittedCount++;
        cursor = afterValue;
    }

    // Build next state with only the unparsed residual bytes
    auto nextState = std::unique_ptr<ParserState, ParserStateDeleter>(new ParserState());
    nextState->buffer = newBuffer.substr(cursor);
    nextState->bytesConsumed = state.bytesConsumed + chunk.size();
    nextState->currentDepth = state.currentDepth;
    nextState->emittedCount = emittedCount;
    nextState->lastEmitted = lastEmitted;

    return ParseChunkResult{
        .emittedNodes = std::move(emitted),
        .nextState = std::move(nextState),
        .error = std::nullopt
    };
}

auto finalizeParse(const ParserState& state) -> ParseResult {
    // Case 1: Buffer is empty
    if (state.buffer.empty()) {
        if (state.emittedCount == 0) {
            // No data was ever provided
            return ParseResult{
                .root = nullptr,
                .error = ParseError{0, "Empty input"}
            };
        }
        if (state.emittedCount == 1) {
            // Exactly one value was emitted by parseChunk - return it as root
            return ParseResult{.root = state.lastEmitted, .error = std::nullopt};
        }
        // Multiple values were emitted - this is an error (multiple top-level values)
        return ParseResult{
            .root = nullptr,
            .error = ParseError{0, "Unexpected content after JSON value"}
        };
    }

    // Case 2: Buffer has content - check if it's only whitespace
    bool onlyWhitespace = true;
    for (char c : state.buffer) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            onlyWhitespace = false;
            break;
        }
    }

    if (onlyWhitespace) {
        // Trailing whitespace only - same as empty buffer
        if (state.emittedCount == 0) {
            return ParseResult{
                .root = nullptr,
                .error = ParseError{0, "Empty input"}
            };
        }
        if (state.emittedCount == 1) {
            return ParseResult{.root = state.lastEmitted, .error = std::nullopt};
        }
        return ParseResult{
            .root = nullptr,
            .error = ParseError{0, "Unexpected content after JSON value"}
        };
    }

    // Case 3: Buffer has non-whitespace content - parse it
    InternalParser parser(state.buffer, state.bytesConsumed - state.buffer.size());
    auto [root, err] = parser.parseValue("");

    if (err) {
        return ParseResult{.root = nullptr, .error = err};
    }

    // Check for trailing non-whitespace content after the parsed value
    parser.skipWhitespace();
    if (!parser.atEnd()) {
        return ParseResult{
            .root = nullptr,
            .error = parser.makeError("Unexpected content after JSON value")
        };
    }

    // If values were previously emitted, having another value here means
    // multiple top-level values total
    if (state.emittedCount > 0) {
        return ParseResult{
            .root = nullptr,
            .error = ParseError{state.bytesConsumed - state.buffer.size(),
                                "Unexpected content after JSON value"}
        };
    }

    return ParseResult{.root = std::move(root), .error = std::nullopt};
}

} // namespace jsontitan::core
