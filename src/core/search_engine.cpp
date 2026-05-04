#include "core/search_engine.h"

namespace jsontitan::core {

auto filter(const JsonNode& /*root*/, const SearchQuery& /*query*/) -> FilterResult {
    // Stub — will be implemented in Task 6
    return FilterResult{.matches = {}, .error = std::nullopt};
}

} // namespace jsontitan::core
