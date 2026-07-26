#include "core/csv_exporter.h"

#include "core/json_escape.h"

#include <algorithm>
#include <set>
#include <vector>

namespace jsontitan::core {

namespace {

// Escape a CSV cell value per RFC 4180:
// If the value contains commas, double quotes, or newlines, enclose it in
// double quotes and double any internal double quotes.
//
// Additionally, when guardFormula is set, neutralize formula injection:
// cells beginning with '=', '+', '-', '@', tab, or CR are interpreted as
// formulas by Excel/LibreOffice. Values originating from untrusted JSON
// strings get prefixed with a single quote (the standard neutralization)
// and force-quoted. Numeric/boolean/null cells are exported unguarded so
// negative numbers stay numeric.
auto escapeCsvCell(const std::string& value, bool guardFormula = true) -> std::string {
    bool formulaRisk = false;
    if (guardFormula && !value.empty()) {
        char first = value.front();
        formulaRisk = (first == '=' || first == '+' || first == '-' ||
                       first == '@' || first == '\t' || first == '\r');
    }

    bool needsQuoting = formulaRisk;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuoting = true;
            break;
        }
    }

    if (!needsQuoting) {
        return value;
    }

    std::string result;
    result.reserve(value.size() + 5);
    result += '"';
    if (formulaRisk) {
        result += '\'';
    }
    for (char c : value) {
        if (c == '"') {
            result += "\"\"";
        } else {
            result += c;
        }
    }
    result += '"';
    return result;
}

// Serialize a JsonNode to compact single-line JSON (for embedding in CSV cells).
void serializeCompactJson(const JsonNode& node, std::string& out) {
    switch (node.type) {
        case NodeType::Object: {
            out += '{';
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0) out += ',';
                out += escapeJsonString(node.children[i]->key);
                out += ':';
                serializeCompactJson(*node.children[i], out);
            }
            out += '}';
            return;
        }
        case NodeType::Array: {
            out += '[';
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0) out += ',';
                serializeCompactJson(*node.children[i], out);
            }
            out += ']';
            return;
        }
        case NodeType::String:
            out += escapeJsonString(node.value);
            return;
        case NodeType::Number:
            out += node.value;
            return;
        case NodeType::Boolean:
            out += node.value;
            return;
        case NodeType::Null:
            out += "null";
            return;
    }
}

// Serialize a JsonNode field value for a CSV cell.
// Scalars produce their plain value; nested objects/arrays produce compact JSON.
auto serializeNodeValue(const JsonNode& node) -> std::string {
    switch (node.type) {
        case NodeType::String:
            return node.value;
        case NodeType::Number:
            return node.value;
        case NodeType::Boolean:
            return node.value;
        case NodeType::Null:
            return "null";
        case NodeType::Object:
        case NodeType::Array: {
            std::string result;
            serializeCompactJson(node, result);
            return result;
        }
    }
    return "";
}

// Compute the union of all keys across all objects in the array,
// preserving first-seen order for deterministic output.
auto computeHeaders(const JsonNode& arrayNode) -> std::vector<std::string> {
    std::vector<std::string> headers;
    std::set<std::string> seen;

    for (const auto& obj : arrayNode.children) {
        for (const auto& child : obj->children) {
            if (seen.find(child->key) == seen.end()) {
                seen.insert(child->key);
                headers.push_back(child->key);
            }
        }
    }
    return headers;
}

} // anonymous namespace

auto exportCsv(const JsonNode& node) -> CsvResult {
    // Validate: must be an array of objects
    if (node.type != NodeType::Array) {
        if (node.type == NodeType::Object) {
            return CsvError{.description = "Selected node is a plain object, not an array of objects. CSV export requires an array of objects."};
        }
        return CsvError{.description = "Selected node is a scalar value, not an array of objects. CSV export requires an array of objects."};
    }

    if (!node.children.empty()) {
        // Check that all children are objects
        bool hasObject = false;
        bool hasNonObject = false;
        for (const auto& child : node.children) {
            if (child->type == NodeType::Object) {
                hasObject = true;
            } else {
                hasNonObject = true;
            }
        }

        if (hasNonObject && hasObject) {
            return CsvError{.description = "Selected array contains a mix of objects and non-object values. CSV export requires an array where all elements are objects."};
        }
        if (hasNonObject) {
            return CsvError{.description = "Selected array contains non-object values. CSV export requires an array of objects."};
        }
    }

    // Compute column headers (union of all keys, first-seen order)
    auto headers = computeHeaders(node);

    std::string result;

    // Write header row
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            result += ',';
        }
        result += escapeCsvCell(headers[i]);
    }
    result += "\r\n";

    // Write data rows
    for (const auto& obj : node.children) {
        for (std::size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) {
                result += ',';
            }

            // Find the value for this header in the current object
            const std::string& headerKey = headers[i];
            bool found = false;
            for (const auto& field : obj->children) {
                if (field->key == headerKey) {
                    std::string cellValue = serializeNodeValue(*field);
                    bool textual = field->type != NodeType::Number &&
                                   field->type != NodeType::Boolean &&
                                   field->type != NodeType::Null;
                    result += escapeCsvCell(cellValue, textual);
                    found = true;
                    break;
                }
            }
            // If key not present in this object, emit empty cell
            if (!found) {
                // empty cell — nothing to append
            }
        }
        result += "\r\n";
    }

    return result;
}

} // namespace jsontitan::core
