add_executable(
    panthera_core_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/core/safety_kernel_tests.cpp
)

target_link_libraries(panthera_core_tests PRIVATE panthera_core Qt6::Test)

add_test(NAME panthera_core_tests COMMAND panthera_core_tests)
set_tests_properties(
    panthera_core_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)
