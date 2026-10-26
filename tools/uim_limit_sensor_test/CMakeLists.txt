cmake_minimum_required(VERSION 3.21)

project(
    UimLimitSensorTest
    VERSION 1.0.0
    DESCRIPTION "Passive UIM242 S3 limit sensor test tool"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a separate build directory.")
endif()

option(UIM_LIMIT_BUILD_TESTS "Build protocol and workflow tests" ON)

set(UIM_SDK_DLL_PATH
    "D:/PanSoftware/UIMDemo/UISimCanFunc.dll"
    CACHE FILEPATH "Path to the UIROBOT UISimCanFunc.dll"
)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui SerialPort Widgets)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    add_compile_options(-U__cpp_impl_three_way_comparison)
endif()

add_library(uim_limit_sensor_core STATIC
    src/limit_test_session.cpp
    src/limit_test_session.h
    src/uim_can_protocol.cpp
    src/uim_can_protocol.h
    src/uim_passive_gateway.cpp
    src/uim_passive_gateway.h
)

target_include_directories(uim_limit_sensor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(uim_limit_sensor_core PUBLIC Qt6::Core)

add_executable(UimLimitSensorTest WIN32
    resources/resources.qrc
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)

target_link_libraries(UimLimitSensorTest
    PRIVATE
        uim_limit_sensor_core
        Qt6::Gui
        Qt6::SerialPort
        Qt6::Widgets
)

if(EXISTS "${UIM_SDK_DLL_PATH}")
    add_custom_command(
        TARGET UimLimitSensorTest
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UIM_SDK_DLL_PATH}"
            "$<TARGET_FILE_DIR:UimLimitSensorTest>/UISimCanFunc.dll"
        COMMENT "Deploying UIROBOT SDK DLL"
    )
else()
    message(WARNING "UISimCanFunc.dll was not found at ${UIM_SDK_DLL_PATH}; select it in the application before hardware testing.")
endif()

if(WIN32)
    get_target_property(UIM_LIMIT_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
    if(NOT UIM_LIMIT_QT_CORE_LOCATION)
        get_target_property(UIM_LIMIT_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
    endif()
    if(NOT UIM_LIMIT_QT_CORE_LOCATION)
        get_target_property(UIM_LIMIT_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
    endif()

    if(UIM_LIMIT_QT_CORE_LOCATION)
        get_filename_component(UIM_LIMIT_QT_BIN_DIR "${UIM_LIMIT_QT_CORE_LOCATION}" DIRECTORY)
        find_program(
            UIM_LIMIT_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt.exe windeployqt
            HINTS "${UIM_LIMIT_QT_BIN_DIR}"
            NO_DEFAULT_PATH
        )
    endif()

    if(UIM_LIMIT_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(
            TARGET UimLimitSensorTest
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E env "PATH=${UIM_LIMIT_QT_BIN_DIR};$ENV{PATH}"
                "${UIM_LIMIT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:UimLimitSensorTest>"
            COMMENT "Deploying Qt runtime plugins"
            VERBATIM
        )
    endif()
endif()

add_custom_command(
    TARGET UimLimitSensorTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:UimLimitSensorTest>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/limit_sensor_test.ini"
        "$<TARGET_FILE_DIR:UimLimitSensorTest>/config/limit_sensor_test.ini"
    COMMENT "Deploying limit sensor test defaults"
)

if(UIM_LIMIT_BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(uim_limit_sensor_tests
        tests/uim_limit_sensor_tests.cpp
    )
    target_link_libraries(uim_limit_sensor_tests
        PRIVATE
            uim_limit_sensor_core
            Qt6::Test
    )
    add_test(NAME uim_limit_sensor_tests COMMAND uim_limit_sensor_tests)
endif()

