set(PANTHERA_ADAPTERS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/config/local_settings_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mysql/mysql_clinical_data_repository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mysql/mysql_repository_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/seed/seed_clinical_data_repository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sim/simulation_device_facade.cpp
)

add_library(panthera_adapters STATIC ${PANTHERA_ADAPTERS_SOURCES})

target_link_libraries(panthera_adapters PUBLIC panthera_core Qt6::Core Qt6::Sql)
target_include_directories(panthera_adapters PUBLIC ${PROJECT_SOURCE_DIR}/src)
