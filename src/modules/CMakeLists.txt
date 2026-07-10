set(PANTHERA_MODULE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/auth/login_dialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/auth/login_dialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/dashboard/device_monitor_page.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/data/data_management_page.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/planning/planning_page.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/energy_output_chart_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/mock_ultrasound_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/therapy_imaging_algorithms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/usb_camera_frame_source.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shell/main_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treatment/treatment_page.cpp
    ${PROJECT_SOURCE_DIR}/src/resources/resources.qrc
)

add_library(panthera_modules STATIC ${PANTHERA_MODULE_SOURCES})

target_link_libraries(panthera_modules PUBLIC panthera_core panthera_adapters Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    target_link_libraries(panthera_modules PRIVATE strmiids ole32 oleaut32 uuid)
endif()
target_include_directories(panthera_modules PUBLIC ${PROJECT_SOURCE_DIR}/src)
