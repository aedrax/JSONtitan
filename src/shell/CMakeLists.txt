# Collect shell sources (excluding main.cpp) into an object library
# so that shell_tests can reuse them without recompiling.
set(SHELL_SOURCES
    main_window.cpp
    document_session.cpp
    model_paths.cpp
    search_controller.cpp
    tree_model.cpp
    filter_proxy_model.cpp
    file_loader.cpp
    export_handler.cpp
    recent_files_manager.cpp
    save_handler.cpp
    search_worker.cpp
    syntax_highlighter.cpp
)

add_library(jsontitan_shell_lib OBJECT ${SHELL_SOURCES})

target_include_directories(jsontitan_shell_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(jsontitan_shell_lib
    PUBLIC
        jsontitan_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

# Main executable
add_executable(jsontitan_shell main.cpp resources.qrc)

target_link_libraries(jsontitan_shell
    PRIVATE
        jsontitan_shell_lib
)
