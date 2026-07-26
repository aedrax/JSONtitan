// ---------------------------------------------------------------------------
// Tests for the streaming exporters (stream_export.h).
//
// - streamed output == string-API output for random trees (all 3 formats)
// - chunking: concatenation equals the whole, chunks stay near 64 KB
// - abort propagation: sink returning false stops the export immediately
// - arena-vs-JsonNode: the same document streams identical bytes from
//   either backing
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/csv_exporter.h"
#include "core/json_exporter.h"
#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"
#include "core/stream_export.h"
#include "core/xml_exporter.h"

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// Generators (following json_exporter_pbt.cpp patterns)
// ---------------------------------------------------------------------------

// Generate a safe string key (alphanumeric + underscore, non-empty)
rc::Gen<std::string> genSafeKey() {
    return rc::gen::exec([]() -> std::string {
        int len = *rc::gen::inRange(1, 10);
        std::string key;
        for (int i = 0; i < len; ++i) {
            int choice = *rc::gen::inRange(0, 36);
            if (choice < 26) {
                key += static_cast<char>('a' + choice);
            } else {
                key += static_cast<char>('0' + (choice - 26));
            }
        }
        return key;
    });
}

// Generate string values exercising CSV quoting, formula-injection guards,
// XML escaping, and JSON escaping.
rc::Gen<std::string> genStringValue() {
    return rc::gen::exec([]() -> std::string {
        int len = *rc::gen::inRange(0, 15);
        std::string val;
        for (int i = 0; i < len; ++i) {
            int choice = *rc::gen::inRange(0, 8);
            switch (choice) {
                case 0: val += ',';  break;               // CSV quoting
                case 1: val += '"';  break;               // CSV + JSON escaping
                case 2: val += '\n'; break;               // CSV + JSON escaping
                case 3: val += static_cast<char>(*rc::gen::inRange(1, 0x20)); break;
                case 4: val += *rc::gen::element<char>('=', '+', '-', '@'); break;
                case 5: val += '<';  break;               // XML escaping
                case 6: val += '&';  break;               // XML escaping
                default:
                    val += static_cast<char>(*rc::gen::inRange(0x20, 0x7F));
                    break;
            }
        }
        return val;
    });
}

// Generate a random JsonNode tree with configurable max depth.
rc::Gen<std::shared_ptr<const JsonNode>> genJsonNode(int maxDepth = 3) {
    return rc::gen::exec([maxDepth]() -> std::shared_ptr<const JsonNode> {
        int choice = (maxDepth <= 0) ? *rc::gen::inRange(2, 6) : *rc::gen::inRange(0, 6);
        switch (choice) {
            case 0: {
                int childCount = *rc::gen::inRange(0, 5);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < childCount; ++i) {
                    auto child = *genJsonNode(maxDepth - 1);
                    std::string key = *genSafeKey();
                    children.push_back(std::make_shared<JsonNode>(JsonNode{
                        child->type, key, child->value, child->children}));
                }
                return JsonNode::makeObject("", std::move(children));
            }
            case 1: {
                int childCount = *rc::gen::inRange(0, 5);
                std::vector<std::shared_ptr<const JsonNode>> children;
                for (int i = 0; i < childCount; ++i) {
                    children.push_back(*genJsonNode(maxDepth - 1));
                }
                return JsonNode::makeArray("", std::move(children));
            }
            case 2:
                return JsonNode::makeString("", *genStringValue());
            case 3:
                return JsonNode::makeNumber("", std::to_string(*rc::gen::inRange(-100000, 100000)));
            case 4:
                return JsonNode::makeBool("", *rc::gen::arbitrary<bool>());
            default:
                return JsonNode::makeNull("");
        }
    });
}

// Generate an array-of-objects tree (the shape CSV export accepts).
rc::Gen<std::shared_ptr<const JsonNode>> genCsvableTree() {
    return rc::gen::exec([]() -> std::shared_ptr<const JsonNode> {
        int rowCount = *rc::gen::inRange(0, 8);
        std::vector<std::shared_ptr<const JsonNode>> rows;
        for (int r = 0; r < rowCount; ++r) {
            int fieldCount = *rc::gen::inRange(0, 6);
            std::vector<std::shared_ptr<const JsonNode>> fields;
            for (int f = 0; f < fieldCount; ++f) {
                auto value = *genJsonNode(1);
                std::string key = *genSafeKey();
                fields.push_back(std::make_shared<JsonNode>(JsonNode{
                    value->type, key, value->value, value->children}));
            }
            rows.push_back(JsonNode::makeObject("", std::move(fields)));
        }
        return JsonNode::makeArray("", std::move(rows));
    });
}

// Collect all streamed chunks; optionally abort after acceptedCalls calls.
struct CollectingSink {
    std::vector<std::string> chunks;
    std::size_t callCount = 0;
    std::size_t acceptEvery = 0;   // 0 = accept all; N = reject on call N (1-based)

    ByteSink fn() {
        return [this](std::string_view chunk) {
            ++callCount;
            if (acceptEvery != 0 && callCount >= acceptEvery) {
                return false;
            }
            chunks.emplace_back(chunk);
            return true;
        };
    }

    [[nodiscard]] std::string concatenated() const {
        std::string all;
        for (const auto& c : chunks) {
            all += c;
        }
        return all;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Streamed output == string API output
// ---------------------------------------------------------------------------

TEST(StreamExportEquivalence, JsonStreamMatchesStringApi) {
    rc::check("exportJsonStream output equals exportJson for random trees",
        []() {
            auto node = *genJsonNode(3);
            JsonExportOptions opts;
            opts.mode = *rc::gen::arbitrary<bool>() ? IndentMode::PrettyPrint
                                                    : IndentMode::Compact;
            opts.indentWidth = *rc::gen::inRange(1, 9);
            opts.trailingNewline = *rc::gen::arbitrary<bool>();

            CollectingSink sink;
            auto result = exportJsonStream(NodeView(*node), sink.fn(), opts);

            RC_ASSERT(result.ok);
            RC_ASSERT(sink.concatenated() == exportJson(*node, opts));
        });
}

TEST(StreamExportEquivalence, CsvStreamMatchesStringApi) {
    rc::check("exportCsvStream output equals exportCsv for array-of-objects trees",
        []() {
            auto node = *genCsvableTree();

            CollectingSink sink;
            auto streamResult = exportCsvStream(NodeView(*node), sink.fn());
            auto stringResult = exportCsv(*node);

            RC_ASSERT(streamResult.ok);
            RC_ASSERT(std::holds_alternative<std::string>(stringResult));
            RC_ASSERT(sink.concatenated() == std::get<std::string>(stringResult));
        });
}

TEST(StreamExportEquivalence, CsvStreamMatchesStringApiErrors) {
    rc::check("exportCsvStream rejects the same trees with the same messages as exportCsv",
        []() {
            auto node = *genJsonNode(3);

            CollectingSink sink;
            auto streamResult = exportCsvStream(NodeView(*node), sink.fn());
            auto stringResult = exportCsv(*node);

            if (auto* err = std::get_if<CsvError>(&stringResult)) {
                RC_ASSERT(!streamResult.ok);
                RC_ASSERT(streamResult.error == err->description);
                // Validation errors are reported before any bytes are written.
                RC_ASSERT(sink.chunks.empty());
            } else {
                RC_ASSERT(streamResult.ok);
                RC_ASSERT(sink.concatenated() == std::get<std::string>(stringResult));
            }
        });
}

TEST(StreamExportEquivalence, XmlStreamMatchesStringApi) {
    rc::check("exportXmlStream output equals exportXml for random trees",
        []() {
            auto node = *genJsonNode(3);

            CollectingSink sink;
            auto result = exportXmlStream(NodeView(*node), sink.fn(), "root");

            RC_ASSERT(result.ok);
            RC_ASSERT(sink.concatenated() == exportXml(*node, "root"));
        });
}

// ---------------------------------------------------------------------------
// Chunking behavior
// ---------------------------------------------------------------------------

TEST(StreamExportChunking, LargeDocumentStreamsInBoundedChunks) {
    // Build an array of many objects so the pretty-printed JSON output is
    // several hundred KB — forcing multiple flushes.
    std::vector<std::shared_ptr<const JsonNode>> rows;
    for (int i = 0; i < 5000; ++i) {
        std::vector<std::shared_ptr<const JsonNode>> fields;
        fields.push_back(JsonNode::makeNumber("id", std::to_string(i)));
        fields.push_back(JsonNode::makeString("name", "item_" + std::to_string(i)));
        fields.push_back(JsonNode::makeBool("active", i % 2 == 0));
        rows.push_back(JsonNode::makeObject("", std::move(fields)));
    }
    auto root = JsonNode::makeArray("", std::move(rows));

    JsonExportOptions opts;  // PrettyPrint, indent 2, trailing newline
    CollectingSink sink;
    auto result = exportJsonStream(NodeView(*root), sink.fn(), opts);

    ASSERT_TRUE(result.ok);
    EXPECT_GE(sink.chunks.size(), 2u) << "expected multiple flushes for a large doc";

    // Every chunk stays near the 64 KB flush threshold: at most
    // threshold + the largest single token appended after crossing it.
    constexpr std::size_t kFlushThreshold = 64 * 1024;
    constexpr std::size_t kMaxTokenSlack = 4096;
    for (const auto& chunk : sink.chunks) {
        EXPECT_LE(chunk.size(), kFlushThreshold + kMaxTokenSlack);
    }

    // Concatenation equals the whole document.
    EXPECT_EQ(sink.concatenated(), exportJson(*root, opts));
}

// ---------------------------------------------------------------------------
// Abort propagation
// ---------------------------------------------------------------------------

TEST(StreamExportAbort, SinkRejectionStopsExportImmediately) {
    // Large enough to require several flushes if not aborted.
    std::vector<std::shared_ptr<const JsonNode>> rows;
    for (int i = 0; i < 20000; ++i) {
        rows.push_back(JsonNode::makeString("", "row_" + std::to_string(i)));
    }
    auto root = JsonNode::makeArray("", std::move(rows));

    for (std::size_t rejectOnCall : {std::size_t{1}, std::size_t{2}}) {
        CollectingSink sink;
        sink.acceptEvery = rejectOnCall;

        auto result = exportJsonStream(NodeView(*root), sink.fn(), JsonExportOptions{});

        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.empty());
        // The sink must never be called again after returning false.
        EXPECT_EQ(sink.callCount, rejectOnCall);
    }
}

TEST(StreamExportAbort, XmlAndCsvPropagateAbort) {
    std::vector<std::shared_ptr<const JsonNode>> rows;
    for (int i = 0; i < 20000; ++i) {
        std::vector<std::shared_ptr<const JsonNode>> fields;
        fields.push_back(JsonNode::makeNumber("id", std::to_string(i)));
        rows.push_back(JsonNode::makeObject("", std::move(fields)));
    }
    auto root = JsonNode::makeArray("", std::move(rows));

    {
        CollectingSink sink;
        sink.acceptEvery = 1;
        auto result = exportXmlStream(NodeView(*root), sink.fn(), "root");
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(sink.callCount, 1u);
    }
    {
        CollectingSink sink;
        sink.acceptEvery = 1;
        auto result = exportCsvStream(NodeView(*root), sink.fn());
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(sink.callCount, 1u);
    }
}

// ---------------------------------------------------------------------------
// Arena and JsonNode backings stream identical bytes
// ---------------------------------------------------------------------------

TEST(StreamExportBackings, ArenaAndJsonNodeStreamIdenticalBytes) {
    // Array-of-objects document so all three exporters accept it.
    std::string json = "[";
    for (int i = 0; i < 200; ++i) {
        if (i > 0) json += ",";
        json += R"({"id":)" + std::to_string(i) +
                R"(,"name":"item )" + std::to_string(i) +
                R"(","tags":["a","b"],"active":)" + (i % 2 == 0 ? "true" : "false") +
                R"(,"extra":null})";
    }
    json += "]";

    auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
    ASSERT_TRUE(arenaResult.ok());
    auto jsonRoot = arenaResult.root->toJsonNode();
    ASSERT_NE(jsonRoot, nullptr);

    NodeView arenaView(*arenaResult.root);
    NodeView jsonView(*jsonRoot);

    // JSON
    {
        CollectingSink a, b;
        ASSERT_TRUE(exportJsonStream(arenaView, a.fn(), JsonExportOptions{}).ok);
        ASSERT_TRUE(exportJsonStream(jsonView, b.fn(), JsonExportOptions{}).ok);
        EXPECT_EQ(a.concatenated(), b.concatenated());
    }
    // CSV
    {
        CollectingSink a, b;
        ASSERT_TRUE(exportCsvStream(arenaView, a.fn()).ok);
        ASSERT_TRUE(exportCsvStream(jsonView, b.fn()).ok);
        EXPECT_EQ(a.concatenated(), b.concatenated());
    }
    // XML
    {
        CollectingSink a, b;
        ASSERT_TRUE(exportXmlStream(arenaView, a.fn(), "root").ok);
        ASSERT_TRUE(exportXmlStream(jsonView, b.fn(), "root").ok);
        EXPECT_EQ(a.concatenated(), b.concatenated());
    }
}

TEST(StreamExportBackings, PropertyArenaAndJsonNodeStreamIdenticalJson) {
    rc::check("arena-backed and JsonNode-backed trees stream identical JSON/XML bytes",
        []() {
            // Serialize a random tree, parse it into the arena, and compare
            // streaming from both backings.
            auto node = *genJsonNode(3);
            auto compactOpts = JsonExportOptions{.mode = IndentMode::Compact,
                                                .indentWidth = 2,
                                                .trailingNewline = false};
            std::string json = exportJson(*node, compactOpts);

            auto arenaResult = parseBuffer(std::string(json), ParseBufferOptions{});
            RC_PRE(arenaResult.ok());
            auto jsonRoot = arenaResult.root->toJsonNode();
            RC_PRE(jsonRoot != nullptr);

            CollectingSink a, b;
            RC_ASSERT(exportJsonStream(NodeView(*arenaResult.root), a.fn(),
                                       JsonExportOptions{}).ok);
            RC_ASSERT(exportJsonStream(NodeView(*jsonRoot), b.fn(),
                                       JsonExportOptions{}).ok);
            RC_ASSERT(a.concatenated() == b.concatenated());

            CollectingSink c, d;
            RC_ASSERT(exportXmlStream(NodeView(*arenaResult.root), c.fn(), "root").ok);
            RC_ASSERT(exportXmlStream(NodeView(*jsonRoot), d.fn(), "root").ok);
            RC_ASSERT(c.concatenated() == d.concatenated());
        });
}
