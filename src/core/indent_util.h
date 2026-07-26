#pragma once

#include <algorithm>

namespace jsontitan::core {

// Clamp a caller-supplied indent width to a sane range before it is
// multiplied by nesting depth to build per-line indent strings. Without
// this, a hostile or buggy indentWidth (negative, or huge) produces
// undefined size_t casts or pathological memory use.
// json_exporter applies its own [1, 8] clamp; printers accept 0 (flat).
inline auto clampIndentWidth(int width) -> int {
    return std::clamp(width, 0, 8);
}

} // namespace jsontitan::core
