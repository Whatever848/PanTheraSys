#include "adapters/dobot/dobot_alarm_manager.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QScopedValueRollback>

#include <utility>

#include "core/application/event_bus.h"
#include "core/safety/safety_kernel.h"

namespace panthera::adapters::dobot {
namespace {

constexpr int kDefaultPollIntervalMs = 750;
constexpr int kRobotModeEnabled = 5;
constexpr int kRobotModeRunning = 7;
constexpr int kRobotModeSingleMove = 8;
constexpr int kRobotModeAlarm = 9;
constexpr int kRobotModeCollision = 11;

QString commandFailureText(
    const QString& command,
    const DobotCommandResult& result,
    const QString& errorMessage)
{
    QString detail = errorMessage.trimmed();
    if (detail.isEmpty()) {
        detail = result.protocolError.trimmed();
    }
    if (detail.isEmpty()) {
        detail = QStringLiteral("ErrorID=%1").arg(result.errorId);
    }
    return QStringLiteral("%1 失败：%2").arg(command, detail);
}

}  // namespace

DobotAlarmManager::DobotAlarmManager(
    DobotControllerClient* client,
    panthera::core::SafetyKernel* safetyKernel,
    panthera::core::EventBus* eventBus,
    QObject* parent)
    : DobotAlarmManager(commandApiForClient(client), safetyKernel, eventBus, parent)
{
}

DobotAlarmManager::DobotAlarmManager(
    CommandApi commandApi,
    panthera::core::SafetyKernel* safetyKernel,
    panthera::core::EventBus* eventBus,
    QObject* parent)
    : QObject(parent)
    , m_commands(std::move(commandApi))
    , m_safetyKernel(safetyKernel)
    , m_eventBus(eventBus)
{
    m_pollTimer.setInterval(kDefaultPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &DobotAlarmManager::pollNow);
}

DobotAlarmManager::CommandApi DobotAlarmManager::commandApiForClient(DobotControllerClient* client)
{
    CommandApi api;
    api.isConnected = [client]() { return client != nullptr && client->isConnected(); };
    api.robotMode = [client](int* mode, QString* error) {
        return client != nullptr ? client->robotMode(mode, error) : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("DOBOT client is null.")};
    };
    api.getErrorId = [client](QString* payload, QString* error) {
        return client != nullptr ? client->getErrorId(payload, error) : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("DOBOT client is null.")};
    };
    api.clearError = [client](QString* error) {
        return client != nullptr ? client->clearError(error) : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("DOBOT client is null.")};
    };
    return api;
}

DobotAlarmStatus DobotAlarmManager::status() const
{
    return m_status;
}

bool DobotAlarmManager::isMonitoring() const
{
    return m_pollTimer.isActive();
}

void DobotAlarmManager::setPollInterval(int intervalMs)
{
    m_pollTimer.setInterval(qBound(250, intervalMs, 5000));
}

bool DobotAlarmManager::alarmPayloadIsClear(const QString& payload)
{
    QString normalized = payload.trimmed();
    if (normalized.isEmpty()) {
        return true;
    }

    normalized.remove(QRegularExpression(QStringLiteral("[\\s\\[\\]\\{\\}\\(\\),;]")));
    if (normalized.isEmpty()) {
        return true;
    }

    bool sawNumber = false;
    const QRegularExpression integerExpression(QStringLiteral("-?\\d+"));
    QRegularExpressionMatchIterator iterator = integerExpression.globalMatch(normalized);
    while (iterator.hasNext()) {
        sawNumber = true;
        if (iterator.next().captured().toLongLong() != 0) {
            return false;
        }
    }
    return sawNumber && !normalized.contains(QRegularExpression(QStringLiteral("[A-Za-z]")));
}

QStringList DobotAlarmManager::alarmCodesFromPayload(const QString& payload)
{
    QStringList codes;
    const QRegularExpression integerExpression(QStringLiteral("-?\\d+"));
    QRegularExpressionMatchIterator iterator = integerExpression.globalMatch(payload);
    while (iterator.hasNext()) {
        const QString code = iterator.next().captured();
        if (code.toLongLong() == 0 || codes.contains(code)) {
            continue;
        }
        codes.push_back(code);
    }
    if (codes.isEmpty() && !alarmPayloadIsClear(payload)) {
        codes.push_back(payload.trimmed());
    }
    return codes;
}

void DobotAlarmManager::startMonitoring()
{
    if (!m_pollTimer.isActive()) {
        m_pollTimer.start();
    }
    pollNow();
}

void DobotAlarmManager::stopMonitoring()
{
    m_pollTimer.stop();
}

bool DobotAlarmManager::readRobotStatus(int* robotMode, QString* alarmPayload, QString* errorMessage)
{
    if (robotMode != nullptr) {
        *robotMode = -1;
    }
    if (alarmPayload != nullptr) {
        alarmPayload->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QString modeError;
    const DobotCommandResult modeResult = m_commands.robotMode != nullptr
        ? m_commands.robotMode(robotMode, &modeError)
        : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("RobotMode callback is unavailable.")};

    QString alarmError;
    const DobotCommandResult alarmResult = m_commands.getErrorId != nullptr
        ? m_commands.getErrorId(alarmPayload, &alarmError)
        : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("GetErrorID callback is unavailable.")};

    const bool modePayloadValid = modeResult.ok() && robotMode != nullptr && *robotMode >= 0;
    if (modePayloadValid && alarmResult.ok()) {
        return true;
    }

    QStringList errors;
    if (!modePayloadValid) {
        errors.push_back(modeResult.ok()
                ? QStringLiteral("RobotMode() 失败：返回值不是有效整数")
                : commandFailureText(QStringLiteral("RobotMode()"), modeResult, modeError));
    }
    if (!alarmResult.ok()) {
        errors.push_back(commandFailureText(QStringLiteral("GetErrorID()"), alarmResult, alarmError));
    }
    if (errorMessage != nullptr) {
        *errorMessage = errors.join(QStringLiteral("；"));
    }
    return false;
}

bool DobotAlarmManager::observationHasAlarm(int robotMode, const QString& alarmPayload) const
{
    return robotMode == kRobotModeCollision
        || robotMode == kRobotModeAlarm
        || !alarmPayloadIsClear(alarmPayload);
}

void DobotAlarmManager::pollNow()
{
    if (m_commandInProgress) {
        return;
    }
    QScopedValueRollback<bool> busyGuard(m_commandInProgress, true);

    if (m_commands.isConnected == nullptr || !m_commands.isConnected()) {
        if (m_status.active || m_status.restartRequired) {
            markCommunicationFailure(QStringLiteral("DOBOT 通信已断开，不能确认报警已解除。"));
        }
        return;
    }

    int robotMode = -1;
    QString alarmPayload;
    QString errorMessage;
    if (!readRobotStatus(&robotMode, &alarmPayload, &errorMessage)) {
        markCommunicationFailure(errorMessage);
        return;
    }

    applyObservation(robotMode, alarmPayload);
}

void DobotAlarmManager::applyObservation(int robotMode, const QString& alarmPayload)
{
    const bool wasActive = m_status.active;
    const bool wasCommunicationHealthy = m_status.communicationHealthy;
    const int previousRobotMode = m_status.robotMode;
    const QString previousAlarmPayload = m_status.rawAlarmPayload;
    m_status.communicationHealthy = true;
    m_status.robotMode = robotMode;
    m_status.rawAlarmPayload = alarmPayload.trimmed();
    m_status.alarmCodes = alarmCodesFromPayload(alarmPayload);

    if (robotMode == kRobotModeCollision) {
        m_collisionLatched = true;
    }

    if (observationHasAlarm(robotMode, alarmPayload)) {
        m_alarmEverRaised = true;
        m_status.active = true;
        m_status.restartRequired = true;
        m_status.kind = m_collisionLatched ? DobotAlarmKind::Collision : DobotAlarmKind::ControllerAlarm;
        m_status.details = m_collisionLatched
            ? QStringLiteral("机械臂触发安全保护。请排除碰撞原因，确认现场安全后人工复位。")
            : QStringLiteral("机械臂处于报警状态。请根据报警代码排除原因，确认现场安全后人工复位。");
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setRobotMotionHealthy(false);
        }
        publishCurrentEvent({}, !wasActive);
        return;
    }

    if (wasActive) {
        resolveAlarm(
            robotMode,
            alarmPayload,
            QStringLiteral("控制器回读确认报警已解除。机器人仍未恢复运动，必须另行执行人工上使能。"));
    } else if (!wasCommunicationHealthy
        || previousRobotMode != robotMode
        || previousAlarmPayload != m_status.rawAlarmPayload) {
        // Publish ordinary mode transitions as well.  The dashboard uses this
        // signal to invalidate its cached enable switch when the controller
        // falls back to DISABLED/POWEROFF or communication recovers.
        emit statusChanged(m_status);
    }
}

void DobotAlarmManager::requestReset()
{
    if (m_commandInProgress) {
        return;
    }
    QScopedValueRollback<bool> busyGuard(m_commandInProgress, true);
    emit resetStarted();

    if (m_commands.isConnected == nullptr || !m_commands.isConnected()) {
        failReset(QStringLiteral("复位失败：DOBOT 通信已断开，报警状态保持不变。"));
        return;
    }

    // Re-read both values before changing controller state. This is also the
    // final gate for field safety checks performed by the operator in the UI.
    int modeBeforeReset = -1;
    QString alarmsBeforeReset;
    QString commandError;
    if (!readRobotStatus(&modeBeforeReset, &alarmsBeforeReset, &commandError)) {
        markCommunicationFailure(commandError);
        failReset(QStringLiteral("复位前状态检查失败：%1").arg(commandError));
        return;
    }
    applyObservation(modeBeforeReset, alarmsBeforeReset);

    if (!observationHasAlarm(modeBeforeReset, alarmsBeforeReset)) {
        resolveAlarm(
            modeBeforeReset,
            alarmsBeforeReset,
            QStringLiteral("复位前回读确认报警已解除；未发送 ClearError()。机器人仍需人工上使能。"));
        const QString message = QStringLiteral("报警已解除，未发送 ClearError()；请另行人工上使能。");
        emit resetFinished(true, message);
        return;
    }

    if (modeBeforeReset == kRobotModeRunning || modeBeforeReset == kRobotModeSingleMove) {
        failReset(QStringLiteral("复位被拒绝：机器人仍处于运动状态 RobotMode=%1。请先按现场安全规程停止机器人。").arg(modeBeforeReset));
        return;
    }
    if (m_safetyKernel != nullptr
        && m_safetyKernel->snapshot().activeInterlocks.contains(panthera::core::InterlockReason::EmergencyStop)) {
        failReset(QStringLiteral("复位被拒绝：医疗设备急停联锁仍处于触发状态。请先按现场规程检查并释放急停。"));
        return;
    }

    commandError.clear();
    const DobotCommandResult clearResult = m_commands.clearError != nullptr
        ? m_commands.clearError(&commandError)
        : DobotCommandResult {-1, {}, {}, {}, QStringLiteral("ClearError callback is unavailable.")};
    if (!clearResult.ok()) {
        failReset(QStringLiteral("复位失败：%1。部分报警需先排除原因或重启控制柜。")
                      .arg(commandFailureText(QStringLiteral("ClearError()"), clearResult, commandError)));
        return;
    }

    int modeAfterReset = -1;
    QString alarmsAfterReset;
    commandError.clear();
    if (!readRobotStatus(&modeAfterReset, &alarmsAfterReset, &commandError)) {
        markCommunicationFailure(commandError);
        failReset(QStringLiteral("ClearError() 已返回成功，但复位后状态确认失败：%1。报警状态保持不变。").arg(commandError));
        return;
    }

    if (observationHasAlarm(modeAfterReset, alarmsAfterReset)) {
        applyObservation(modeAfterReset, alarmsAfterReset);
        failReset(QStringLiteral("复位未完成：RobotMode=%1，GetErrorID=%2。请排除报警原因后重试，必要时重启控制柜。")
                      .arg(modeAfterReset)
                      .arg(alarmsAfterReset));
        return;
    }

    resolveAlarm(
        modeAfterReset,
        alarmsAfterReset,
        QStringLiteral("ClearError() 成功，RobotMode/GetErrorID 回读确认报警解除。机器人不会自动使能或恢复运动。"));
    emit resetFinished(true, QStringLiteral("报警复位成功。请关闭危险区域后，另行执行人工上使能。"));
}

void DobotAlarmManager::notifyRobotEnabledByOperator()
{
    if (m_commandInProgress || !m_status.restartRequired) {
        return;
    }
    QScopedValueRollback<bool> busyGuard(m_commandInProgress, true);

    if (m_commands.isConnected == nullptr || !m_commands.isConnected()) {
        markCommunicationFailure(QStringLiteral("人工上使能后通信断开，不能确认机器人状态。"));
        return;
    }

    int robotMode = -1;
    QString alarmPayload;
    QString errorMessage;
    if (!readRobotStatus(&robotMode, &alarmPayload, &errorMessage)) {
        markCommunicationFailure(QStringLiteral("人工上使能后的状态确认失败：%1").arg(errorMessage));
        return;
    }

    if (robotMode != kRobotModeEnabled || observationHasAlarm(robotMode, alarmPayload)) {
        applyObservation(robotMode, alarmPayload);
        if (!m_status.active) {
            m_status.details = QStringLiteral("人工上使能未得到安全确认：RobotMode=%1，GetErrorID=%2。")
                                   .arg(robotMode)
                                   .arg(alarmPayload);
            emit statusChanged(m_status);
        }
        return;
    }

    m_status.communicationHealthy = true;
    m_status.robotMode = robotMode;
    m_status.rawAlarmPayload = alarmPayload.trimmed();
    m_status.alarmCodes.clear();
    m_status.active = false;
    m_status.restartRequired = false;
    m_status.kind = DobotAlarmKind::None;
    m_status.details = QStringLiteral("操作员已另行执行上使能，RobotMode=5 且 GetErrorID 为空，机器人运动联锁恢复。");
    m_collisionLatched = false;
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setRobotMotionHealthy(true);
    }
    emit statusChanged(m_status);
    publishCurrentEvent(m_status.details, true);
}

void DobotAlarmManager::resolveAlarm(int robotMode, const QString& alarmPayload, const QString& details)
{
    m_status.communicationHealthy = true;
    m_status.robotMode = robotMode;
    m_status.rawAlarmPayload = alarmPayload.trimmed();
    m_status.alarmCodes.clear();
    m_status.active = false;
    m_status.restartRequired = true;
    m_status.details = details;
    if (m_safetyKernel != nullptr) {
        // ClearError is not allowed to release the medical motion interlock.
        m_safetyKernel->setRobotMotionHealthy(false);
    }
    emit statusChanged(m_status);
    publishCurrentEvent(details, true);
    m_collisionLatched = false;
}

void DobotAlarmManager::markCommunicationFailure(const QString& details)
{
    const bool changed = m_status.communicationHealthy || m_status.details != details;
    m_status.communicationHealthy = false;
    m_status.robotMode = -1;
    m_status.rawAlarmPayload.clear();
    m_status.alarmCodes.clear();
    m_status.details = details;
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setRobotMotionHealthy(false);
    }
    if (changed) {
        // Communication loss is also a loss of proof of enablement.  Always
        // notify consumers, even when no alarm was latched previously.
        emit statusChanged(m_status);
        publishCurrentEvent(details, true);
    }
}

void DobotAlarmManager::failReset(const QString& details)
{
    m_status.details = details;
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setRobotMotionHealthy(false);
    }
    emit statusChanged(m_status);
    publishCurrentEvent(details, true);
    emit resetFinished(false, details);
}

void DobotAlarmManager::publishCurrentEvent(const QString& details, bool force)
{
    if (m_eventBus == nullptr || (!m_alarmEverRaised && !m_status.active)) {
        return;
    }

    const QString signature = statusSignature() + QLatin1Char('|') + details;
    if (!force && signature == m_lastPublishedSignature) {
        return;
    }
    m_lastPublishedSignature = signature;
    m_eventBus->publishAlarmEvent(makeAlarmEvent(details));
}

panthera::core::AlarmEvent DobotAlarmManager::makeAlarmEvent(const QString& details) const
{
    panthera::core::AlarmEvent event;
    event.occurredAt = QDateTime::currentDateTime();
    event.id = QStringLiteral("dobot-robot-alarm");
    event.source = QStringLiteral("DOBOT");
    event.title = m_status.kind == DobotAlarmKind::Collision
        ? QStringLiteral("碰撞检测")
        : QStringLiteral("机器人报警");
    event.level = panthera::core::AlarmLevel::Critical;
    event.codes = m_status.alarmCodes;
    event.reason = m_status.kind == DobotAlarmKind::Collision
        ? QStringLiteral("机械臂触发安全保护，底座黄色环形灯亮起；控制器返回 RobotMode=11。")
        : QStringLiteral("机器人返回 RobotMode=9 或 GetErrorID() 存在报警。RobotMode=9 不是碰撞状态。");
    event.suggestion = m_status.kind == DobotAlarmKind::Collision
        ? QStringLiteral("排除碰撞原因并检查工具、负载和碰撞阈值。确认现场安全后点击“复位”；部分报警需重启控制柜。")
        : QStringLiteral("排除报警原因并确认急停已释放、现场安全后点击“复位”；必要时重启控制柜。");
    event.details = details.isEmpty() ? m_status.details : details;
    event.deviceMode = m_status.robotMode;
    event.active = m_status.active;
    event.collision = m_status.kind == DobotAlarmKind::Collision;
    event.communicationHealthy = m_status.communicationHealthy;
    event.requiresManualReset = true;
    event.restartRequired = m_status.restartRequired;
    return event;
}

QString DobotAlarmManager::statusSignature() const
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(static_cast<int>(m_status.kind))
        .arg(m_status.active)
        .arg(m_status.communicationHealthy)
        .arg(m_status.restartRequired)
        .arg(m_status.robotMode)
        .arg(m_status.rawAlarmPayload)
        .arg(m_status.alarmCodes.join(QLatin1Char(',')));
}

}  // namespace panthera::adapters::dobot

