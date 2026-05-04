add_library(jsontitan_core STATIC
    json_node.cpp
    parser.cpp
    pretty_printer.cpp
    search_engine.cpp
    union_engine.cpp
    csv_exporter.cpp
    xml_exporter.cpp
    arena_allocator.cpp
)

target_include_directories(jsontitan_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# NOTE: jsontitan_core must NOT link any Qt modules.
# This enforces the Functional Core / Imperative Shell boundary (Requirement 8.2).
