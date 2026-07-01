#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QVector>

#include <array>

namespace panthera::adapters::dobot {

struct DobotConnectionSettings {
    QString host {QStringLiteral("192.168.5.1")};
    quint16 commandPort {29999};
    quint16 motionPort {29999};
    int timeoutMs {3000};
};

struct DobotPose {
    double x {0.0};
    double y {0.0};
    double z {0.0};
    double rx {0.0};
    double ry {0.0};
    double rz {0.0};
};

struct DobotJointAngles {
    double j1 {0.0};
    double j2 {0.0};
    double j3 {0.0};
    double j4 {0.0};
    double j5 {0.0};
    double j6 {0.0};
};

struct DobotStartPose {
    int pointType {-1};
    DobotJointAngles joints;
    int userIndex {0};
    int toolIndex {0};
    DobotPose pose;
    bool hasJoints {false};
    bool hasPose {false};
};

struct DobotMotionOptions {
    int userIndex {-1};
    int toolIndex {-1};
    int accelerationPercent {-1};
    int velocityPercent {-1};
    int smoothPercent {-1};
    double speedMmPerSecond {-1.0};
    double radiusMm {-1.0};
};

struct DobotStartPathOptions {
    int isConst {0};
    double multi {1.0};
    int sample {50};
    double freq {0.2};
    int userIndex {-1};
    int toolIndex {-1};
};

struct DobotCommandResult {
    int errorId {0};
    QString payload;
    QString command;
    QString raw;
    QString protocolError;

    [[nodiscard]] bool protocolValid() const;
    [[nodiscard]] bool ok() const;
};

QString formatDobotNumber(double value);
QString formatDobotCommand(const QString& name, const QStringList& arguments = {});
QString formatDobotPoseArgument(const DobotPose& pose);
QString formatDobotJointArgument(const DobotJointAngles& joints);
QString formatDobotOption(const QString& key, int value);
QString formatDobotOption(const QString& key, double value);

DobotCommandResult parseDobotResponse(const QString& response);
QVector<double> parseDobotDoublePayload(const QString& payload, bool* ok = nullptr);
bool parseDobotPosePayload(const QString& payload, DobotPose* pose);
bool parseDobotJointPayload(const QString& payload, DobotJointAngles* joints);
bool parseDobotStartPosePayload(const QString& payload, DobotStartPose* startPose);

class DobotTcpClient final : public QObject {
    Q_OBJECT

public:
    explicit DobotTcpClient(DobotConnectionSettings settings = {}, QObject* parent = nullptr);

    [[nodiscard]] DobotConnectionSettings settings() const;
    void setSettings(const DobotConnectionSettings& settings);

    bool connectToController(QString* errorMessage = nullptr);
    void disconnectFromController();
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QTcpSocket* socket();
    [[nodiscard]] const QTcpSocket* socket() const;

    DobotCommandResult sendCommand(const QString& command, QString* errorMessage = nullptr);
    DobotCommandResult sendCommand(const QString& name, const QStringList& arguments, QString* errorMessage = nullptr);

private:
    DobotCommandResult makeProtocolError(const QString& message, const QString& raw = {}) const;
    bool ensureConnected(QString* errorMessage);
    bool setError(const QString& message, QString* errorMessage);

    DobotConnectionSettings m_settings;
    QTcpSocket m_socket;
    QString m_lastError;
};

class DobotControllerClient final : public QObject {
    Q_OBJECT

public:
    explicit DobotControllerClient(DobotConnectionSettings settings = {}, QObject* parent = nullptr);

    [[nodiscard]] DobotConnectionSettings settings() const;
    void setSettings(const DobotConnectionSettings& settings);

    bool connectToController(QString* errorMessage = nullptr);
    void disconnectFromController();
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QTcpSocket* dashboardSocket();
    [[nodiscard]] const QTcpSocket* dashboardSocket() const;

    DobotCommandResult rawCommand(const QString& command, QString* errorMessage = nullptr);

    DobotCommandResult requestControl(QString* errorMessage = nullptr);
    DobotCommandResult powerOn(QString* errorMessage = nullptr);
    DobotCommandResult enableRobot(QString* errorMessage = nullptr);
    DobotCommandResult enableRobot(double loadKg, QString* errorMessage = nullptr);
    DobotCommandResult enableRobot(
        double loadKg,
        double centerX,
        double centerY,
        double centerZ,
        bool checkLoad,
        QString* errorMessage = nullptr);
    DobotCommandResult disableRobot(QString* errorMessage = nullptr);
    DobotCommandResult clearError(QString* errorMessage = nullptr);
    DobotCommandResult stop(QString* errorMessage = nullptr);
    DobotCommandResult pause(QString* errorMessage = nullptr);
    DobotCommandResult continueMotion(QString* errorMessage = nullptr);
    DobotCommandResult emergencyStop(bool pressed, QString* errorMessage = nullptr);
    DobotCommandResult runScript(const QString& projectName, QString* errorMessage = nullptr);
    DobotCommandResult stopScript(QString* errorMessage = nullptr);
    DobotCommandResult pauseScript(QString* errorMessage = nullptr);
    DobotCommandResult continueScript(QString* errorMessage = nullptr);
    DobotCommandResult getStartPose(const QString& traceName, DobotStartPose* startPose = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getStartPose(const QString& traceName, int pathType, DobotStartPose* startPose = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult startPath(const QString& traceName, QString* errorMessage = nullptr);
    DobotCommandResult startPath(const QString& traceName, const DobotStartPathOptions& options, QString* errorMessage = nullptr);
    DobotCommandResult pathRecovery(QString* errorMessage = nullptr);
    DobotCommandResult pathRecoveryStop(QString* errorMessage = nullptr);
    DobotCommandResult pathRecoveryStatus(QString* statusPayload = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult sync(QString* errorMessage = nullptr);
    DobotCommandResult offsetPara(
        double x,
        double y,
        double z,
        double rx,
        double ry,
        double rz,
        QString* errorMessage = nullptr);
    DobotCommandResult runTo(const DobotStartPose& startPose, int accelerationPercent, int velocityPercent, QString* errorMessage = nullptr);
    DobotCommandResult runTo(const DobotPose& pose, int userIndex, int toolIndex, int moveType, int accelerationPercent, int velocityPercent, QString* errorMessage = nullptr);
    DobotCommandResult runTo(const DobotJointAngles& joints, int moveType, int accelerationPercent, int velocityPercent, QString* errorMessage = nullptr);

    DobotCommandResult speedFactor(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult user(int index, QString* errorMessage = nullptr);
    DobotCommandResult tool(int index, QString* errorMessage = nullptr);
    DobotCommandResult accJ(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult accL(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult speedJ(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult speedL(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult velJ(int ratio, QString* errorMessage = nullptr);
    DobotCommandResult velL(int ratio, QString* errorMessage = nullptr);

    DobotCommandResult robotMode(int* mode = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getErrorId(QString* alarmPayload = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getPose(DobotPose* pose = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getPose(int userIndex, int toolIndex, DobotPose* pose = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getAngle(DobotJointAngles* joints = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult getCurrentCommandId(qint64* commandId = nullptr, QString* errorMessage = nullptr);

    DobotCommandResult digitalInput(int index, bool* value = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult digitalOutput(int index, bool* value = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult setDigitalOutput(int index, bool on, int durationMs = -1, QString* errorMessage = nullptr);
    DobotCommandResult setDigitalOutputInstant(int index, bool on, QString* errorMessage = nullptr);
    DobotCommandResult toolDigitalInput(int index, bool* value = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult toolDigitalOutput(int index, bool* value = nullptr, QString* errorMessage = nullptr);
    DobotCommandResult setToolDigitalOutput(int index, bool on, QString* errorMessage = nullptr);
    DobotCommandResult setToolDigitalOutputInstant(int index, bool on, QString* errorMessage = nullptr);

    DobotCommandResult movJ(const DobotPose& pose, const DobotMotionOptions& options = {}, QString* errorMessage = nullptr);
    DobotCommandResult movJ(
        const DobotJointAngles& joints,
        const DobotMotionOptions& options = {},
        QString* errorMessage = nullptr);
    DobotCommandResult movL(const DobotPose& pose, const DobotMotionOptions& options = {}, QString* errorMessage = nullptr);
    DobotCommandResult movL(
        const DobotJointAngles& joints,
        const DobotMotionOptions& options = {},
        QString* errorMessage = nullptr);

private:
    DobotCommandResult send(const QString& commandName, const QStringList& arguments = {}, QString* errorMessage = nullptr);
    DobotCommandResult sendMotion(const QString& commandName, const QStringList& arguments = {}, QString* errorMessage = nullptr);
    DobotCommandResult sendAndReadBool(const QString& commandName, int index, bool* value, QString* errorMessage);
    DobotCommandResult makeLocalError(const QString& message, QString* errorMessage) const;
    QStringList motionArguments(const QString& targetArgument, const DobotMotionOptions& options) const;
    bool validatePercent(const QString& commandName, int value, QString* errorMessage) const;
    bool validateIndex(const QString& commandName, int value, QString* errorMessage) const;
    bool validatePlainArgument(const QString& commandName, const QString& value, QString* errorMessage) const;
    bool setError(const QString& message, QString* errorMessage) const;

    DobotTcpClient m_client;
    DobotTcpClient m_motionClient;
};

}  // namespace panthera::adapters::dobot
