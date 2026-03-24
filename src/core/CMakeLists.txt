set(PANTHERA_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/application/application_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/application/event_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/system_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/safety/safety_kernel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/audit_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/clinical_data_service.cpp
)

add_library(panthera_core STATIC ${PANTHERA_CORE_SOURCES})

target_link_libraries(panthera_core PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(panthera_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
