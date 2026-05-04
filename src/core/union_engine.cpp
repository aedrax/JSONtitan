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

        // Re-root the file's tree under the disambiguated key.
        // We create a new Object node with the key set to the filename,
        // carrying the original root's children.
        auto child = JsonNode::makeObject(std::move(key), entry.root->children);
        children.push_back(std::move(child));
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
