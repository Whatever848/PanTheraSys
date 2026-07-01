# 用法：在 .pro 中加入：include(path/to/dobot_trajectory.pri)
# 注意：Qt 本身不带 SFTP，需要 libssh2。请修改下面两个路径为你本机 libssh2 的 include/lib 路径。

INCLUDEPATH += $$PWD/../src
SOURCES += \
    $$PWD/../src/DobotSftpClient.cpp \
    $$PWD/../src/DobotTrajectoryManager.cpp \
    $$PWD/../src/DobotDashboardClient.cpp \
    $$PWD/../src/DobotTrajectoryRunner.cpp

HEADERS += \
    $$PWD/../src/DobotSftpClient.h \
    $$PWD/../src/DobotTrajectoryManager.h \
    $$PWD/../src/DobotDashboardClient.h \
    $$PWD/../src/DobotTrajectoryRunner.h

# 示例：vcpkg installed/x64-windows
# LIBSSH2_ROOT = D:/vcpkg/installed/x64-windows
# INCLUDEPATH += $$LIBSSH2_ROOT/include
# LIBS += -L$$LIBSSH2_ROOT/lib -llibssh2

win32:LIBS += -lws2_32
