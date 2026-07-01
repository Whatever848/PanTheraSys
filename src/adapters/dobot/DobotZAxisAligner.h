#pragma once

#include "adapters/dobot/dobot_tcp_client.h"

#include <QTcpSocket>
#include <QString>
#include <QVector>

namespace panthera::adapters::dobot {

class DobotZAxisAligner final {
public:
    explicit DobotZAxisAligner(QTcpSocket* dashboardSocket = nullptr);

    void setDashboardSocket(QTcpSocket* dashboardSocket);
    [[nodiscard]] QTcpSocket* dashboardSocket() const;

    bool zAlignAndMove(
        int userIndex = 0,
        int toolIndex = 0,
        int acc = 10,
        int vel = 10,
        QString* errorMessage = nullptr);

    bool calculateZAlignTarget(
        int userIndex,
        int toolIndex,
        DobotPose& targetPose,
        QVector<double>& targetJoint,
        QString* errorMessage = nullptr);

    bool runToJoint(
        const QVector<double>& joint,
        int acc = 10,
        int vel = 10,
        QString* errorMessage = nullptr);

    bool waitForJointTarget(
        const QVector<double>& targetJoint,
        int timeoutMs = 120000,
        double toleranceDeg = 1.0,
        int stableSamples = 3,
        int pollIntervalMs = 200,
        QString* errorMessage = nullptr);
    bool stop(QString* errorMessage = nullptr);
    void setMaxJointDeltaDeg(double value);

private:
    QString sendCommand(const QString& cmd, int timeoutMs = 3000);
    bool parseReply(const QString& reply, int& errorId, QVector<double>& values, QString* errorMessage = nullptr) const;
    bool getAngle(QVector<double>& joint, QString* errorMessage = nullptr);
    bool getPose(int userIndex, int toolIndex, DobotPose& pose, QString* errorMessage = nullptr);
    bool inverseKin(
        const DobotPose& pose,
        const QVector<double>& jointNear,
        int userIndex,
        int toolIndex,
        QVector<double>& resultJoint,
        QString* errorMessage = nullptr);
    DobotPose calcZAlignPose(const DobotPose& currentPose) const;
    bool checkJointDelta(
        const QVector<double>& currentJoint,
        const QVector<double>& targetJoint,
        QString* errorMessage = nullptr) const;
    static QString number(double value);

    QTcpSocket* m_socket {nullptr};
    double m_maxJointDeltaDeg {120.0};
};

}  // namespace panthera::adapters::dobot
