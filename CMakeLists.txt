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

set(PANTHERA_UIM_DLL_CANDIDATES
    "D:/PanSoftware/UIMDemo/UISimCanFunc.dll"
    "D:/PanSoftware/DIANJIDEMO2/build/mingw/apps/three_axis_motor/UISimCanFunc.dll"
    "D:/PanSoftware/DiJiDemo/build/mingw/apps/motor_demo/UISimCanFunc.dll"
)
set(PANTHERA_DEFAULT_UIM_DLL_PATH "D:/PanSoftware/UIMDemo/UISimCanFunc.dll")
foreach(PANTHERA_UIM_DLL_CANDIDATE IN LISTS PANTHERA_UIM_DLL_CANDIDATES)
    if(EXISTS "${PANTHERA_UIM_DLL_CANDIDATE}")
        set(PANTHERA_DEFAULT_UIM_DLL_PATH "${PANTHERA_UIM_DLL_CANDIDATE}")
        break()
    endif()
endforeach()
set(PANTHERA_UIM_DLL_PATH
    "${PANTHERA_DEFAULT_UIM_DLL_PATH}"
    CACHE FILEPATH "Path to UISimCanFunc.dll used by the UIM three-axis controller")

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network SerialPort Sql Test)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libssh2 CONFIG REQUIRED)

if(TARGET libssh2::libssh2)
    set(PANTHERA_LIBSSH2_TARGET libssh2::libssh2)
elseif(TARGET unofficial::libssh2::libssh2)
    set(PANTHERA_LIBSSH2_TARGET unofficial::libssh2::libssh2)
elseif(TARGET Libssh2::libssh2)
    set(PANTHERA_LIBSSH2_TARGET Libssh2::libssh2)
elseif(TARGET Libssh2::Libssh2)
    set(PANTHERA_LIBSSH2_TARGET Libssh2::Libssh2)
else()
    message(FATAL_ERROR "libssh2 target not found after find_package(libssh2 CONFIG REQUIRED)")
endif()

message(STATUS "Using libssh2 target: ${PANTHERA_LIBSSH2_TARGET}")

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    add_compile_options(-U__cpp_impl_three_way_comparison)
endif()

add_subdirectory(src/core)
add_subdirectory(src/adapters)
add_subdirectory(src/modules)
add_subdirectory(apps/console)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PanTheraConsole)

if(PANTHERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
