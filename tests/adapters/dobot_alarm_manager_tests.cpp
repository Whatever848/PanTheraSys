#include <QtTest>

#include "adapters/dobot/dobot_alarm_manager.h"
#include "core/application/event_bus.h"
#include "core/safety/safety_kernel.h"

namespace {

using panthera::adapters::dobot::DobotAlarmKind;
using panthera::adapters::dobot::DobotAlarmManager;
using panthera::adapters::dobot::DobotCommandResult;

DobotCommandResult successfulResult(const QString& command, const QString& payload = {})
{
    DobotCommandResult result;
    result.errorId = 0;
    result.command = command;
    result.payload = payload;
    return result;
}

struct FakeDobotAlarmApi {
    bool connected {true};
    int mode {5};
    QString alarms {QStringLiteral("[]")};
    bool clearSucceeds {true};
    bool clearRemovesAlarm {true};
    QStringList calls;

    DobotAlarmManager::CommandApi api()
    {
        DobotAlarmManager::CommandApi result;
        result.isConnected = [this]() { return connected; };
        result.robotMode = [this](int* value, QString*) {
            calls.push_back(QStringLiteral("RobotMode"));
            if (value != nullptr) {
                *value = mode;
            }
            return successfulResult(QStringLiteral("RobotMode()"), QString::number(mode));
        };
        result.getErrorId = [this](QString* value, QString*) {
            calls.push_back(QStringLiteral("GetErrorID"));
            if (value != nullptr) {
                *value = alarms;
            }
            return successfulResult(QStringLiteral("GetErrorID()"), alarms);
        };
        result.clearError = [this](QString* errorMessage) {
            calls.push_back(QStringLiteral("ClearError"));
            if (!clearSucceeds) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("controller rejected reset");
                }
                DobotCommandResult failure;
                failure.errorId = -2;
                failure.command = QStringLiteral("ClearError()");
                return failure;
            }
            if (clearRemovesAlarm) {
                mode = 4;
                alarms = QStringLiteral("[]");
            }
            return successfulResult(QStringLiteral("ClearError()"));
        };
        return result;
    }
};

class DobotAlarmManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void payloadParserSupportsMultipleAlarmCodes();
    void distinguishesCollisionModeFromGeneralAlarmMode();
    void successfulResetDoesNotReleaseMotionInterlock();
    void emergencyStopInterlockBlocksClearError();
    void failedResetKeepsAlarmActive();
    void disconnectDoesNotAssumeAlarmCleared();
};

void DobotAlarmManagerTests::payloadParserSupportsMultipleAlarmCodes()
{
    QVERIFY(DobotAlarmManager::alarmPayloadIsClear(QStringLiteral("[]")));
    QVERIFY(DobotAlarmManager::alarmPayloadIsClear(QStringLiteral("[[],[],[]]")));
    QVERIFY(DobotAlarmManager::alarmPayloadIsClear(QStringLiteral("{0,0}")));
    QVERIFY(!DobotAlarmManager::alarmPayloadIsClear(QStringLiteral("[[11740],[11741]]")));
    QCOMPARE(
        DobotAlarmManager::alarmCodesFromPayload(QStringLiteral("[[11740],[11741],[11740]]")),
        QStringList({QStringLiteral("11740"), QStringLiteral("11741")}));
}

void DobotAlarmManagerTests::distinguishesCollisionModeFromGeneralAlarmMode()
{
    qRegisterMetaType<panthera::core::AlarmEvent>();
    panthera::core::EventBus collisionBus;
    panthera::core::SafetyKernel collisionSafety;
    FakeDobotAlarmApi collisionApi;
    collisionApi.mode = 11;
    collisionApi.alarms = QStringLiteral("[[11740],[11741]]");
    DobotAlarmManager collisionManager(collisionApi.api(), &collisionSafety, &collisionBus);
    QSignalSpy collisionEventSpy(&collisionBus, &panthera::core::EventBus::alarmEventPublished);

    collisionManager.pollNow();
    QCOMPARE(collisionManager.status().kind, DobotAlarmKind::Collision);
    QVERIFY(collisionManager.status().active);
    QCOMPARE(collisionManager.status().robotMode, 11);
    QCOMPARE(collisionEventSpy.count(), 1);
    const panthera::core::AlarmEvent collisionEvent =
        qvariant_cast<panthera::core::AlarmEvent>(collisionEventSpy.constFirst().at(0));
    QCOMPARE(collisionEvent.title, QStringLiteral("碰撞检测"));
    QVERIFY(collisionEvent.collision);
    QCOMPARE(collisionEvent.codes.size(), 2);

    panthera::core::EventBus alarmBus;
    panthera::core::SafetyKernel alarmSafety;
    FakeDobotAlarmApi alarmApi;
    alarmApi.mode = 9;
    alarmApi.alarms = QStringLiteral("[[20001]]");
    DobotAlarmManager alarmManager(alarmApi.api(), &alarmSafety, &alarmBus);
    QSignalSpy alarmEventSpy(&alarmBus, &panthera::core::EventBus::alarmEventPublished);

    alarmManager.pollNow();
    QCOMPARE(alarmManager.status().kind, DobotAlarmKind::ControllerAlarm);
    QVERIFY(alarmManager.status().active);
    QCOMPARE(alarmManager.status().robotMode, 9);
    const panthera::core::AlarmEvent alarmEvent =
        qvariant_cast<panthera::core::AlarmEvent>(alarmEventSpy.constFirst().at(0));
    QCOMPARE(alarmEvent.title, QStringLiteral("机器人报警"));
    QVERIFY(!alarmEvent.collision);
}

void DobotAlarmManagerTests::successfulResetDoesNotReleaseMotionInterlock()
{
    qRegisterMetaType<panthera::core::AlarmEvent>();
    panthera::core::EventBus eventBus;
    panthera::core::SafetyKernel safetyKernel;
    FakeDobotAlarmApi fake;
    fake.mode = 11;
    fake.alarms = QStringLiteral("[[11740]]");
    DobotAlarmManager manager(fake.api(), &safetyKernel, &eventBus);
    QSignalSpy alarmEventSpy(&eventBus, &panthera::core::EventBus::alarmEventPublished);

    manager.pollNow();
    fake.calls.clear();
    manager.requestReset();

    QCOMPARE(
        fake.calls,
        QStringList({
            QStringLiteral("RobotMode"),
            QStringLiteral("GetErrorID"),
            QStringLiteral("ClearError"),
            QStringLiteral("RobotMode"),
            QStringLiteral("GetErrorID")}));
    QVERIFY(!manager.status().active);
    QVERIFY(manager.status().restartRequired);
    QVERIFY(!qvariant_cast<panthera::core::AlarmEvent>(alarmEventSpy.constLast().at(0)).active);
    QVERIFY(safetyKernel.snapshot().activeInterlocks.contains(panthera::core::InterlockReason::MotionFault));

    // A separate, explicit operator enable notification is required before
    // the manager may release its independent robot-motion interlock.
    fake.mode = 5;
    fake.alarms = QStringLiteral("[]");
    manager.notifyRobotEnabledByOperator();
    QVERIFY(!manager.status().restartRequired);
    QVERIFY(!safetyKernel.snapshot().activeInterlocks.contains(panthera::core::InterlockReason::MotionFault));
}

void DobotAlarmManagerTests::failedResetKeepsAlarmActive()
{
    qRegisterMetaType<panthera::core::AlarmEvent>();
    panthera::core::EventBus eventBus;
    panthera::core::SafetyKernel safetyKernel;
    FakeDobotAlarmApi fake;
    fake.mode = 11;
    fake.alarms = QStringLiteral("[[11740]]");
    fake.clearSucceeds = false;
    DobotAlarmManager manager(fake.api(), &safetyKernel, &eventBus);
    QSignalSpy resetSpy(&manager, &DobotAlarmManager::resetFinished);

    manager.pollNow();
    manager.requestReset();

    QVERIFY(manager.status().active);
    QVERIFY(manager.status().restartRequired);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(resetSpy.constFirst().at(0).toBool(), false);
}

void DobotAlarmManagerTests::emergencyStopInterlockBlocksClearError()
{
    panthera::core::EventBus eventBus;
    panthera::core::SafetyKernel safetyKernel;
    safetyKernel.setEmergencyStopReleased(false);
    FakeDobotAlarmApi fake;
    fake.mode = 11;
    fake.alarms = QStringLiteral("[[11740]]");
    DobotAlarmManager manager(fake.api(), &safetyKernel, &eventBus);

    manager.pollNow();
    fake.calls.clear();
    manager.requestReset();

    QVERIFY(manager.status().active);
    QVERIFY(!fake.calls.contains(QStringLiteral("ClearError")));
    QVERIFY(manager.status().details.contains(QStringLiteral("急停")));
}

void DobotAlarmManagerTests::disconnectDoesNotAssumeAlarmCleared()
{
    panthera::core::EventBus eventBus;
    panthera::core::SafetyKernel safetyKernel;
    FakeDobotAlarmApi fake;
    fake.mode = 11;
    fake.alarms = QStringLiteral("[[11740]]");
    DobotAlarmManager manager(fake.api(), &safetyKernel, &eventBus);

    manager.pollNow();
    fake.connected = false;
    fake.mode = 5;
    fake.alarms = QStringLiteral("[]");
    manager.pollNow();

    QVERIFY(manager.status().active);
    QVERIFY(!manager.status().communicationHealthy);
    QVERIFY(safetyKernel.snapshot().activeInterlocks.contains(panthera::core::InterlockReason::MotionFault));
}

}  // namespace

QTEST_GUILESS_MAIN(DobotAlarmManagerTests)
#include "dobot_alarm_manager_tests.moc"

