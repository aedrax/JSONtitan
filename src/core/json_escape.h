#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace jsontitan::core {

// Escape a string value for JSON output per RFC 8259 Section 7, including
// the surrounding double quotes.
// Preserves multi-byte UTF-8 sequences as-is; escapes control characters
// below 0x20 as \uXXXX (with the usual short forms for \n, \t, ...).
// Single source of truth: previously copy-pasted in json_exporter,
// csv_exporter, pretty_printer, and token_emitter.
inline auto escapeJsonString(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    // Control character — emit \uXXXX
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Normal ASCII or multi-byte UTF-8 byte — pass through
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

} // namespace jsontitan::core
