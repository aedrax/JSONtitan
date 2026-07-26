#include "core/jsonpath.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace jsontitan::core {

namespace {

using IndexPath = std::vector<std::size_t>;

auto parseError(std::size_t pos, std::string description)
    -> std::unexpected<JsonPathError> {
    return std::unexpected(JsonPathError{pos, std::move(description)});
}

auto isNameChar(char c) -> bool {
    auto uc = static_cast<unsigned char>(c);
    // Unquoted member names: everything printable except the syntax
    // characters '.', '[', ']', '*', and whitespace. Multi-byte UTF-8
    // continuation bytes (>= 0x80) are allowed.
    return uc >= 0x80 ||
           (std::isgraph(uc) != 0 && c != '.' && c != '[' && c != ']' && c != '*');
}

// Parses the bracket expression starting just after '['. Returns the step
// (Kind and payload only; caller decides recursive-ness) and advances pos
// past the closing ']'.
auto parseBracket(std::string_view expr, std::size_t& pos)
    -> std::expected<JsonPathStep, JsonPathError> {
    if (pos >= expr.size()) {
        return parseError(pos, "Unterminated '['");
    }

    if (expr[pos] == '?') {
        return parseError(pos - 1, "JSONPath filter expressions are not supported");
    }

    if (expr[pos] == '*') {
        ++pos;
        if (pos >= expr.size() || expr[pos] != ']') {
            return parseError(pos, "Expected ']' after '*'");
        }
        ++pos;
        return JsonPathStep{.kind = JsonPathStep::Kind::Wildcard};
    }

    if (expr[pos] == '\'' || expr[pos] == '"') {
        const char quote = expr[pos];
        ++pos;
        std::string name;
        while (pos < expr.size() && expr[pos] != quote) {
            if (expr[pos] == '\\' && pos + 1 < expr.size()) {
                ++pos;  // escaped char inside quoted name
            }
            name += expr[pos];
            ++pos;
        }
        if (pos >= expr.size()) {
            return parseError(pos, "Unterminated quoted name");
        }
        ++pos;  // closing quote
        if (pos >= expr.size() || expr[pos] != ']') {
            return parseError(pos, "Expected ']' after quoted name");
        }
        ++pos;
        return JsonPathStep{.kind = JsonPathStep::Kind::Key, .key = std::move(name)};
    }

    if (std::isdigit(static_cast<unsigned char>(expr[pos])) != 0) {
        std::size_t start = pos;
        std::size_t value = 0;
        while (pos < expr.size() &&
               std::isdigit(static_cast<unsigned char>(expr[pos])) != 0) {
            value = value * 10 + static_cast<std::size_t>(expr[pos] - '0');
            ++pos;
        }
        if (pos >= expr.size() || expr[pos] != ']') {
            return parseError(pos, "Expected ']' after index");
        }
        ++pos;
        (void)start;
        return JsonPathStep{.kind = JsonPathStep::Kind::Index, .index = value};
    }

    return parseError(pos, "Expected index, quoted name, or '*' inside '[]'");
}

auto makeRecursive(JsonPathStep step) -> JsonPathStep {
    switch (step.kind) {
        case JsonPathStep::Kind::Key:
            step.kind = JsonPathStep::Kind::RecursiveKey;
            break;
        case JsonPathStep::Kind::Index:
            step.kind = JsonPathStep::Kind::RecursiveIndex;
            break;
        case JsonPathStep::Kind::Wildcard:
            step.kind = JsonPathStep::Kind::RecursiveWildcard;
            break;
        default:
            break;
    }
    return step;
}

// A node in the evaluation working set: the node plus its child-index path.
struct WorkItem {
    NodeView node;
    IndexPath path;
};

// Enumerate `item` and every descendant (pre-order) into `out`.
void enumerateRecursive(const WorkItem& item, std::vector<WorkItem>& out) {
    out.push_back(item);
    const std::size_t count = item.node.childCount();
    for (std::size_t i = 0; i < count; ++i) {
        WorkItem child{item.node.child(i), item.path};
        child.path.push_back(i);
        enumerateRecursive(child, out);
    }
}

// Apply the non-recursive core of `step` to a single node, appending results.
void applyBase(const JsonPathStep& step, const WorkItem& item,
               std::vector<WorkItem>& out) {
    const std::size_t count = item.node.childCount();
    switch (step.kind) {
        case JsonPathStep::Kind::Key:
        case JsonPathStep::Kind::RecursiveKey: {
            if (item.node.type() != NodeType::Object) {
                return;
            }
            for (std::size_t i = 0; i < count; ++i) {
                NodeView child = item.node.child(i);
                if (child.key() == step.key) {
                    WorkItem next{child, item.path};
                    next.path.push_back(i);
                    out.push_back(std::move(next));
                }
            }
            break;
        }
        case JsonPathStep::Kind::Index:
        case JsonPathStep::Kind::RecursiveIndex: {
            if (item.node.type() != NodeType::Array || step.index >= count) {
                return;
            }
            WorkItem next{item.node.child(step.index), item.path};
            next.path.push_back(step.index);
            out.push_back(std::move(next));
            break;
        }
        case JsonPathStep::Kind::Wildcard:
        case JsonPathStep::Kind::RecursiveWildcard: {
            for (std::size_t i = 0; i < count; ++i) {
                WorkItem next{item.node.child(i), item.path};
                next.path.push_back(i);
                out.push_back(std::move(next));
            }
            break;
        }
    }
}

auto isRecursive(const JsonPathStep& step) -> bool {
    return step.kind == JsonPathStep::Kind::RecursiveKey ||
           step.kind == JsonPathStep::Kind::RecursiveIndex ||
           step.kind == JsonPathStep::Kind::RecursiveWildcard;
}

} // anonymous namespace

auto parseJsonPath(std::string_view expr)
    -> std::expected<std::vector<JsonPathStep>, JsonPathError> {
    if (expr.empty() || expr[0] != '$') {
        return parseError(0, "JSONPath must start with '$'");
    }

    std::vector<JsonPathStep> steps;
    std::size_t pos = 1;

    while (pos < expr.size()) {
        if (expr[pos] == '.') {
            bool recursive = false;
            ++pos;
            if (pos < expr.size() && expr[pos] == '.') {
                recursive = true;
                ++pos;
            }

            if (pos < expr.size() && expr[pos] == '*') {
                ++pos;
                JsonPathStep step{.kind = JsonPathStep::Kind::Wildcard};
                steps.push_back(recursive ? makeRecursive(step) : step);
                continue;
            }
            if (pos < expr.size() && expr[pos] == '[') {
                // "..[n]" / "..['name']" / "..[*]"
                ++pos;
                auto bracket = parseBracket(expr, pos);
                if (!bracket) {
                    return std::unexpected(bracket.error());
                }
                steps.push_back(recursive ? makeRecursive(*bracket) : *bracket);
                continue;
            }

            std::size_t start = pos;
            std::string name;
            while (pos < expr.size() && isNameChar(expr[pos])) {
                name += expr[pos];
                ++pos;
            }
            if (name.empty()) {
                return parseError(start, recursive ? "Expected name after '..'"
                                                   : "Expected name after '.'");
            }
            JsonPathStep step{.kind = JsonPathStep::Kind::Key, .key = std::move(name)};
            steps.push_back(recursive ? makeRecursive(step) : step);
            continue;
        }

        if (expr[pos] == '[') {
            ++pos;
            auto bracket = parseBracket(expr, pos);
            if (!bracket) {
                return std::unexpected(bracket.error());
            }
            steps.push_back(*bracket);
            continue;
        }

        return parseError(pos, std::string("Unexpected character '") + expr[pos] + "'");
    }

    return steps;
}

auto evalJsonPath(NodeView root, std::span<const JsonPathStep> steps) -> FilterResult {
    std::vector<WorkItem> current;
    current.push_back(WorkItem{root, {}});

    for (const auto& step : steps) {
        std::vector<WorkItem> expanded;
        if (isRecursive(step)) {
            for (const auto& item : current) {
                enumerateRecursive(item, expanded);
            }
        } else {
            expanded = std::move(current);
        }

        std::vector<WorkItem> next;
        for (const auto& item : expanded) {
            applyBase(step, item, next);
        }
        current = std::move(next);
        if (current.empty()) {
            break;
        }
    }

    // Deduplicate (recursive steps can reach the same node twice) and emit.
    std::set<IndexPath> seen;
    FilterResult result;
    for (auto& item : current) {
        if (seen.insert(item.path).second) {
            result.matches.push_back(SearchMatch{
                .ancestorIndices = std::move(item.path),
                .node = nullptr,
            });
        }
    }
    return result;
}

auto queryJsonPath(NodeView root, std::string_view expr) -> FilterResult {
    auto steps = parseJsonPath(expr);
    if (!steps) {
        return FilterResult{
            .matches = {},
            .error = SearchError{
                .description = "JSONPath error at position " +
                               std::to_string(steps.error().position) + ": " +
                               steps.error().description},
        };
    }
    return evalJsonPath(root, *steps);
}

} // namespace jsontitan::core
