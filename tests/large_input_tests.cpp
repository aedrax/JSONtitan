// ---------------------------------------------------------------------------
// Large-input regression tests: each needs a real input of a size the bug
// only shows at, so they are labelled "large" (ctest -LE large skips them).
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <string>

#include "core/arena_allocator.h"
#include "core/simdjson_adapter.h"

using namespace jsontitan::core;

// simdjson saturates array/object size() at 0xFFFFFF. The child pointer
// array used to be sized from it, so a larger container wrote past its end.
// (~1.7 GB peak: the bug needs more than 16,777,215 real nodes.)
TEST(SimdjsonRegression, ContainerLargerThanSaturatedSizeKeepsEveryChild) {
    constexpr std::size_t kCount = 0xFFFFFF + 10;
    std::string json;
    json.reserve(kCount * 2 + 1);
    json += '[';
    for (std::size_t i = 0; i < kCount; ++i) {
        json += i == 0 ? "0" : ",0";
    }
    json += ']';

    ArenaAllocator arena;
    SourceBuffer source(std::move(json));
    auto result = simdjsonParse(source, arena);
    ASSERT_FALSE(result.error.has_value()) << result.error->description;
    ASSERT_NE(result.root, nullptr);
    EXPECT_EQ(result.root->childCount, kCount);
    EXPECT_EQ(result.root->childAt(kCount - 1)->valueView(), "0");
}
