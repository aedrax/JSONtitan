// Tests for JSON Lines and JSONC support: extension detection, the in-place
// JSONC stripper, and the format-aware parseBuffer pipeline (no Qt).

#include <gtest/gtest.h>

#include <string>

#include "core/json_format.h"
#include "core/parse_orchestrator.h"

using namespace jsontitan::core;

namespace {

auto strip(std::string text) -> std::pair<std::string, JsoncStripResult> {
    auto result = stripJsonc(std::span<char>(text.data(), text.size()));
    return {text, result};
}

auto parse(std::string input, JsonFormat format, bool lenient = false)
    -> ArenaParseResult {
    ParseBufferOptions options;
    options.format = format;
    options.lenientFallback = lenient;
    return parseBuffer(std::move(input), options);
}

} // anonymous namespace

// --- formatForPath -----------------------------------------------------------

TEST(FormatForPath, RecognisesExtensionsCaseInsensitively) {
    EXPECT_EQ(formatForPath("/a/b.json"), JsonFormat::Json);
    EXPECT_EQ(formatForPath("/a/b.JSON"), JsonFormat::Json);
    EXPECT_EQ(formatForPath("settings.jsonc"), JsonFormat::JsonWithComments);
    EXPECT_EQ(formatForPath("log.jsonl"), JsonFormat::JsonLines);
    EXPECT_EQ(formatForPath("log.NDJSON"), JsonFormat::JsonLines);
}

TEST(FormatForPath, UnknownExtensionsHaveNoFormat) {
    EXPECT_EQ(formatForPath("data.txt"), std::nullopt);
    EXPECT_EQ(formatForPath("json"), std::nullopt);
    EXPECT_EQ(formatForPath("archive.json.gz"), std::nullopt);
    EXPECT_EQ(formatForPath(""), std::nullopt);
}

// --- stripJsonc --------------------------------------------------------------

TEST(StripJsonc, BlanksLineAndBlockCommentsKeepingLength) {
    const std::string input = "{ // note\n  \"a\": /* x\ny */ 1\n}";
    auto [out, r] = strip(input);
    EXPECT_EQ(out.size(), input.size());
    EXPECT_EQ(out, "{        \n  \"a\":     \n     1\n}");
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.hadComments);
    EXPECT_FALSE(r.error);
}

TEST(StripJsonc, LeavesCommentMarkersInsideStrings) {
    const std::string input = R"({"url": "http://x/*y*/", "q": "\"//"})";
    auto [out, r] = strip(input);
    EXPECT_EQ(out, input);
    EXPECT_FALSE(r.changed);
    EXPECT_FALSE(r.hadComments);
}

TEST(StripJsonc, EscapedBackslashEndsStringBeforeComment) {
    // The string is "\\" (one escaped backslash); the // after it is a comment.
    auto [out, r] = strip(R"(["\\"]//x)");
    EXPECT_EQ(out, R"(["\\"]   )");
    EXPECT_TRUE(r.hadComments);
}

TEST(StripJsonc, RemovesTrailingCommasOnly) {
    auto [out, r] = strip("[1, 2, ] {\"a\": true, /* c */ }");
    // ", /* c */ " is 10 bytes, all blanked.
    EXPECT_EQ(out, "[1, 2  ] {\"a\": true" + std::string(10, ' ') + "}");
    EXPECT_TRUE(r.changed);
}

TEST(StripJsonc, KeepsCommasThatDoNotFollowAValue) {
    for (const std::string input : {"[,]", "{\"a\":,}", "[1,,]", "{,}"}) {
        auto [out, r] = strip(input);
        EXPECT_EQ(out, input) << input;
        EXPECT_FALSE(r.changed) << input;
    }
}

TEST(StripJsonc, UnterminatedBlockCommentReportsOffset) {
    auto [out, r] = strip("{\"a\": 1 /* never closed");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->byteOffset, 8U);
}

TEST(StripJsonc, LoneSlashIsLeftForTheParser) {
    auto [out, r] = strip("[1 / 2]");
    EXPECT_EQ(out, "[1 / 2]");
    EXPECT_FALSE(r.changed);
}

// --- parseBuffer: JSONC ------------------------------------------------------

TEST(ParseJsonc, ParsesCommentsAndTrailingCommas) {
    auto r = parse("// header\n{\"a\": [1, 2,], /* b */ \"b\": null,}\n",
                   JsonFormat::JsonWithComments);
    ASSERT_TRUE(r.ok()) << r.error->description;
    EXPECT_EQ(r.format, JsonFormat::JsonWithComments);
    EXPECT_TRUE(r.hadComments);
    ASSERT_EQ(r.root->type, NodeType::Object);
    ASSERT_EQ(r.root->childCount, 2U);
    EXPECT_EQ(r.root->childAt(0)->childCount, 2U);
}

TEST(ParseJsonc, NoCommentsMeansNothingToLose) {
    auto r = parse("{\"a\": 1,}", JsonFormat::JsonWithComments);
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.hadComments);
}

TEST(ParseJsonc, UnterminatedCommentIsAnError) {
    auto r = parse("{} /* oops", JsonFormat::JsonWithComments);
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->byteOffset, 3U);
}

// --- parseBuffer: JSON Lines -------------------------------------------------

TEST(ParseJsonLines, EachLineBecomesARootArrayElement) {
    auto r = parse("{\"a\":1}\r\n\n  \n[2,3]\n\"s\"\n42", JsonFormat::JsonLines);
    ASSERT_TRUE(r.ok()) << r.error->description;
    EXPECT_EQ(r.format, JsonFormat::JsonLines);
    ASSERT_EQ(r.root->type, NodeType::Array);
    ASSERT_EQ(r.root->childCount, 4U);
    EXPECT_EQ(r.root->childAt(0)->type, NodeType::Object);
    EXPECT_EQ(r.root->childAt(1)->childCount, 2U);
    EXPECT_EQ(r.root->childAt(2)->valueView(), "s");
    EXPECT_EQ(r.root->childAt(3)->valueView(), "42");
    // root + object + its member + array + 2 items + string + number
    EXPECT_EQ(r.nodeCount, 8U);
}

TEST(ParseJsonLines, BlankOnlyInputIsAnEmptyArray) {
    auto r = parse("\n  \n", JsonFormat::JsonLines);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.root->childCount, 0U);
}

TEST(ParseJsonLines, ErrorNamesTheFailingLine) {
    auto r = parse("{\"a\":1}\n\n{\"b\":}\n{\"c\":3}", JsonFormat::JsonLines);
    ASSERT_TRUE(r.error);
    EXPECT_NE(r.error->description.find("Line 3"), std::string::npos)
        << r.error->description;
}

TEST(ParseJsonLines, RecordSpanningLinesIsAnError) {
    auto r = parse("{\"a\":\n1}", JsonFormat::JsonLines);
    ASSERT_TRUE(r.error);
    EXPECT_NE(r.error->description.find("Line 1"), std::string::npos);
}

// --- parseBuffer: lenient .json fallback -------------------------------------

TEST(ParseLenientJson, ValidJsonStaysStrict) {
    auto r = parse("{\"a\": 1}", JsonFormat::Json, true);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.format, JsonFormat::Json);
    EXPECT_FALSE(r.hadComments);
}

TEST(ParseLenientJson, FallsBackToJsonc) {
    auto r = parse("{\n  // tsconfig style\n  \"strict\": true,\n}", JsonFormat::Json, true);
    ASSERT_TRUE(r.ok()) << r.error->description;
    EXPECT_EQ(r.format, JsonFormat::JsonWithComments);
    EXPECT_TRUE(r.hadComments);
    EXPECT_EQ(r.root->childCount, 1U);
}

TEST(ParseLenientJson, FallsBackToJsonLines) {
    auto r = parse("{\"a\":1}\n{\"a\":2}\n", JsonFormat::Json, true);
    ASSERT_TRUE(r.ok()) << r.error->description;
    EXPECT_EQ(r.format, JsonFormat::JsonLines);
    EXPECT_EQ(r.root->childCount, 2U);
}

TEST(ParseLenientJson, KeepsStrictErrorWhenEveryAttemptFails) {
    auto strict = parse("{\"a\": }", JsonFormat::Json);
    auto lenient = parse("{\"a\": }", JsonFormat::Json, true);
    ASSERT_TRUE(strict.error);
    ASSERT_TRUE(lenient.error);
    EXPECT_EQ(lenient.error->description, strict.error->description);
}

TEST(ParseLenientJson, WhitespaceOnlyIsStillAnError) {
    auto r = parse(" \n \n", JsonFormat::Json, true);
    EXPECT_TRUE(r.error);
}

TEST(ParseLenientJson, OffByDefault) {
    auto r = parse("[1, 2,]", JsonFormat::Json);
    EXPECT_TRUE(r.error);
}
