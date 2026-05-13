#pragma once

#include <QObject>

#include "adapters/dobot/dobot_tcp_client.h"
#include "core/device/device_interfaces.h"

namespace panthera::adapters::dobot {

class DobotMotionController final
    : public QObject
    , public panthera::core::IMotionController {
    Q_OBJECT

public:
    enum class MotionMode {
        Joint,
        Linear
    };

    explicit DobotMotionController(DobotConnectionSettings settings = {}, QObject* parent = nullptr);

    [[nodiscard]] DobotConnectionSettings settings() const;
    void setSettings(const DobotConnectionSettings& settings);

    bool connectToController(QString* errorMessage = nullptr);
    void disconnectFromController();
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] QString lastError() const;

    void setMotionCommandsEnabled(bool enabled);
    [[nodiscard]] bool motionCommandsEnabled() const;

    void setDefaultMotionMode(MotionMode mode);
    [[nodiscard]] MotionMode defaultMotionMode() const;

    void setDefaultMotionOptions(const DobotMotionOptions& options);
    [[nodiscard]] DobotMotionOptions defaultMotionOptions() const;

    DobotControllerClient& client();
    const DobotControllerClient& client() const;

    panthera::core::Coordinate6D currentPosition() const override;
    bool moveTo(const panthera::core::Coordinate6D& target, QString* errorMessage = nullptr) override;
    bool home(QString* errorMessage = nullptr) override;

private:
    static DobotPose toPose(const panthera::core::Coordinate6D& coordinate);
    static panthera::core::Coordinate6D toCoordinate(const DobotPose& pose);
    bool setError(const QString& message, QString* errorMessage) const;

    mutable DobotControllerClient m_client;
    mutable panthera::core::Coordinate6D m_lastKnownPosition;
    DobotMotionOptions m_defaultOptions;
    MotionMode m_defaultMotionMode {MotionMode::Linear};
    bool m_motionCommandsEnabled {false};
    mutable QString m_lastError;
};

}  // namespace panthera::adapters::dobot
