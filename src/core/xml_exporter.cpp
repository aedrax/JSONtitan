#include "core/xml_exporter.h"

#include "core/stream_export.h"

#include <cctype>
#include <string>

namespace jsontitan::core {

namespace {

// Check if a character is valid as the first character of an XML name.
// XML NameStartChar: ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6] |
// [#xF8-#x2FF] | [#x370-#x37D] | [#x37F-#x1FFF] | [#x200C-#x200D] |
// [#x2070-#x218F] | [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] |
// [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
// For simplicity, we allow ASCII letters, underscore, and colon as start chars.
auto isXmlNameStartChar(char c) -> bool {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':';
}

// Check if a character is valid within an XML name (not first position).
// XML NameChar: NameStartChar | "-" | "." | [0-9] | #xB7 | [#x0300-#x036F] |
// [#x203F-#x2040]
// For simplicity, we allow ASCII letters, digits, underscore, colon, hyphen, dot.
auto isXmlNameChar(char c) -> bool {
    return isXmlNameStartChar(c) || std::isdigit(static_cast<unsigned char>(c)) ||
           c == '-' || c == '.';
}

} // anonymous namespace

auto sanitizeXmlName(const std::string& name) -> std::string {
    if (name.empty()) {
        return "_";
    }

    std::string result;
    result.reserve(name.size() + 1);

    // First character: must be a valid XML NameStartChar
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        // Prepend underscore if starts with a digit
        result += '_';
        result += name[0];
    } else if (!isXmlNameStartChar(name[0])) {
        result += '_';
    } else {
        result += name[0];
    }

    // Subsequent characters: must be valid XML NameChars
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (isXmlNameChar(name[i])) {
            result += name[i];
        } else {
            result += '_';
        }
    }

    return result;
}

auto escapeXmlText(const std::string& text) -> std::string {
    std::string result;
    result.reserve(text.size());

    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: {
                // JSON strings may contain control characters (via \u escapes)
                // that are illegal in XML 1.0 even as character references
                // (everything below 0x20 except TAB/LF/CR). Emitting them raw
                // produces a file every conforming XML parser rejects, so
                // substitute U+FFFD (as UTF-8) instead.
                auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                    result += "\xEF\xBF\xBD";  // U+FFFD REPLACEMENT CHARACTER
                } else {
                    result += c;
                }
                break;
            }
        }
    }

    return result;
}

auto exportXml(const JsonNode& node, const std::string& rootElementName)
    -> std::string {
    // Thin wrapper over the streaming exporter with a string-appending sink.
    std::string result;
    ByteSink sink = [&result](std::string_view chunk) {
        result += chunk;
        return true;
    };
    exportXmlStream(NodeView(node), sink, rootElementName);
    return result;
}

} // namespace jsontitan::core
