#pragma once

#include <QObject>

#include <cstdint>
#include <memory>

#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "core/search_engine.h"

class SearchWorker : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QObject* parent = nullptr);

public slots:
    void executeSearch(jsontitan::core::SearchQuery query,
                       std::shared_ptr<const jsontitan::core::JsonNode> root,
                       uint64_t generation);

    void executeArenaSearch(jsontitan::core::SearchQuery query,
                            std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                            uint64_t generation);

signals:
    void searchComplete(jsontitan::core::FilterResult result, uint64_t generation);
};

// Register types for cross-thread signal/slot connections
Q_DECLARE_METATYPE(jsontitan::core::SearchQuery)
Q_DECLARE_METATYPE(jsontitan::core::FilterResult)
