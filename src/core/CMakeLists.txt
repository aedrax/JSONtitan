add_library(jsontitan_core STATIC
    json_node.cpp
    parser.cpp
    pretty_printer.cpp
    recent_files.cpp
    search_engine.cpp
    union_engine.cpp
    csv_exporter.cpp
    xml_exporter.cpp
    arena_allocator.cpp
    simd_scanner.cpp
    structural_scanner.cpp
    chunk_parser.cpp
    parse_orchestrator.cpp
)

target_include_directories(jsontitan_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# NOTE: jsontitan_core must NOT link any Qt modules.
# This enforces the Functional Core / Imperative Shell boundary (Requirement 8.2).
