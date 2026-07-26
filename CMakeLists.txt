cmake_minimum_required(VERSION 3.16)
project(JSONTitan VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

add_subdirectory(src/core)
add_subdirectory(src/shell)
# add_subdirectory(tools)

# Testing (skipped in release/packaging builds via -DJSONTITAN_BUILD_TESTS=OFF)
option(JSONTITAN_BUILD_TESTS "Build the test suite" ON)
if(JSONTITAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
