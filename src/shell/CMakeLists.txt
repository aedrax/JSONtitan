# Collect shell sources (excluding main.cpp) into an object library
# so that shell_tests can reuse them without recompiling.
set(SHELL_SOURCES
    main_window.cpp
    clipboard_utils.cpp
    detail_panel_presenter.cpp
    document_session.cpp
    edit_controller.cpp
    model_paths.cpp
    search_controller.cpp
    tree_model.cpp
    union_controller.cpp
    filter_proxy_model.cpp
    file_loader.cpp
    export_handler.cpp
    recent_files_manager.cpp
    save_handler.cpp
    search_worker.cpp
    syntax_highlighter.cpp
    theme_manager.cpp
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
# WIN32 suppresses the console window on Windows; MACOSX_BUNDLE produces a
# JSONTitan.app that macdeployqt can package. Both are ignored elsewhere.
add_executable(jsontitan_shell WIN32 MACOSX_BUNDLE main.cpp resources.qrc)

set_target_properties(jsontitan_shell PROPERTIES
    OUTPUT_NAME JSONTitan
    MACOSX_BUNDLE_BUNDLE_NAME "JSONTitan"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.aedrax.jsontitan"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_link_libraries(jsontitan_shell
    PRIVATE
        jsontitan_shell_lib
)
