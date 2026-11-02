#include <QtTest/QtTest>
#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>
#include <QtSerialPort/QtSerialPort>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <sstream>

// Inject feedback without loading the vendor SDK or connecting to hardware.
#define private public
#include "modules/dashboard/device_monitor_page.h"
#undef private

using panthera::modules::DeviceMonitorPage;
using diji::adapters::uim::UimMotorSnapshot;

namespace {

struct MonitorFixture {
    panthera::adapters::SimulationDeviceFacade simulation;
    panthera::core::SafetyKernel safety;
    DeviceMonitorPage page {&simulation, &safety};

    MonitorFixture()
    {
        page.m_threeAxisRefreshTimer.stop();
        page.m_threeAxisGateway.m_gatewayOpen = true;
        page.m_threeAxisMotionReady = true;
        for (quint32 nodeId : {6U, 7U, 8U}) {
            diji::adapters::uim::UimNodeInfo node;
            node.nodeId = nodeId;
            page.m_threeAxisNodes.push_back(node);
        }
    }

    ~MonitorFixture()
    {
        page.m_threeAxisGateway.m_gatewayOpen = false;
    }

    bool openFakeSdk(QString* error)
    {
        page.m_threeAxisGateway.m_gatewayOpen = false;
        return page.m_threeAxisGateway.loadSdk(QStringLiteral(PANTHERA_FAKE_UIM_SDK_PATH), error)
            && page.m_threeAxisGateway.openGateway(1024, error);
    }
};

struct FakeSdk {
    QLibrary library {QStringLiteral(PANTHERA_FAKE_UIM_SDK_PATH)};
    using Reset = void (*)();
    using Feedback = void (*)(quint32, int, int, int);
    using FailClear = void (*)(int);
    using Count = int (*)();
    using Command = int (*)(int);
    using Node = quint32 (*)(int);
    Reset reset = reinterpret_cast<Reset>(library.resolve("FakeReset"));
    Feedback feedback = reinterpret_cast<Feedback>(library.resolve("FakeSetFeedback"));
    FailClear failClear = reinterpret_cast<FailClear>(library.resolve("FakeFailClear"));
    Count count = reinterpret_cast<Count>(library.resolve("FakeCommandCount"));
    Command kind = reinterpret_cast<Command>(library.resolve("FakeCommandKind"));
    Command value = reinterpret_cast<Command>(library.resolve("FakeCommandValue"));
    Node node = reinterpret_cast<Node>(library.resolve("FakeCommandNode"));

    bool ready() const { return reset && feedback && failClear && count && kind && value && node; }
};

bool directionButtonsAgree(const DeviceMonitorPage& page)
{
    for (size_t axis = 0; axis < 3; ++axis) {
        if (page.m_threeAxisNegativeButtons[axis]->isEnabled() != page.m_threeAxisContinuousNegativeButtons[axis]->isEnabled()
            || page.m_threeAxisPositiveButtons[axis]->isEnabled() != page.m_threeAxisContinuousPositiveButtons[axis]->isEnabled()) {
            return false;
        }
    }
    return true;
}

}  // namespace

class DeviceMonitorLimitTests final : public QObject {
    Q_OBJECT

private slots:
    void limitButtons_data();
    void limitButtons();
    void absoluteMoveButtonAvailableAtLimits_data();
    void absoluteMoveButtonAvailableAtLimits();
    void absoluteMoveRejectsUnsafeTarget_data();
    void absoluteMoveRejectsUnsafeTarget();
    void motionInterlocks_data();
    void motionInterlocks();
    void retreatCommands_data();
    void retreatCommands();
    void heldMoveFinishes_data();
    void heldMoveFinishes();
    void cancellationFailureKeepsMotionLocked();
    void jogClampsToRemainingTravel_data();
    void jogClampsToRemainingTravel();
    void joggingReplacesHeldMove_data();
    void joggingReplacesHeldMove();
    void linearHomeDetection_data();
    void linearHomeDetection();
    void linearZeroStopsAtS1_data();
    void linearZeroStopsAtS1();
    void waterLoopThresholds_data();
    void waterLoopThresholds();
    void waterLoopMonitorUsesDedicatedThread();
    void emergencyStopReleaseDiscardsMotion_data();
    void emergencyStopReleaseDiscardsMotion();
    void releaseEmergencyStopRequiresOpenGateway();
};

void DeviceMonitorLimitTests::linearHomeDetection_data()
{
    QTest::addColumn<bool>("hasPosition");
    QTest::addColumn<int>("position");
    QTest::addColumn<bool>("sensor1");
    QTest::addColumn<bool>("sensor2");
    QTest::addColumn<bool>("expected");

    QTest::newRow("s1-at-0.130-cm") << true << 832 << false << true << true;
    QTest::newRow("s1-at-0.260-cm") << true << 1664 << false << true << true;
    QTest::newRow("s1-without-position") << false << 0 << false << true << true;
    QTest::newRow("position-tolerance-fallback") << true << 400 << true << true << true;
    QTest::newRow("s2-is-not-zero-side") << true << 832 << true << false << false;
    QTest::newRow("no-limit-outside-tolerance") << true << 832 << true << true << false;
}

void DeviceMonitorLimitTests::linearHomeDetection()
{
    QFETCH(bool, hasPosition);
    QFETCH(int, position);
    QFETCH(bool, sensor1);
    QFETCH(bool, sensor2);
    QFETCH(bool, expected);

    MonitorFixture fixture;
    UimMotorSnapshot snapshot;
    snapshot.hasPosition = hasPosition;
    snapshot.position = position;
    snapshot.hasSensorFeedback = true;
    snapshot.sensor1 = sensor1;
    snapshot.sensor2 = sensor2;

    QCOMPARE(fixture.page.threeAxisLinearHomeReached(snapshot), expected);
}

void DeviceMonitorLimitTests::linearZeroStopsAtS1_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("position");

    QTest::newRow("node7-s1-at-0.260-cm") << 1 << 1664;
    QTest::newRow("node8-s1-at-0.130-cm") << 2 << 832;
}

void DeviceMonitorLimitTests::linearZeroStopsAtS1()
{
    QFETCH(int, axis);
    QFETCH(int, position);

    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, position, 0, 1);

    QVERIFY2(fixture.page.zeroThreeAxisLinearMotorToS1(axis, &error), qPrintable(error));
    QCOMPARE(sdk.count(), 2);
    QCOMPARE(sdk.kind(0), 1);
    QCOMPARE(sdk.value(0), 0);
    QCOMPARE(sdk.kind(1), 2);
    QCOMPARE(sdk.value(1), 0);
    QCOMPARE(sdk.node(0), static_cast<quint32>(axis + 6));
    QCOMPARE(sdk.node(1), static_cast<quint32>(axis + 6));
}

void DeviceMonitorLimitTests::limitButtons_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("position");
    QTest::addColumn<bool>("sensor1");
    QTest::addColumn<bool>("sensor2");
    QTest::addColumn<double>("jog");
    QTest::addColumn<bool>("negativeButton");
    QTest::addColumn<bool>("positiveButton");

    // The reported -10.14 degree / S:-23 state, including a small jog
    // that remains inside the software range in either direction.
    QTest::newRow("node6-s1-reported") << 0 << -18027 << false << true << 1.0 << true << false;
    QTest::newRow("node6-s1-small-jog") << 0 << -18027 << false << true << 0.1 << true << false;
    QTest::newRow("node6-s2") << 0 << 18000 << true << false << 0.1 << false << true;
    QTest::newRow("node6-s1-positive-limit") << 0 << 19062 << false << true << 1.0 << false << true;
    QTest::newRow("node6-s1-positive-before-soft-limit") << 0 << 18000 << false << true << 0.1 << false << true;
    QTest::newRow("node6-s1-positive-overshoot") << 0 << 19065 << false << true << 1.0 << false << true;
    QTest::newRow("node6-s1-negative-overshoot") << 0 << -19293 << false << true << 1.0 << true << false;
    QTest::newRow("node6-limit-at-zero-is-ambiguous") << 0 << 0 << false << true << 1.0 << false << false;
    QTest::newRow("node6-clear") << 0 << 0 << true << true << 1.0 << true << true;
    QTest::newRow("node6-both-limits") << 0 << 0 << false << false << 1.0 << false << false;
    QTest::newRow("node6-software-minimum") << 0 << -19290 << true << true << 0.1 << true << false;
    QTest::newRow("node6-software-maximum") << 0 << 19062 << true << true << 0.1 << false << true;
    QTest::newRow("node7-s2") << 1 << 1000 << true << false << 0.1 << false << true;
    QTest::newRow("node7-s1") << 1 << 73000 << false << true << 0.1 << true << false;
    QTest::newRow("node8-s2") << 2 << 1000 << true << false << 0.1 << false << true;
    QTest::newRow("node8-s1") << 2 << 151000 << false << true << 0.1 << true << false;
    QTest::newRow("node8-upper-reported") << 2 << 640 << false << true << 1.0 << false << true;
    QTest::newRow("node8-lower-s2") << 2 << 151674 << true << false << 1.0 << true << false;
    QTest::newRow("node7-left-s1") << 1 << 640 << false << true << 1.0 << false << true;
    QTest::newRow("node7-right-s2") << 1 << 73821 << true << false << 1.0 << true << false;
    QTest::newRow("node7-both-limits") << 1 << 640 << false << false << 1.0 << false << false;
    QTest::newRow("node8-both-limits") << 2 << 640 << false << false << 1.0 << false << false;
    QTest::newRow("node7-limit-at-center-is-ambiguous") << 1 << 37230 << false << true << 1.0 << false << false;
    QTest::newRow("node8-limit-at-center-is-ambiguous") << 2 << 76157 << false << true << 1.0 << false << false;
    for (int axis : {0, 1, 2}) {
        const int minimum = axis == 0 ? -19290 : 0;
        const int maximum = axis == 0 ? 19062 : (axis == 1 ? 74461 : 152314);
        for (int position : {minimum, maximum}) {
            for (bool sensor1 : {false, true}) {
                const QByteArray name = "node" + QByteArray::number(axis + 6) + "-end-" + QByteArray::number(position) + (sensor1 ? "-s2" : "-s1");
                const bool negative = axis == 0 ? position == minimum : position == maximum;
                QTest::newRow(name.constData()) << axis << position << sensor1 << !sensor1 << 1.0 << negative << !negative;
            }
        }
    }
}

void DeviceMonitorLimitTests::limitButtons()
{
    QFETCH(int, axis);
    QFETCH(int, position);
    QFETCH(bool, sensor1);
    QFETCH(bool, sensor2);
    QFETCH(double, jog);
    QFETCH(bool, negativeButton);
    QFETCH(bool, positiveButton);
    MonitorFixture fixture;
    auto& page = fixture.page;
    page.m_threeAxisJogDistanceSpins[axis]->setValue(jog);

    UimMotorSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.nodeId = axis + 6;
    snapshot.hasPosition = true;
    snapshot.position = position;
    snapshot.hasSensorFeedback = true;
    snapshot.sensor1 = sensor1;
    snapshot.sensor2 = sensor2;
    snapshot.sensor3 = true;
    page.updateThreeAxisSnapshot(snapshot);

    QCOMPARE(page.m_threeAxisNegativeButtons[axis]->isEnabled(), negativeButton);
    QCOMPARE(page.m_threeAxisPositiveButtons[axis]->isEnabled(), positiveButton);
    QCOMPARE(page.m_threeAxisContinuousNegativeButtons[axis]->isEnabled(), negativeButton);
    QCOMPARE(page.m_threeAxisContinuousPositiveButtons[axis]->isEnabled(), positiveButton);
    QVERIFY(page.m_threeAxisJogDistanceSpins[axis]->isEnabled());
}

void DeviceMonitorLimitTests::absoluteMoveButtonAvailableAtLimits_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("position");
    QTest::addColumn<bool>("sensor1");
    QTest::addColumn<bool>("sensor2");
    QTest::addColumn<double>("target");

    QTest::newRow("node6-at-limit") << 0 << 19098 << false << true << 10.70;
    QTest::newRow("node7-at-limit") << 1 << 70318 << false << true << 11.000;
    QTest::newRow("node8-at-limit") << 2 << 832 << false << true << 0.000;
    QTest::newRow("node7-inside-range") << 1 << 32000 << true << true << 5.000;
}

void DeviceMonitorLimitTests::absoluteMoveButtonAvailableAtLimits()
{
    QFETCH(int, axis);
    QFETCH(int, position);
    QFETCH(bool, sensor1);
    QFETCH(bool, sensor2);
    QFETCH(double, target);

    MonitorFixture fixture;
    auto& page = fixture.page;
    UimMotorSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.nodeId = axis + 6;
    snapshot.hasPosition = true;
    snapshot.position = position;
    snapshot.hasSensorFeedback = true;
    snapshot.sensor1 = sensor1;
    snapshot.sensor2 = sensor2;
    snapshot.sensor3 = true;
    page.updateThreeAxisSnapshot(snapshot);

    page.m_threeAxisTargetPositionSpins[axis]->setValue(target);
    QVERIFY(page.m_threeAxisMoveToButtons[axis]->isEnabled());
}

void DeviceMonitorLimitTests::absoluteMoveRejectsUnsafeTarget_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("position");
    QTest::addColumn<bool>("sensor1");
    QTest::addColumn<bool>("sensor2");
    QTest::addColumn<double>("target");
    QTest::addColumn<bool>("expandInputRange");
    QTest::addColumn<QString>("expectedMessage");

    QTest::newRow("node7-toward-active-limit")
        << 1 << 70318 << false << true << 11.000 << false << QStringLiteral("限位保护");
    QTest::newRow("node8-toward-active-limit")
        << 2 << 832 << false << true << 0.000 << false << QStringLiteral("限位保护");
    QTest::newRow("node6-outside-software-range")
        << 0 << 0 << true << true << 12.000 << true << QStringLiteral("超出安全范围");
}

void DeviceMonitorLimitTests::absoluteMoveRejectsUnsafeTarget()
{
    QFETCH(int, axis);
    QFETCH(int, position);
    QFETCH(bool, sensor1);
    QFETCH(bool, sensor2);
    QFETCH(double, target);
    QFETCH(bool, expandInputRange);
    QFETCH(QString, expectedMessage);

    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, position, sensor1 ? 1 : 0, sensor2 ? 1 : 0);
    page.refreshThreeAxisMotors();

    if (expandInputRange) {
        page.m_threeAxisTargetPositionSpins[axis]->setRange(-1000.0, 1000.0);
    }
    page.m_threeAxisTargetPositionSpins[axis]->setValue(target);
    QVERIFY(page.m_threeAxisMoveToButtons[axis]->isEnabled());
    page.m_threeAxisMoveToButtons[axis]->click();

    QCOMPARE(sdk.count(), 0);
    QVERIFY(page.m_threeAxisLogEdit->toPlainText().contains(expectedMessage));
}

void DeviceMonitorLimitTests::motionInterlocks_data()
{
    QTest::addColumn<QString>("interlock");
    for (const auto* interlock : {"unknown-sensors", "unknown-position", "emergency", "initialization", "zeroing"}) {
        QTest::newRow(interlock) << QString::fromLatin1(interlock);
    }
}

void DeviceMonitorLimitTests::motionInterlocks()
{
    QFETCH(QString, interlock);
    MonitorFixture fixture;
    auto& page = fixture.page;
    UimMotorSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.nodeId = 6;
    snapshot.hasPosition = interlock != QStringLiteral("unknown-position");
    snapshot.position = -18027;
    snapshot.hasSensorFeedback = interlock != QStringLiteral("unknown-sensors");
    snapshot.sensor1 = false;
    snapshot.sensor2 = true;
    snapshot.sensor3 = true;
    page.m_threeAxisEmergencyStopActive = interlock == QStringLiteral("emergency");
    page.m_threeAxisMotionReady = interlock != QStringLiteral("initialization");
    page.m_threeAxisZeroingActive = interlock == QStringLiteral("zeroing");
    page.updateThreeAxisSnapshot(snapshot);

    QVERIFY(!page.m_threeAxisMoveToButtons[0]->isEnabled());
    QVERIFY(!page.m_threeAxisNegativeButtons[0]->isEnabled());
    QVERIFY(!page.m_threeAxisPositiveButtons[0]->isEnabled());
    QVERIFY(!page.m_threeAxisContinuousNegativeButtons[0]->isEnabled());
    QVERIFY(!page.m_threeAxisContinuousPositiveButtons[0]->isEnabled());
}

void DeviceMonitorLimitTests::retreatCommands_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("position");
    QTest::addColumn<int>("direction");
    QTest::addColumn<QString>("action");
    for (int axis : {0, 1, 2}) {
        const int minimum = axis == 0 ? -19290 : 0;
        const int maximum = axis == 0 ? 19062 : (axis == 1 ? 74461 : 152314);
        for (int position : {minimum + 640, minimum - 3, maximum - 640, maximum + 3}) {
            const int direction = position < (minimum + maximum) / 2 ? 1 : -1;
            for (const auto* action : {"absolute", "jog", "continuous"}) {
                const QByteArray name = "node" + QByteArray::number(axis + 6) + '-' + QByteArray::number(position) + '-' + action;
                QTest::newRow(name.constData()) << axis << position << direction << QString::fromLatin1(action);
            }
        }
    }
}

void DeviceMonitorLimitTests::retreatCommands()
{
    QFETCH(int, axis);
    QFETCH(int, position);
    QFETCH(int, direction);
    QFETCH(QString, action);
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, position, 0, 1);
    page.refreshThreeAxisMotors();
    bool buttonsAgreed = directionButtonsAgree(page);
    connect(&page.m_threeAxisGateway, &diji::adapters::uim::UimMotorGateway::snapshotChanged, &page,
        [&]() { buttonsAgreed = buttonsAgreed && directionButtonsAgree(page); });

    // Even direct slot calls must reject the unsafe continuous direction,
    // including a position a few steps beyond the rounded software maximum.
    page.startThreeAxisContinuousMove(axis, -direction);
    page.moveThreeAxisMotor(axis, -direction);
    QCOMPARE(sdk.count(), 0);

    const bool useNegativeButton = (axis == 0 ? direction > 0 : direction < 0);
    int expectedDelta = 0;
    if (action == QStringLiteral("absolute")) {
        // In the reported node 8 upper-limit state, 0.000 cm is outward;
        // submission stays available and the click-time check decides whether
        // the requested direction is safe.
        if (axis != 0 && position == 640) {
            page.m_threeAxisTargetPositionSpins[axis]->setValue(0.0);
            QVERIFY(page.m_threeAxisMoveToButtons[axis]->isEnabled());
        }
        const double target = axis == 0 ? 0.0 : (direction > 0 ? 1.0 : 10.0);
        page.m_threeAxisTargetPositionSpins[axis]->setValue(target);
        QVERIFY(page.m_threeAxisMoveToButtons[axis]->isEnabled());
        page.m_threeAxisMoveToButtons[axis]->click();
        expectedDelta = (axis == 0 ? 0 : (direction > 0 ? 6400 : 64000)) - position;
    } else if (action == QStringLiteral("jog")) {
        auto* button = useNegativeButton ? page.m_threeAxisNegativeButtons[axis] : page.m_threeAxisPositiveButtons[axis];
        QVERIFY(button->isEnabled());
        button->click();
        expectedDelta = direction * (axis == 0 ? 1778 : 6400);
    } else {
        auto* button = useNegativeButton ? page.m_threeAxisContinuousNegativeButtons[axis] : page.m_threeAxisContinuousPositiveButtons[axis];
        QVERIFY(button->isEnabled());
        button->setDown(true);
        QVERIFY(QMetaObject::invokeMethod(button, "pressed", Qt::DirectConnection));
        const int minimum = axis == 0 ? -19290 : 0;
        const int maximum = axis == 0 ? 19062 : (axis == 1 ? 74461 : 152314);
        expectedDelta = (direction > 0 ? maximum : minimum) - position;
        QVERIFY(page.m_threeAxisContinuousMoving[axis]);
        // The still-active limit permits retreat while the button is held.
        page.refreshThreeAxisMotors();
        QVERIFY(page.m_threeAxisContinuousMoving[axis]);
    }

    QVERIFY(buttonsAgreed);
    QCOMPARE(sdk.count(), 4);
    QCOMPARE(sdk.kind(0), 1); QCOMPARE(sdk.value(0), 0); // SPD=0
    QCOMPARE(sdk.kind(1), 2); QCOMPARE(sdk.value(1), 0); // cancel retained STP
    QCOMPARE(sdk.kind(2), 1); QCOMPARE(sdk.value(2), 2400);
    QCOMPARE(sdk.kind(3), 2); QCOMPARE(sdk.value(3), expectedDelta);
    for (int index = 0; index < sdk.count(); ++index) {
        QCOMPARE(sdk.node(index), static_cast<quint32>(axis + 6));
    }
}

void DeviceMonitorLimitTests::heldMoveFinishes_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("direction");
    QTest::addColumn<int>("position");
    QTest::addColumn<bool>("sensor1");
    QTest::addColumn<bool>("held");
    for (int axis : {0, 1, 2}) {
        const int minimum = axis == 0 ? -19290 : 0;
        const int maximum = axis == 0 ? 19062 : (axis == 1 ? 74461 : 152314);
        for (int direction : {-1, 1}) {
            const int end = direction > 0 ? maximum : minimum;
            const QByteArray name = "node" + QByteArray::number(axis + 6) + (direction > 0 ? "-maximum" : "-minimum");
            QTest::newRow((name + "-hardware").constData()) << axis << direction << end - direction * 640 << false << true;
            QTest::newRow((name + "-software").constData()) << axis << direction << end << true << true;
            QTest::newRow((name + "-combined").constData()) << axis << direction << end << false << true;
        }
        const QByteArray name = "node" + QByteArray::number(axis + 6) + "-lost-release";
        QTest::newRow(name.constData()) << axis << 1 << 5000 << true << false;
    }
}

void DeviceMonitorLimitTests::heldMoveFinishes()
{
    QFETCH(int, axis);
    QFETCH(int, direction);
    QFETCH(int, position);
    QFETCH(bool, sensor1);
    QFETCH(bool, held);
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, axis == 0 ? 0 : 32000, 1, 1);
    page.refreshThreeAxisMotors();
    bool buttonsAgreed = directionButtonsAgree(page);
    connect(&page.m_threeAxisGateway, &diji::adapters::uim::UimMotorGateway::snapshotChanged, &page,
        [&]() { buttonsAgreed = buttonsAgreed && directionButtonsAgree(page); });

    const bool useNegativeButton = (axis == 0 ? direction > 0 : direction < 0);
    auto* button = useNegativeButton ? page.m_threeAxisContinuousNegativeButtons[axis] : page.m_threeAxisContinuousPositiveButtons[axis];
    button->setDown(true);
    QVERIFY(QMetaObject::invokeMethod(button, "pressed", Qt::DirectConnection));
    QVERIFY(page.m_threeAxisContinuousMoving[axis]);
    QCOMPARE(sdk.count(), 4);

    sdk.feedback(axis + 6, position, sensor1 ? 1 : 0, 1);
    button->setDown(held);
    page.refreshThreeAxisMotors();
    QVERIFY(buttonsAgreed);
    QVERIFY(!page.m_threeAxisContinuousMoving[axis]);
    QCOMPARE(page.m_threeAxisContinuousDirections[axis], 0);
    QVERIFY(!button->isDown());
    QCOMPARE(sdk.count(), 6);
    QCOMPARE(sdk.kind(4), 1); QCOMPARE(sdk.value(4), 0);
    QCOMPARE(sdk.kind(5), 2); QCOMPARE(sdk.value(5), 0);

    page.m_threeAxisTargetPositionSpins[axis]->setValue(axis == 0 ? 0.0 : 5.0);
    QVERIFY(page.m_threeAxisMoveToButtons[axis]->isEnabled());
    auto* retreat = useNegativeButton ? page.m_threeAxisPositiveButtons[axis] : page.m_threeAxisNegativeButtons[axis];
    QVERIFY(retreat->isEnabled());
    if (held) {
        QVERIFY(!button->isEnabled());
    }
    retreat->click();
    QCOMPARE(sdk.count(), 10);
    const int jogSteps = axis == 0 ? 1778 : 6400;
    const int expectedDelta = !held && axis != 0 ? -5000 : -direction * jogSteps;
    QCOMPARE(sdk.value(9), expectedDelta);
}

void DeviceMonitorLimitTests::cancellationFailureKeepsMotionLocked()
{
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(6, 0, 1, 1);
    page.refreshThreeAxisMotors();
    auto* button = page.m_threeAxisContinuousNegativeButtons[0];
    button->setDown(true);
    QVERIFY(QMetaObject::invokeMethod(button, "pressed", Qt::DirectConnection));
    sdk.feedback(6, 19062, 0, 1);
    sdk.failClear(1);
    page.refreshThreeAxisMotors();
    QVERIFY(page.m_threeAxisContinuousMoving[0]);
    QVERIFY(!page.m_threeAxisMoveToButtons[0]->isEnabled());
    QVERIFY(!page.m_threeAxisPositiveButtons[0]->isEnabled());

    sdk.failClear(0);
    page.refreshThreeAxisMotors();
    QVERIFY(!page.m_threeAxisContinuousMoving[0]);
    QVERIFY(page.m_threeAxisMoveToButtons[0]->isEnabled());
    QVERIFY(page.m_threeAxisPositiveButtons[0]->isEnabled());
}

void DeviceMonitorLimitTests::jogClampsToRemainingTravel_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("direction");
    QTest::addColumn<int>("position");
    for (int axis : {0, 1, 2}) {
        const int minimum = axis == 0 ? -19290 : 0;
        const int maximum = axis == 0 ? 19062 : (axis == 1 ? 74461 : 152314);
        const QByteArray name = "node" + QByteArray::number(axis + 6);
        QTest::newRow((name + "-minimum").constData()) << axis << -1 << minimum + 100;
        QTest::newRow((name + "-maximum").constData()) << axis << 1 << maximum - 100;
    }
}

void DeviceMonitorLimitTests::jogClampsToRemainingTravel()
{
    QFETCH(int, axis);
    QFETCH(int, direction);
    QFETCH(int, position);
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, position, 1, 1);
    page.refreshThreeAxisMotors();
    QVERIFY(directionButtonsAgree(page));
    const bool negativeButton = axis == 0 ? direction > 0 : direction < 0;
    auto* jog = negativeButton ? page.m_threeAxisNegativeButtons[axis] : page.m_threeAxisPositiveButtons[axis];
    auto* hold = negativeButton ? page.m_threeAxisContinuousNegativeButtons[axis] : page.m_threeAxisContinuousPositiveButtons[axis];
    QVERIFY(jog->isEnabled());
    QVERIFY(hold->isEnabled());
    jog->click();
    QCOMPARE(sdk.count(), 4);
    QCOMPARE(sdk.value(3), direction * 100);

    sdk.feedback(axis + 6, position + direction * 100, 1, 1);
    page.refreshThreeAxisMotors();
    QVERIFY(!jog->isEnabled());
    QVERIFY(!hold->isEnabled());
    QVERIFY(directionButtonsAgree(page));
}

void DeviceMonitorLimitTests::joggingReplacesHeldMove_data()
{
    QTest::addColumn<int>("axis");
    QTest::newRow("node6") << 0;
    QTest::newRow("node7") << 1;
    QTest::newRow("node8") << 2;
}

void DeviceMonitorLimitTests::joggingReplacesHeldMove()
{
    QFETCH(int, axis);
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    sdk.feedback(axis + 6, axis == 0 ? 0 : 32000, 1, 1);
    page.refreshThreeAxisMotors();
    auto* hold = axis == 0 ? page.m_threeAxisContinuousNegativeButtons[axis] : page.m_threeAxisContinuousPositiveButtons[axis];
    auto* jog = axis == 0 ? page.m_threeAxisNegativeButtons[axis] : page.m_threeAxisPositiveButtons[axis];
    hold->setDown(true);
    QVERIFY(QMetaObject::invokeMethod(hold, "pressed", Qt::DirectConnection));
    page.refreshThreeAxisMotors();
    QVERIFY(page.m_threeAxisContinuousMoving[axis]);
    QVERIFY(hold->isDown());
    QVERIFY(directionButtonsAgree(page));
    QVERIFY(jog->isEnabled());
    jog->click();
    QVERIFY(!page.m_threeAxisContinuousMoving[axis]);
    QCOMPARE(sdk.count(), 10);
    QCOMPARE(sdk.kind(4), 1); QCOMPARE(sdk.value(4), 0);
    QCOMPARE(sdk.kind(5), 2); QCOMPARE(sdk.value(5), 0);
    QCOMPARE(sdk.value(9), axis == 0 ? 1778 : 6400);
    QVERIFY(QMetaObject::invokeMethod(hold, "released", Qt::DirectConnection));
    QCOMPARE(sdk.count(), 10);
    QVERIFY(directionButtonsAgree(page));
}

void DeviceMonitorLimitTests::waterLoopThresholds_data()
{
    QTest::addColumn<double>("levelCentimeters");
    QTest::addColumn<int>("expectedMode");

    QTest::newRow("below-lower") << 12.9 << static_cast<int>(panthera::modules::WaterLoopPumpMode::SupplyOnly);
    QTest::newRow("at-lower") << 13.0 << static_cast<int>(panthera::modules::WaterLoopPumpMode::Both);
    QTest::newRow("inside-range") << 14.0 << static_cast<int>(panthera::modules::WaterLoopPumpMode::Both);
    QTest::newRow("at-upper") << 15.0 << static_cast<int>(panthera::modules::WaterLoopPumpMode::Both);
    QTest::newRow("above-upper") << 15.1 << static_cast<int>(panthera::modules::WaterLoopPumpMode::ReturnOnly);
}

void DeviceMonitorLimitTests::waterLoopThresholds()
{
    QFETCH(double, levelCentimeters);
    QFETCH(int, expectedMode);

    QCOMPARE(
        static_cast<int>(panthera::modules::waterLoopPumpModeForLevel(levelCentimeters)),
        expectedMode);
}

void DeviceMonitorLimitTests::waterLoopMonitorUsesDedicatedThread()
{
    QObject dispatchTarget;
    int pollCount = 0;
    panthera::modules::WaterLoopMonitorThread monitor(
        &dispatchTarget,
        [&pollCount]() { ++pollCount; },
        20);

    monitor.startMonitoring();
    QTRY_VERIFY_WITH_TIMEOUT(pollCount >= 2, 500);
    QVERIFY(monitor.isMonitoring());
    monitor.stopMonitoring();
    QVERIFY(!monitor.isRunning());
    QVERIFY(!monitor.isMonitoring());
}

void DeviceMonitorLimitTests::emergencyStopReleaseDiscardsMotion_data()
{
    QTest::addColumn<bool>("movingBeforeEmergency");
    QTest::newRow("idle-before-emergency") << false;
    QTest::newRow("moving-before-emergency") << true;
}

void DeviceMonitorLimitTests::emergencyStopReleaseDiscardsMotion()
{
    QFETCH(bool, movingBeforeEmergency);
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    for (int axis = 0; axis < 3; ++axis) {
        sdk.feedback(axis + 6, axis == 0 ? 0 : 32000, 1, 1);
    }
    page.refreshThreeAxisMotors();
    QVERIFY(std::all_of(page.m_threeAxisMotorEnabled.cbegin(), page.m_threeAxisMotorEnabled.cend(), [](bool enabled) {
        return enabled;
    }));

    if (movingBeforeEmergency) {
        page.startThreeAxisContinuousMove(2, 1);
        QVERIFY(page.m_threeAxisContinuousMoving[2]);
    }
    const int commandCountBeforeEmergency = sdk.count();

    page.emergencyStopThreeAxisMotors();
    QVERIFY(page.m_threeAxisEmergencyStopActive);
    QVERIFY(std::none_of(page.m_threeAxisMotorEnabled.cbegin(), page.m_threeAxisMotorEnabled.cend(), [](bool enabled) {
        return enabled;
    }));
    QCOMPARE(sdk.count(), commandCountBeforeEmergency + 9);
    int emergencyDisableCount = 0;
    for (int index = commandCountBeforeEmergency; index < sdk.count(); ++index) {
        QVERIFY(sdk.kind(index) != 3);
        if (sdk.kind(index) == 4) {
            ++emergencyDisableCount;
        }
        if (sdk.kind(index) == 1 || sdk.kind(index) == 2) {
            QCOMPARE(sdk.value(index), 0);
        }
    }
    QCOMPARE(emergencyDisableCount, 3);

    const int commandCountBeforeRelease = sdk.count();
    page.releaseThreeAxisEmergencyStop();
    QVERIFY(!page.m_threeAxisEmergencyStopActive);
    if (movingBeforeEmergency) {
        QCOMPARE(page.m_threeAxisGateway.latestSnapshot().nodeId, 8U);
        QVERIFY(page.m_threeAxisGateway.latestSnapshot().step != 0);
    }
    QVERIFY(std::none_of(page.m_threeAxisMotorEnabled.cbegin(), page.m_threeAxisMotorEnabled.cend(), [](bool enabled) {
        return enabled;
    }));
    QCOMPARE(sdk.count(), commandCountBeforeRelease + 9);
    int releaseDisableCount = 0;
    for (int index = commandCountBeforeRelease; index < sdk.count(); ++index) {
        QVERIFY(sdk.kind(index) != 3);
        if (sdk.kind(index) == 4) {
            ++releaseDisableCount;
        }
        if (sdk.kind(index) == 1 || sdk.kind(index) == 2) {
            QCOMPARE(sdk.value(index), 0);
        }
    }
    QCOMPARE(releaseDisableCount, 3);
    for (int axis = 0; axis < 3; ++axis) {
        QVERIFY(!page.m_threeAxisMoveToButtons[axis]->isEnabled());
        QVERIFY(!page.m_threeAxisNegativeButtons[axis]->isEnabled());
        QVERIFY(!page.m_threeAxisPositiveButtons[axis]->isEnabled());
        QVERIFY(!page.m_threeAxisContinuousMoving[axis]);
    }

    const int commandCountBeforeEnable = sdk.count();
    page.enableAllThreeAxisMotors();
    QVERIFY(std::all_of(page.m_threeAxisMotorEnabled.cbegin(), page.m_threeAxisMotorEnabled.cend(), [](bool enabled) {
        return enabled;
    }));
    QCOMPARE(sdk.count(), commandCountBeforeEnable + 18);
    int safeEnableCount = 0;
    int safeEnableDisableCount = 0;
    for (int index = commandCountBeforeEnable; index < sdk.count(); ++index) {
        if (sdk.kind(index) == 3) {
            ++safeEnableCount;
        } else if (sdk.kind(index) == 4) {
            ++safeEnableDisableCount;
        }
        if (sdk.kind(index) == 1 || sdk.kind(index) == 2) {
            QCOMPARE(sdk.value(index), 0);
        }
    }
    QCOMPARE(safeEnableCount, 3);
    QCOMPARE(safeEnableDisableCount, 3);
}

void DeviceMonitorLimitTests::releaseEmergencyStopRequiresOpenGateway()
{
    FakeSdk sdk;
    QVERIFY(sdk.ready());
    sdk.reset();
    MonitorFixture fixture;
    auto& page = fixture.page;
    QString error;
    QVERIFY2(fixture.openFakeSdk(&error), qPrintable(error));
    page.m_threeAxisEmergencyStopActive = true;
    page.m_threeAxisGateway.closeGateway();

    page.releaseThreeAxisEmergencyStop();

    QVERIFY(page.m_threeAxisEmergencyStopActive);
    QCOMPARE(sdk.count(), 0);
}

QTEST_MAIN(DeviceMonitorLimitTests)

#include "device_monitor_limit_tests.moc"
