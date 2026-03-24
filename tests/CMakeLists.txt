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

add_executable(
    panthera_application_context_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/core/application_context_tests.cpp
)

target_link_libraries(panthera_application_context_tests PRIVATE panthera_core Qt6::Test)

add_test(NAME panthera_application_context_tests COMMAND panthera_application_context_tests)
set_tests_properties(
    panthera_application_context_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)

add_executable(
    panthera_clinical_data_service_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/core/clinical_data_service_tests.cpp
)

target_link_libraries(panthera_clinical_data_service_tests PRIVATE panthera_core panthera_adapters Qt6::Test)

add_test(NAME panthera_clinical_data_service_tests COMMAND panthera_clinical_data_service_tests)
set_tests_properties(
    panthera_clinical_data_service_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)
