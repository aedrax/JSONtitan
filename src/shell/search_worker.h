#pragma once

#include <QObject>

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "core/search_engine.h"

class SearchWorker : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QObject* parent = nullptr);

    // Thread-safe (atomic store); call directly from the GUI thread, never
    // via a queued slot (the worker's event loop is busy while searching).
    // Queued searches older than this generation are skipped entirely and
    // the currently-running search aborts at its next cancellation poll —
    // previously every stale keystroke's search ran to completion, pinning
    // a core for the full tree walk before its result was discarded.
    void updateLatestGeneration(uint64_t generation);

public slots:
    void executeSearch(jsontitan::core::SearchQuery query,
                       std::shared_ptr<const jsontitan::core::JsonNode> root,
                       uint64_t generation);

    void executeArenaSearch(jsontitan::core::SearchQuery query,
                            std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                            uint64_t generation);

signals:
    void searchComplete(jsontitan::core::FilterResult result, uint64_t generation);

private:
    // True when `generation` has been superseded by a newer dispatch.
    bool isStale(uint64_t generation) const;

    std::atomic<uint64_t> m_latestGeneration{0};
};

// Register types for cross-thread signal/slot connections
Q_DECLARE_METATYPE(jsontitan::core::SearchQuery)
Q_DECLARE_METATYPE(jsontitan::core::FilterResult)
