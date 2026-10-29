// ---------------------------------------------------------------------------
// Find & Replace engine tests (unit + property-based)
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <clocale>
#include <memory>
#include <string>

#include "core/edit_engine.h"
#include "core/json_exporter.h"
#include "core/json_node.h"
#include "core/jsonpath.h"
#include "core/replace_engine.h"
#include "core/xml_exporter.h"

using namespace jsontitan::core;

namespace {

auto parse(const char* json) -> std::shared_ptr<const JsonNode> {
    auto parsed = parseValueInput(json);
    EXPECT_TRUE(parsed.has_value()) << json;
    return parsed ? *parsed : nullptr;
}

auto compact(const std::shared_ptr<const JsonNode>& node) -> std::string {
    return exportJson(*node, {.mode = IndentMode::Compact, .trailingNewline = false});
}

}  // namespace

TEST(ReplaceEngine, SubstringInStringValuesOnlyByDefault) {
    auto root = parse(R"({"cat": "cat and Cat", "n": 1, "list": ["concat", true]})");
    auto result = replaceAll(root, {.pattern = "cat", .replacement = "dog"});
    ASSERT_FALSE(result.error.has_value());
    // Case-insensitive by default; keys and numbers untouched.
    EXPECT_EQ(compact(result.root), R"({"cat":"dog and dog","n":1,"list":["condog",true]})");
    EXPECT_EQ(result.valuesChanged, 2u);
    EXPECT_EQ(result.keysChanged, 0u);
}

TEST(ReplaceEngine, CaseSensitiveAndSharing) {
    auto root = parse(R"({"a": "Cat cat", "b": {"c": "none"}})");
    auto result = replaceAll(root, {.pattern = "cat", .replacement = "x", .caseSensitive = true});
    EXPECT_EQ(compact(result.root), R"({"a":"Cat x","b":{"c":"none"}})");
    // The untouched subtree is shared, not copied.
    EXPECT_EQ(result.root->children[1].get(), root->children[1].get());
}

TEST(ReplaceEngine, RegexWithGroups) {
    auto root = parse(R"({"date": "2024-03-15", "other": "x"})");
    auto result = replaceAll(root, {.pattern = R"((\d{4})-(\d{2})-(\d{2}))",
                                    .replacement = "$3/$2/$1",
                                    .regex = true});
    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(compact(result.root), R"({"date":"15/03/2024","other":"x"})");

    auto bad = replaceAll(root, {.pattern = "(", .replacement = "", .regex = true});
    ASSERT_TRUE(bad.error.has_value());
    EXPECT_EQ(bad.root.get(), root.get());
}

TEST(ReplaceEngine, KeysWithCollisionsSkipped) {
    auto root = parse(R"({"old_a": 1, "old_b": 2, "b": 3, "nested": [{"old_c": 4}]})");
    auto result = replaceAll(root, {.pattern = "old_",
                                    .replacement = "",
                                    .caseSensitive = true,
                                    .inValues = false,
                                    .inKeys = true});
    // old_b -> b would duplicate "b", so it is skipped.
    EXPECT_EQ(compact(result.root), R"({"a":1,"old_b":2,"b":3,"nested":[{"c":4}]})");
    EXPECT_EQ(result.keysChanged, 2u);
    EXPECT_EQ(result.keysSkipped, 1u);

    // Two keys renamed onto the same new key: both are skipped.
    auto pair = parse(R"({"xa": 1, "ya": 2})");
    auto clash = replaceAll(pair, {.pattern = "^[xy]", .replacement = "", .regex = true,
                                   .inValues = false, .inKeys = true});
    EXPECT_EQ(compact(clash.root), R"({"xa":1,"ya":2})");
    EXPECT_EQ(clash.keysSkipped, 2u);
}

TEST(ReplaceEngine, NoMatchOrEmptyPattern) {
    auto root = parse(R"({"a": "b"})");
    auto none = replaceAll(root, {.pattern = "zzz", .replacement = "y"});
    EXPECT_EQ(none.root.get(), root.get());
    EXPECT_EQ(none.valuesChanged, 0u);

    auto empty = replaceAll(root, {.pattern = "", .replacement = "y"});
    ASSERT_TRUE(empty.error.has_value());
    EXPECT_EQ(empty.root.get(), root.get());
}

// Replacing a string that cannot occur with anything is the identity, and
// replacing X by X never changes content.
TEST(ReplaceEnginePBT, IdentityReplacementsKeepContent) {
    EXPECT_TRUE(rc::check("replaceAll is the identity when it cannot change anything", [] {
        const auto text = *rc::gen::container<std::string>(rc::gen::inRange('a', 'e'));
        auto root = parse(("{\"k\": \"" + text + "\", \"l\": [\"" + text + "x\"]}").c_str());
        RC_ASSERT(root != nullptr);
        const auto pattern = *rc::gen::nonEmpty(
            rc::gen::container<std::string>(rc::gen::inRange('a', 'f')));

        auto same = replaceAll(root, {.pattern = pattern, .replacement = pattern,
                                      .caseSensitive = true, .inKeys = true});
        RC_ASSERT(compact(same.root) == compact(root));

        auto absent = replaceAll(root, {.pattern = "#", .replacement = "!"});
        RC_ASSERT(absent.root.get() == root.get());
    }));
}

// Regression: a skipped rename keeps its old key, and another member could
// then be renamed onto it, producing a duplicate key.
TEST(ReplaceEngine, SkippedRenameKeepsItsKeyReserved) {
    auto root = parse(R"({"ax": 1, "axy": 2, "axyy": 3})");
    auto result = replaceAll(root, {.pattern = "xy",
                                    .replacement = "x",
                                    .caseSensitive = true,
                                    .inValues = false,
                                    .inKeys = true});
    // axy -> ax collides with "ax"; axy then stays, so axyy -> axy collides too.
    EXPECT_EQ(compact(result.root), R"({"ax":1,"axy":2,"axyy":3})");
    EXPECT_EQ(result.keysChanged, 0u);
    EXPECT_EQ(result.keysSkipped, 2u);
}

// Regression: std::regex matches bytes, so a match could cut a multi-byte
// character in half and the saved file was not valid UTF-8.
TEST(ReplaceEngine, RegexNeverSplitsACharacter) {
    // "ã" is C3 A3; the class [é] is the bytes C3 and A9, so it matches C3.
    auto root = parse("{\"k\\u00e3\": \"\\u00e3\", \"ok\": \"abc\"}");
    auto result = replaceAll(root, {.pattern = "[\xC3\xA9]",
                                    .replacement = "e",
                                    .regex = true,
                                    .caseSensitive = true,
                                    .inValues = true,
                                    .inKeys = true});
    EXPECT_EQ(result.root.get(), root.get());
    EXPECT_EQ(result.valuesSkipped, 1u);
    EXPECT_EQ(result.keysSkipped, 1u);

    // Whole characters still replace.
    auto whole = replaceAll(parse("[\"\\u00e3b\"]"), {.pattern = "\xC3\xA3",
                                                      .replacement = "a",
                                                      .regex = true});
    EXPECT_EQ(compact(whole.root), R"(["ab"])");

    // A split can also leave valid UTF-8: "." matches each byte of "é",
    // which turned "café" into five stars.
    auto cafe = parse("[\"caf\\u00e9\", \"tea\"]");
    auto dots = replaceAll(cafe, {.pattern = ".", .replacement = "*", .regex = true});
    EXPECT_EQ(compact(dots.root), R"(["café","***"])");
    EXPECT_EQ(dots.valuesSkipped, 1u);

    // The whole match can be whole characters while a capture group splits
    // one: "(.)(.)" matches both bytes of "é", and "$2$1" swapped them.
    auto swapped = parse("{\"\\u00e9\": \"\\u00e9\"}");
    auto groups = replaceAll(swapped, {.pattern = "(.)(.)",
                                       .replacement = "$2$1",
                                       .regex = true,
                                       .inValues = true,
                                       .inKeys = true});
    EXPECT_EQ(groups.root.get(), swapped.get());
    EXPECT_EQ(groups.valuesSkipped, 1u);
    EXPECT_EQ(groups.keysSkipped, 1u);
}

// Regression: case folding, XML names and path trimming used <cctype>, which
// follows the process locale. Qt sets it from the system, and in a UTF-8
// locale tolower(0xC3) is 0xE3 and isspace(0xA0) is true, so single bytes
// of multi-byte characters were folded, renamed or trimmed.
TEST(ReplaceEngine, TextHandlingIgnoresTheProcessLocale) {
    struct RestoreLocale {
        std::string previous = std::setlocale(LC_ALL, nullptr);
        ~RestoreLocale() { std::setlocale(LC_ALL, previous.c_str()); }
    } restore;
    if (!std::setlocale(LC_ALL, "en_US.UTF-8") && !std::setlocale(LC_ALL, "C.UTF-8")) {
        GTEST_SKIP() << "no UTF-8 locale installed";
    }

    // U+38C0 is E3 A3 80; folding C3 to E3 made "ã" (C3 A3) match inside it.
    auto cjk = parse("[\"\\u38c0\"]");
    auto replaced = replaceAll(cjk, {.pattern = "\xC3\xA3", .replacement = "a"});
    EXPECT_EQ(replaced.root.get(), cjk.get());

    EXPECT_EQ(sanitizeXmlName("caf\xC3\xA9"), "caf__");

    auto path = parseDefinitePath("$.\xC3\xA0");  // "à" ends in byte A0
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(std::get<std::string>(path->front()), "\xC3\xA0");
}
