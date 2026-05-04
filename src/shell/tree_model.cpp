#include "shell/tree_model.h"

using namespace jsontitan::core;

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

void TreeModel::setRootNode(std::shared_ptr<const JsonNode> root) {
    beginResetModel();
    m_rootJsonNode = std::move(root);
    m_rootInternal.reset();

    if (m_rootJsonNode) {
        m_rootInternal = std::make_unique<InternalNode>();
        m_rootInternal->jsonNode = m_rootJsonNode.get();
        m_rootInternal->parentNode = nullptr;
        m_rootInternal->rowInParent = 0;
        m_rootInternal->fetchedChildCount = 0;
    }
    endResetModel();
}

TreeModel::InternalNode* TreeModel::nodeFromIndex(const QModelIndex& index) const {
    if (!index.isValid())
        return m_rootInternal.get();
    return static_cast<InternalNode*>(index.internalPointer());
}

TreeModel::InternalNode* TreeModel::ensureChildNode(InternalNode* parent, int row) const {
    if (!parent || !parent->jsonNode)
        return nullptr;

    // Grow the childNodes vector if needed
    if (row >= static_cast<int>(parent->childNodes.size())) {
        parent->childNodes.resize(static_cast<size_t>(row + 1));
    }

    if (!parent->childNodes[static_cast<size_t>(row)]) {
        auto child = std::make_unique<InternalNode>();
        child->jsonNode = parent->jsonNode->children[static_cast<size_t>(row)].get();
        child->parentNode = parent;
        child->rowInParent = row;
        child->fetchedChildCount = 0;
        parent->childNodes[static_cast<size_t>(row)] = std::move(child);
    }

    return parent->childNodes[static_cast<size_t>(row)].get();
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!m_rootInternal || column != 0)
        return {};

    auto* parentNode = nodeFromIndex(parent);
    if (!parentNode || !parentNode->jsonNode)
        return {};

    if (row < 0 || row >= parentNode->fetchedChildCount)
        return {};

    // Use const_cast because ensureChildNode mutates the internal cache
    // but does not change the logical model state
    auto* mutableParent = const_cast<InternalNode*>(parentNode);
    auto* childNode = ensureChildNode(mutableParent, row);
    if (!childNode)
        return {};

    return createIndex(row, column, childNode);
}

QModelIndex TreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid())
        return {};

    auto* childNode = static_cast<InternalNode*>(child.internalPointer());
    if (!childNode || !childNode->parentNode)
        return {};

    // If the parent is the root, return invalid index (root has no parent)
    if (childNode->parentNode == m_rootInternal.get())
        return {};

    return createIndex(childNode->parentNode->rowInParent, 0, childNode->parentNode);
}

int TreeModel::rowCount(const QModelIndex& parent) const {
    if (!m_rootInternal)
        return 0;

    auto* node = nodeFromIndex(parent);
    if (!node || !node->jsonNode)
        return 0;

    return node->fetchedChildCount;
}

int TreeModel::columnCount(const QModelIndex& /*parent*/) const {
    return 1;
}

QVariant TreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole)
        return {};

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node || !node->jsonNode)
        return {};

    // Determine array index: if parent is an Array type, use the row
    int arrayIndex = -1;
    if (node->parentNode && node->parentNode->jsonNode &&
        node->parentNode->jsonNode->type == NodeType::Array) {
        arrayIndex = node->rowInParent;
    }

    return formatNodeDisplay(*node->jsonNode, arrayIndex);
}

bool TreeModel::hasChildren(const QModelIndex& parent) const {
    if (!m_rootInternal)
        return false;

    auto* node = nodeFromIndex(parent);
    if (!node || !node->jsonNode)
        return false;

    // Check directly on the JsonNode without triggering rowCount
    return !node->jsonNode->children.empty();
}

bool TreeModel::canFetchMore(const QModelIndex& parent) const {
    if (!m_rootInternal)
        return false;

    auto* node = nodeFromIndex(parent);
    if (!node || !node->jsonNode)
        return false;

    return node->fetchedChildCount < static_cast<int>(node->jsonNode->children.size());
}

void TreeModel::fetchMore(const QModelIndex& parent) {
    if (!m_rootInternal)
        return;

    auto* node = nodeFromIndex(parent);
    if (!node || !node->jsonNode)
        return;

    int totalChildren = static_cast<int>(node->jsonNode->children.size());
    int currentFetched = node->fetchedChildCount;
    int remaining = totalChildren - currentFetched;
    int toFetch = std::min(remaining, FETCH_BATCH_SIZE);

    if (toFetch <= 0)
        return;

    beginInsertRows(parent, currentFetched, currentFetched + toFetch - 1);
    node->fetchedChildCount += toFetch;
    endInsertRows();
}

const JsonNode* TreeModel::jsonNodeForIndex(const QModelIndex& index) const {
    if (!index.isValid())
        return m_rootInternal ? m_rootInternal->jsonNode : nullptr;

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node)
        return nullptr;
    return node->jsonNode;
}

QString TreeModel::formatNodeDisplay(const JsonNode& node, int arrayIndex) {
    QString display;

    // Key or array index
    if (arrayIndex >= 0) {
        display = QStringLiteral("[%1]").arg(arrayIndex);
    } else if (!node.key.empty()) {
        display = QString::fromStdString(node.key);
    }

    // Type indicator and value preview
    switch (node.type) {
    case NodeType::Object:
        if (!display.isEmpty())
            display += QStringLiteral(" ");
        display += QStringLiteral("{Object}");
        if (!node.children.empty()) {
            display += QStringLiteral(" (%1 items)").arg(node.children.size());
        }
        break;

    case NodeType::Array:
        if (!display.isEmpty())
            display += QStringLiteral(" ");
        display += QStringLiteral("[Array]");
        if (!node.children.empty()) {
            display += QStringLiteral(" (%1 items)").arg(node.children.size());
        }
        break;

    case NodeType::String:
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QStringLiteral("\"");
        {
            QString val = QString::fromStdString(node.value);
            if (val.length() > 50) {
                display += val.left(50) + QStringLiteral("...");
            } else {
                display += val;
            }
        }
        display += QStringLiteral("\"");
        break;

    case NodeType::Number:
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QString::fromStdString(node.value);
        break;

    case NodeType::Boolean:
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QString::fromStdString(node.value);
        break;

    case NodeType::Null:
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QStringLiteral("null");
        break;
    }

    return display;
}
