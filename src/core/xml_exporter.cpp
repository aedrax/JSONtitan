#include "core/xml_exporter.h"

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

// Serialize a JsonNode subtree to XML, appending to the output string.
void serializeNode(const JsonNode& node, const std::string& elementName,
                   int indent, std::string& out) {
    std::string indentStr(static_cast<std::size_t>(indent * 2), ' ');
    std::string sanitized = sanitizeXmlName(elementName);

    switch (node.type) {
        case NodeType::Object: {
            out += indentStr + "<" + sanitized + ">\n";
            for (const auto& child : node.children) {
                std::string childName = child->key.empty() ? "item" : child->key;
                serializeNode(*child, childName, indent + 1, out);
            }
            out += indentStr + "</" + sanitized + ">\n";
            return;
        }
        case NodeType::Array: {
            out += indentStr + "<" + sanitized + ">\n";
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                std::string itemName = "item_" + std::to_string(i);
                serializeNode(*node.children[i], itemName, indent + 1, out);
            }
            out += indentStr + "</" + sanitized + ">\n";
            return;
        }
        case NodeType::String:
            out += indentStr + "<" + sanitized + ">" +
                   escapeXmlText(node.value) +
                   "</" + sanitized + ">\n";
            return;
        case NodeType::Number:
            out += indentStr + "<" + sanitized + ">" +
                   escapeXmlText(node.value) +
                   "</" + sanitized + ">\n";
            return;
        case NodeType::Boolean:
            out += indentStr + "<" + sanitized + ">" +
                   node.value +
                   "</" + sanitized + ">\n";
            return;
        case NodeType::Null:
            out += indentStr + "<" + sanitized + "/>\n";
            return;
    }
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
            default:   result += c;        break;
        }
    }

    return result;
}

auto exportXml(const JsonNode& node, const std::string& rootElementName)
    -> std::string {
    std::string result;
    result += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    serializeNode(node, rootElementName, 0, result);
    return result;
}

} // namespace jsontitan::core
