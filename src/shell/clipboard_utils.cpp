#include "shell/clipboard_utils.h"

#include <string>
#include <string_view>
#include <variant>

#include "core/stream_export.h"

namespace jsontitan::shell {

namespace {

// A key can use bare dot notation when it matches [A-Za-z_][A-Za-z0-9_]*.
bool isBareKey(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    const auto isAlpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    const auto isAlnum = [&isAlpha](char c) {
        return isAlpha(c) || (c >= '0' && c <= '9');
    };
    if (!isAlpha(key.front())) {
        return false;
    }
    for (char c : key.substr(1)) {
        if (!isAlnum(c)) {
            return false;
        }
    }
    return true;
}

QString bracketQuoted(std::string_view key) {
    std::string escaped;
    escaped.reserve(key.size() + 4);
    escaped += "['";
    for (char c : key) {
        if (c == '\\' || c == '\'') {
            escaped += '\\';
        }
        escaped += c;
    }
    escaped += "']";
    return QString::fromUtf8(escaped.data(),
                             static_cast<qsizetype>(escaped.size()));
}

}  // namespace

auto jsonPathText(const jsontitan::core::NodePath& path) -> QString {
    QString text = QStringLiteral("$");
    for (const auto& segment : path) {
        if (const auto* key = std::get_if<std::string>(&segment)) {
            if (isBareKey(*key)) {
                text += QLatin1Char('.');
                text += QString::fromStdString(*key);
            } else {
                text += bracketQuoted(*key);
            }
        } else {
            text += QStringLiteral("[%1]").arg(std::get<std::size_t>(segment));
        }
    }
    return text;
}

auto valueClipboardText(jsontitan::core::NodeView node, std::size_t maxBytes)
    -> std::optional<QString> {
    using jsontitan::core::NodeType;
    switch (node.type()) {
    case NodeType::Object:
    case NodeType::Array: {
        // Compact JSON of the whole subtree, streamed into a string that
        // stops growing at the limit.
        std::string out;
        jsontitan::core::JsonExportOptions opts;
        opts.mode = jsontitan::core::IndentMode::Compact;
        opts.trailingNewline = false;
        const auto result = jsontitan::core::exportJsonStream(
            node,
            [&out, maxBytes](std::string_view chunk) {
                if (out.size() + chunk.size() > maxBytes) {
                    return false;
                }
                out.append(chunk);
                return true;
            },
            opts);
        if (!result.ok) {
            return std::nullopt;
        }
        return QString::fromUtf8(out.data(),
                                 static_cast<qsizetype>(out.size()));
    }
    case NodeType::Null:
        return QStringLiteral("null");
    default: {
        std::string_view value = node.value();
        if (value.size() > maxBytes) {
            return std::nullopt;
        }
        return QString::fromUtf8(value.data(),
                                 static_cast<qsizetype>(value.size()));
    }
    }
}

auto keyClipboardText(const TreeModel& model, const QModelIndex& sourceIndex)
    -> std::optional<QString> {
    if (!sourceIndex.isValid()) {
        return std::nullopt;  // the root
    }
    // Read from the node, not its path: a later member with a duplicated
    // key is named by position in its path.
    const QModelIndex parent = sourceIndex.parent();
    const auto* parentJson = model.jsonNodeForIndex(parent);
    const auto* parentArena = model.arenaNodeForIndex(parent);
    const auto parentType = parentJson    ? parentJson->type
                            : parentArena ? parentArena->type
                                          : jsontitan::core::NodeType::Array;
    if (parentType != jsontitan::core::NodeType::Object) {
        return std::nullopt;  // array element
    }
    std::string_view key;
    if (const auto* node = model.jsonNodeForIndex(sourceIndex)) {
        key = node->key;
    } else if (const auto* node = model.arenaNodeForIndex(sourceIndex)) {
        key = node->keyView();
    }
    return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
}

}  // namespace jsontitan::shell
