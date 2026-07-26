#pragma once

#include <QAbstractItemModel>
#include <limits>

#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"
#include "core/parse_orchestrator.h"

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);

    void setRootNode(std::shared_ptr<const jsontitan::core::JsonNode> root);

    // Set root from an arena parse result (zero-copy path for simdjson backend).
    // Keeps the ArenaParseResult alive for the lifetime of the tree model.
    void setArenaRoot(std::shared_ptr<jsontitan::core::ArenaParseResult> result);

    // QAbstractItemModel required overrides
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    // Lazy loading
    bool hasChildren(const QModelIndex& parent) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    // Utility: format a node for display (exposed for property testing)
    static QString formatNodeDisplay(const jsontitan::core::JsonNode& node, int arrayIndex = -1);
    static QString formatNodeDisplay(const jsontitan::core::ArenaJsonNode& node, int arrayIndex = -1);

    // Get the JsonNode pointer for a given model index (returns nullptr for arena-backed nodes)
    const jsontitan::core::JsonNode* jsonNodeForIndex(const QModelIndex& index) const;

    // Get the ArenaJsonNode pointer for a given model index (returns nullptr for JsonNode-backed nodes)
    const jsontitan::core::ArenaJsonNode* arenaNodeForIndex(const QModelIndex& index) const;

    // Get the root JsonNode
    std::shared_ptr<const jsontitan::core::JsonNode> rootNode() const { return m_rootJsonNode; }

    // Get the arena parse result (nullptr if using JsonNode path).
    // Const element type: a const model must not hand out mutable arena state.
    std::shared_ptr<const jsontitan::core::ArenaParseResult> arenaResult() const { return m_arenaResult; }

private:
    // Internal node wrapper that tracks which children have been fetched.
    // Supports both JsonNode* (union mode) and ArenaJsonNode* (simdjson path).
    struct InternalNode {
        std::variant<const jsontitan::core::JsonNode*, const jsontitan::core::ArenaJsonNode*> nodeData;
        InternalNode* parentNode = nullptr;
        int rowInParent = 0;
        int fetchedChildCount = 0;
        std::vector<std::unique_ptr<InternalNode>> childNodes;

        // Convenience accessors
        [[nodiscard]] bool isArena() const {
            return std::holds_alternative<const jsontitan::core::ArenaJsonNode*>(nodeData);
        }

        [[nodiscard]] const jsontitan::core::JsonNode* jsonNode() const {
            if (auto* p = std::get_if<const jsontitan::core::JsonNode*>(&nodeData))
                return *p;
            return nullptr;
        }

        [[nodiscard]] const jsontitan::core::ArenaJsonNode* arenaNode() const {
            if (auto* p = std::get_if<const jsontitan::core::ArenaJsonNode*>(&nodeData))
                return *p;
            return nullptr;
        }

        // Total child count regardless of backing type, clamped to int:
        // Qt models are int-bounded, and a >2^31-element array must saturate
        // rather than overflow into a negative count.
        [[nodiscard]] int totalChildCount() const {
            std::size_t count = 0;
            if (auto* jn = jsonNode())
                count = jn->children.size();
            else if (auto* an = arenaNode())
                count = an->childCount;
            constexpr auto kMax =
                static_cast<std::size_t>(std::numeric_limits<int>::max());
            return static_cast<int>(count < kMax ? count : kMax);
        }
    };

    InternalNode* nodeFromIndex(const QModelIndex& index) const;
    InternalNode* ensureChildNode(InternalNode* parent, int row) const;

    std::shared_ptr<const jsontitan::core::JsonNode> m_rootJsonNode;
    std::shared_ptr<jsontitan::core::ArenaParseResult> m_arenaResult;
    std::unique_ptr<InternalNode> m_rootInternal;

    static constexpr int FETCH_BATCH_SIZE = 100;
};
