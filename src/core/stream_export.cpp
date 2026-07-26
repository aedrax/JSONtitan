#include "core/stream_export.h"

#include "core/json_escape.h"
#include "core/xml_exporter.h"  // sanitizeXmlName, escapeXmlText

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jsontitan::core {

namespace {

constexpr const char* kAbortedError = "Export aborted by sink";

// Accumulates output in a buffer and flushes it to the sink once it reaches
// ~64 KB. Once the sink reports an abort (returns false) the writer goes
// inert: every subsequent append fails fast and the sink is never invoked
// again.
class BufferedWriter {
public:
    static constexpr std::size_t kFlushThreshold = 64 * 1024;

    explicit BufferedWriter(const ByteSink& sink) : m_sink(sink) {
        m_buffer.reserve(kFlushThreshold);
    }

    [[nodiscard]] bool append(std::string_view chunk) {
        if (m_aborted) {
            return false;
        }
        m_buffer += chunk;
        if (m_buffer.size() >= kFlushThreshold) {
            return flush();
        }
        return true;
    }

    [[nodiscard]] bool append(char c) {
        return append(std::string_view(&c, 1));
    }

    [[nodiscard]] bool appendRepeated(char c, std::size_t count) {
        if (m_aborted) {
            return false;
        }
        m_buffer.append(count, c);
        if (m_buffer.size() >= kFlushThreshold) {
            return flush();
        }
        return true;
    }

    // Deliver any buffered bytes to the sink.
    [[nodiscard]] bool flush() {
        if (m_aborted) {
            return false;
        }
        if (m_buffer.empty()) {
            return true;
        }
        if (!m_sink(m_buffer)) {
            m_aborted = true;
            return false;
        }
        m_buffer.clear();
        return true;
    }

    [[nodiscard]] bool aborted() const { return m_aborted; }

private:
    const ByteSink& m_sink;
    std::string m_buffer;
    bool m_aborted = false;
};

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

// Ported from json_exporter.cpp serializeNode; must produce byte-identical
// output (the string API is now a wrapper over this).
bool serializeJsonNode(NodeView node,
                       const JsonExportOptions& options,
                       int indentWidth,
                       int depth,
                       BufferedWriter& out) {
    const bool pretty = options.mode == IndentMode::PrettyPrint;

    switch (node.type()) {
        case NodeType::Object: {
            const std::size_t count = node.childCount();
            if (count == 0) {
                return out.append("{}");
            }

            if (!out.append('{')) return false;
            if (pretty && !out.append('\n')) return false;

            for (std::size_t i = 0; i < count; ++i) {
                NodeView child = node.child(i);
                if (pretty &&
                    !out.appendRepeated(' ', static_cast<std::size_t>((depth + 1) * indentWidth))) {
                    return false;
                }
                if (!out.append(escapeJsonString(child.key()))) return false;
                if (!out.append(pretty ? std::string_view(": ") : std::string_view(":"))) {
                    return false;
                }
                if (!serializeJsonNode(child, options, indentWidth, depth + 1, out)) {
                    return false;
                }
                if (i + 1 < count && !out.append(',')) return false;
                if (pretty && !out.append('\n')) return false;
            }

            if (pretty &&
                !out.appendRepeated(' ', static_cast<std::size_t>(depth * indentWidth))) {
                return false;
            }
            return out.append('}');
        }

        case NodeType::Array: {
            const std::size_t count = node.childCount();
            if (count == 0) {
                return out.append("[]");
            }

            if (!out.append('[')) return false;
            if (pretty && !out.append('\n')) return false;

            for (std::size_t i = 0; i < count; ++i) {
                if (pretty &&
                    !out.appendRepeated(' ', static_cast<std::size_t>((depth + 1) * indentWidth))) {
                    return false;
                }
                if (!serializeJsonNode(node.child(i), options, indentWidth, depth + 1, out)) {
                    return false;
                }
                if (i + 1 < count && !out.append(',')) return false;
                if (pretty && !out.append('\n')) return false;
            }

            if (pretty &&
                !out.appendRepeated(' ', static_cast<std::size_t>(depth * indentWidth))) {
                return false;
            }
            return out.append(']');
        }

        case NodeType::String:
            return out.append(escapeJsonString(node.value()));

        case NodeType::Number:
            return out.append(node.value());

        case NodeType::Boolean:
            return out.append(node.value());  // "true" or "false"

        case NodeType::Null:
            return out.append("null");
    }
    return true;  // unreachable, satisfies compiler
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

// Escape a CSV cell value per RFC 4180 (ported from csv_exporter.cpp).
//
// When guardFormula is set, neutralize formula injection: cells beginning
// with '=', '+', '-', '@', tab, or CR are interpreted as formulas by
// Excel/LibreOffice. Values originating from untrusted JSON strings get
// prefixed with a single quote (the standard neutralization) and
// force-quoted. Numeric/boolean/null cells are exported unguarded so
// negative numbers stay numeric.
auto escapeCsvCell(std::string_view value, bool guardFormula = true) -> std::string {
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
        return std::string(value);
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

// Serialize a node to compact single-line JSON (for embedding in CSV cells).
void serializeCompactJson(NodeView node, std::string& out) {
    switch (node.type()) {
        case NodeType::Object: {
            out += '{';
            const std::size_t count = node.childCount();
            for (std::size_t i = 0; i < count; ++i) {
                if (i > 0) out += ',';
                NodeView child = node.child(i);
                out += escapeJsonString(child.key());
                out += ':';
                serializeCompactJson(child, out);
            }
            out += '}';
            return;
        }
        case NodeType::Array: {
            out += '[';
            const std::size_t count = node.childCount();
            for (std::size_t i = 0; i < count; ++i) {
                if (i > 0) out += ',';
                serializeCompactJson(node.child(i), out);
            }
            out += ']';
            return;
        }
        case NodeType::String:
            out += escapeJsonString(node.value());
            return;
        case NodeType::Number:
            out += node.value();
            return;
        case NodeType::Boolean:
            out += node.value();
            return;
        case NodeType::Null:
            out += "null";
            return;
    }
}

// Serialize a field value for a CSV cell.
// Scalars produce their plain value; nested objects/arrays produce compact JSON.
auto serializeNodeValue(NodeView node) -> std::string {
    switch (node.type()) {
        case NodeType::String:
        case NodeType::Number:
        case NodeType::Boolean:
            return std::string(node.value());
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

// ---------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------

// Ported from xml_exporter.cpp serializeNode; must produce byte-identical
// output. Preserves the control-character substitution done by
// escapeXmlText (U+FFFD for chars illegal in XML 1.0).
bool serializeXmlNode(NodeView node, const std::string& elementName,
                      int indent, BufferedWriter& out) {
    const auto indentLen = static_cast<std::size_t>(indent * 2);
    std::string sanitized = sanitizeXmlName(elementName);

    switch (node.type()) {
        case NodeType::Object: {
            if (!out.appendRepeated(' ', indentLen)) return false;
            if (!out.append('<') || !out.append(sanitized) || !out.append(">\n")) {
                return false;
            }
            const std::size_t count = node.childCount();
            for (std::size_t i = 0; i < count; ++i) {
                NodeView child = node.child(i);
                std::string childName = child.key().empty()
                    ? std::string("item") : std::string(child.key());
                if (!serializeXmlNode(child, childName, indent + 1, out)) {
                    return false;
                }
            }
            if (!out.appendRepeated(' ', indentLen)) return false;
            return out.append("</") && out.append(sanitized) && out.append(">\n");
        }
        case NodeType::Array: {
            if (!out.appendRepeated(' ', indentLen)) return false;
            if (!out.append('<') || !out.append(sanitized) || !out.append(">\n")) {
                return false;
            }
            const std::size_t count = node.childCount();
            for (std::size_t i = 0; i < count; ++i) {
                std::string itemName = "item_" + std::to_string(i);
                if (!serializeXmlNode(node.child(i), itemName, indent + 1, out)) {
                    return false;
                }
            }
            if (!out.appendRepeated(' ', indentLen)) return false;
            return out.append("</") && out.append(sanitized) && out.append(">\n");
        }
        case NodeType::String:
        case NodeType::Number:
            return out.appendRepeated(' ', indentLen) &&
                   out.append('<') && out.append(sanitized) && out.append('>') &&
                   out.append(escapeXmlText(std::string(node.value()))) &&
                   out.append("</") && out.append(sanitized) && out.append(">\n");
        case NodeType::Boolean:
            return out.appendRepeated(' ', indentLen) &&
                   out.append('<') && out.append(sanitized) && out.append('>') &&
                   out.append(node.value()) &&
                   out.append("</") && out.append(sanitized) && out.append(">\n");
        case NodeType::Null:
            return out.appendRepeated(' ', indentLen) &&
                   out.append('<') && out.append(sanitized) && out.append("/>\n");
    }
    return true;  // unreachable
}

auto finish(BufferedWriter& out) -> StreamExportResult {
    if (out.aborted() || !out.flush()) {
        return StreamExportResult{false, kAbortedError};
    }
    return StreamExportResult{true, {}};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

auto exportJsonStream(NodeView root, const ByteSink& sink,
                      JsonExportOptions opts) -> StreamExportResult {
    BufferedWriter out(sink);
    const int indentWidth = std::clamp(opts.indentWidth, 1, 8);
    if (!serializeJsonNode(root, opts, indentWidth, 0, out)) {
        return StreamExportResult{false, kAbortedError};
    }
    if (opts.trailingNewline && !out.append('\n')) {
        return StreamExportResult{false, kAbortedError};
    }
    return finish(out);
}

auto exportCsvStream(NodeView root, const ByteSink& sink) -> StreamExportResult {
    // Validate: must be an array of objects (same messages as exportCsv).
    if (root.type() != NodeType::Array) {
        if (root.type() == NodeType::Object) {
            return StreamExportResult{false,
                "Selected node is a plain object, not an array of objects. CSV export requires an array of objects."};
        }
        return StreamExportResult{false,
            "Selected node is a scalar value, not an array of objects. CSV export requires an array of objects."};
    }

    const std::size_t rowCount = root.childCount();
    if (rowCount > 0) {
        bool hasObject = false;
        bool hasNonObject = false;
        for (std::size_t i = 0; i < rowCount; ++i) {
            if (root.child(i).type() == NodeType::Object) {
                hasObject = true;
            } else {
                hasNonObject = true;
            }
        }

        if (hasNonObject && hasObject) {
            return StreamExportResult{false,
                "Selected array contains a mix of objects and non-object values. CSV export requires an array where all elements are objects."};
        }
        if (hasNonObject) {
            return StreamExportResult{false,
                "Selected array contains non-object values. CSV export requires an array of objects."};
        }
    }

    // Compute column headers (union of all keys, first-seen order) and a
    // header -> column map so each row is filled in a single pass over its
    // fields instead of a per-header linear scan.
    // string_views point at node-owned key storage, which outlives the export.
    std::vector<std::string_view> headers;
    std::unordered_map<std::string_view, std::size_t> headerIndex;
    for (std::size_t r = 0; r < rowCount; ++r) {
        NodeView obj = root.child(r);
        const std::size_t fields = obj.childCount();
        for (std::size_t f = 0; f < fields; ++f) {
            std::string_view key = obj.child(f).key();
            if (headerIndex.emplace(key, headers.size()).second) {
                headers.push_back(key);
            }
        }
    }

    BufferedWriter out(sink);

    // Header row
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i > 0 && !out.append(',')) {
            return StreamExportResult{false, kAbortedError};
        }
        if (!out.append(escapeCsvCell(headers[i]))) {
            return StreamExportResult{false, kAbortedError};
        }
    }
    if (!out.append("\r\n")) {
        return StreamExportResult{false, kAbortedError};
    }

    // Data rows: fill a cell vector in one pass over the object's fields.
    struct Cell {
        std::string text;
        bool textual = false;
        bool present = false;
    };
    std::vector<Cell> cells(headers.size());

    for (std::size_t r = 0; r < rowCount; ++r) {
        for (auto& cell : cells) {
            cell.present = false;
        }

        NodeView obj = root.child(r);
        const std::size_t fields = obj.childCount();
        for (std::size_t f = 0; f < fields; ++f) {
            NodeView field = obj.child(f);
            auto it = headerIndex.find(field.key());
            if (it == headerIndex.end()) {
                continue;  // cannot happen: headers are the union of all keys
            }
            Cell& cell = cells[it->second];
            if (cell.present) {
                continue;  // duplicate key in this object: first occurrence wins
            }
            cell.text = serializeNodeValue(field);
            cell.textual = field.type() != NodeType::Number &&
                           field.type() != NodeType::Boolean &&
                           field.type() != NodeType::Null;
            cell.present = true;
        }

        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i > 0 && !out.append(',')) {
                return StreamExportResult{false, kAbortedError};
            }
            if (cells[i].present &&
                !out.append(escapeCsvCell(cells[i].text, cells[i].textual))) {
                return StreamExportResult{false, kAbortedError};
            }
        }
        if (!out.append("\r\n")) {
            return StreamExportResult{false, kAbortedError};
        }
    }

    return finish(out);
}

auto exportXmlStream(NodeView root, const ByteSink& sink,
                     const std::string& rootElementName) -> StreamExportResult {
    BufferedWriter out(sink);
    if (!out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")) {
        return StreamExportResult{false, kAbortedError};
    }
    if (!serializeXmlNode(root, rootElementName, 0, out)) {
        return StreamExportResult{false, kAbortedError};
    }
    return finish(out);
}

} // namespace jsontitan::core
