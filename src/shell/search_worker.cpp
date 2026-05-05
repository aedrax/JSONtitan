#include "shell/search_worker.h"

#include "core/search_engine.h"

SearchWorker::SearchWorker(QObject* parent)
    : QObject(parent) {
    // Register metatypes for queued signal/slot connections across threads
    qRegisterMetaType<jsontitan::core::SearchQuery>("jsontitan::core::SearchQuery");
    qRegisterMetaType<jsontitan::core::FilterResult>("jsontitan::core::FilterResult");
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>(
        "std::shared_ptr<const jsontitan::core::JsonNode>");
    qRegisterMetaType<uint64_t>("uint64_t");
}

void SearchWorker::executeSearch(jsontitan::core::SearchQuery query,
                                 std::shared_ptr<const jsontitan::core::JsonNode> root,
                                 uint64_t generation) {
    if (!root) {
        emit searchComplete(jsontitan::core::FilterResult{.matches = {}, .error = std::nullopt},
                            generation);
        return;
    }

    auto result = jsontitan::core::filter(*root, query);
    emit searchComplete(std::move(result), generation);
}
