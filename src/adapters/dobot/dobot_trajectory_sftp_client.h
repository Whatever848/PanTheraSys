#pragma once

#include <QtGlobal>

#include <QString>
#include <QStringList>

namespace panthera::adapters::dobot {

struct DobotTrajectorySftpSettings {
    QString host {QStringLiteral("192.168.5.1")};
    quint16 port {22};
    QString username {QStringLiteral("root")};
    QString password {QStringLiteral("dobot")};
    QString remoteDirectory {QStringLiteral("/dobot/userdata/project/process/trajectory/")};
    int timeoutMs {5000};
};

class DobotTrajectorySftpClient final {
public:
    static QStringList listTrajectoryCsvFiles(
        const DobotTrajectorySftpSettings& settings,
        QString* errorMessage = nullptr);

    static QStringList sortTrajectoryCsvFiles(const QStringList& fileNames);
};

}  // namespace panthera::adapters::dobot
