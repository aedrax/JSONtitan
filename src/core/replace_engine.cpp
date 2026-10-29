#include "core/replace_engine.h"

#include <algorithm>
#include <regex>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ascii.h"

namespace jsontitan::core {

namespace {

class Replacer {
public:
    Replacer(const ReplaceOptions& options, std::optional<std::regex> regex)
        : m_options(options), m_regex(std::move(regex)) {
        if (!options.caseSensitive) {
            m_foldedPattern.reserve(options.pattern.size());
            for (char c : options.pattern) {
                m_foldedPattern += ascii::toLower(c);
            }
        }
    }

    // `text` with every match replaced, or std::nullopt when nothing matched.
    // `splits` is set when a regex match or capture group starts or ends
    // inside a multi-byte character: a regex works on bytes ("." matches
    // each byte of "é", and "$2$1" could swap them), so the result may not
    // be valid UTF-8. Substring matches cannot split: the pattern is whole
    // UTF-8 itself, and folding is ASCII-only.
    auto apply(std::string_view text, bool& splits) const -> std::optional<std::string> {
        splits = false;
        if (m_regex) {
            const auto insideCharacter = [text](std::ptrdiff_t pos) {
                return static_cast<std::size_t>(pos) < text.size() &&
                       (static_cast<unsigned char>(text[static_cast<std::size_t>(pos)]) &
                        0xC0) == 0x80;
            };
            std::string out;
            bool matched = false;
            const char* rest = text.data();
            for (std::cregex_iterator it(text.data(), text.data() + text.size(), *m_regex), end;
                 it != end; ++it) {
                const auto& match = *it;
                for (std::size_t g = 0; g < match.size() && !splits; ++g) {
                    splits = match[g].matched &&
                             (insideCharacter(match.position(g)) ||
                              insideCharacter(match.position(g) + match.length(g)));
                }
                matched = true;
                out.append(match.prefix().first, match.prefix().second);
                match.format(std::back_inserter(out), m_options.replacement);
                rest = match[0].second;
            }
            if (!matched) {
                return std::nullopt;
            }
            out.append(rest, text.data() + text.size());
            return out;
        }

        const std::string_view pattern = m_options.pattern;
        std::string out;
        bool matched = false;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t hit = find(text, pos);
            if (hit == std::string_view::npos) {
                break;
            }
            matched = true;
            out.append(text.substr(pos, hit - pos));
            out.append(m_options.replacement);
            pos = hit + pattern.size();
        }
        if (!matched) {
            return std::nullopt;
        }
        out.append(text.substr(pos));
        return out;
    }

    // Rebuilt subtree, or `node` itself when nothing in it changed.
    auto visit(const std::shared_ptr<const JsonNode>& node, ReplaceResult& result) const
        -> std::shared_ptr<const JsonNode> {
        switch (node->type) {
            case NodeType::String: {
                if (!m_options.inValues) {
                    return node;
                }
                bool splits = false;
                auto replaced = apply(node->value, splits);
                if (!replaced || *replaced == node->value) {
                    return node;
                }
                if (splits) {
                    ++result.valuesSkipped;
                    return node;
                }
                ++result.valuesChanged;
                return JsonNode::makeString(node->key, std::move(*replaced));
            }
            case NodeType::Object:
            case NodeType::Array:
                break;
            default:
                return node;
        }

        bool changed = false;
        std::vector<std::shared_ptr<const JsonNode>> children;
        children.reserve(node->children.size());
        for (const auto& child : node->children) {
            auto updated = visit(child, result);
            changed = changed || updated.get() != child.get();
            children.push_back(std::move(updated));
        }
        if (node->type == NodeType::Object && m_options.inKeys) {
            changed = renameKeys(children, result) || changed;
        }
        if (!changed) {
            return node;
        }
        return node->type == NodeType::Object
                   ? JsonNode::makeObject(node->key, std::move(children))
                   : JsonNode::makeArray(node->key, std::move(children));
    }

private:
    const ReplaceOptions& m_options;
    std::optional<std::regex> m_regex;
    std::string m_foldedPattern;

    auto find(std::string_view text, std::size_t from) const -> std::size_t {
        if (m_options.caseSensitive) {
            return text.find(m_options.pattern, from);
        }
        auto it = std::search(text.begin() + static_cast<std::ptrdiff_t>(from), text.end(),
                              m_foldedPattern.begin(), m_foldedPattern.end(),
                              [](char a, char b) { return ascii::toLower(a) == b; });
        return it == text.end() ? std::string_view::npos
                                : static_cast<std::size_t>(it - text.begin());
    }

    // Rename matching member keys in place; a rename that would collide with
    // a sibling's (final) key is skipped. Returns whether any key changed.
    auto renameKeys(std::vector<std::shared_ptr<const JsonNode>>& members,
                    ReplaceResult& result) const -> bool {
        std::vector<std::optional<std::string>> newKeys;
        newKeys.reserve(members.size());
        for (const auto& member : members) {
            bool splits = false;
            auto replaced = apply(member->key, splits);
            if (replaced && *replaced == member->key) {
                replaced.reset();
            }
            if (replaced && splits) {
                ++result.keysSkipped;
                replaced.reset();
            }
            newKeys.push_back(std::move(replaced));
        }

        // A skipped rename keeps its old key, which can make another rename
        // collide in turn: repeat until no rename collides. Each pass skips
        // every rename colliding at its start, so two keys renamed onto the
        // same new key are both skipped.
        std::vector<bool> skipped(members.size(), false);
        auto finalKey = [&](std::size_t i) -> std::string_view {
            return newKeys[i] && !skipped[i] ? std::string_view(*newKeys[i])
                                             : std::string_view(members[i]->key);
        };
        for (bool again = true; again;) {
            std::multiset<std::string_view> finalKeys;
            for (std::size_t i = 0; i < members.size(); ++i) {
                finalKeys.insert(finalKey(i));
            }
            again = false;
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (newKeys[i] && !skipped[i] && finalKeys.count(*newKeys[i]) > 1) {
                    skipped[i] = true;
                    ++result.keysSkipped;
                    again = true;
                }
            }
        }

        bool changed = false;
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (!newKeys[i] || skipped[i]) {
                continue;
            }
            const auto& member = members[i];
            members[i] = std::make_shared<JsonNode>(
                JsonNode{member->type, std::move(*newKeys[i]), member->value, member->children});
            ++result.keysChanged;
            changed = true;
        }
        return changed;
    }
};

} // anonymous namespace

auto replaceAll(std::shared_ptr<const JsonNode> root, const ReplaceOptions& options)
    -> ReplaceResult {
    ReplaceResult result;
    result.root = root;
    if (options.pattern.empty()) {
        result.error = "Enter the text to find";
        return result;
    }
    if (!root || (!options.inValues && !options.inKeys)) {
        return result;
    }

    std::optional<std::regex> regex;
    if (options.regex) {
        try {
            auto flags = std::regex::ECMAScript;
            if (!options.caseSensitive) {
                flags |= std::regex::icase;
            }
            regex.emplace(options.pattern, flags);
        } catch (const std::regex_error& e) {
            result.error = std::string("Invalid regex pattern: ") + e.what();
            return result;
        }
    }

    const Replacer replacer(options, std::move(regex));
    result.root = replacer.visit(root, result);
    return result;
}

} // namespace jsontitan::core
