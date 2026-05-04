#pragma once

#include <memory>
#include <string>
#include <vector>

namespace jsontitan::core {

enum class NodeType { Object, Array, String, Number, Boolean, Null };

struct JsonNode {
    NodeType type;
    std::string key;
    std::string value;
    std::vector<std::shared_ptr<const JsonNode>> children;

    static auto makeObject(std::string key,
                           std::vector<std::shared_ptr<const JsonNode>> children)
        -> std::shared_ptr<const JsonNode>;

    static auto makeArray(std::string key,
                          std::vector<std::shared_ptr<const JsonNode>> children)
        -> std::shared_ptr<const JsonNode>;

    static auto makeString(std::string key, std::string value)
        -> std::shared_ptr<const JsonNode>;

    static auto makeNumber(std::string key, std::string value)
        -> std::shared_ptr<const JsonNode>;

    static auto makeBool(std::string key, bool value)
        -> std::shared_ptr<const JsonNode>;

    static auto makeNull(std::string key)
        -> std::shared_ptr<const JsonNode>;
};

} // namespace jsontitan::core
