#pragma once

#include <QString>

#include <memory>

namespace jsontitan::core {
struct JsonNode;
}

class SaveHandler {
public:
    // Save the tree to the given file path using atomic write.
    // Returns empty QString on success, error description on failure.
    static auto saveToFile(const jsontitan::core::JsonNode& tree,
                           const QString& filePath) -> QString;
};
