#pragma once

#include <QAbstractItemModel>

#include <memory>
#include <unordered_set>
#include <vector>

#include "core/json_node.h"

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);

    void setRootNode(std::shared_ptr<const jsontitan::core::JsonNode> root);

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

private:
    // Internal node wrapper that tracks which children have been fetched
    struct InternalNode {
        const jsontitan::core::JsonNode* jsonNode = nullptr;
        InternalNode* parentNode = nullptr;
        int rowInParent = 0;
        int fetchedChildCount = 0;
        std::vector<std::unique_ptr<InternalNode>> childNodes;
    };

    InternalNode* nodeFromIndex(const QModelIndex& index) const;
    InternalNode* ensureChildNode(InternalNode* parent, int row) const;

    std::shared_ptr<const jsontitan::core::JsonNode> m_rootJsonNode;
    std::unique_ptr<InternalNode> m_rootInternal;

    static constexpr int FETCH_BATCH_SIZE = 100;
};
