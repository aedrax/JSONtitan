#include "shell/tree_model.h"

#include <optional>
#include <string_view>

using namespace jsontitan::core;

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

void TreeModel::setRootNode(std::shared_ptr<const JsonNode> root) {
    beginResetModel();
    m_rootJsonNode = std::move(root);
    m_arenaResult.reset();
    m_rootInternal.reset();

    if (m_rootJsonNode) {
        m_rootInternal = std::make_unique<InternalNode>();
        m_rootInternal->nodeData = m_rootJsonNode.get();
        m_rootInternal->parentNode = nullptr;
        m_rootInternal->rowInParent = 0;
        m_rootInternal->fetchedChildCount = 0;
    }
    endResetModel();
}

void TreeModel::setArenaRoot(std::shared_ptr<ArenaParseResult> result) {
    beginResetModel();
    m_arenaResult = std::move(result);
    m_rootJsonNode.reset();
    m_rootInternal.reset();

    if (m_arenaResult && m_arenaResult->root) {
        m_rootInternal = std::make_unique<InternalNode>();
        m_rootInternal->nodeData = static_cast<const ArenaJsonNode*>(m_arenaResult->root);
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
    if (!parent)
        return nullptr;

    // Grow the childNodes vector if needed
    if (row >= static_cast<int>(parent->childNodes.size())) {
        parent->childNodes.resize(static_cast<size_t>(row + 1));
    }

    if (!parent->childNodes[static_cast<size_t>(row)]) {
        auto child = std::make_unique<InternalNode>();
        child->parentNode = parent;
        child->rowInParent = row;
        child->fetchedChildCount = 0;

        if (auto* jn = parent->jsonNode()) {
            child->nodeData = jn->children[static_cast<size_t>(row)].get();
        } else if (auto* an = parent->arenaNode()) {
            child->nodeData = static_cast<const ArenaJsonNode*>(an->children[static_cast<size_t>(row)]);
        } else {
            return nullptr;
        }

        parent->childNodes[static_cast<size_t>(row)] = std::move(child);
    }

    return parent->childNodes[static_cast<size_t>(row)].get();
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!m_rootInternal || column < 0 || column >= ColumnCountValue)
        return {};
    // Standard tree-model convention: only column-0 indexes have children.
    if (parent.isValid() && parent.column() != 0)
        return {};

    auto* parentNode = nodeFromIndex(parent);
    if (!parentNode || parentNode->totalChildCount() == 0)
        return {};

    if (row < 0 || row >= parentNode->fetchedChildCount)
        return {};

    // ensureChildNode mutates only the lazily-built fetch cache, not the
    // logical model state (nodeFromIndex already returns a mutable pointer).
    auto* childNode = ensureChildNode(parentNode, row);
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
    if (parent.isValid() && parent.column() != 0)
        return 0;

    auto* node = nodeFromIndex(parent);
    if (!node)
        return 0;

    return node->fetchedChildCount;
}

int TreeModel::columnCount(const QModelIndex& /*parent*/) const {
    return ColumnCountValue;
}

QVariant TreeModel::headerData(int section, Qt::Orientation orientation,
                               int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColumnKeyValue:
        return tr("Key / Value");
    case ColumnType:
        return tr("Type");
    case ColumnSize:
        return tr("Size");
    default:
        return {};
    }
}

QVariant TreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
        return {};

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node)
        return {};

    // A uniform view over whichever backing this row belongs to.
    std::optional<NodeView> view;
    if (auto* jn = node->jsonNode()) {
        view.emplace(*jn);
    } else if (auto* an = node->arenaNode()) {
        view.emplace(*an);
    } else {
        return {};
    }

    if (role == Qt::EditRole) {
        // Raw scalar value text — what an inline editor should show, not the
        // composite "key: value" display string. Containers are not
        // editable, and only column 0 carries the editable value.
        if (index.column() != ColumnKeyValue)
            return {};
        switch (view->type()) {
        case NodeType::Object:
        case NodeType::Array:
            return {};
        case NodeType::Null:
            return QStringLiteral("null");
        default: {
            std::string_view valStr = view->value();
            return QString::fromUtf8(valStr.data(),
                                     static_cast<qsizetype>(valStr.size()));
        }
        }
    }

    switch (index.column()) {
    case ColumnKeyValue: {
        // Determine array index: if parent is an Array type, use the row
        int arrayIndex = -1;
        if (node->parentNode) {
            if (auto* parentJn = node->parentNode->jsonNode()) {
                if (parentJn->type == NodeType::Array)
                    arrayIndex = node->rowInParent;
            } else if (auto* parentAn = node->parentNode->arenaNode()) {
                if (parentAn->type == NodeType::Array)
                    arrayIndex = node->rowInParent;
            }
        }
        return formatNodeDisplay(*view, arrayIndex);
    }
    case ColumnType:
        return typeText(*view);
    case ColumnSize: {
        QString size = sizeText(*view);
        return size.isEmpty() ? QVariant() : QVariant(size);
    }
    default:
        return {};
    }
}

Qt::ItemFlags TreeModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags itemFlags = QAbstractItemModel::flags(index);
    if (!index.isValid())
        return itemFlags;

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node)
        return itemFlags;

    NodeType type;
    if (auto* jn = node->jsonNode()) {
        type = jn->type;
    } else if (auto* an = node->arenaNode()) {
        type = an->type;
    } else {
        return itemFlags;
    }

    // Only the key/value column of scalar rows is editable.
    if (index.column() == ColumnKeyValue &&
        type != NodeType::Object && type != NodeType::Array) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

bool TreeModel::setData(const QModelIndex& index, const QVariant& value,
                        int role) {
    if (role != Qt::EditRole || !index.isValid() || !m_editCommitHandler)
        return false;

    // Delegate the commit to the injected handler. The model never mutates
    // trees itself and deliberately emits nothing here: setData runs inside
    // the item delegate's commit, and the handler installs the edited tree
    // via a QUEUED model reset (a synchronous reset would destroy the editor
    // that is still committing).
    return m_editCommitHandler(index, value.toString());
}

bool TreeModel::hasChildren(const QModelIndex& parent) const {
    if (!m_rootInternal)
        return false;
    if (parent.isValid() && parent.column() != 0)
        return false;

    auto* node = nodeFromIndex(parent);
    if (!node)
        return false;

    return node->totalChildCount() > 0;
}

bool TreeModel::canFetchMore(const QModelIndex& parent) const {
    if (!m_rootInternal)
        return false;
    if (parent.isValid() && parent.column() != 0)
        return false;

    auto* node = nodeFromIndex(parent);
    if (!node)
        return false;

    return node->fetchedChildCount < node->totalChildCount();
}

void TreeModel::fetchMore(const QModelIndex& parent) {
    if (!m_rootInternal)
        return;

    auto* node = nodeFromIndex(parent);
    if (!node)
        return;

    int totalChildren = node->totalChildCount();
    int currentFetched = node->fetchedChildCount;
    int remaining = totalChildren - currentFetched;
    int toFetch = (std::min)(remaining, FETCH_BATCH_SIZE);

    if (toFetch <= 0)
        return;

    beginInsertRows(parent, currentFetched, currentFetched + toFetch - 1);
    node->fetchedChildCount += toFetch;
    endInsertRows();
}

const JsonNode* TreeModel::jsonNodeForIndex(const QModelIndex& index) const {
    if (!index.isValid())
        return m_rootInternal ? m_rootInternal->jsonNode() : nullptr;

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node)
        return nullptr;
    return node->jsonNode();
}

const ArenaJsonNode* TreeModel::arenaNodeForIndex(const QModelIndex& index) const {
    if (!index.isValid())
        return m_rootInternal ? m_rootInternal->arenaNode() : nullptr;

    auto* node = static_cast<InternalNode*>(index.internalPointer());
    if (!node)
        return nullptr;
    return node->arenaNode();
}

QString TreeModel::formatNodeDisplay(NodeView node, int arrayIndex) {
    QString display;

    // Key or array index
    if (arrayIndex >= 0) {
        display = QStringLiteral("[%1]").arg(arrayIndex);
    } else {
        std::string_view keyStr = node.key();
        if (!keyStr.empty()) {
            display = QString::fromUtf8(keyStr.data(),
                                        static_cast<qsizetype>(keyStr.size()));
        }
    }

    // Scalars carry a (truncated) value preview; containers show the key
    // alone — their type and child count live in the Type/Size columns.
    switch (node.type()) {
    case NodeType::Object:
    case NodeType::Array:
        break;

    case NodeType::String: {
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QStringLiteral("\"");
        std::string_view valStr = node.value();
        QString val = QString::fromUtf8(valStr.data(),
                                        static_cast<qsizetype>(valStr.size()));
        if (val.size() > 50) {
            display += val.left(50) + QStringLiteral("...");
        } else {
            display += val;
        }
        display += QStringLiteral("\"");
        break;
    }

    case NodeType::Number:
    case NodeType::Boolean: {
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        std::string_view valStr = node.value();
        display += QString::fromUtf8(valStr.data(),
                                     static_cast<qsizetype>(valStr.size()));
        break;
    }

    case NodeType::Null:
        if (!display.isEmpty())
            display += QStringLiteral(": ");
        display += QStringLiteral("null");
        break;
    }

    return display;
}

QString TreeModel::formatNodeDisplay(const JsonNode& node, int arrayIndex) {
    return formatNodeDisplay(NodeView(node), arrayIndex);
}

QString TreeModel::formatNodeDisplay(const ArenaJsonNode& node, int arrayIndex) {
    return formatNodeDisplay(NodeView(node), arrayIndex);
}

QString TreeModel::typeText(NodeView node) {
    switch (node.type()) {
    case NodeType::Object:  return QStringLiteral("Object");
    case NodeType::Array:   return QStringLiteral("Array");
    case NodeType::String:  return QStringLiteral("String");
    case NodeType::Number:  return QStringLiteral("Number");
    case NodeType::Boolean: return QStringLiteral("Boolean");
    case NodeType::Null:    return QStringLiteral("Null");
    }
    return {};
}

QString TreeModel::sizeText(NodeView node) {
    switch (node.type()) {
    case NodeType::Object:
    case NodeType::Array: {
        const std::size_t count = node.childCount();
        return count == 1 ? QStringLiteral("1 item")
                          : QStringLiteral("%1 items").arg(count);
    }
    case NodeType::String:
        return QStringLiteral("%1 B").arg(node.value().size());
    default:
        return {};
    }
}
