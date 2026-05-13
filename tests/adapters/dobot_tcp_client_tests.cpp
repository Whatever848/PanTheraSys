#include <QtTest/QtTest>

#include "adapters/dobot/dobot_motion_controller.h"
#include "adapters/dobot/dobot_tcp_client.h"

using namespace panthera::adapters::dobot;

class DobotTcpClientTests final : public QObject {
    Q_OBJECT

private slots:
    void formatsCommandsFromPdfSyntax();
    void parsesCommandResponses();
    void parsesPoseAndJointPayloads();
    void reportsProtocolErrors();
    void motionControllerBlocksHardwareMotionByDefault();
};

void DobotTcpClientTests::formatsCommandsFromPdfSyntax()
{
    QCOMPARE(formatDobotCommand(QStringLiteral("EnableRobot")), QStringLiteral("EnableRobot()"));
    QCOMPARE(
        formatDobotCommand(QStringLiteral("SpeedFactor"), {QStringLiteral("80")}),
        QStringLiteral("SpeedFactor(80)"));

    const DobotPose pose {-500.0, 100.0, 200.0, 150.0, 0.0, 90.0};
    QCOMPARE(formatDobotPoseArgument(pose), QStringLiteral("pose={-500,100,200,150,0,90}"));

    DobotMotionOptions options;
    options.userIndex = 1;
    options.toolIndex = 0;
    options.accelerationPercent = 20;
    options.velocityPercent = 50;
    options.smoothPercent = 100;

    const QString command = formatDobotCommand(
        QStringLiteral("MovJ"),
        {
            formatDobotPoseArgument(pose),
            formatDobotOption(QStringLiteral("user"), options.userIndex),
            formatDobotOption(QStringLiteral("tool"), options.toolIndex),
            formatDobotOption(QStringLiteral("a"), options.accelerationPercent),
            formatDobotOption(QStringLiteral("v"), options.velocityPercent),
            formatDobotOption(QStringLiteral("cp"), options.smoothPercent),
        });

    QCOMPARE(
        command,
        QStringLiteral("MovJ(pose={-500,100,200,150,0,90},user=1,tool=0,a=20,v=50,cp=100)"));
}

void DobotTcpClientTests::parsesCommandResponses()
{
    const DobotCommandResult emptyResult = parseDobotResponse(QStringLiteral("0,{},EnableRobot();"));
    QVERIFY(emptyResult.ok());
    QCOMPARE(emptyResult.errorId, 0);
    QCOMPARE(emptyResult.payload, QString());
    QCOMPARE(emptyResult.command, QStringLiteral("EnableRobot()"));

    const DobotCommandResult commandIdResult = parseDobotResponse(QStringLiteral("0,{42},MovL(pose={1,2,3,4,5,6});"));
    QVERIFY(commandIdResult.ok());
    QCOMPARE(commandIdResult.payload, QStringLiteral("42"));
    QCOMPARE(commandIdResult.command, QStringLiteral("MovL(pose={1,2,3,4,5,6})"));

    const DobotCommandResult failedResult = parseDobotResponse(QStringLiteral("-10000,{},Mov();"));
    QVERIFY(failedResult.protocolValid());
    QVERIFY(!failedResult.ok());
    QCOMPARE(failedResult.errorId, -10000);
}

void DobotTcpClientTests::parsesPoseAndJointPayloads()
{
    DobotPose pose;
    QVERIFY(parseDobotPosePayload(QStringLiteral("1,2,3,4,5,6"), &pose));
    QCOMPARE(pose.x, 1.0);
    QCOMPARE(pose.y, 2.0);
    QCOMPARE(pose.z, 3.0);
    QCOMPARE(pose.rx, 4.0);
    QCOMPARE(pose.ry, 5.0);
    QCOMPARE(pose.rz, 6.0);

    DobotJointAngles joints;
    QVERIFY(parseDobotJointPayload(QStringLiteral("-1.5,2,3.25,4,5,6"), &joints));
    QCOMPARE(joints.j1, -1.5);
    QCOMPARE(joints.j3, 3.25);
    QCOMPARE(joints.j6, 6.0);
}

void DobotTcpClientTests::reportsProtocolErrors()
{
    const DobotCommandResult missingPayload = parseDobotResponse(QStringLiteral("0,EnableRobot();"));
    QVERIFY(!missingPayload.protocolValid());
    QVERIFY(!missingPayload.ok());

    const DobotCommandResult nonIntegerError = parseDobotResponse(QStringLiteral("ok,{},EnableRobot();"));
    QVERIFY(!nonIntegerError.protocolValid());
    QVERIFY(!nonIntegerError.ok());
}

void DobotTcpClientTests::motionControllerBlocksHardwareMotionByDefault()
{
    DobotMotionController controller;
    QString errorMessage;

    QVERIFY(!controller.moveTo(panthera::core::Coordinate6D {1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("disabled")));
}

QTEST_GUILESS_MAIN(DobotTcpClientTests)

#include "dobot_tcp_client_tests.moc"
