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

# ── Search Performance Tests (Google Test + RapidCheck + Qt for async validation) ───
add_executable(search_perf_tests search_perf_tests.cpp)

target_link_libraries(search_perf_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        GTest::gtest
        rapidcheck
)

gtest_discover_tests(search_perf_tests)

# ── Search Preservation Tests (Google Test + RapidCheck, no Qt) ──
add_executable(search_preservation_tests search_preservation_tests.cpp)

target_link_libraries(search_preservation_tests
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
        rapidcheck
)

gtest_discover_tests(search_preservation_tests)

# ── FileLoader Tests (Qt Test + RapidCheck) ──────────────────────
add_executable(file_loader_tests file_loader_tests.cpp)

target_link_libraries(file_loader_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        rapidcheck
)

add_test(NAME file_loader_tests COMMAND file_loader_tests)

# ── SearchWorker Tests (Qt Test) ─────────────────────────────────
add_executable(search_worker_tests search_worker_tests.cpp)

target_link_libraries(search_worker_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME search_worker_tests COMMAND search_worker_tests)

# ── Debounce Tests (Qt Test) ─────────────────────────────────────
add_executable(debounce_tests debounce_tests.cpp)

target_link_libraries(debounce_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME debounce_tests COMMAND debounce_tests)

# ── Toggle Button Tests (Qt Test) ────────────────────────────────
add_executable(toggle_tests toggle_tests.cpp)

target_link_libraries(toggle_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME toggle_tests COMMAND toggle_tests)

# ── Integration Search Tests (Qt Test) ───────────────────────────
add_executable(integration_search_tests integration_search_tests.cpp)

target_link_libraries(integration_search_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME integration_search_tests COMMAND integration_search_tests)

# ── QSS Resource Tests (Qt Test — validates embedded stylesheet) ─────────
add_executable(qss_resource_tests
    qss_resource_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/resources.qrc
)

target_link_libraries(qss_resource_tests
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME qss_resource_tests COMMAND qss_resource_tests)

# ── Token Emitter Tests (Google Test + RapidCheck, no Qt) ────────
add_executable(token_emitter_tests token_emitter_tests.cpp)

target_link_libraries(token_emitter_tests
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
        rapidcheck
)

gtest_discover_tests(token_emitter_tests)

# ── CLI Parser Property-Based Tests (Google Test + RapidCheck, no Qt) ────
add_executable(cli_parser_pbt cli_parser_pbt.cpp)

target_link_libraries(cli_parser_pbt
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
        rapidcheck
)

gtest_discover_tests(cli_parser_pbt)

# ── CLI Parser Unit + Integration Tests (Google Test + Qt Test) ──
add_executable(cli_parser_tests cli_parser_tests.cpp)

target_link_libraries(cli_parser_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        GTest::gtest
)

add_test(NAME cli_parser_tests COMMAND cli_parser_tests)

# ── Syntax Highlighter Tests (Google Test + Qt for QTextEdit) ────
add_executable(syntax_highlighter_tests syntax_highlighter_tests.cpp)

target_link_libraries(syntax_highlighter_tests
    PRIVATE
        jsontitan_shell_lib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(syntax_highlighter_tests)

# ── simdjson Adapter Tests (Google Test + RapidCheck, no Qt) ─────
add_executable(simdjson_adapter_tests simdjson_adapter_tests.cpp)

target_link_libraries(simdjson_adapter_tests
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
        rapidcheck
)

gtest_discover_tests(simdjson_adapter_tests)

# ── simdjson Integration Tests (Google Test, no Qt) ──────────────
add_executable(simdjson_integration_tests simdjson_integration_tests.cpp)

target_link_libraries(simdjson_integration_tests
    PRIVATE
        jsontitan_core
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(simdjson_integration_tests)
