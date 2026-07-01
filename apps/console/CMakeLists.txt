add_executable(
    PanTheraConsole
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(PanTheraConsole PRIVATE panthera_modules)

if(WIN32)
    if(EXISTS "${PANTHERA_UIM_DLL_PATH}")
        add_custom_command(
            TARGET PanTheraConsole
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PANTHERA_UIM_DLL_PATH}"
                "$<TARGET_FILE_DIR:PanTheraConsole>/UISimCanFunc.dll"
            COMMENT "Deploying UIM motor SDK DLL for PanTheraConsole"
            VERBATIM
        )
    endif()

    get_target_property(PANTHERA_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
    if(NOT PANTHERA_QT_CORE_LOCATION)
        get_target_property(PANTHERA_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
    endif()
    if(NOT PANTHERA_QT_CORE_LOCATION)
        get_target_property(PANTHERA_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
    endif()

    if(PANTHERA_QT_CORE_LOCATION)
        get_filename_component(PANTHERA_QT_BIN_DIR "${PANTHERA_QT_CORE_LOCATION}" DIRECTORY)
        find_program(
            PANTHERA_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt.exe windeployqt
            HINTS "${PANTHERA_QT_BIN_DIR}"
            NO_DEFAULT_PATH
        )
    endif()

    if(PANTHERA_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(
            TARGET PanTheraConsole
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E env "PATH=${PANTHERA_QT_BIN_DIR};$ENV{PATH}"
                "${PANTHERA_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:PanTheraConsole>"
            COMMENT "Deploying Qt runtime plugins for PanTheraConsole"
            VERBATIM
        )
    endif()
endif()
