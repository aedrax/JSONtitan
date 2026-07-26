include(FetchContent)

# ── simdjson ─────────────────────────────────────────────────────
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
)
FetchContent_MakeAvailable(simdjson)

add_library(jsontitan_core STATIC
    cli_parser.cpp
    json_node.cpp
    parser.cpp
    pretty_printer.cpp
    recent_files.cpp
    search_engine.cpp
    jsonpath.cpp
    union_engine.cpp
    csv_exporter.cpp
    xml_exporter.cpp
    arena_allocator.cpp
    parse_orchestrator.cpp
    token_emitter.cpp
    simdjson_adapter.cpp
    deletion_engine.cpp
    edit_engine.cpp
    json_exporter.cpp
    stream_export.cpp
)

target_include_directories(jsontitan_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# simdjson is linked PRIVATE to keep its headers out of the shell layer.
# This preserves the Functional Core / Imperative Shell boundary.
target_link_libraries(jsontitan_core PRIVATE simdjson)

# NOTE: jsontitan_core must NOT link any Qt modules.
# This enforces the Functional Core / Imperative Shell boundary (Requirement 8.2).
