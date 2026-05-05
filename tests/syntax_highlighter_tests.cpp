// ---------------------------------------------------------------------------
// Unit Tests for Syntax_Highlighter
// Task 3.5: Validate Catppuccin Mocha theme colors, distinctness, contrast,
// and truncation indicator rendering.
// Requirements: 2.1, 2.2, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 4.1, 4.2, 4.3
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QTextEdit>

#include <chrono>
#include <cmath>
#include <vector>

#include "core/json_node.h"
#include "core/pretty_printer.h"
#include "core/token_emitter.h"
#include "shell/syntax_highlighter.h"

using namespace jsontitan::core;
using namespace jsontitan::shell;

// ---------------------------------------------------------------------------
// Helper: WCAG relative luminance and contrast ratio
// ---------------------------------------------------------------------------

namespace {

// Linearize an sRGB channel value (0.0–1.0) to linear RGB
double linearize(double channel) {
    if (channel <= 0.03928) {
        return channel / 12.92;
    }
    return std::pow((channel + 0.055) / 1.055, 2.4);
}

// Compute WCAG relative luminance from a QColor
double relativeLuminance(const QColor& color) {
    double r = linearize(color.redF());
    double g = linearize(color.greenF());
    double b = linearize(color.blueF());
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

// Compute WCAG contrast ratio between two colors
double contrastRatio(const QColor& fg, const QColor& bg) {
    double l1 = relativeLuminance(fg);
    double l2 = relativeLuminance(bg);
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05) / (l2 + 0.05);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: Catppuccin Mocha palette values match expected hex codes
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterTheme, PaletteValuesMatchExpectedHexCodes) {
    auto theme = catppuccinMochaTheme();

    // Element type colors
    EXPECT_EQ(theme.keyColor, QColor("#89b4fa"));
    EXPECT_EQ(theme.stringColor, QColor("#a6e3a1"));
    EXPECT_EQ(theme.numberColor, QColor("#fab387"));
    EXPECT_EQ(theme.booleanColor, QColor("#cba6f7"));
    EXPECT_EQ(theme.nullColor, QColor("#f38ba8"));
    EXPECT_EQ(theme.defaultColor, QColor("#cdd6f4"));

    // Brace palette colors
    EXPECT_EQ(theme.bracePalette[0], QColor("#f38ba8"));  // Red
    EXPECT_EQ(theme.bracePalette[1], QColor("#fab387"));  // Peach
    EXPECT_EQ(theme.bracePalette[2], QColor("#f9e2af"));  // Yellow
    EXPECT_EQ(theme.bracePalette[3], QColor("#a6e3a1"));  // Green
    EXPECT_EQ(theme.bracePalette[4], QColor("#74c7ec"));  // Sapphire
    EXPECT_EQ(theme.bracePalette[5], QColor("#b4befe"));  // Lavender
}

// ---------------------------------------------------------------------------
// Test 2: All theme colors are distinct within their groups
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterTheme, ElementColorsAreDistinct) {
    auto theme = catppuccinMochaTheme();

    // The 6 element type colors must all be distinct from each other
    std::vector<QColor> elementColors = {
        theme.keyColor,
        theme.stringColor,
        theme.numberColor,
        theme.booleanColor,
        theme.nullColor,
        theme.defaultColor,
    };

    for (size_t i = 0; i < elementColors.size(); ++i) {
        for (size_t j = i + 1; j < elementColors.size(); ++j) {
            EXPECT_NE(elementColors[i], elementColors[j])
                << "Element colors at index " << i << " and " << j << " are identical";
        }
    }
}

TEST(SyntaxHighlighterTheme, BracePaletteColorsAreDistinct) {
    auto theme = catppuccinMochaTheme();

    // The 6 brace palette colors must all be distinct from each other
    for (size_t i = 0; i < theme.bracePalette.size(); ++i) {
        for (size_t j = i + 1; j < theme.bracePalette.size(); ++j) {
            EXPECT_NE(theme.bracePalette[i], theme.bracePalette[j])
                << "Brace palette colors at index " << i << " and " << j << " are identical";
        }
    }
}

// ---------------------------------------------------------------------------
// Test 3: All theme colors meet minimum contrast ratio against #313244
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterTheme, AllColorsHaveSufficientContrastAgainstBackground) {
    auto theme = catppuccinMochaTheme();
    const QColor background("#313244");
    constexpr double minContrast = 4.5;

    // Element type colors
    EXPECT_GE(contrastRatio(theme.keyColor, background), minContrast)
        << "keyColor fails contrast check";
    EXPECT_GE(contrastRatio(theme.stringColor, background), minContrast)
        << "stringColor fails contrast check";
    EXPECT_GE(contrastRatio(theme.numberColor, background), minContrast)
        << "numberColor fails contrast check";
    EXPECT_GE(contrastRatio(theme.booleanColor, background), minContrast)
        << "booleanColor fails contrast check";
    EXPECT_GE(contrastRatio(theme.nullColor, background), minContrast)
        << "nullColor fails contrast check";
    EXPECT_GE(contrastRatio(theme.defaultColor, background), minContrast)
        << "defaultColor fails contrast check";

    // Brace palette colors
    for (size_t i = 0; i < theme.bracePalette.size(); ++i) {
        EXPECT_GE(contrastRatio(theme.bracePalette[i], background), minContrast)
            << "bracePalette[" << i << "] fails contrast check";
    }
}

// ---------------------------------------------------------------------------
// Test 4: Truncation indicator is appended in default color
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterRender, TruncationIndicatorAppendedInDefaultColor) {
    auto theme = catppuccinMochaTheme();

    // Build a TokenEmitResult with truncated=true and a few tokens
    TokenEmitResult result;
    result.truncated = true;
    result.tokens.push_back(Token{TokenType::BraceOpen, "{", 0});
    result.tokens.push_back(Token{TokenType::Key, "\"key\"", 0});
    result.tokens.push_back(Token{TokenType::Colon, ": ", 0});
    result.tokens.push_back(Token{TokenType::StringValue, "\"value\"", 0});

    QTextEdit editor;
    renderHighlighted(&editor, result, theme);

    // Verify the text ends with the truncation indicator
    QString text = editor.toPlainText();
    EXPECT_TRUE(text.endsWith("\n\n... (output truncated)"))
        << "Text does not end with truncation indicator. Got: "
        << text.toStdString();

    // Verify the truncation indicator is rendered in the default color.
    // Move cursor to the start of the truncation text and check its format.
    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::End);
    // Move back into the truncation indicator text
    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 5);
    QTextCharFormat format = cursor.charFormat();
    EXPECT_EQ(format.foreground().color(), theme.defaultColor)
        << "Truncation indicator is not in default color";
}

// ===========================================================================
// Integration Tests for Highlighted Rendering (Task 5.3)
// Requirements: 5.1, 4.3
// ===========================================================================

// ---------------------------------------------------------------------------
// Integration Test 1: Selecting a tree node produces highlighted output
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterIntegration, TreeNodeProducesHighlightedOutput) {
    auto theme = catppuccinMochaTheme();

    // Build a simple JSON object: {"name": "Alice", "age": 30, "active": true}
    auto nameNode = JsonNode::makeString("name", "Alice");
    auto ageNode = JsonNode::makeNumber("age", "30");
    auto activeNode = JsonNode::makeBool("active", true);

    auto root = JsonNode::makeObject("", {nameNode, ageNode, activeNode});

    // Run the full pipeline: emitTokens → renderHighlighted
    auto tokenResult = emitTokens(*root);

    QTextEdit editor;
    renderHighlighted(&editor, tokenResult, theme);

    // Verify the plain text matches prettyPrint output
    std::string expectedText = prettyPrint(*root);
    QString actualText = editor.toPlainText();
    EXPECT_EQ(actualText.toStdString(), expectedText)
        << "Highlighted output text does not match prettyPrint output";

    // Verify that formatting is applied (colors are set on different parts)
    QTextCursor cursor(editor.document());

    // Move to the start and check the first character (should be '{' with brace color)
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    QColor braceColor = cursor.charFormat().foreground().color();
    EXPECT_EQ(braceColor, theme.bracePalette[0])
        << "Opening brace should have depth-0 palette color";

    // Find a key token - look for "name" in the text
    int namePos = actualText.indexOf("\"name\"");
    ASSERT_GE(namePos, 0) << "Could not find key 'name' in output";
    cursor.setPosition(namePos + 1);  // Move into the key text
    QColor keyColor = cursor.charFormat().foreground().color();
    EXPECT_EQ(keyColor, theme.keyColor)
        << "Key token should have key color";

    // Find a string value - look for "Alice"
    int alicePos = actualText.indexOf("\"Alice\"");
    ASSERT_GE(alicePos, 0) << "Could not find value 'Alice' in output";
    cursor.setPosition(alicePos + 1);  // Move into the string value text
    QColor stringColor = cursor.charFormat().foreground().color();
    EXPECT_EQ(stringColor, theme.stringColor)
        << "String value token should have string color";

    // Find a number value - look for "30"
    int numPos = actualText.indexOf("30");
    ASSERT_GE(numPos, 0) << "Could not find number '30' in output";
    cursor.setPosition(numPos + 1);  // Move into the number text
    QColor numColor = cursor.charFormat().foreground().color();
    EXPECT_EQ(numColor, theme.numberColor)
        << "Number token should have number color";

    // Verify different token types have different colors
    EXPECT_NE(keyColor, stringColor) << "Key and string colors should differ";
    EXPECT_NE(keyColor, numColor) << "Key and number colors should differ";
    EXPECT_NE(stringColor, numColor) << "String and number colors should differ";
}

// ---------------------------------------------------------------------------
// Integration Test 2: Large node renders within performance budget
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterIntegration, LargeNodeRendersWithinPerformanceBudget) {
    auto theme = catppuccinMochaTheme();

    // Build a large JSON object with many keys and string values to approach 64 KB
    // Each entry like: "key_XXXX": "value_with_padding_XXXX..." is ~50-60 chars
    // We need ~1200 entries to approach 64 KB of pretty-printed output
    std::vector<std::shared_ptr<const JsonNode>> children;
    children.reserve(1200);
    for (int i = 0; i < 1200; ++i) {
        std::string key = "key_" + std::to_string(i);
        // Create a value string long enough to generate substantial output
        std::string value = "value_padding_" + std::to_string(i) + "_abcdefghijklmnop";
        children.push_back(JsonNode::makeString(std::move(key), std::move(value)));
    }
    auto root = JsonNode::makeObject("", std::move(children));

    // Use maxOutputSize near 64 KB
    PrettyPrintOptions opts;
    opts.maxOutputSize = 65536;

    // Time the full pipeline
    auto startTime = std::chrono::steady_clock::now();

    auto tokenResult = emitTokens(*root, opts);
    QTextEdit editor;
    renderHighlighted(&editor, tokenResult, theme);

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Assert it completes within 200ms (generous margin to avoid flaky CI)
    EXPECT_LE(elapsed.count(), 200)
        << "Rendering took " << elapsed.count() << "ms, exceeding 200ms budget";

    // Verify output was actually produced
    QString text = editor.toPlainText();
    EXPECT_GT(text.size(), 1000)
        << "Expected substantial output from large node";
}

// ---------------------------------------------------------------------------
// Integration Test 3: Truncated output shows indicator without artifacts
// ---------------------------------------------------------------------------

TEST(SyntaxHighlighterIntegration, TruncatedOutputShowsIndicatorWithoutArtifacts) {
    auto theme = catppuccinMochaTheme();

    // Build a JSON tree that would exceed 64 KB when pretty-printed
    std::vector<std::shared_ptr<const JsonNode>> children;
    for (int i = 0; i < 500; ++i) {
        std::string key = "item_" + std::to_string(i);
        std::string value = "this_is_a_long_value_string_for_testing_" + std::to_string(i);
        children.push_back(JsonNode::makeString(std::move(key), std::move(value)));
    }
    auto root = JsonNode::makeObject("", std::move(children));

    // Use a very small maxOutputSize to force truncation
    PrettyPrintOptions opts;
    opts.maxOutputSize = 100;

    auto tokenResult = emitTokens(*root, opts);

    // Verify truncation was triggered
    ASSERT_TRUE(tokenResult.truncated)
        << "Expected truncation with maxOutputSize=100";

    QTextEdit editor;
    renderHighlighted(&editor, tokenResult, theme);

    QString text = editor.toPlainText();

    // Verify the plain text ends with the truncation indicator
    EXPECT_TRUE(text.endsWith("\n\n... (output truncated)"))
        << "Text should end with truncation indicator. Got: "
        << text.toStdString();

    // Verify the truncation indicator text is in default color (no highlighting artifacts)
    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::End);
    // Move back into the truncation indicator text "... (output truncated)"
    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 10);
    QTextCharFormat truncFormat = cursor.charFormat();
    EXPECT_EQ(truncFormat.foreground().color(), theme.defaultColor)
        << "Truncation indicator should be in default color (no highlighting artifacts)";

    // Verify the text before the indicator is valid (not cut mid-character)
    // The content before "\n\n... (output truncated)" should be valid token text
    int indicatorPos = text.indexOf("\n\n... (output truncated)");
    ASSERT_GT(indicatorPos, 0) << "Truncation indicator not found in output";

    QString contentBeforeIndicator = text.left(indicatorPos);
    EXPECT_FALSE(contentBeforeIndicator.isEmpty())
        << "There should be content before the truncation indicator";

    // Verify the content before indicator matches concatenated token texts
    std::string concatenatedTokens;
    for (const auto& token : tokenResult.tokens) {
        concatenatedTokens += token.text;
    }
    EXPECT_EQ(contentBeforeIndicator.toStdString(), concatenatedTokens)
        << "Content before indicator should match concatenated token texts";
}

// ---------------------------------------------------------------------------
// Custom main: GTest needs a QApplication for QTextEdit widget tests
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
