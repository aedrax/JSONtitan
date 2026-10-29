#include "core/simdjson_adapter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

namespace jsontitan::core {

namespace {

// Map a simdjson error code to a human-readable ParseError.
// The DOM API does not report an error location, so byteOffset is always 0
// and the descriptions must not fabricate one.
auto mapSimdjsonError(simdjson::error_code error) -> ParseError {
    switch (error) {
        case simdjson::EMPTY:
            return ParseError{0, "Empty input"};
        case simdjson::UNCLOSED_STRING:
            return ParseError{0, "Unterminated string"};
        case simdjson::TAPE_ERROR:
            return ParseError{0, "Structural error (location unavailable)"};
        case simdjson::DEPTH_ERROR:
            return ParseError{0, "Document exceeds maximum nesting depth"};
        case simdjson::CAPACITY:
            return ParseError{0, "Document exceeds maximum size (4 GB)"};
        case simdjson::MEMALLOC:
            return ParseError{0, "Memory allocation failed during parsing"};
        case simdjson::UTF8_ERROR:
            return ParseError{0, "Invalid UTF-8 encoding (location unavailable)"};
        case simdjson::TRAILING_CONTENT:
            return ParseError{0, "Unexpected trailing content after JSON value"};
        default:
            return ParseError{0, std::string("Parse error: ") + simdjson::error_message(error)};
    }
}

// Threshold above which intermediate progress is reported during tree-building.
constexpr std::size_t kProgressReportingThreshold = 10 * 1024 * 1024; // 10 MB

// Approximate bytes per node used to estimate total node count from document size.
constexpr std::size_t kEstimatedBytesPerNode = 50;

// How often (in nodes) to report intermediate progress.
constexpr std::size_t kProgressReportInterval = 1000;

// Context passed through the recursive tree-building to track progress and
// poll for cooperative cancellation.
struct ProgressContext {
    std::function<void(float)> callback;
    std::function<bool()> cancelCheck;
    std::size_t estimatedTotalNodes = 0;
    std::size_t nodesCreated = 0;
    bool cancelled = false;
};

// Copy `text` into the arena. False when the arena is out of memory, which
// copyString reports as an empty view: the caller must fail the parse rather
// than keep an empty string where data was.
auto copyInto(ArenaAllocator& arena, std::string_view text, StringRef& out) -> bool {
    auto copy = arena.copyString(text);
    if (copy.empty() && !text.empty()) {
        return false;
    }
    out = StringRef{copy.data(), copy.size()};
    return true;
}

// Number of members in a simdjson array or object. size() saturates at
// 0xFFFFFF, so larger containers are counted by walking them.
template <typename Container>
auto memberCount(const Container& container) -> std::size_t {
    constexpr std::size_t kSaturated = 0xFFFFFF;
    std::size_t count = container.size();
    if (count >= kSaturated) {
        count = 0;
        for ([[maybe_unused]] auto&& member : container) {
            ++count;
        }
    }
    return count;
}

// Convert a simdjson DOM element into an ArenaJsonNode tree, recursively.
// All string data is copied into the arena so that the resulting tree
// outlives the simdjson parser's internal buffers.
auto convertElement(simdjson::dom::element elem,
                    StringRef key,
                    ArenaAllocator& arena,
                    ProgressContext& progress) -> ArenaJsonNode* {
    auto* node = arena.construct<ArenaJsonNode>();
    if (!node) {
        return nullptr;
    }
    node->key = key;

    // Count every constructed node unconditionally (nodesCreated doubles as
    // the final node count); progress/cancellation callbacks stay optional.
    progress.nodesCreated++;
    if (progress.nodesCreated % kProgressReportInterval == 0) {
        if (progress.cancelCheck && progress.cancelCheck()) {
            progress.cancelled = true;
            return nullptr;
        }
        if (progress.callback && progress.estimatedTotalNodes > 0) {
            // Progress during tree-building is mapped to [0.5, 1.0) range
            // since simdjson parse (first half) is already done.
            float treeBuildFraction = static_cast<float>(progress.nodesCreated) /
                                     static_cast<float>(progress.estimatedTotalNodes);
            if (treeBuildFraction > 1.0F) {
                treeBuildFraction = 1.0F;
            }
            float overallProgress = 0.5F + (treeBuildFraction * 0.5F);
            if (overallProgress > 0.99F) {
                overallProgress = 0.99F; // Reserve 1.0 for completion
            }
            progress.callback(overallProgress);
        }
    }

    switch (elem.type()) {
        case simdjson::dom::element_type::OBJECT: {
            node->type = NodeType::Object;
            simdjson::dom::object obj;
            if (elem.get(obj) != simdjson::SUCCESS) {
                return nullptr;
            }

            // Count children first to allocate the pointer array.
            std::size_t count = memberCount(obj);
            if (count > 0) {
                auto** children = static_cast<ArenaJsonNode**>(
                    arena.allocate(count * sizeof(ArenaJsonNode*), alignof(ArenaJsonNode*)));
                if (!children) {
                    return nullptr;
                }

                std::size_t idx = 0;
                for (auto [k, v] : obj) {
                    // Copy the key string into the arena.
                    StringRef childKey = {};
                    if (!copyInto(arena, k, childKey)) {
                        return nullptr;
                    }

                    auto* child = convertElement(v, childKey, arena, progress);
                    if (!child) {
                        return nullptr;
                    }
                    children[idx++] = child;
                }

                node->children = children;
                node->childCount = count;
            }
            break;
        }

        case simdjson::dom::element_type::ARRAY: {
            node->type = NodeType::Array;
            simdjson::dom::array arr;
            if (elem.get(arr) != simdjson::SUCCESS) {
                return nullptr;
            }

            std::size_t count = memberCount(arr);
            if (count > 0) {
                auto** children = static_cast<ArenaJsonNode**>(
                    arena.allocate(count * sizeof(ArenaJsonNode*), alignof(ArenaJsonNode*)));
                if (!children) {
                    return nullptr;
                }

                std::size_t idx = 0;
                for (auto v : arr) {
                    StringRef emptyKey = {};
                    auto* child = convertElement(v, emptyKey, arena, progress);
                    if (!child) {
                        return nullptr;
                    }
                    children[idx++] = child;
                }

                node->children = children;
                node->childCount = count;
            }
            break;
        }

        case simdjson::dom::element_type::STRING: {
            node->type = NodeType::String;
            std::string_view sv;
            if (elem.get(sv) != simdjson::SUCCESS) {
                return nullptr;
            }
            if (!copyInto(arena, sv, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::INT64: {
            node->type = NodeType::Number;
            std::int64_t val = {};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            // Convert integer to string via std::to_chars.
            std::array<char, 21> buf{}; // max digits for int64 + sign
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            if (!copyInto(arena, numStr, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::UINT64: {
            node->type = NodeType::Number;
            std::uint64_t val = {};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            std::array<char, 20> buf{}; // max digits for uint64
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            if (!copyInto(arena, numStr, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::BIGINT: {
            // Only produced with parser.number_as_string(true); keep the raw digits.
            node->type = NodeType::Number;
            std::string_view digits;
            if (elem.get_bigint().get(digits) != simdjson::SUCCESS) {
                return nullptr;
            }
            if (!copyInto(arena, digits, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::DOUBLE: {
            node->type = NodeType::Number;
            double val = {};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            // Use std::to_chars with general format for round-trip fidelity.
            std::array<char, 32> buf{};
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val,
                                           std::chars_format::general);
            if (ec != std::errc{}) {
                return nullptr;
            }
            std::string_view numStr(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            if (!copyInto(arena, numStr, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::BOOL: {
            node->type = NodeType::Boolean;
            bool val = {};
            if (elem.get(val) != simdjson::SUCCESS) {
                return nullptr;
            }
            std::string_view boolStr = val ? "true" : "false";
            if (!copyInto(arena, boolStr, node->value)) {
                return nullptr;
            }
            break;
        }

        case simdjson::dom::element_type::NULL_VALUE: {
            node->type = NodeType::Null;
            // value remains empty (default StringRef).
            break;
        }
    }

    return node;
}

} // anonymous namespace

auto simdjsonParse(const SourceBuffer& source,
                   ArenaAllocator& arena,
                   SimdjsonParseOptions options) -> SimdjsonResult {
    // Empty input check.
    if (source.size() == 0) {
        return SimdjsonResult{nullptr, ParseError{0, "Empty input"}};
    }

    // Report initial progress (0.0) if callback is provided.
    if (options.progressCallback) {
        options.progressCallback(0.0F);
    }

    // Create the simdjson parser on the stack — RAII ensures its internal
    // buffers are released when this function returns.
    simdjson::dom::parser parser;
    // Integers beyond 64 bits arrive as BIGINT digit strings instead of
    // failing the whole document.
    parser.number_as_string(true);

    // Parse the input. simdjson requires a padded_string_view for safety.
    // SourceBuffer appends kSimdjsonPadding (64) zero bytes at construction time,
    // so we can use padded_string_view directly without copying the buffer.
    simdjson::padded_string_view paddedInput(source.data(), source.size(), source.paddedSize());

    simdjson::dom::element doc;
    auto error = parser.parse(paddedInput).get(doc);
    if (error != simdjson::SUCCESS) {
        return SimdjsonResult{nullptr, mapSimdjsonError(error)};
    }

    // After simdjson parse completes, report 0.5 progress (parsing is ~half the work).
    if (options.progressCallback) {
        options.progressCallback(0.5F);
    }

    // Set up progress/cancellation tracking for the tree-building phase.
    // The context is always passed so node counting is unconditional; the
    // callbacks inside it stay optional.
    ProgressContext progressCtx;
    if (options.progressCallback && source.size() > kProgressReportingThreshold) {
        progressCtx.callback = options.progressCallback;
        progressCtx.estimatedTotalNodes = source.size() / kEstimatedBytesPerNode;
    }
    if (options.cancelCallback) {
        progressCtx.cancelCheck = options.cancelCallback;
    }

    // Walk the DOM tree and convert to ArenaJsonNode.
    StringRef rootKey = {}; // Root node has no key.
    auto* root = convertElement(doc, rootKey, arena, progressCtx);
    if (progressCtx.cancelled) {
        return SimdjsonResult{nullptr, ParseError{0, "Parse cancelled"}};
    }
    if (!root) {
        return SimdjsonResult{nullptr, ParseError{0, "Memory allocation failed during tree construction"}};
    }

    // Report completion progress (1.0).
    if (options.progressCallback) {
        options.progressCallback(1.0F);
    }

    // At this point, the simdjson parser (and its internal buffers) will be
    // destroyed when this function returns, since `parser` and `paddedInput`
    // are stack-local. All string data has been copied into the arena.
    return SimdjsonResult{root, std::nullopt, progressCtx.nodesCreated};
}

auto simdjsonParseLines(const SourceBuffer& source,
                        ArenaAllocator& arena,
                        SimdjsonParseOptions options) -> SimdjsonResult {
    if (source.size() == 0) {
        return SimdjsonResult{nullptr, ParseError{0, "Empty input"}};
    }

    if (options.progressCallback) {
        options.progressCallback(0.0F);
    }

    // Progress is reported by bytes consumed in the loop below; the context
    // only counts nodes and polls cancellation inside large records.
    ProgressContext progressCtx;
    progressCtx.cancelCheck = options.cancelCallback;

    simdjson::dom::parser parser;
    parser.number_as_string(true);  // see simdjsonParse
    std::vector<ArenaJsonNode*> records;

    const char* data = source.data();
    const std::size_t size = source.size();
    std::size_t lineStart = 0;
    std::size_t lineNumber = 0;
    std::size_t nextProgressAt = kProgressReportingThreshold;

    while (lineStart < size) {
        ++lineNumber;
        const auto* newline = static_cast<const char*>(
            std::memchr(data + lineStart, '\n', size - lineStart));
        const std::size_t lineEnd =
            newline ? static_cast<std::size_t>(newline - data) : size;
        const std::string_view line(data + lineStart, lineEnd - lineStart);

        const bool blank = line.find_first_not_of(" \t\r") == std::string_view::npos;
        if (!blank) {
            // Each line is a view into the shared buffer: everything after it
            // (plus the trailing zero padding) is readable, so simdjson can
            // parse in place without copying the line.
            simdjson::padded_string_view paddedLine(
                line.data(), line.size(), source.paddedSize() - lineStart);
            simdjson::dom::element doc;
            auto error = parser.parse(paddedLine).get(doc);
            if (error != simdjson::SUCCESS) {
                auto mapped = mapSimdjsonError(error);
                mapped.description = "Line " + std::to_string(lineNumber) + ": " +
                                     mapped.description;
                return SimdjsonResult{nullptr, std::move(mapped)};
            }

            auto* record = convertElement(doc, StringRef{}, arena, progressCtx);
            if (progressCtx.cancelled) {
                return SimdjsonResult{nullptr, ParseError{0, "Parse cancelled"}};
            }
            if (!record) {
                return SimdjsonResult{nullptr, ParseError{0, "Memory allocation failed during tree construction"}};
            }
            records.push_back(record);
        }

        lineStart = lineEnd + 1;
        if (lineStart >= nextProgressAt) {
            nextProgressAt = lineStart + kProgressReportingThreshold;
            if (options.cancelCallback && options.cancelCallback()) {
                return SimdjsonResult{nullptr, ParseError{0, "Parse cancelled"}};
            }
            if (options.progressCallback) {
                options.progressCallback(std::min(
                    0.99F, static_cast<float>(lineStart) / static_cast<float>(size)));
            }
        }
    }

    auto* root = arena.construct<ArenaJsonNode>();
    if (!root) {
        return SimdjsonResult{nullptr, ParseError{0, "Memory allocation failed during tree construction"}};
    }
    root->type = NodeType::Array;
    if (!records.empty()) {
        auto** children = static_cast<ArenaJsonNode**>(arena.allocate(
            records.size() * sizeof(ArenaJsonNode*), alignof(ArenaJsonNode*)));
        if (!children) {
            return SimdjsonResult{nullptr, ParseError{0, "Memory allocation failed during tree construction"}};
        }
        std::copy(records.begin(), records.end(), children);
        root->children = children;
        root->childCount = records.size();
    }

    if (options.progressCallback) {
        options.progressCallback(1.0F);
    }

    // +1 for the synthetic root array.
    return SimdjsonResult{root, std::nullopt, progressCtx.nodesCreated + 1};
}

} // namespace jsontitan::core
