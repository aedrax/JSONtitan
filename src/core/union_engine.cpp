#include "core/union_engine.h"

namespace jsontitan::core {

auto unionTrees(std::span<const FileEntry> /*entries*/)
    -> std::shared_ptr<const JsonNode> {
    // Stub — will be implemented in Task 7
    return JsonNode::makeObject("", {});
}

auto removeFromUnion(const JsonNode& /*unionRoot*/, const std::string& /*filenameKey*/)
    -> std::shared_ptr<const JsonNode> {
    // Stub — will be implemented in Task 7
    return JsonNode::makeObject("", {});
}

} // namespace jsontitan::core
