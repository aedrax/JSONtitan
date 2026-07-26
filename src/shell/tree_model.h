#pragma once

#include <QAbstractItemModel>
#include <limits>

#include <functional>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "core/arena_json_node.h"
#include "core/json_node.h"
#include "core/node_view.h"
#include "core/parse_orchestrator.h"

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);

    // Columns: 0 = key (or [i]) with a scalar value preview, 1 = node type,
    // 2 = size (child count for containers, byte length for strings).
    enum Column {
        ColumnKeyValue = 0,
        ColumnType = 1,
        ColumnSize = 2,
        ColumnCountValue = 3,
    };

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
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Editing: scalar rows (String/Number/Boolean/Null, either backing) are
    // editable; EditRole returns the RAW value text ("true", "null", the
    // number/string text) rather than the composite display string.
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Invoked by setData with the source index and the editor's raw text.
    // Returns whether the commit was accepted. The handler is responsible
    // for validating and installing the edited tree; the model NEVER mutates
    // trees itself and emits nothing from setData (a synchronous model reset
    // during the delegate's commit would destroy the editor mid-commit).
    using EditCommitHandler =
        std::function<bool(const QModelIndex&, const QString&)>;
    void setEditCommitHandler(EditCommitHandler handler) {
        m_editCommitHandler = std::move(handler);
    }

    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    // Lazy loading
    bool hasChildren(const QModelIndex& parent) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    // Utility: format a node's column-0 display — the key (or [i]) plus a
    // truncated value preview for scalars; containers show the key alone
    // (their type and child count live in the Type/Size columns). Exposed
    // for property testing. The concrete-type overloads forward to the
    // NodeView implementation.
    static QString formatNodeDisplay(jsontitan::core::NodeView node, int arrayIndex = -1);
    static QString formatNodeDisplay(const jsontitan::core::JsonNode& node, int arrayIndex = -1);
    static QString formatNodeDisplay(const jsontitan::core::ArenaJsonNode& node, int arrayIndex = -1);

    // Column 1 text: Object/Array/String/Number/Boolean/Null.
    static QString typeText(jsontitan::core::NodeView node);
    // Column 2 text: "N items" for containers, "N B" for strings, empty
    // otherwise.
    static QString sizeText(jsontitan::core::NodeView node);

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
            return std::get_if<const jsontitan::core::ArenaJsonNode*>(&nodeData) != nullptr;
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
                static_cast<std::size_t>((std::numeric_limits<int>::max)());
            return static_cast<int>(count < kMax ? count : kMax);
        }
    };

    InternalNode* nodeFromIndex(const QModelIndex& index) const;
    InternalNode* ensureChildNode(InternalNode* parent, int row) const;

    std::shared_ptr<const jsontitan::core::JsonNode> m_rootJsonNode;
    std::shared_ptr<jsontitan::core::ArenaParseResult> m_arenaResult;
    std::unique_ptr<InternalNode> m_rootInternal;
    EditCommitHandler m_editCommitHandler;

    static constexpr int FETCH_BATCH_SIZE = 100;
};
