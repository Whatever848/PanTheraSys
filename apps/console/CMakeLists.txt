add_executable(
    PanTheraConsole
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(PanTheraConsole PRIVATE panthera_modules)
