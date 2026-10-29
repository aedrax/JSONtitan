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

// True when `text` is well-formed UTF-8 (RFC 3629: no overlong forms,
// surrogates, or code points past U+10FFFF).
auto isValidUtf8(std::string_view text) -> bool {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t continuations = 0;
        unsigned char low = 0x80;  // allowed range of the first continuation byte
        unsigned char high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            continuations = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuations = 2;
            low = lead == 0xE0 ? 0xA0 : 0x80;
            high = lead == 0xED ? 0x9F : 0xBF;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuations = 3;
            low = lead == 0xF0 ? 0x90 : 0x80;
            high = lead == 0xF4 ? 0x8F : 0xBF;
        } else {
            return false;
        }
        if (i + continuations >= text.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= continuations; ++k) {
            const auto byte = static_cast<unsigned char>(text[i + k]);
            if (byte < (k == 1 ? low : 0x80) || byte > (k == 1 ? high : 0xBF)) {
                return false;
            }
        }
        i += continuations + 1;
    }
    return true;
}

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
    auto apply(std::string_view text) const -> std::optional<std::string> {
        if (m_regex) {
            if (!std::regex_search(text.begin(), text.end(), *m_regex)) {
                return std::nullopt;
            }
            std::string out;
            std::regex_replace(std::back_inserter(out), text.begin(), text.end(), *m_regex,
                               m_options.replacement);
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
                auto replaced = apply(node->value);
                if (!replaced || *replaced == node->value) {
                    return node;
                }
                if (splitsCharacter(*replaced)) {
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

    // A regex works on bytes, so a match can cut a multi-byte character in
    // half. (Substring matches cannot: the pattern is whole UTF-8 itself.)
    auto splitsCharacter(std::string_view replaced) const -> bool {
        return m_regex && !isValidUtf8(replaced);
    }

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
            auto replaced = apply(member->key);
            if (replaced && *replaced == member->key) {
                replaced.reset();
            }
            if (replaced && splitsCharacter(*replaced)) {
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
