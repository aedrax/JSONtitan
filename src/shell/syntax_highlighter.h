#pragma once

#include <QColor>
#include <QTextEdit>
#include <array>

#include "core/token_emitter.h"

namespace jsontitan::shell {

struct SyntaxTheme {
    QColor keyColor;
    QColor stringColor;
    QColor numberColor;
    QColor booleanColor;
    QColor nullColor;
    QColor defaultColor;                  // Colon, Comma, Whitespace
    std::array<QColor, 6> bracePalette;   // Cycling brace colors
};

auto catppuccinMochaTheme() -> SyntaxTheme;  // dark
auto catppuccinLatteTheme() -> SyntaxTheme;  // light

void renderHighlighted(QTextEdit* editor,
                       const jsontitan::core::TokenEmitResult& result,
                       const SyntaxTheme& theme);

} // namespace jsontitan::shell
