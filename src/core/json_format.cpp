#include "core/json_format.h"

#include <algorithm>
#include <cctype>

namespace jsontitan::core {

namespace {

auto endsWithNoCase(std::string_view text, std::string_view suffix) -> bool {
    if (text.size() < suffix.size()) {
        return false;
    }
    return std::equal(suffix.begin(), suffix.end(),
                      text.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

auto isJsonWhitespace(char c) -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

} // anonymous namespace

auto formatForPath(std::string_view path) -> std::optional<JsonFormat> {
    if (endsWithNoCase(path, ".jsonl") || endsWithNoCase(path, ".ndjson")) {
        return JsonFormat::JsonLines;
    }
    if (endsWithNoCase(path, ".jsonc")) {
        return JsonFormat::JsonWithComments;
    }
    if (endsWithNoCase(path, ".json")) {
        return JsonFormat::Json;
    }
    return std::nullopt;
}

auto stripJsonc(std::span<char> text) -> JsoncStripResult {
    JsoncStripResult result;
    const std::size_t size = text.size();

    // A comma is only "trailing" when it follows a complete value and the
    // next token closes the container; [,] and {"a":,} must stay invalid.
    bool lastTokenEndsValue = false;
    std::optional<std::size_t> pendingComma;

    std::size_t i = 0;
    while (i < size) {
        const char c = text[i];

        if (isJsonWhitespace(c)) {
            ++i;
            continue;
        }

        if (c == '/' && i + 1 < size && text[i + 1] == '/') {
            // Line comment: blank up to (not including) the newline.
            result.hadComments = true;
            result.changed = true;
            while (i < size && text[i] != '\n') {
                text[i++] = ' ';
            }
            continue;
        }

        if (c == '/' && i + 1 < size && text[i + 1] == '*') {
            // Block comment: find the terminator first so an unterminated
            // comment is reported without half-blanking it.
            std::size_t end = i + 2;
            while (end + 1 < size && !(text[end] == '*' && text[end + 1] == '/')) {
                ++end;
            }
            if (end + 1 >= size) {
                result.error = ParseError{i, "Unterminated block comment"};
                return result;
            }
            result.hadComments = true;
            result.changed = true;
            for (std::size_t j = i; j < end + 2; ++j) {
                if (text[j] != '\n') {
                    text[j] = ' ';
                }
            }
            i = end + 2;
            continue;
        }

        // Every other byte starts a significant token.
        if ((c == '}' || c == ']') && pendingComma) {
            text[*pendingComma] = ' ';
            result.changed = true;
        }
        pendingComma.reset();

        if (c == ',') {
            if (lastTokenEndsValue) {
                pendingComma = i;
            }
            lastTokenEndsValue = false;
            ++i;
            continue;
        }

        if (c == '"') {
            // Skip the string, honouring escapes, so // or /* inside it is
            // never mistaken for a comment.
            ++i;
            while (i < size && text[i] != '"') {
                i += (text[i] == '\\') ? 2 : 1;
            }
            ++i;  // closing quote (or past the end if unterminated)
            lastTokenEndsValue = true;
            continue;
        }

        lastTokenEndsValue = !(c == '{' || c == '[' || c == ':');
        ++i;
    }

    return result;
}

} // namespace jsontitan::core
