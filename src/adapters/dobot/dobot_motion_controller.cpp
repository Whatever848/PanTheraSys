#include "adapters/dobot/dobot_motion_controller.h"

#include <utility>

namespace panthera::adapters::dobot {

DobotMotionController::DobotMotionController(DobotConnectionSettings settings, QObject* parent)
    : QObject(parent)
    , m_client(std::move(settings), this)
{
}

DobotConnectionSettings DobotMotionController::settings() const
{
    return m_client.settings();
}

void DobotMotionController::setSettings(const DobotConnectionSettings& settings)
{
    m_client.setSettings(settings);
}

bool DobotMotionController::connectToController(QString* errorMessage)
{
    const bool connected = m_client.connectToController(errorMessage);
    if (connected) {
        m_lastError.clear();
    } else if (errorMessage != nullptr) {
        m_lastError = *errorMessage;
    }
    return connected;
}

void DobotMotionController::disconnectFromController()
{
    m_client.disconnectFromController();
}

bool DobotMotionController::isConnected() const
{
    return m_client.isConnected();
}

QString DobotMotionController::lastError() const
{
    return !m_lastError.isEmpty() ? m_lastError : m_client.lastError();
}

void DobotMotionController::setMotionCommandsEnabled(bool enabled)
{
    m_motionCommandsEnabled = enabled;
}

bool DobotMotionController::motionCommandsEnabled() const
{
    return m_motionCommandsEnabled;
}

void DobotMotionController::setDefaultMotionMode(MotionMode mode)
{
    m_defaultMotionMode = mode;
}

DobotMotionController::MotionMode DobotMotionController::defaultMotionMode() const
{
    return m_defaultMotionMode;
}

void DobotMotionController::setDefaultMotionOptions(const DobotMotionOptions& options)
{
    m_defaultOptions = options;
}

DobotMotionOptions DobotMotionController::defaultMotionOptions() const
{
    return m_defaultOptions;
}

DobotControllerClient& DobotMotionController::client()
{
    return m_client;
}

const DobotControllerClient& DobotMotionController::client() const
{
    return m_client;
}

panthera::core::Coordinate6D DobotMotionController::currentPosition() const
{
    DobotPose pose;
    QString errorMessage;
    const DobotCommandResult result = m_client.getPose(&pose, &errorMessage);
    if (result.ok()) {
        m_lastKnownPosition = toCoordinate(pose);
        m_lastError.clear();
    } else {
        m_lastError = errorMessage.isEmpty() ? result.protocolError : errorMessage;
    }
    return m_lastKnownPosition;
}

bool DobotMotionController::moveTo(const panthera::core::Coordinate6D& target, QString* errorMessage)
{
    if (!m_motionCommandsEnabled) {
        return setError(
            QStringLiteral("DOBOT motion commands are disabled. Enable them explicitly before hardware testing."),
            errorMessage);
    }

    DobotCommandResult result;
    if (m_defaultMotionMode == MotionMode::Linear) {
        result = m_client.movL(toPose(target), m_defaultOptions, errorMessage);
    } else {
        result = m_client.movJ(toPose(target), m_defaultOptions, errorMessage);
    }

    if (!result.ok()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = result.protocolError;
        }
        m_lastError = errorMessage != nullptr ? *errorMessage : result.protocolError;
        return false;
    }

    m_lastKnownPosition = target;
    m_lastError.clear();
    return true;
}

bool DobotMotionController::home(QString* errorMessage)
{
    return setError(
        QStringLiteral("DOBOT home position is not configured. Use a validated fixture-specific pose instead."),
        errorMessage);
}

DobotPose DobotMotionController::toPose(const panthera::core::Coordinate6D& coordinate)
{
    return DobotPose {coordinate.x, coordinate.y, coordinate.z, coordinate.a, coordinate.b, coordinate.c};
}

panthera::core::Coordinate6D DobotMotionController::toCoordinate(const DobotPose& pose)
{
    return panthera::core::Coordinate6D {pose.x, pose.y, pose.z, pose.rx, pose.ry, pose.rz};
}

bool DobotMotionController::setError(const QString& message, QString* errorMessage) const
{
    m_lastError = message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace panthera::adapters::dobot
