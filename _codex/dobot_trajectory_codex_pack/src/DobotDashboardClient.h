#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QString>

class DobotDashboardClient final : public QObject
{
    Q_OBJECT
public:
    explicit DobotDashboardClient(QObject* parent = nullptr);
    ~DobotDashboardClient() override;

    bool connectToRobot(const QString& ip, quint16 port = 29999, int timeoutMs = 3000, QString* errorMessage = nullptr);
    void disconnectFromRobot();
    bool isConnected() const;

    // 发送 29999 Dashboard 指令。中文文件名必须保持 UTF-8；本函数使用 QString::toUtf8()。
    QString sendCommand(const QString& command, int timeoutMs = 5000, QString* errorMessage = nullptr);

private:
    QTcpSocket m_socket;
};
