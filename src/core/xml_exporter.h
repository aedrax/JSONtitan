#pragma once

#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

// Export a JsonNode tree to well-formed XML.
// Maps object keys to element names, arrays to repeated elements with indexed
// wrapper, and scalar values to text content.
auto exportXml(const JsonNode& node, const std::string& rootElementName = "root")
    -> std::string;

// Sanitize a string for use as an XML element name:
// - Replace characters invalid in XML names with underscores
// - Prepend underscore if the name starts with a digit
auto sanitizeXmlName(const std::string& name) -> std::string;

// Escape XML-reserved characters in text content:
// & -> &amp;   < -> &lt;   > -> &gt;   " -> &quot;   ' -> &apos;
auto escapeXmlText(const std::string& text) -> std::string;

} // namespace jsontitan::core
