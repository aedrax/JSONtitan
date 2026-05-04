#include "core/json_node.h"

#include <utility>

namespace jsontitan::core {

auto JsonNode::makeObject(std::string key,
                          std::vector<std::shared_ptr<const JsonNode>> children)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::Object;
    node->key = std::move(key);
    node->children = std::move(children);
    return node;
}

auto JsonNode::makeArray(std::string key,
                         std::vector<std::shared_ptr<const JsonNode>> children)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::Array;
    node->key = std::move(key);
    node->children = std::move(children);
    return node;
}

auto JsonNode::makeString(std::string key, std::string value)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::String;
    node->key = std::move(key);
    node->value = std::move(value);
    return node;
}

auto JsonNode::makeNumber(std::string key, std::string value)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::Number;
    node->key = std::move(key);
    node->value = std::move(value);
    return node;
}

auto JsonNode::makeBool(std::string key, bool value)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::Boolean;
    node->key = std::move(key);
    node->value = value ? "true" : "false";
    return node;
}

auto JsonNode::makeNull(std::string key)
    -> std::shared_ptr<const JsonNode> {
    auto node = std::make_shared<JsonNode>();
    node->type = NodeType::Null;
    node->key = std::move(key);
    node->value = "null";
    return node;
}

} // namespace jsontitan::core
