#include <QApplication>
#include <QFile>

#include "main_window.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("PanTheraSys"));
    application.setApplicationName(QStringLiteral("UimLimitSensorTest"));
    application.setApplicationDisplayName(QStringLiteral("液位上下限位传感器测试"));

    QFile styleFile(QStringLiteral(":/styles/panthera.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    panthera::tools::uim_limit::MainWindow window;
    window.show();
    return application.exec();
}

