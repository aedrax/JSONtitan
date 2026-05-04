#pragma once

#include <memory>
#include <span>
#include <string>

#include "core/json_node.h"

namespace jsontitan::core {

struct FileEntry {
    std::string filename;
    std::shared_ptr<const JsonNode> root;
};

auto unionTrees(std::span<const FileEntry> entries)
    -> std::shared_ptr<const JsonNode>;

auto removeFromUnion(const JsonNode& unionRoot, const std::string& filenameKey)
    -> std::shared_ptr<const JsonNode>;

} // namespace jsontitan::core
