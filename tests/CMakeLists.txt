include(FetchContent)

# ── Google Test ──────────────────────────────────────────────────
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── RapidCheck ───────────────────────────────────────────────────
FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(rapidcheck)

# ── Core Tests (Google Test + RapidCheck, no Qt) ─────────────────
add_executable(core_tests core_tests.cpp)

target_link_libraries(core_tests
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
        rapidcheck
)

include(GoogleTest)
gtest_discover_tests(core_tests)

# ── Shell Tests (Qt Test + RapidCheck) ───────────────────────────
add_executable(shell_tests shell_tests.cpp)

target_link_libraries(shell_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        rapidcheck
)

add_test(NAME shell_tests COMMAND shell_tests)
