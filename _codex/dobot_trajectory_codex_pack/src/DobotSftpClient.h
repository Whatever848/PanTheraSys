#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>

#include <libssh2.h>
#include <libssh2_sftp.h>

struct DobotSftpConfig
{
    QString host = QStringLiteral("192.168.5.1");
    quint16 port = 22;
    QString username = QStringLiteral("root");
    QString password = QStringLiteral("dobot");
    QString trajectoryDir = QStringLiteral("/dobot/userdata/project/process/trajectory");
    int connectTimeoutMs = 3000;
    int operationTimeoutMs = 5000;
};

class DobotSftpClient final : public QObject
{
    Q_OBJECT
public:
    explicit DobotSftpClient(QObject* parent = nullptr);
    ~DobotSftpClient() override;

    void setConfig(const DobotSftpConfig& config);
    DobotSftpConfig config() const;

    // 读取控制器 trajectory 目录下的 .csv 文件。返回值只包含文件名，不包含路径。
    QStringList listTrajectoryCsvFiles(QString* errorMessage = nullptr);

private:
    bool connectSession(QString* errorMessage);
    void disconnectSession();
    QString sessionLastError() const;

private:
    DobotSftpConfig m_config;
    QTcpSocket* m_socket = nullptr;
    LIBSSH2_SESSION* m_session = nullptr;
    LIBSSH2_SFTP* m_sftp = nullptr;
};
