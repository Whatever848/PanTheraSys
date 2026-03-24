cmake_minimum_required(VERSION 3.21)

project(
    PanTheraSys
    VERSION 0.1.0
    DESCRIPTION "Breast ultrasound ablation workstation prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a separate build directory.")
endif()

option(PANTHERA_BUILD_TESTS "Build automated tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Sql Test)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/adapters)
add_subdirectory(src/modules)
add_subdirectory(apps/console)

if(PANTHERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
