#pragma once

#include <QAbstractItemModel>

#include <memory>

namespace jsontitan::core {
struct JsonNode;
}

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);

    void setRootNode(std::shared_ptr<const jsontitan::core::JsonNode> root);

    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    bool hasChildren(const QModelIndex& parent) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
};
