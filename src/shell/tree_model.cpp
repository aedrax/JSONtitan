#include "shell/tree_model.h"

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

void TreeModel::setRootNode(std::shared_ptr<const jsontitan::core::JsonNode> /*root*/) {
    // Stub — will be implemented in Task 11
}

QModelIndex TreeModel::index(int /*row*/, int /*column*/, const QModelIndex& /*parent*/) const {
    return {};
}

QModelIndex TreeModel::parent(const QModelIndex& /*child*/) const {
    return {};
}

int TreeModel::rowCount(const QModelIndex& /*parent*/) const {
    return 0;
}

int TreeModel::columnCount(const QModelIndex& /*parent*/) const {
    return 1;
}

QVariant TreeModel::data(const QModelIndex& /*index*/, int /*role*/) const {
    return {};
}

bool TreeModel::hasChildren(const QModelIndex& /*parent*/) const {
    return false;
}

bool TreeModel::canFetchMore(const QModelIndex& /*parent*/) const {
    return false;
}

void TreeModel::fetchMore(const QModelIndex& /*parent*/) {
    // Stub — will be implemented in Task 11
}
