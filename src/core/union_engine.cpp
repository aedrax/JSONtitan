#include "core/union_engine.h"

#include <unordered_map>
#include <utility>

namespace jsontitan::core {

auto unionTrees(std::span<const FileEntry> entries)
    -> std::shared_ptr<const JsonNode> {
    // Track how many times each filename has appeared so far
    std::unordered_map<std::string, int> nameCount;
    std::vector<std::shared_ptr<const JsonNode>> children;
    children.reserve(entries.size());

    for (const auto& entry : entries) {
        auto it = nameCount.find(entry.filename);
        std::string key;

        if (it == nameCount.end()) {
            // First occurrence — use the original filename
            nameCount[entry.filename] = 1;
            key = entry.filename;
        } else {
            // Duplicate — increment count and append suffix
            it->second += 1;
            key = entry.filename + " (" + std::to_string(it->second) + ")";
        }

        // Re-root the file's tree under the disambiguated key: same type,
        // value and children (an array or scalar root stays one).
        const auto& root = *entry.root;
        children.push_back(std::make_shared<JsonNode>(
            JsonNode{root.type, std::move(key), root.value, root.children}));
    }

    // Synthetic root: an Object with empty key
    return JsonNode::makeObject("", std::move(children));
}

auto removeFromUnion(const JsonNode& unionRoot, const std::string& filenameKey)
    -> std::shared_ptr<const JsonNode> {
    std::vector<std::shared_ptr<const JsonNode>> remaining;
    remaining.reserve(unionRoot.children.size());

    for (const auto& child : unionRoot.children) {
        if (child->key != filenameKey) {
            remaining.push_back(child);
        }
    }

    return JsonNode::makeObject("", std::move(remaining));
}

} // namespace jsontitan::core
