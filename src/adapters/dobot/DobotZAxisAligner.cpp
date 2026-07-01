#include "adapters/dobot/DobotZAxisAligner.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace panthera::adapters::dobot {

namespace {

constexpr double kEpsilon = 1.0e-9;
constexpr double kPi = 3.141592653589793238462643383279502884;

struct Vec3 {
    double x {0.0};
    double y {0.0};
    double z {0.0};
};

struct Mat3 {
    double m[3][3] {
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
    };
};

double degToRad(double deg)
{
    return deg * kPi / 180.0;
}

double radToDeg(double rad)
{
    return rad * 180.0 / kPi;
}

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double norm(const Vec3& v)
{
    return std::sqrt(dot(v, v));
}

Vec3 normalize(const Vec3& v)
{
    const double n = norm(v);
    if (n < kEpsilon) {
        return {0.0, 0.0, 0.0};
    }
    return {v.x / n, v.y / n, v.z / n};
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul(const Vec3& v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}

Mat3 mulMat(const Mat3& a, const Mat3& b)
{
    Mat3 r {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r.m[row][col] = 0.0;
            for (int k = 0; k < 3; ++k) {
                r.m[row][col] += a.m[row][k] * b.m[k][col];
            }
        }
    }
    return r;
}

Mat3 rotX(double deg)
{
    const double r = degToRad(deg);
    const double c = std::cos(r);
    const double s = std::sin(r);
    return {{{1.0, 0.0, 0.0}, {0.0, c, -s}, {0.0, s, c}}};
}

Mat3 rotY(double deg)
{
    const double r = degToRad(deg);
    const double c = std::cos(r);
    const double s = std::sin(r);
    return {{{c, 0.0, s}, {0.0, 1.0, 0.0}, {-s, 0.0, c}}};
}

Mat3 rotZ(double deg)
{
    const double r = degToRad(deg);
    const double c = std::cos(r);
    const double s = std::sin(r);
    return {{{c, -s, 0.0}, {s, c, 0.0}, {0.0, 0.0, 1.0}}};
}

Mat3 poseToMatrix(const DobotPose& p)
{
    return mulMat(mulMat(rotZ(p.rz), rotY(p.ry)), rotX(p.rx));
}

Vec3 column(const Mat3& matrix, int index)
{
    return {matrix.m[0][index], matrix.m[1][index], matrix.m[2][index]};
}

Mat3 fromColumns(const Vec3& x, const Vec3& y, const Vec3& z)
{
    Mat3 matrix {};
    matrix.m[0][0] = x.x;
    matrix.m[1][0] = x.y;
    matrix.m[2][0] = x.z;
    matrix.m[0][1] = y.x;
    matrix.m[1][1] = y.y;
    matrix.m[2][1] = y.z;
    matrix.m[0][2] = z.x;
    matrix.m[1][2] = z.y;
    matrix.m[2][2] = z.z;
    return matrix;
}

DobotPose matrixToPoseRPY(const Mat3& r, const DobotPose& oldPoseKeepXYZ)
{
    DobotPose p = oldPoseKeepXYZ;
    const double sy = -r.m[2][0];
    const double cy = std::sqrt(std::max(0.0, 1.0 - sy * sy));

    if (cy > 1.0e-6) {
        p.rx = radToDeg(std::atan2(r.m[2][1], r.m[2][2]));
        p.ry = radToDeg(std::atan2(sy, cy));
        p.rz = radToDeg(std::atan2(r.m[1][0], r.m[0][0]));
    } else {
        p.rx = 0.0;
        p.ry = radToDeg(std::atan2(sy, cy));
        p.rz = radToDeg(std::atan2(-r.m[0][1], r.m[1][1]));
    }

    return p;
}

QString poseSummary(const DobotPose& pose)
{
    return QStringLiteral("{x=%1,y=%2,z=%3,rx=%4,ry=%5,rz=%6}")
        .arg(formatDobotNumber(pose.x))
        .arg(formatDobotNumber(pose.y))
        .arg(formatDobotNumber(pose.z))
        .arg(formatDobotNumber(pose.rx))
        .arg(formatDobotNumber(pose.ry))
        .arg(formatDobotNumber(pose.rz));
}

QString jointSummary(const QVector<double>& joint)
{
    QStringList parts;
    parts.reserve(joint.size());
    for (double value : joint) {
        parts.push_back(formatDobotNumber(value));
    }
    return QStringLiteral("{%1}").arg(parts.join(QLatin1Char(',')));
}

double nearestEquivalentJointDeg(double current, double target)
{
    if (!std::isfinite(current) || !std::isfinite(target)) {
        return target;
    }

    double adjusted = target;
    while (adjusted - current > 180.0) {
        adjusted -= 360.0;
    }
    while (adjusted - current < -180.0) {
        adjusted += 360.0;
    }
    return adjusted;
}

QVector<double> normalizeJointTargetNearCurrent(
    const QVector<double>& currentJoint,
    const QVector<double>& targetJoint)
{
    QVector<double> normalized = targetJoint;
    const int count = std::min(currentJoint.size(), normalized.size());
    for (int i = 0; i < count; ++i) {
        normalized[i] = nearestEquivalentJointDeg(currentJoint[i], normalized[i]);
    }
    return normalized;
}

void setError(const QString& message, QString* errorMessage)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

}  // namespace

DobotZAxisAligner::DobotZAxisAligner(QTcpSocket* dashboardSocket)
    : m_socket(dashboardSocket)
{
}

void DobotZAxisAligner::setDashboardSocket(QTcpSocket* dashboardSocket)
{
    m_socket = dashboardSocket;
}

QTcpSocket* DobotZAxisAligner::dashboardSocket() const
{
    return m_socket;
}

void DobotZAxisAligner::setMaxJointDeltaDeg(double value)
{
    m_maxJointDeltaDeg = value;
}

QString DobotZAxisAligner::number(double value)
{
    return formatDobotNumber(value);
}

QString DobotZAxisAligner::sendCommand(const QString& cmd, int timeoutMs)
{
    if (m_socket == nullptr) {
        throw std::runtime_error("socket is null");
    }
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        throw std::runtime_error("socket is not connected");
    }

    if (m_socket->bytesAvailable() > 0) {
        m_socket->readAll();
    }

    const QByteArray data = cmd.trimmed().toUtf8();
    const qint64 written = m_socket->write(data);
    if (written != data.size()) {
        throw std::runtime_error("socket write failed");
    }
    if (!m_socket->waitForBytesWritten(timeoutMs)) {
        throw std::runtime_error(("socket write timeout: " + m_socket->errorString()).toStdString());
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (buffer.contains(';')) {
            break;
        }
        const int remainingMs = std::max(1, timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_socket->waitForReadyRead(remainingMs)) {
            break;
        }
        buffer.append(m_socket->readAll());
    }

    if (buffer.isEmpty()) {
        throw std::runtime_error("socket read timeout");
    }

    return QString::fromUtf8(buffer).trimmed();
}

bool DobotZAxisAligner::parseReply(
    const QString& reply,
    int& errorId,
    QVector<double>& values,
    QString* errorMessage) const
{
    const DobotCommandResult parsed = parseDobotResponse(reply);
    errorId = parsed.errorId;
    values.clear();

    if (!parsed.protocolValid()) {
        setError(parsed.protocolError, errorMessage);
        return false;
    }
    if (!parsed.ok()) {
        setError(QStringLiteral("DOBOT command returned ErrorID %1: %2").arg(parsed.errorId).arg(parsed.payload), errorMessage);
        return false;
    }

    bool ok = false;
    values = parseDobotDoublePayload(parsed.payload, &ok);
    if (!ok) {
        setError(QStringLiteral("DOBOT payload is not numeric: %1").arg(parsed.payload), errorMessage);
        return false;
    }
    return true;
}

bool DobotZAxisAligner::getAngle(QVector<double>& joint, QString* errorMessage)
{
    try {
        const QString reply = sendCommand(QStringLiteral("GetAngle()"));
        int errorId = 0;
        QVector<double> values;
        if (!parseReply(reply, errorId, values, errorMessage)) {
            return false;
        }
        if (values.size() != 6) {
            setError(QStringLiteral("GetAngle returned value count != 6: %1").arg(reply), errorMessage);
            return false;
        }
        joint = values;
        return true;
    } catch (const std::exception& e) {
        setError(QStringLiteral("GetAngle exception: %1").arg(QString::fromUtf8(e.what())), errorMessage);
        return false;
    }
}

bool DobotZAxisAligner::getPose(int userIndex, int toolIndex, DobotPose& pose, QString* errorMessage)
{
    try {
        const QString cmd = QStringLiteral("GetPose(user=%1,tool=%2)").arg(userIndex).arg(toolIndex);
        const QString reply = sendCommand(cmd);
        int errorId = 0;
        QVector<double> values;
        if (!parseReply(reply, errorId, values, errorMessage)) {
            return false;
        }
        if (values.size() != 6) {
            setError(QStringLiteral("GetPose returned value count != 6: %1").arg(reply), errorMessage);
            return false;
        }

        pose = DobotPose {values[0], values[1], values[2], values[3], values[4], values[5]};
        return true;
    } catch (const std::exception& e) {
        setError(QStringLiteral("GetPose exception: %1").arg(QString::fromUtf8(e.what())), errorMessage);
        return false;
    }
}

bool DobotZAxisAligner::inverseKin(
    const DobotPose& pose,
    const QVector<double>& jointNear,
    int userIndex,
    int toolIndex,
    QVector<double>& resultJoint,
    QString* errorMessage)
{
    if (jointNear.size() != 6) {
        setError(QStringLiteral("InverseKin jointNear count != 6"), errorMessage);
        return false;
    }

    try {
        const QString cmd = QStringLiteral(
            "InverseKin(%1,%2,%3,%4,%5,%6,useJointNear=1,jointNear={%7,%8,%9,%10,%11,%12},user=%13,tool=%14)")
            .arg(number(pose.x))
            .arg(number(pose.y))
            .arg(number(pose.z))
            .arg(number(pose.rx))
            .arg(number(pose.ry))
            .arg(number(pose.rz))
            .arg(number(jointNear[0]))
            .arg(number(jointNear[1]))
            .arg(number(jointNear[2]))
            .arg(number(jointNear[3]))
            .arg(number(jointNear[4]))
            .arg(number(jointNear[5]))
            .arg(userIndex)
            .arg(toolIndex);

        const QString reply = sendCommand(cmd, 3000);
        int errorId = 0;
        QVector<double> values;
        if (!parseReply(reply, errorId, values, errorMessage)) {
            return false;
        }
        if (values.size() != 6) {
            setError(QStringLiteral("InverseKin returned value count != 6: %1").arg(reply), errorMessage);
            return false;
        }

        resultJoint = values;
        return true;
    } catch (const std::exception& e) {
        setError(QStringLiteral("InverseKin exception: %1").arg(QString::fromUtf8(e.what())), errorMessage);
        return false;
    }
}

DobotPose DobotZAxisAligner::calcZAlignPose(const DobotPose& currentPose) const
{
    const Mat3 currentRotation = poseToMatrix(currentPose);
    const Vec3 userZ {0.0, 0.0, 1.0};
    const Vec3 groundFacingToolZ {0.0, 0.0, -1.0};
    const Vec3 curX = column(currentRotation, 0);
    const Vec3 curY = column(currentRotation, 1);
    const Vec3 xProj = sub(curX, mul(userZ, dot(curX, userZ)));

    Vec3 targetX;
    Vec3 targetY;
    const Vec3 targetZ = groundFacingToolZ;
    if (norm(xProj) > 1.0e-6) {
        targetX = normalize(xProj);
        targetY = normalize(cross(targetZ, targetX));
    } else {
        const Vec3 yProj = sub(curY, mul(userZ, dot(curY, userZ)));
        if (norm(yProj) > 1.0e-6) {
            targetY = normalize(yProj);
            targetX = normalize(cross(targetY, targetZ));
        } else {
            targetX = {1.0, 0.0, 0.0};
            targetY = normalize(cross(targetZ, targetX));
        }
    }

    DobotPose targetPose = matrixToPoseRPY(fromColumns(targetX, targetY, targetZ), currentPose);
    targetPose.x = currentPose.x;
    targetPose.y = currentPose.y;
    targetPose.z = currentPose.z;
    return targetPose;
}

bool DobotZAxisAligner::checkJointDelta(
    const QVector<double>& currentJoint,
    const QVector<double>& targetJoint,
    QString* errorMessage) const
{
    if (m_maxJointDeltaDeg <= 0.0) {
        return true;
    }
    if (currentJoint.size() != 6 || targetJoint.size() != 6) {
        setError(QStringLiteral("joint count != 6"), errorMessage);
        return false;
    }

    for (int i = 0; i < 6; ++i) {
        const double delta = std::abs(targetJoint[i] - currentJoint[i]);
        if (delta > m_maxJointDeltaDeg) {
            setError(
                QStringLiteral("Safety blocked: J%1 delta too large. current=%2, target=%3, delta=%4 deg")
                    .arg(i + 1)
                    .arg(currentJoint[i], 0, 'f', 3)
                    .arg(targetJoint[i], 0, 'f', 3)
                    .arg(delta, 0, 'f', 3),
                errorMessage);
            return false;
        }
    }
    return true;
}

bool DobotZAxisAligner::calculateZAlignTarget(
    int userIndex,
    int toolIndex,
    DobotPose& targetPose,
    QVector<double>& targetJoint,
    QString* errorMessage)
{
    QVector<double> currentJoint;
    DobotPose currentPose;

    if (!getAngle(currentJoint, errorMessage)) {
        qDebug() << "DOBOT Z axis align GetAngle failed:" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }
    if (!getPose(userIndex, toolIndex, currentPose, errorMessage)) {
        qDebug() << "DOBOT Z axis align GetPose failed:" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }

    targetPose = calcZAlignPose(currentPose);

    QVector<double> ikJoint;
    if (!inverseKin(targetPose, currentJoint, userIndex, toolIndex, ikJoint, errorMessage)) {
        qDebug() << "DOBOT Z axis align InverseKin failed:"
                 << "targetPose=" << poseSummary(targetPose)
                 << "error=" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }
    const QVector<double> normalizedJoint = normalizeJointTargetNearCurrent(currentJoint, ikJoint);
    if (normalizedJoint != ikJoint) {
        qDebug() << "DOBOT Z axis align normalized joint target near current:"
                 << "rawTargetJoint=" << jointSummary(ikJoint)
                 << "normalizedTargetJoint=" << jointSummary(normalizedJoint)
                 << "currentJoint=" << jointSummary(currentJoint);
    }

    if (!checkJointDelta(currentJoint, normalizedJoint, errorMessage)) {
        qDebug() << "DOBOT Z axis align safety check failed:"
                 << "currentJoint=" << jointSummary(currentJoint)
                 << "targetJoint=" << jointSummary(normalizedJoint)
                 << "error=" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }

    targetJoint = normalizedJoint;
    qDebug() << "DOBOT Z axis align targetPose=" << poseSummary(targetPose)
             << "targetJoint=" << jointSummary(targetJoint);
    return true;
}

bool DobotZAxisAligner::runToJoint(
    const QVector<double>& joint,
    int acc,
    int vel,
    QString* errorMessage)
{
    if (joint.size() != 6) {
        setError(QStringLiteral("runToJoint error: joint count != 6"), errorMessage);
        return false;
    }

    acc = std::clamp(acc, 1, 100);
    vel = std::clamp(vel, 1, 100);

    try {
        const QString cmd = QStringLiteral("MovJ(joint={%1,%2,%3,%4,%5,%6},a=%7,v=%8)")
            .arg(number(joint[0]))
            .arg(number(joint[1]))
            .arg(number(joint[2]))
            .arg(number(joint[3]))
            .arg(number(joint[4]))
            .arg(number(joint[5]))
            .arg(acc)
            .arg(vel);

        qDebug() << "DOBOT Z axis align MovJ command:" << cmd;
        const QString reply = sendCommand(cmd, 3000);
        int errorId = 0;
        QVector<double> values;
        if (!parseReply(reply, errorId, values, errorMessage)) {
            qDebug() << "DOBOT Z axis align MovJ failed:" << (errorMessage != nullptr ? *errorMessage : QString());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(QStringLiteral("MovJ exception: %1").arg(QString::fromUtf8(e.what())), errorMessage);
        qDebug() << "DOBOT Z axis align MovJ exception:" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }
}

bool DobotZAxisAligner::zAlignAndMove(
    int userIndex,
    int toolIndex,
    int acc,
    int vel,
    QString* errorMessage)
{
    DobotPose targetPose;
    QVector<double> targetJoint;
    if (!calculateZAlignTarget(userIndex, toolIndex, targetPose, targetJoint, errorMessage)) {
        return false;
    }
    return runToJoint(targetJoint, acc, vel, errorMessage);
}

bool DobotZAxisAligner::waitForJointTarget(
    const QVector<double>& targetJoint,
    int timeoutMs,
    double toleranceDeg,
    int stableSamples,
    int pollIntervalMs,
    QString* errorMessage)
{
    if (targetJoint.size() != 6) {
        setError(QStringLiteral("waitForJointTarget error: target joint count != 6"), errorMessage);
        return false;
    }

    timeoutMs = std::max(timeoutMs, 1000);
    pollIntervalMs = std::clamp(pollIntervalMs, 50, 1000);
    toleranceDeg = std::max(toleranceDeg, 0.01);
    stableSamples = std::max(stableSamples, 1);

    int stableCount = 0;
    QVector<double> currentJoint;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        currentJoint.clear();
        if (!getAngle(currentJoint, errorMessage)) {
            qDebug() << "DOBOT Z axis align wait GetAngle failed:"
                     << (errorMessage != nullptr ? *errorMessage : QString());
            return false;
        }

        bool arrived = currentJoint.size() == 6;
        double maxDelta = 0.0;
        for (int i = 0; arrived && i < 6; ++i) {
            const double adjustedTarget = nearestEquivalentJointDeg(currentJoint[i], targetJoint[i]);
            const double delta = std::abs(adjustedTarget - currentJoint[i]);
            maxDelta = std::max(maxDelta, delta);
            if (delta > toleranceDeg) {
                arrived = false;
            }
        }

        if (arrived) {
            ++stableCount;
            if (stableCount >= stableSamples) {
                qDebug() << "DOBOT Z axis align arrived:"
                         << "currentJoint=" << jointSummary(currentJoint)
                         << "targetJoint=" << jointSummary(targetJoint)
                         << "toleranceDeg=" << toleranceDeg;
                return true;
            }
        } else {
            stableCount = 0;
        }

        QThread::msleep(static_cast<unsigned long>(pollIntervalMs));
    }

    setError(
        QStringLiteral("waitForJointTarget timeout after %1 ms, targetJoint=%2, lastJoint=%3, tolerance=%4 deg")
            .arg(timeoutMs)
            .arg(jointSummary(targetJoint))
            .arg(jointSummary(currentJoint))
            .arg(toleranceDeg, 0, 'f', 3),
        errorMessage);
    return false;
}

bool DobotZAxisAligner::stop(QString* errorMessage)
{
    try {
        const QString reply = sendCommand(QStringLiteral("Stop()"), 3000);
        int errorId = 0;
        QVector<double> values;
        if (!parseReply(reply, errorId, values, errorMessage)) {
            qDebug() << "DOBOT Z axis align Stop failed:" << (errorMessage != nullptr ? *errorMessage : QString());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(QStringLiteral("Stop exception: %1").arg(QString::fromUtf8(e.what())), errorMessage);
        qDebug() << "DOBOT Z axis align Stop exception:" << (errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }
}

}  // namespace panthera::adapters::dobot
