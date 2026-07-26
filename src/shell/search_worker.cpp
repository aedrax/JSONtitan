#include "shell/search_worker.h"

#include "core/search_engine.h"

SearchWorker::SearchWorker(QObject* parent)
    : QObject(parent) {
    // Register metatypes for queued signal/slot connections across threads
    qRegisterMetaType<jsontitan::core::SearchQuery>("jsontitan::core::SearchQuery");
    qRegisterMetaType<jsontitan::core::FilterResult>("jsontitan::core::FilterResult");
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>(
        "std::shared_ptr<const jsontitan::core::JsonNode>");
    qRegisterMetaType<std::shared_ptr<jsontitan::core::ArenaParseResult>>(
        "std::shared_ptr<jsontitan::core::ArenaParseResult>");
    qRegisterMetaType<uint64_t>("uint64_t");
}

void SearchWorker::updateLatestGeneration(uint64_t generation) {
    // Monotonic max: never let an older caller lower the bar.
    uint64_t current = m_latestGeneration.load(std::memory_order_relaxed);
    while (current < generation &&
           !m_latestGeneration.compare_exchange_weak(current, generation,
                                                     std::memory_order_relaxed)) {
    }
}

bool SearchWorker::isStale(uint64_t generation) const {
    return generation < m_latestGeneration.load(std::memory_order_relaxed);
}

void SearchWorker::executeSearch(jsontitan::core::SearchQuery query,
                                 std::shared_ptr<const jsontitan::core::JsonNode> root,
                                 uint64_t generation) {
    if (isStale(generation)) {
        return;  // superseded while queued
    }
    if (!root) {
        emit searchComplete(jsontitan::core::FilterResult{.matches = {}, .error = std::nullopt},
                            generation);
        return;
    }

    auto result = jsontitan::core::filter(
        *root, query, [this, generation]() { return isStale(generation); });
    if (isStale(generation)) {
        return;  // aborted (or completed just before supersession): discard
    }
    emit searchComplete(std::move(result), generation);
}

void SearchWorker::executeArenaSearch(jsontitan::core::SearchQuery query,
                                      std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                                      uint64_t generation) {
    if (isStale(generation)) {
        return;  // superseded while queued
    }
    if (!result || !result->root) {
        emit searchComplete(jsontitan::core::FilterResult{.matches = {}, .error = std::nullopt},
                            generation);
        return;
    }

    auto filterResult = jsontitan::core::filter(
        *result->root, query, [this, generation]() { return isStale(generation); });
    if (isStale(generation)) {
        return;
    }
    emit searchComplete(std::move(filterResult), generation);
}
