#pragma once

#include <cstddef>
#include <memory>

#include "core/json_node.h"
#include "core/source_buffer.h"

namespace jsontitan::core {

// An arena-allocated JSON node that uses StringRef instead of std::string
// and raw pointers instead of shared_ptr. This is the internal representation
// used during parsing; it can be converted to the public JsonNode type at
// the API boundary.
struct ArenaJsonNode {
    NodeType type = NodeType::Null;
    StringRef key{};
    StringRef value{};
    ArenaJsonNode** children = nullptr;
    std::size_t childCount = 0;

    // Convert this subtree to the public JsonNode type (deep copy).
    // Materializes all StringRef values into owned std::string instances.
    [[nodiscard]] auto toJsonNode() const -> std::shared_ptr<const JsonNode> {
        std::vector<std::shared_ptr<const JsonNode>> childVec;
        childVec.reserve(childCount);
        for (std::size_t i = 0; i < childCount; ++i) {
            childVec.push_back(children[i]->toJsonNode());
        }

        switch (type) {
            case NodeType::Object:
                return JsonNode::makeObject(key.toString(), std::move(childVec));
            case NodeType::Array:
                return JsonNode::makeArray(key.toString(), std::move(childVec));
            case NodeType::String:
                return JsonNode::makeString(key.toString(), value.toString());
            case NodeType::Number:
                return JsonNode::makeNumber(key.toString(), value.toString());
            case NodeType::Boolean:
                return JsonNode::makeBool(key.toString(), value.view() == "true");
            case NodeType::Null:
                return JsonNode::makeNull(key.toString());
        }

        // Unreachable, but satisfies compiler warnings.
        return JsonNode::makeNull(key.toString());
    }
};

} // namespace jsontitan::core
