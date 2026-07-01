# 用法：在主 CMakeLists.txt 中 include(cmake/DobotTrajectoryModule.cmake)
# 前提：系统已安装 libssh2。Windows 推荐 vcpkg：vcpkg install libssh2:x64-windows

find_package(Libssh2 CONFIG QUIET)
if (NOT Libssh2_FOUND)
    find_package(libssh2 CONFIG QUIET)
endif()

set(DOBOT_TRAJECTORY_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotSftpClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotTrajectoryManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotDashboardClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotTrajectoryRunner.cpp
)

set(DOBOT_TRAJECTORY_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotSftpClient.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotTrajectoryManager.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotDashboardClient.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/DobotTrajectoryRunner.h
)

# 在你的 add_executable(...) 之后调用：dobot_trajectory_attach(YourTarget)
function(dobot_trajectory_attach target_name)
    target_sources(${target_name} PRIVATE ${DOBOT_TRAJECTORY_SOURCES} ${DOBOT_TRAJECTORY_HEADERS})
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src)

    if (TARGET Libssh2::libssh2)
        target_link_libraries(${target_name} PRIVATE Libssh2::libssh2)
    elseif (TARGET libssh2::libssh2)
        target_link_libraries(${target_name} PRIVATE libssh2::libssh2)
    elseif (TARGET libssh2)
        target_link_libraries(${target_name} PRIVATE libssh2)
    else()
        message(FATAL_ERROR "找不到 libssh2。请安装 libssh2，或在此处手动指定 include/lib 路径。")
    endif()
endfunction()
