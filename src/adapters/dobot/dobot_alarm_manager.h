#pragma once

#include <functional>

#include <QObject>
#include <QStringList>
#include <QTimer>

#include "adapters/dobot/dobot_tcp_client.h"
#include "core/domain/system_types.h"

namespace panthera::core {
class EventBus;
class SafetyKernel;
}

namespace panthera::adapters::dobot {

enum class DobotAlarmKind {
    None,
    ControllerAlarm,
    Collision
};

struct DobotAlarmStatus {
    DobotAlarmKind kind {DobotAlarmKind::None};
    bool active {false};
    bool communicationHealthy {true};
    bool restartRequired {false};
    int robotMode {-1};
    QString rawAlarmPayload;
    QStringList alarmCodes;
    QString details;
};

// Monitors and resets DOBOT alarms through the existing controller client.
// It deliberately has no EnableRobot/Continue operation: resetting an alarm
// and restarting robot motion are separate operator actions.
class DobotAlarmManager final : public QObject {
    Q_OBJECT

public:
    struct CommandApi {
        std::function<bool()> isConnected;
        std::function<DobotCommandResult(int*, QString*)> robotMode;
        std::function<DobotCommandResult(QString*, QString*)> getErrorId;
        std::function<DobotCommandResult(QString*)> clearError;
    };

    DobotAlarmManager(
        DobotControllerClient* client,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::EventBus* eventBus,
        QObject* parent = nullptr);
    DobotAlarmManager(
        CommandApi commandApi,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::EventBus* eventBus,
        QObject* parent = nullptr);

    [[nodiscard]] DobotAlarmStatus status() const;
    [[nodiscard]] bool isMonitoring() const;
    void setPollInterval(int intervalMs);

    static bool alarmPayloadIsClear(const QString& payload);
    static QStringList alarmCodesFromPayload(const QString& payload);

public slots:
    void startMonitoring();
    void stopMonitoring();
    void pollNow();
    void requestReset();
    void notifyRobotEnabledByOperator();

signals:
    void statusChanged(const panthera::adapters::dobot::DobotAlarmStatus& status);
    void resetStarted();
    void resetFinished(bool success, const QString& message);

private:
    static CommandApi commandApiForClient(DobotControllerClient* client);
    bool readRobotStatus(int* robotMode, QString* alarmPayload, QString* errorMessage);
    bool observationHasAlarm(int robotMode, const QString& alarmPayload) const;
    void applyObservation(int robotMode, const QString& alarmPayload);
    void resolveAlarm(int robotMode, const QString& alarmPayload, const QString& details);
    void markCommunicationFailure(const QString& details);
    void failReset(const QString& details);
    void publishCurrentEvent(const QString& details = {}, bool force = false);
    panthera::core::AlarmEvent makeAlarmEvent(const QString& details) const;
    QString statusSignature() const;

    CommandApi m_commands;
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::EventBus* m_eventBus {nullptr};
    QTimer m_pollTimer;
    DobotAlarmStatus m_status;
    QString m_lastPublishedSignature;
    bool m_collisionLatched {false};
    bool m_alarmEverRaised {false};
    bool m_commandInProgress {false};
};

}  // namespace panthera::adapters::dobot

Q_DECLARE_METATYPE(panthera::adapters::dobot::DobotAlarmStatus)

