#include "core/jsonpath.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <optional>
#include <regex>
#include <set>
#include <utility>

namespace jsontitan::core {

// Operand of a filter comparison: a path from the tested node ('@') or the
// document root ('$'), or a literal.
struct FilterOperand {
    enum class Kind { Relative, Absolute, Literal };
    Kind kind = Kind::Literal;
    std::vector<JsonPathStep> path;  // Key / Index steps only
    NodeType literalType = NodeType::Null;
    std::string literalText;  // string contents, number text, "true"/"false"
};

struct FilterExpr {
    enum class Op { Or, And, Not, Exists, Compare, Match };
    Op op = Op::Exists;
    std::shared_ptr<const FilterExpr> lhs;  // Or / And / Not
    std::shared_ptr<const FilterExpr> rhs;  // Or / And
    FilterOperand left;                     // Exists / Compare / Match
    FilterOperand right;                    // Compare
    std::string comparator;                 // Compare: == != < <= > >=
    std::shared_ptr<const std::regex> regex;  // Match
};

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

auto parseFilter(std::string_view expr, std::size_t& pos)
    -> std::expected<std::shared_ptr<const FilterExpr>, JsonPathError>;

// Parses the bracket expression starting just after '['. Returns the step
// (Kind and payload only; caller decides recursive-ness) and advances pos
// past the closing ']'.
auto parseBracket(std::string_view expr, std::size_t& pos)
    -> std::expected<JsonPathStep, JsonPathError> {
    if (pos >= expr.size()) {
        return parseError(pos, "Unterminated '['");
    }

    if (expr[pos] == '?') {
        ++pos;
        auto filter = parseFilter(expr, pos);
        if (!filter) {
            return std::unexpected(filter.error());
        }
        if (pos >= expr.size() || expr[pos] != ']') {
            return parseError(pos, "Expected ']' after filter expression");
        }
        ++pos;
        return JsonPathStep{.kind = JsonPathStep::Kind::Filter,
                            .filter = std::move(*filter)};
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
        const std::size_t start = pos;
        std::size_t value = 0;
        while (pos < expr.size() &&
               std::isdigit(static_cast<unsigned char>(expr[pos])) != 0) {
            const auto digit = static_cast<std::size_t>(expr[pos] - '0');
            // Overflow guard: the draft silently wrapped huge indices around
            // std::size_t, turning e.g. [18446744073709551616] into [0].
            if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
                return parseError(start, "Array index too large");
            }
            value = value * 10 + digit;
            ++pos;
        }
        if (pos >= expr.size() || expr[pos] != ']') {
            return parseError(pos, "Expected ']' after index");
        }
        ++pos;
        return JsonPathStep{.kind = JsonPathStep::Kind::Index, .index = value};
    }

    return parseError(pos, "Expected index, quoted name, or '*' inside '[]'");
}

// Recursive-descent parser for the expression after "[?". Positions in
// errors are byte offsets into the whole JSONPath expression.
class FilterParser {
public:
    FilterParser(std::string_view expr, std::size_t pos) : m_expr(expr), m_pos(pos) {}

    auto parse() -> std::shared_ptr<const FilterExpr> { return parseOr(); }
    [[nodiscard]] auto position() const -> std::size_t { return m_pos; }
    [[nodiscard]] auto error() const -> const std::optional<JsonPathError>& { return m_error; }

private:
    std::string_view m_expr;
    std::size_t m_pos;
    std::optional<JsonPathError> m_error;

    auto fail(std::size_t pos, std::string description) -> std::shared_ptr<const FilterExpr> {
        if (!m_error) {
            m_error = JsonPathError{pos, std::move(description)};
        }
        return nullptr;
    }

    [[nodiscard]] auto atEnd() const -> bool { return m_pos >= m_expr.size(); }
    [[nodiscard]] auto peek() const -> char { return atEnd() ? '\0' : m_expr[m_pos]; }

    void skipSpace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(m_expr[m_pos])) != 0) {
            ++m_pos;
        }
    }

    auto consume(std::string_view token) -> bool {
        skipSpace();
        if (m_expr.substr(m_pos, token.size()) == token) {
            m_pos += token.size();
            return true;
        }
        return false;
    }

    static auto binary(FilterExpr::Op op, std::shared_ptr<const FilterExpr> lhs,
                       std::shared_ptr<const FilterExpr> rhs)
        -> std::shared_ptr<const FilterExpr> {
        auto node = std::make_shared<FilterExpr>();
        node->op = op;
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        return node;
    }

    auto parseOr() -> std::shared_ptr<const FilterExpr> {
        auto lhs = parseAnd();
        while (lhs && consume("||")) {
            auto rhs = parseAnd();
            if (!rhs) {
                return nullptr;
            }
            lhs = binary(FilterExpr::Op::Or, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    auto parseAnd() -> std::shared_ptr<const FilterExpr> {
        auto lhs = parseUnary();
        while (lhs && consume("&&")) {
            auto rhs = parseUnary();
            if (!rhs) {
                return nullptr;
            }
            lhs = binary(FilterExpr::Op::And, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    auto parseUnary() -> std::shared_ptr<const FilterExpr> {
        skipSpace();
        if (peek() == '!' && m_expr.substr(m_pos, 2) != "!=") {
            ++m_pos;
            auto operand = parseUnary();
            if (!operand) {
                return nullptr;
            }
            return binary(FilterExpr::Op::Not, std::move(operand), nullptr);
        }
        if (peek() == '(') {
            ++m_pos;
            auto inner = parseOr();
            if (!inner) {
                return nullptr;
            }
            if (!consume(")")) {
                return fail(m_pos, "Expected ')' in filter expression");
            }
            return inner;
        }
        return parseComparison();
    }

    auto parseComparison() -> std::shared_ptr<const FilterExpr> {
        skipSpace();
        const std::size_t start = m_pos;
        auto left = parseOperand();
        if (!left) {
            return nullptr;
        }
        auto node = std::make_shared<FilterExpr>();
        node->left = std::move(*left);

        if (consume("=~")) {
            node->op = FilterExpr::Op::Match;
            node->regex = parseRegex();
            return node->regex ? node : nullptr;
        }
        for (const std::string_view comparator : {"==", "!=", "<=", ">=", "<", ">"}) {
            if (consume(comparator)) {
                auto right = parseOperand();
                if (!right) {
                    return nullptr;
                }
                node->op = FilterExpr::Op::Compare;
                node->comparator = std::string(comparator);
                node->right = std::move(*right);
                return node;
            }
        }
        if (node->left.kind == FilterOperand::Kind::Literal) {
            return fail(start, "A literal on its own is not a condition; compare it "
                               "with ==, !=, <, <=, > or >=");
        }
        node->op = FilterExpr::Op::Exists;
        return node;
    }

    static auto isFilterNameChar(char c) -> bool {
        const auto uc = static_cast<unsigned char>(c);
        return uc >= 0x80 || std::isalnum(uc) != 0 || c == '_' || c == '-' || c == '$';
    }

    auto parseOperand() -> std::optional<FilterOperand> {
        skipSpace();
        FilterOperand operand;
        const char c = peek();
        if (c == '@' || c == '$') {
            ++m_pos;
            operand.kind = c == '@' ? FilterOperand::Kind::Relative
                                    : FilterOperand::Kind::Absolute;
            if (!parseOperandPath(operand.path)) {
                return std::nullopt;
            }
            return operand;
        }
        if (c == '\'' || c == '"') {
            operand.literalType = NodeType::String;
            if (!parseString(operand.literalText)) {
                return std::nullopt;
            }
            return operand;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            const std::size_t start = m_pos;
            if (c == '-') {
                ++m_pos;
            }
            while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
                                peek() == '.' || peek() == 'e' || peek() == 'E' ||
                                ((peek() == '+' || peek() == '-') &&
                                 (m_expr[m_pos - 1] == 'e' || m_expr[m_pos - 1] == 'E')))) {
                ++m_pos;
            }
            operand.literalType = NodeType::Number;
            operand.literalText = std::string(m_expr.substr(start, m_pos - start));
            double unused = 0;
            const char* first = operand.literalText.data();
            const char* last = first + operand.literalText.size();
            auto [end, ec] = std::from_chars(first, last, unused);
            if (ec != std::errc{} || end != last) {
                fail(start, "Invalid number '" + operand.literalText + "'");
                return std::nullopt;
            }
            return operand;
        }
        for (const std::string_view keyword : {"true", "false", "null"}) {
            if (m_expr.substr(m_pos, keyword.size()) == keyword &&
                !isFilterNameChar(m_pos + keyword.size() < m_expr.size()
                                      ? m_expr[m_pos + keyword.size()]
                                      : ' ')) {
                m_pos += keyword.size();
                operand.literalType = keyword == "null" ? NodeType::Null : NodeType::Boolean;
                operand.literalText = std::string(keyword);
                return operand;
            }
        }
        fail(m_pos, atEnd() ? "Unterminated filter expression"
                            : "Expected '@', '$', or a literal in filter expression");
        return std::nullopt;
    }

    // Names and indices after '@' / '$': .name, ['name'], [n].
    auto parseOperandPath(std::vector<JsonPathStep>& path) -> bool {
        while (!atEnd()) {
            if (peek() == '.') {
                const std::size_t start = ++m_pos;
                while (!atEnd() && isFilterNameChar(peek())) {
                    ++m_pos;
                }
                if (m_pos == start) {
                    fail(start, "Expected a name after '.' (filter paths take no "
                                "wildcards or '..')");
                    return false;
                }
                path.push_back(JsonPathStep{.kind = JsonPathStep::Kind::Key,
                                            .key = std::string(m_expr.substr(start, m_pos - start))});
                continue;
            }
            if (peek() == '[') {
                const std::size_t start = m_pos;
                ++m_pos;
                auto step = parseBracket(m_expr, m_pos);
                if (!step) {
                    fail(step.error().position, step.error().description);
                    return false;
                }
                if (step->kind != JsonPathStep::Kind::Key &&
                    step->kind != JsonPathStep::Kind::Index) {
                    fail(start, "Filter paths take names and indices only");
                    return false;
                }
                path.push_back(std::move(*step));
                continue;
            }
            break;
        }
        return true;
    }

    auto parseString(std::string& out) -> bool {
        const std::size_t start = m_pos;
        const char quote = m_expr[m_pos++];
        while (!atEnd() && peek() != quote) {
            char c = m_expr[m_pos++];
            if (c == '\\' && !atEnd()) {
                c = m_expr[m_pos++];
                if (c == 'n') {
                    c = '\n';
                } else if (c == 't') {
                    c = '\t';
                }
            }
            out += c;
        }
        if (atEnd()) {
            fail(start, "Unterminated string in filter expression");
            return false;
        }
        ++m_pos;  // closing quote
        return true;
    }

    auto parseRegex() -> std::shared_ptr<const std::regex> {
        skipSpace();
        const std::size_t start = m_pos;
        if (peek() != '/') {
            fail(m_pos, "Expected /pattern/ after '=~'");
            return nullptr;
        }
        ++m_pos;
        std::string pattern;
        while (!atEnd() && peek() != '/') {
            if (peek() == '\\' && m_pos + 1 < m_expr.size() && m_expr[m_pos + 1] == '/') {
                ++m_pos;  // "\/" is a literal slash
            }
            pattern += m_expr[m_pos++];
        }
        if (atEnd()) {
            fail(start, "Unterminated /pattern/");
            return nullptr;
        }
        ++m_pos;  // closing '/'
        auto flags = std::regex::ECMAScript;
        while (!atEnd() && std::isalpha(static_cast<unsigned char>(peek())) != 0) {
            if (peek() != 'i') {
                fail(m_pos, std::string("Unknown regex flag '") + peek() + "'");
                return nullptr;
            }
            flags |= std::regex::icase;
            ++m_pos;
        }
        try {
            return std::make_shared<const std::regex>(pattern, flags);
        } catch (const std::regex_error&) {
            fail(start, "Invalid regular expression /" + pattern + "/");
            return nullptr;
        }
    }
};

auto parseFilter(std::string_view expr, std::size_t& pos)
    -> std::expected<std::shared_ptr<const FilterExpr>, JsonPathError> {
    FilterParser parser(expr, pos);
    auto filter = parser.parse();
    if (!filter) {
        return std::unexpected(parser.error().value_or(
            JsonPathError{parser.position(), "Invalid filter expression"}));
    }
    pos = parser.position();
    while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos])) != 0) {
        ++pos;
    }
    return filter;
}

// --- Filter evaluation ------------------------------------------------------

// Follow `path` (Key / Index steps) down from `node`.
auto resolveOperandPath(NodeView node, const std::vector<JsonPathStep>& path)
    -> std::optional<NodeView> {
    for (const auto& step : path) {
        const std::size_t count = node.childCount();
        if (step.kind == JsonPathStep::Kind::Index) {
            if (node.type() != NodeType::Array || step.index >= count) {
                return std::nullopt;
            }
            node = node.child(step.index);
            continue;
        }
        if (node.type() != NodeType::Object) {
            return std::nullopt;
        }
        bool found = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (node.child(i).key() == step.key) {
                node = node.child(i);
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return node;
}

// A scalar-or-container value taking part in a comparison.
struct OperandValue {
    NodeType type;
    std::string_view text;
};

auto operandValue(const FilterOperand& operand, NodeView current, NodeView documentRoot)
    -> std::optional<OperandValue> {
    if (operand.kind == FilterOperand::Kind::Literal) {
        return OperandValue{operand.literalType, operand.literalText};
    }
    auto node = resolveOperandPath(
        operand.kind == FilterOperand::Kind::Relative ? current : documentRoot, operand.path);
    if (!node) {
        return std::nullopt;
    }
    return OperandValue{node->type(), node->value()};
}

auto toNumber(std::string_view text) -> std::optional<double> {
    double value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

auto compareValues(const OperandValue& a, const OperandValue& b, std::string_view comparator)
    -> bool {
    const bool equalityOp = comparator == "==" || comparator == "!=";
    if (a.type != b.type) {
        return comparator == "!=";
    }
    int order = 0;  // <0, 0, >0
    switch (a.type) {
        case NodeType::Number: {
            auto x = toNumber(a.text);
            auto y = toNumber(b.text);
            if (!x || !y) {
                return comparator == "!=";
            }
            order = *x < *y ? -1 : (*x > *y ? 1 : 0);
            break;
        }
        case NodeType::String:
            order = a.text.compare(b.text);
            break;
        case NodeType::Boolean:
        case NodeType::Null:
            if (!equalityOp) {
                return false;  // only numbers and strings are ordered
            }
            order = a.text == b.text ? 0 : 1;
            break;
        case NodeType::Object:
        case NodeType::Array:
            return comparator == "!=";  // containers never compare equal
    }
    if (comparator == "==") return order == 0;
    if (comparator == "!=") return order != 0;
    if (comparator == "<") return order < 0;
    if (comparator == "<=") return order <= 0;
    if (comparator == ">") return order > 0;
    return order >= 0;  // ">="
}

auto evalFilter(const FilterExpr& expr, NodeView current, NodeView documentRoot) -> bool {
    switch (expr.op) {
        case FilterExpr::Op::Or:
            return evalFilter(*expr.lhs, current, documentRoot) ||
                   evalFilter(*expr.rhs, current, documentRoot);
        case FilterExpr::Op::And:
            return evalFilter(*expr.lhs, current, documentRoot) &&
                   evalFilter(*expr.rhs, current, documentRoot);
        case FilterExpr::Op::Not:
            return !evalFilter(*expr.lhs, current, documentRoot);
        case FilterExpr::Op::Exists:
            return operandValue(expr.left, current, documentRoot).has_value();
        case FilterExpr::Op::Match: {
            auto value = operandValue(expr.left, current, documentRoot);
            if (!value || value->type != NodeType::String) {
                return false;
            }
            return std::regex_search(value->text.begin(), value->text.end(), *expr.regex);
        }
        case FilterExpr::Op::Compare: {
            auto a = operandValue(expr.left, current, documentRoot);
            auto b = operandValue(expr.right, current, documentRoot);
            if (!a || !b) {
                return expr.comparator == "!=";
            }
            return compareValues(*a, *b, expr.comparator);
        }
    }
    return false;
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
        case JsonPathStep::Kind::Filter:
            step.kind = JsonPathStep::Kind::RecursiveFilter;
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
// Recurses once per tree level; safe because JSON nesting is parse-capped at
// 1024 (parser.cpp kMaxDepth / simdjson's max_depth), far below stack limits.
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
void applyBase(const JsonPathStep& step, const WorkItem& item, NodeView documentRoot,
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
        case JsonPathStep::Kind::Filter:
        case JsonPathStep::Kind::RecursiveFilter: {
            // Like a wildcard, keeping only the children the predicate
            // accepts ('@' is the child).
            for (std::size_t i = 0; i < count; ++i) {
                NodeView child = item.node.child(i);
                if (!evalFilter(*step.filter, child, documentRoot)) {
                    continue;
                }
                WorkItem next{child, item.path};
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
           step.kind == JsonPathStep::Kind::RecursiveWildcard ||
           step.kind == JsonPathStep::Kind::RecursiveFilter;
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
            applyBase(step, item, root, next);
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

auto parseDefinitePath(std::string_view expr)
    -> std::expected<NodePath, JsonPathError> {
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.front())) != 0) {
        expr.remove_prefix(1);
    }
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.back())) != 0) {
        expr.remove_suffix(1);
    }
    if (expr.empty()) {
        return parseError(0, "Enter a path such as $.store.book[0]");
    }

    // Accept the shorthand without '$' by parsing a normalized copy, then
    // shift error positions back onto the text the user typed.
    std::string normalized;
    std::size_t prefixLength = 0;
    if (expr.front() != '$') {
        normalized = expr.front() == '[' ? "$" : "$.";
        prefixLength = normalized.size();
    }
    normalized.append(expr);

    auto steps = parseJsonPath(normalized);
    if (!steps) {
        auto error = steps.error();
        error.position = error.position > prefixLength ? error.position - prefixLength : 0;
        return std::unexpected(std::move(error));
    }

    NodePath path;
    path.reserve(steps->size());
    for (const auto& step : *steps) {
        switch (step.kind) {
            case JsonPathStep::Kind::Key:
                path.emplace_back(step.key);
                break;
            case JsonPathStep::Kind::Index:
                path.emplace_back(step.index);
                break;
            default:
                return parseError(0, "Wildcards and '..' match many nodes; "
                                     "Go to Path needs a single node");
        }
    }
    return path;
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
