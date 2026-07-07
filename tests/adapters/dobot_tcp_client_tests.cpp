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
    void parsesGetPoseResponses();
    void parsesStartPosePayloads();
    void reportsProtocolErrors();
    void formatsScriptTrajectoryCommands();
    void usesDashboardPortForMotionCommandsByDefault();
    void rejectsEmptyScriptNamesBeforeConnecting();
    void motionControllerBlocksHardwareMotionByDefault();
};

void DobotTcpClientTests::formatsCommandsFromPdfSyntax()
{
    QCOMPARE(formatDobotCommand(QStringLiteral("EnableRobot")), QStringLiteral("EnableRobot()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("EnableRobot"), {QStringLiteral("1")}), QStringLiteral("EnableRobot(1)"));
    QCOMPARE(formatDobotCommand(QStringLiteral("DisableRobot")), QStringLiteral("DisableRobot()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("GetErrorID")), QStringLiteral("GetErrorID()"));
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

    const DobotPose safeOriginPose {568.4855, -652.5055, 753.2592, -179.9999, 0.0, -152.9531};
    QCOMPARE(
        formatDobotCommand(
            QStringLiteral("MovJ"),
            {
                formatDobotPoseArgument(safeOriginPose),
                formatDobotOption(QStringLiteral("user"), 0),
                formatDobotOption(QStringLiteral("tool"), 0),
                formatDobotOption(QStringLiteral("a"), 1),
                formatDobotOption(QStringLiteral("v"), 10),
            }),
        QStringLiteral("MovJ(pose={568.4855,-652.5055,753.2592,-179.9999,0,-152.9531},user=0,tool=0,a=1,v=10)"));

    QCOMPARE(
        formatDobotCommand(
            QStringLiteral("MovL"),
            {
                formatDobotPoseArgument(pose),
                formatDobotOption(QStringLiteral("user"), 0),
                formatDobotOption(QStringLiteral("tool"), 0),
                formatDobotOption(QStringLiteral("a"), 1),
                formatDobotOption(QStringLiteral("v"), 10),
            }),
        QStringLiteral("MovL(pose={-500,100,200,150,0,90},user=0,tool=0,a=1,v=10)"));

    QCOMPARE(
        formatDobotCommand(QStringLiteral("GetStartPose"), {QStringLiteral("test1.csv"), QStringLiteral("1")}),
        QStringLiteral("GetStartPose(test1.csv,1)"));
    QCOMPARE(
        formatDobotCommand(
            QStringLiteral("RunTo"),
            {
                formatDobotPoseArgument(pose),
                formatDobotOption(QStringLiteral("user"), 0),
                formatDobotOption(QStringLiteral("tool"), 0),
                formatDobotOption(QStringLiteral("moveType"), 1),
                formatDobotOption(QStringLiteral("a"), 1),
                formatDobotOption(QStringLiteral("v"), 1),
            }),
        QStringLiteral("RunTo(pose={-500,100,200,150,0,90},user=0,tool=0,moveType=1,a=1,v=1)"));
    QCOMPARE(
        formatDobotCommand(
            QStringLiteral("StartPath"),
            {
                QStringLiteral("test1.csv"),
                formatDobotOption(QStringLiteral("isConst"), 1),
                formatDobotOption(QStringLiteral("multi"), 1.0),
                formatDobotOption(QStringLiteral("sample"), 50),
                formatDobotOption(QStringLiteral("freq"), 0.2),
                formatDobotOption(QStringLiteral("user"), 0),
                formatDobotOption(QStringLiteral("tool"), 0),
            }),
        QStringLiteral("StartPath(test1.csv,isConst=1,multi=1,sample=50,freq=0.2,user=0,tool=0)"));
    QCOMPARE(formatDobotCommand(QStringLiteral("PathRecovery")), QStringLiteral("PathRecovery()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("PathRecoveryStop")), QStringLiteral("PathRecoveryStop()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("PathRecoveryStatus")), QStringLiteral("PathRecoveryStatus()"));
    QCOMPARE(
        formatDobotCommand(
            QStringLiteral("OffsetPara"),
            {
                formatDobotNumber(10.0),
                formatDobotNumber(10.0),
                formatDobotNumber(10.0),
                formatDobotNumber(0.0),
                formatDobotNumber(0.0),
                formatDobotNumber(0.0),
            }),
        QStringLiteral("OffsetPara(10,10,10,0,0,0)"));
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

    const DobotCommandResult startPoseResult = parseDobotResponse(QStringLiteral("0,{0,{1,2,3,4,5,6},0,0,{10,20,30,40,50,60}},GetStartPose(test1.csv,1);"));
    QVERIFY(startPoseResult.ok());
    QCOMPARE(startPoseResult.payload, QStringLiteral("0,{1,2,3,4,5,6},0,0,{10,20,30,40,50,60}"));
    QCOMPARE(startPoseResult.command, QStringLiteral("GetStartPose(test1.csv,1)"));

    const DobotCommandResult failedResult = parseDobotResponse(QStringLiteral("-10000,{},Mov();"));
    QVERIFY(failedResult.protocolValid());
    QVERIFY(!failedResult.ok());
    QCOMPARE(failedResult.errorId, -10000);

    const DobotCommandResult enableFailedResult = parseDobotResponse(QStringLiteral("-2,{not tcp mode or system is starting},EnableRobot(1);"));
    QVERIFY(enableFailedResult.protocolValid());
    QVERIFY(!enableFailedResult.ok());
    QCOMPARE(enableFailedResult.errorId, -2);
    QCOMPARE(enableFailedResult.payload, QStringLiteral("not tcp mode or system is starting"));
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

void DobotTcpClientTests::parsesGetPoseResponses()
{
    RobotTcpPose pose;
    QString error;
    QVERIFY(parseGetPoseResponse(QStringLiteral("0,{100.123,200.456,300.789,1.111,2.222,3.333},GetPose();"), pose, error));
    QVERIFY(pose.valid);
    QCOMPARE(pose.x, 100.123);
    QCOMPARE(pose.y, 200.456);
    QCOMPARE(pose.z, 300.789);
    QCOMPARE(pose.rx, 1.111);
    QCOMPARE(pose.ry, 2.222);
    QCOMPARE(pose.rz, 3.333);
    QVERIFY(error.isEmpty());

    QVERIFY(!parseGetPoseResponse(QStringLiteral("-1,{},GetPose();"), pose, error));
    QVERIFY(!pose.valid);
    QVERIFY(error.contains(QStringLiteral("response=-1,{},GetPose();")));

    QVERIFY(!parseGetPoseResponse(QStringLiteral("0,{1,2,3},GetPose();"), pose, error));
    QVERIFY(!pose.valid);
    QVERIFY(error.contains(QStringLiteral("response=0,{1,2,3},GetPose();")));
}

void DobotTcpClientTests::parsesStartPosePayloads()
{
    DobotStartPose startPose;
    QVERIFY(parseDobotStartPosePayload(QStringLiteral("0,{1,2,3,4,5,6},0,1,{10,20,30,40,50,60}"), &startPose));
    QCOMPARE(startPose.pointType, 0);
    QCOMPARE(startPose.userIndex, 0);
    QCOMPARE(startPose.toolIndex, 1);
    QVERIFY(startPose.hasJoints);
    QVERIFY(startPose.hasPose);
    QCOMPARE(startPose.joints.j1, 1.0);
    QCOMPARE(startPose.joints.j6, 6.0);
    QCOMPARE(startPose.pose.x, 10.0);
    QCOMPARE(startPose.pose.rz, 60.0);

    QVERIFY(parseDobotStartPosePayload(QStringLiteral("1,{11,12,13,14,15,16}"), &startPose));
    QCOMPARE(startPose.pointType, 1);
    QVERIFY(startPose.hasJoints);
    QVERIFY(!startPose.hasPose);
    QCOMPARE(startPose.joints.j1, 11.0);
    QCOMPARE(startPose.joints.j6, 16.0);

    QVERIFY(parseDobotStartPosePayload(QStringLiteral("2,{21,22,23,24,25,26}"), &startPose));
    QCOMPARE(startPose.pointType, 2);
    QVERIFY(!startPose.hasJoints);
    QVERIFY(startPose.hasPose);
    QCOMPARE(startPose.pose.x, 21.0);
    QCOMPARE(startPose.pose.rz, 26.0);
}

void DobotTcpClientTests::reportsProtocolErrors()
{
    const DobotCommandResult missingPayload = parseDobotResponse(QStringLiteral("0,EnableRobot();"));
    QVERIFY(!missingPayload.protocolValid());
    QVERIFY(!missingPayload.ok());

    const DobotCommandResult nonIntegerError = parseDobotResponse(QStringLiteral("ok,{},EnableRobot();"));
    QVERIFY(!nonIntegerError.protocolValid());
    QVERIFY(!nonIntegerError.ok());

    const DobotCommandResult wrongControlMode = parseDobotResponse(QStringLiteral("Control Mode Is Not Tcp\t"));
    QVERIFY(!wrongControlMode.protocolValid());
    QVERIFY(!wrongControlMode.ok());
    QVERIFY(wrongControlMode.protocolError.contains(QStringLiteral("TCP control mode")));
}

void DobotTcpClientTests::formatsScriptTrajectoryCommands()
{
    QCOMPARE(
        formatDobotCommand(QStringLiteral("RunScript"), {QStringLiteral("preset_treatment_position")}),
        QStringLiteral("RunScript(preset_treatment_position)"));
    QCOMPARE(formatDobotCommand(QStringLiteral("StopScript")), QStringLiteral("StopScript()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("PauseScript")), QStringLiteral("PauseScript()"));
    QCOMPARE(formatDobotCommand(QStringLiteral("ContinueScript")), QStringLiteral("ContinueScript()"));
}

void DobotTcpClientTests::usesDashboardPortForMotionCommandsByDefault()
{
    const DobotConnectionSettings settings;
    QCOMPARE(settings.commandPort, static_cast<quint16>(29999));
    QCOMPARE(settings.motionPort, settings.commandPort);
}

void DobotTcpClientTests::rejectsEmptyScriptNamesBeforeConnecting()
{
    DobotControllerClient client;
    QString errorMessage;

    const DobotCommandResult result = client.runScript(QStringLiteral("  "), &errorMessage);
    QVERIFY(!result.ok());
    QVERIFY(errorMessage.contains(QStringLiteral("RunScript")));
    QVERIFY(!client.isConnected());
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
