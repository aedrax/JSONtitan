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

auto valueClipboardText(jsontitan::core::NodeView node) -> QString {
    using jsontitan::core::NodeType;
    switch (node.type()) {
    case NodeType::Object:
    case NodeType::Array: {
        // Compact JSON of the whole subtree, streamed into a string.
        std::string out;
        jsontitan::core::JsonExportOptions opts;
        opts.mode = jsontitan::core::IndentMode::Compact;
        opts.trailingNewline = false;
        jsontitan::core::exportJsonStream(
            node,
            [&out](std::string_view chunk) {
                out.append(chunk);
                return true;
            },
            opts);
        return QString::fromUtf8(out.data(),
                                 static_cast<qsizetype>(out.size()));
    }
    case NodeType::Null:
        return QStringLiteral("null");
    default: {
        std::string_view value = node.value();
        return QString::fromUtf8(value.data(),
                                 static_cast<qsizetype>(value.size()));
    }
    }
}

auto keyClipboardText(const jsontitan::core::NodePath& path)
    -> std::optional<QString> {
    if (path.empty()) {
        return std::nullopt;
    }
    const auto* key = std::get_if<std::string>(&path.back());
    if (!key) {
        return std::nullopt;  // array element: no key
    }
    return QString::fromStdString(*key);
}

}  // namespace jsontitan::shell
