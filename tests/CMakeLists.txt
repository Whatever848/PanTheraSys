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

add_executable(
    panthera_dobot_tcp_client_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/dobot_tcp_client_tests.cpp
)

target_link_libraries(
    panthera_dobot_tcp_client_tests
    PRIVATE
        panthera_core
        panthera_adapters
        Qt6::Network
        Qt6::Test
)

add_test(NAME panthera_dobot_tcp_client_tests COMMAND panthera_dobot_tcp_client_tests)
set_tests_properties(
    panthera_dobot_tcp_client_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)

add_executable(
    panthera_simulation_device_facade_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/simulation_device_facade_tests.cpp
)

target_link_libraries(
    panthera_simulation_device_facade_tests
    PRIVATE
        panthera_core
        panthera_adapters
        Qt6::Test
)

add_test(NAME panthera_simulation_device_facade_tests COMMAND panthera_simulation_device_facade_tests)
set_tests_properties(
    panthera_simulation_device_facade_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)

add_executable(
    panthera_mysql_clinical_data_integration_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/mysql_clinical_data_integration_tests.cpp
)

target_link_libraries(
    panthera_mysql_clinical_data_integration_tests
    PRIVATE
        panthera_core
        panthera_adapters
        Qt6::Test
        Qt6::Sql
)

add_test(NAME panthera_mysql_clinical_data_integration_tests COMMAND panthera_mysql_clinical_data_integration_tests)
set_tests_properties(
    panthera_mysql_clinical_data_integration_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)

add_executable(
    panthera_therapy_imaging_algorithms_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/therapy_imaging_algorithms_tests.cpp
)

target_link_libraries(
    panthera_therapy_imaging_algorithms_tests
    PRIVATE
        panthera_core
        panthera_adapters
        panthera_modules
        Qt6::Test
)

add_test(NAME panthera_therapy_imaging_algorithms_tests COMMAND panthera_therapy_imaging_algorithms_tests)
set_tests_properties(
    panthera_therapy_imaging_algorithms_tests
    PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
)

add_executable(
    panthera_planning_page_regression_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/planning_page_regression_tests.cpp
)

target_link_libraries(
    panthera_planning_page_regression_tests
    PRIVATE
        panthera_core
        panthera_adapters
        panthera_modules
        Qt6::Test
        Qt6::Widgets
)

add_test(NAME panthera_planning_page_regression_tests COMMAND panthera_planning_page_regression_tests)
set_tests_properties(
    panthera_planning_page_regression_tests
    PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:PanTheraConsole>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:PanTheraConsole>/platforms"
)
