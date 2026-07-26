#include "shell/syntax_highlighter.h"

#include <QTextCharFormat>
#include <QTextCursor>

namespace jsontitan::shell {

auto catppuccinMochaTheme() -> SyntaxTheme {
    return SyntaxTheme{
        .keyColor = QColor("#89b4fa"),      // Blue
        .stringColor = QColor("#a6e3a1"),   // Green
        .numberColor = QColor("#fab387"),   // Peach
        .booleanColor = QColor("#cba6f7"), // Mauve
        .nullColor = QColor("#f38ba8"),     // Red
        .defaultColor = QColor("#cdd6f4"), // Text
        .bracePalette = {{
            QColor("#f38ba8"),  // Red     (depth 0)
            QColor("#fab387"),  // Peach   (depth 1)
            QColor("#f9e2af"),  // Yellow  (depth 2)
            QColor("#a6e3a1"),  // Green   (depth 3)
            QColor("#74c7ec"),  // Sapphire(depth 4)
            QColor("#b4befe"), // Lavender(depth 5)
        }},
    };
}

void renderHighlighted(QTextEdit* editor,
                       const jsontitan::core::TokenEmitResult& result,
                       const SyntaxTheme& theme) {
    if (!editor) {
        return;
    }

    editor->clear();

    QTextCursor cursor(editor->document());
    // Batch all insertions into one edit block: without it every token
    // triggers its own document-change signal and layout pass, which stalls
    // the UI for outputs with thousands of tokens.
    cursor.beginEditBlock();

    for (const auto& token : result.tokens) {
        QTextCharFormat format;
        // Pre-initialize so every enumerator can be an explicit case below:
        // a default: label would suppress -Wswitch for future TokenTypes.
        QColor color = theme.defaultColor;

        switch (token.type) {
            case core::TokenType::Key:
                color = theme.keyColor;
                break;
            case core::TokenType::StringValue:
                color = theme.stringColor;
                break;
            case core::TokenType::Number:
                color = theme.numberColor;
                break;
            case core::TokenType::Boolean:
                color = theme.booleanColor;
                break;
            case core::TokenType::Null:
                color = theme.nullColor;
                break;
            case core::TokenType::BraceOpen:
            case core::TokenType::BraceClose:
            case core::TokenType::BracketOpen:
            case core::TokenType::BracketClose:
                color = theme.bracePalette[
                    static_cast<size_t>(token.depth) % theme.bracePalette.size()];
                break;
            case core::TokenType::Colon:
            case core::TokenType::Comma:
            case core::TokenType::Whitespace:
                color = theme.defaultColor;
                break;
        }

        format.setForeground(color);
        cursor.insertText(QString::fromStdString(token.text), format);
    }

    if (result.truncated) {
        QTextCharFormat defaultFormat;
        defaultFormat.setForeground(theme.defaultColor);
        cursor.insertText(QStringLiteral("\n\n... (output truncated)"), defaultFormat);
    }

    cursor.endEditBlock();
}

} // namespace jsontitan::shell
