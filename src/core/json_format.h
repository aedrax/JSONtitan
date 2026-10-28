#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "core/parser.h"

namespace jsontitan::core {

// On-disk flavours of JSON the loader understands. The in-memory tree is
// the same for all of them; the format decides how input is parsed and how
// Save writes the tree back.
enum class JsonFormat {
    Json,              // One RFC 8259 document
    JsonWithComments,  // JSONC: // and /* */ comments, trailing commas
    JsonLines          // JSONL / NDJSON: one document per line -> root array
};

// Format implied by the file extension (case-insensitive): .json, .jsonc,
// .jsonl and .ndjson. Returns std::nullopt for any other extension so the
// caller can choose its own default.
[[nodiscard]] auto formatForPath(std::string_view path) -> std::optional<JsonFormat>;

struct JsoncStripResult {
    bool changed = false;      // At least one byte was blanked
    bool hadComments = false;  // At least one comment was removed
    std::optional<ParseError> error = std::nullopt;
};

// Rewrites JSONC into strict JSON in place, without changing its length:
// comments become spaces (newlines are kept, so line numbers survive) and a
// comma directly before } or ] becomes a space. Byte offsets in later parse
// errors therefore still point at the original text. Anything else that is
// invalid is left for the JSON parser to report.
auto stripJsonc(std::span<char> text) -> JsoncStripResult;

} // namespace jsontitan::core
