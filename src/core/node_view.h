#pragma once

#include <cstddef>
#include <string_view>
#include <variant>

#include "core/arena_json_node.h"
#include "core/json_node.h"

namespace jsontitan::core {

// A lightweight, non-owning view over either tree backing (JsonNode or
// ArenaJsonNode). Lets serialization/emission code be written once against a
// uniform read-only interface instead of duplicating it per backing — and
// eliminates the toJsonNode() deep-copies that were previously required to
// feed arena-backed trees into JsonNode-only consumers.
//
// Lifetime: NodeView never owns the node. The underlying tree (shared_ptr
// tree or arena) must outlive every NodeView referring into it.
class NodeView {
public:
    /*implicit*/ NodeView(const JsonNode& n) : m_ptr(&n) {}
    /*implicit*/ NodeView(const ArenaJsonNode& n) : m_ptr(&n) {}

    [[nodiscard]] NodeType type() const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return (*jn)->type;
        }
        return std::get<const ArenaJsonNode*>(m_ptr)->type;
    }

    [[nodiscard]] std::string_view key() const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return (*jn)->key;
        }
        return std::get<const ArenaJsonNode*>(m_ptr)->keyView();
    }

    [[nodiscard]] std::string_view value() const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return (*jn)->value;
        }
        return std::get<const ArenaJsonNode*>(m_ptr)->valueView();
    }

    [[nodiscard]] std::size_t childCount() const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return (*jn)->children.size();
        }
        return std::get<const ArenaJsonNode*>(m_ptr)->childCount;
    }

    // Precondition: i < childCount().
    [[nodiscard]] NodeView child(std::size_t i) const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return NodeView(*(*jn)->children[i]);
        }
        return NodeView(*std::get<const ArenaJsonNode*>(m_ptr)->children[i]);
    }

    [[nodiscard]] bool isArena() const {
        return std::holds_alternative<const ArenaJsonNode*>(m_ptr);
    }

    // Raw JsonNode pointer for callers that need the concrete legacy type.
    // Returns nullptr when this view is arena-backed.
    [[nodiscard]] const JsonNode* asJsonNode() const {
        if (const auto* jn = std::get_if<const JsonNode*>(&m_ptr)) {
            return *jn;
        }
        return nullptr;
    }

private:
    std::variant<const JsonNode*, const ArenaJsonNode*> m_ptr;
};

} // namespace jsontitan::core
