#include "DobotDashboardClient.h"

DobotDashboardClient::DobotDashboardClient(QObject* parent)
    : QObject(parent)
{
}

DobotDashboardClient::~DobotDashboardClient()
{
    disconnectFromRobot();
}

bool DobotDashboardClient::connectToRobot(const QString& ip, quint16 port, int timeoutMs, QString* errorMessage)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        return true;
    }
    m_socket.connectToHost(ip.trimmed(), port);
    if (!m_socket.waitForConnected(timeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("连接 Dashboard 端口失败：%1:%2，%3")
                                .arg(ip)
                                .arg(port)
                                .arg(m_socket.errorString());
        }
        return false;
    }
    return true;
}

void DobotDashboardClient::disconnectFromRobot()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        m_socket.waitForDisconnected(500);
    }
}

bool DobotDashboardClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

QString DobotDashboardClient::sendCommand(const QString& command, int timeoutMs, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    if (!isConnected()) {
        if (errorMessage) *errorMessage = QStringLiteral("Dashboard 未连接");
        return {};
    }

    const QByteArray payload = command.toUtf8();
    if (m_socket.write(payload) != payload.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("Dashboard 指令写入失败：%1").arg(m_socket.errorString());
        return {};
    }
    if (!m_socket.waitForBytesWritten(timeoutMs)) {
        if (errorMessage) *errorMessage = QStringLiteral("Dashboard 指令发送超时：%1").arg(m_socket.errorString());
        return {};
    }

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const int remain = qMax(50, timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_socket.waitForReadyRead(remain)) {
            continue;
        }
        response += m_socket.readAll();
        if (response.contains(';')) {
            break;
        }
    }

    if (response.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Dashboard 无响应或读取超时");
        return {};
    }
    return QString::fromUtf8(response).trimmed();
}
