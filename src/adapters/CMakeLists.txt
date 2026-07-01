set(PANTHERA_ADAPTERS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/anthone/lu926_temperature_modbus_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/anthone/lu926_temperature_modbus_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/anthone/lu926_temperature_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/local_settings_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/DobotZAxisAligner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/DobotZAxisAligner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/DobotPayloadEnableService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/DobotPayloadEnableService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/dobot_trajectory_sftp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/dobot_trajectory_sftp_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/dobot_motion_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dobot/dobot_tcp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/liquidlevel/liquid_level_modbus_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/liquidlevel/liquid_level_modbus_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mysql/mysql_clinical_data_repository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mysql/mysql_repository_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/seed/seed_clinical_data_repository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sim/simulation_device_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/uim/uim_motor_gateway.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/uim/uim_motor_gateway.h
    ${CMAKE_CURRENT_SOURCE_DIR}/waterpump/water_pump_modbus_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/waterpump/water_pump_modbus_client.h
)

add_library(panthera_adapters STATIC ${PANTHERA_ADAPTERS_SOURCES})

target_link_libraries(
    panthera_adapters
    PUBLIC
        panthera_core
        Qt6::Core
        Qt6::Network
        Qt6::SerialPort
        Qt6::Sql
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        ${PANTHERA_LIBSSH2_TARGET}
)
target_include_directories(panthera_adapters PUBLIC ${PROJECT_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(panthera_adapters PUBLIC ws2_32)
endif()
