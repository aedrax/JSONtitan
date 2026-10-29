#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

// Find & Replace over string values and/or object keys. Matching follows
// the search bar: substring (ASCII case folding unless caseSensitive) or an
// ECMAScript regex, whose replacement may use $1, $&, ... Numbers, booleans
// and null are never touched, so a replacement cannot produce invalid JSON.
struct ReplaceOptions {
    std::string pattern;
    std::string replacement;
    bool regex = false;
    bool caseSensitive = false;
    bool inValues = true;  // string values
    bool inKeys = false;   // object member names
};

struct ReplaceResult {
    // The new root, sharing every unchanged subtree with the input; the
    // input root itself when nothing changed.
    std::shared_ptr<const JsonNode> root;
    std::size_t valuesChanged = 0;
    std::size_t keysChanged = 0;
    // Key renames refused because the new key already exists among the
    // member's siblings (a JSON object cannot hold it twice), or because a
    // regex match split a multi-byte character.
    std::size_t keysSkipped = 0;
    // Value replacements refused because a regex match split a multi-byte
    // character (the result would not be valid UTF-8).
    std::size_t valuesSkipped = 0;
    std::optional<std::string> error;  // empty pattern or invalid regex
};

auto replaceAll(std::shared_ptr<const JsonNode> root, const ReplaceOptions& options)
    -> ReplaceResult;

} // namespace jsontitan::core
