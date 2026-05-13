#include "adapters/dobot/dobot_tcp_client.h"

#include <QElapsedTimer>
#include <QLocale>

#include <cmath>
#include <limits>
#include <utility>

namespace panthera::adapters::dobot {

namespace {

constexpr int kRobotPoseValueCount = 6;

QString stateText(QAbstractSocket::SocketState state)
{
    switch (state) {
    case QAbstractSocket::UnconnectedState:
        return QStringLiteral("unconnected");
    case QAbstractSocket::HostLookupState:
        return QStringLiteral("host lookup");
    case QAbstractSocket::ConnectingState:
        return QStringLiteral("connecting");
    case QAbstractSocket::ConnectedState:
        return QStringLiteral("connected");
    case QAbstractSocket::BoundState:
        return QStringLiteral("bound");
    case QAbstractSocket::ListeningState:
        return QStringLiteral("listening");
    case QAbstractSocket::ClosingState:
        return QStringLiteral("closing");
    }
    return QStringLiteral("unknown");
}

QString boolArgument(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

QStringList poseValues(const DobotPose& pose)
{
    return {
        formatDobotNumber(pose.x),
        formatDobotNumber(pose.y),
        formatDobotNumber(pose.z),
        formatDobotNumber(pose.rx),
        formatDobotNumber(pose.ry),
        formatDobotNumber(pose.rz),
    };
}

QStringList jointValues(const DobotJointAngles& joints)
{
    return {
        formatDobotNumber(joints.j1),
        formatDobotNumber(joints.j2),
        formatDobotNumber(joints.j3),
        formatDobotNumber(joints.j4),
        formatDobotNumber(joints.j5),
        formatDobotNumber(joints.j6),
    };
}

bool payloadToBool(const QString& payload, bool* value)
{
    bool ok = false;
    const int parsed = payload.trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }
    if (value != nullptr) {
        *value = parsed != 0;
    }
    return true;
}

bool payloadToInt(const QString& payload, int* value)
{
    bool ok = false;
    const int parsed = payload.trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }
    if (value != nullptr) {
        *value = parsed;
    }
    return true;
}

bool payloadToInt64(const QString& payload, qint64* value)
{
    bool ok = false;
    const qint64 parsed = payload.trimmed().toLongLong(&ok);
    if (!ok) {
        return false;
    }
    if (value != nullptr) {
        *value = parsed;
    }
    return true;
}

QStringList enableRobotArguments(double loadKg, double centerX, double centerY, double centerZ, bool checkLoad)
{
    QStringList args {
        formatDobotNumber(loadKg),
        formatDobotNumber(centerX),
        formatDobotNumber(centerY),
        formatDobotNumber(centerZ),
    };
    if (checkLoad) {
        args.push_back(QStringLiteral("1"));
    }
    return args;
}

}  // namespace

bool DobotCommandResult::protocolValid() const
{
    return protocolError.isEmpty();
}

bool DobotCommandResult::ok() const
{
    return protocolValid() && errorId == 0;
}

QString formatDobotNumber(double value)
{
    if (!std::isfinite(value)) {
        return QStringLiteral("0");
    }

    QString text = QLocale::c().toString(value, 'f', 6);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    if (text == QStringLiteral("-0")) {
        return QStringLiteral("0");
    }
    return text;
}

QString formatDobotCommand(const QString& name, const QStringList& arguments)
{
    return QStringLiteral("%1(%2)").arg(name.trimmed(), arguments.join(QLatin1Char(',')));
}

QString formatDobotPoseArgument(const DobotPose& pose)
{
    return QStringLiteral("pose={%1}").arg(poseValues(pose).join(QLatin1Char(',')));
}

QString formatDobotJointArgument(const DobotJointAngles& joints)
{
    return QStringLiteral("joint={%1}").arg(jointValues(joints).join(QLatin1Char(',')));
}

QString formatDobotOption(const QString& key, int value)
{
    return QStringLiteral("%1=%2").arg(key.trimmed()).arg(value);
}

QString formatDobotOption(const QString& key, double value)
{
    return QStringLiteral("%1=%2").arg(key.trimmed(), formatDobotNumber(value));
}

DobotCommandResult parseDobotResponse(const QString& response)
{
    DobotCommandResult result;
    result.raw = response.trimmed();

    if (result.raw.isEmpty()) {
        result.protocolError = QStringLiteral("Empty DOBOT response.");
        return result;
    }

    const int firstComma = result.raw.indexOf(QLatin1Char(','));
    if (firstComma <= 0) {
        result.protocolError = QStringLiteral("DOBOT response does not contain an ErrorID separator.");
        return result;
    }

    bool errorOk = false;
    result.errorId = result.raw.left(firstComma).trimmed().toInt(&errorOk);
    if (!errorOk) {
        result.protocolError = QStringLiteral("DOBOT response ErrorID is not an integer.");
        return result;
    }

    const int payloadOpen = result.raw.indexOf(QLatin1Char('{'), firstComma + 1);
    const int payloadClose = result.raw.indexOf(QLatin1Char('}'), payloadOpen + 1);
    if (payloadOpen < 0 || payloadClose < payloadOpen) {
        result.protocolError = QStringLiteral("DOBOT response payload braces are missing.");
        return result;
    }

    result.payload = result.raw.mid(payloadOpen + 1, payloadClose - payloadOpen - 1).trimmed();

    int commandStart = payloadClose + 1;
    if (commandStart < result.raw.size() && result.raw.at(commandStart) == QLatin1Char(',')) {
        ++commandStart;
    }
    result.command = result.raw.mid(commandStart).trimmed();
    if (result.command.endsWith(QLatin1Char(';'))) {
        result.command.chop(1);
    }

    return result;
}

QVector<double> parseDobotDoublePayload(const QString& payload, bool* ok)
{
    QVector<double> values;
    bool allOk = true;

    const auto parts = payload.split(QLatin1Char(','), Qt::SkipEmptyParts);
    values.reserve(parts.size());
    for (const QString& part : parts) {
        bool valueOk = false;
        const double value = QLocale::c().toDouble(part.trimmed(), &valueOk);
        if (!valueOk) {
            allOk = false;
            break;
        }
        values.push_back(value);
    }

    if (ok != nullptr) {
        *ok = allOk;
    }
    return allOk ? values : QVector<double> {};
}

bool parseDobotPosePayload(const QString& payload, DobotPose* pose)
{
    bool ok = false;
    const QVector<double> values = parseDobotDoublePayload(payload, &ok);
    if (!ok || values.size() != kRobotPoseValueCount) {
        return false;
    }
    if (pose != nullptr) {
        *pose = DobotPose {values[0], values[1], values[2], values[3], values[4], values[5]};
    }
    return true;
}

bool parseDobotJointPayload(const QString& payload, DobotJointAngles* joints)
{
    bool ok = false;
    const QVector<double> values = parseDobotDoublePayload(payload, &ok);
    if (!ok || values.size() != kRobotPoseValueCount) {
        return false;
    }
    if (joints != nullptr) {
        *joints = DobotJointAngles {values[0], values[1], values[2], values[3], values[4], values[5]};
    }
    return true;
}

DobotTcpClient::DobotTcpClient(DobotConnectionSettings settings, QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
    , m_socket(this)
{
}

DobotConnectionSettings DobotTcpClient::settings() const
{
    return m_settings;
}

void DobotTcpClient::setSettings(const DobotConnectionSettings& settings)
{
    if (isConnected()) {
        disconnectFromController();
    }
    m_settings = settings;
}

bool DobotTcpClient::connectToController(QString* errorMessage)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    if (m_settings.host.trimmed().isEmpty()) {
        return setError(QStringLiteral("DOBOT host is empty."), errorMessage);
    }

    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }

    m_socket.connectToHost(m_settings.host, m_settings.commandPort);
    if (!m_socket.waitForConnected(m_settings.timeoutMs)) {
        return setError(
            QStringLiteral("Failed to connect to DOBOT %1:%2: %3")
                .arg(m_settings.host)
                .arg(m_settings.commandPort)
                .arg(m_socket.errorString()),
            errorMessage);
    }

    m_lastError.clear();
    return true;
}

void DobotTcpClient::disconnectFromController()
{
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        return;
    }

    m_socket.disconnectFromHost();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.waitForDisconnected(500);
    }
}

bool DobotTcpClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

QString DobotTcpClient::lastError() const
{
    return m_lastError;
}

DobotCommandResult DobotTcpClient::sendCommand(const QString& command, QString* errorMessage)
{
    const QString trimmedCommand = command.trimmed();
    if (trimmedCommand.isEmpty()) {
        const QString message = QStringLiteral("DOBOT command is empty.");
        setError(message, errorMessage);
        return makeProtocolError(message);
    }

    if (!ensureConnected(errorMessage)) {
        return makeProtocolError(m_lastError);
    }

    if (m_socket.bytesAvailable() > 0) {
        m_socket.readAll();
    }

    QByteArray bytes = trimmedCommand.toUtf8();
    bytes.append('\n');

    const qint64 written = m_socket.write(bytes);
    if (written != bytes.size()) {
        const QString message = QStringLiteral("Failed to write complete DOBOT command while socket is %1.")
                                    .arg(stateText(m_socket.state()));
        setError(message, errorMessage);
        return makeProtocolError(message);
    }

    if (!m_socket.waitForBytesWritten(m_settings.timeoutMs)) {
        const QString message = QStringLiteral("Timed out writing DOBOT command: %1").arg(m_socket.errorString());
        setError(message, errorMessage);
        return makeProtocolError(message);
    }

    QByteArray responseBytes;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_settings.timeoutMs) {
        if (responseBytes.contains(';')) {
            break;
        }

        const int remainingMs = qMax(1, m_settings.timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_socket.waitForReadyRead(remainingMs)) {
            break;
        }
        responseBytes.append(m_socket.readAll());
    }

    if (responseBytes.isEmpty()) {
        const QString message = QStringLiteral("Timed out waiting for DOBOT response.");
        setError(message, errorMessage);
        return makeProtocolError(message);
    }

    const QString response = QString::fromUtf8(responseBytes).trimmed();
    DobotCommandResult result = parseDobotResponse(response);
    if (!result.protocolValid()) {
        setError(result.protocolError, errorMessage);
    } else if (!result.ok()) {
        setError(QStringLiteral("DOBOT command returned ErrorID %1.").arg(result.errorId), errorMessage);
    } else {
        m_lastError.clear();
    }
    return result;
}

DobotCommandResult DobotTcpClient::sendCommand(
    const QString& name,
    const QStringList& arguments,
    QString* errorMessage)
{
    return sendCommand(formatDobotCommand(name, arguments), errorMessage);
}

DobotCommandResult DobotTcpClient::makeProtocolError(const QString& message, const QString& raw) const
{
    DobotCommandResult result;
    result.errorId = std::numeric_limits<int>::min();
    result.protocolError = message;
    result.raw = raw;
    return result;
}

bool DobotTcpClient::ensureConnected(QString* errorMessage)
{
    if (isConnected()) {
        return true;
    }
    return connectToController(errorMessage);
}

bool DobotTcpClient::setError(const QString& message, QString* errorMessage)
{
    m_lastError = message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

DobotControllerClient::DobotControllerClient(DobotConnectionSettings settings, QObject* parent)
    : QObject(parent)
    , m_client(std::move(settings), this)
{
}

DobotConnectionSettings DobotControllerClient::settings() const
{
    return m_client.settings();
}

void DobotControllerClient::setSettings(const DobotConnectionSettings& settings)
{
    m_client.setSettings(settings);
}

bool DobotControllerClient::connectToController(QString* errorMessage)
{
    return m_client.connectToController(errorMessage);
}

void DobotControllerClient::disconnectFromController()
{
    m_client.disconnectFromController();
}

bool DobotControllerClient::isConnected() const
{
    return m_client.isConnected();
}

QString DobotControllerClient::lastError() const
{
    return m_client.lastError();
}

DobotCommandResult DobotControllerClient::rawCommand(const QString& command, QString* errorMessage)
{
    return m_client.sendCommand(command, errorMessage);
}

DobotCommandResult DobotControllerClient::requestControl(QString* errorMessage)
{
    return send(QStringLiteral("RequestControl"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::powerOn(QString* errorMessage)
{
    return send(QStringLiteral("PowerOn"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::enableRobot(QString* errorMessage)
{
    return send(QStringLiteral("EnableRobot"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::enableRobot(double loadKg, QString* errorMessage)
{
    return send(QStringLiteral("EnableRobot"), {formatDobotNumber(loadKg)}, errorMessage);
}

DobotCommandResult DobotControllerClient::enableRobot(
    double loadKg,
    double centerX,
    double centerY,
    double centerZ,
    bool checkLoad,
    QString* errorMessage)
{
    return send(
        QStringLiteral("EnableRobot"),
        enableRobotArguments(loadKg, centerX, centerY, centerZ, checkLoad),
        errorMessage);
}

DobotCommandResult DobotControllerClient::disableRobot(QString* errorMessage)
{
    return send(QStringLiteral("DisableRobot"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::clearError(QString* errorMessage)
{
    return send(QStringLiteral("ClearError"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::stop(QString* errorMessage)
{
    return send(QStringLiteral("Stop"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::pause(QString* errorMessage)
{
    return send(QStringLiteral("Pause"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::continueMotion(QString* errorMessage)
{
    return send(QStringLiteral("Continue"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::emergencyStop(bool pressed, QString* errorMessage)
{
    return send(QStringLiteral("EmergencyStop"), {boolArgument(pressed)}, errorMessage);
}

DobotCommandResult DobotControllerClient::speedFactor(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("SpeedFactor"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("SpeedFactor ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("SpeedFactor"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::user(int index, QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("User"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("User index must be non-negative."), errorMessage);
    }
    return send(QStringLiteral("User"), {QString::number(index)}, errorMessage);
}

DobotCommandResult DobotControllerClient::tool(int index, QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("Tool"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("Tool index must be non-negative."), errorMessage);
    }
    return send(QStringLiteral("Tool"), {QString::number(index)}, errorMessage);
}

DobotCommandResult DobotControllerClient::accJ(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("AccJ"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("AccJ ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("AccJ"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::accL(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("AccL"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("AccL ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("AccL"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::velJ(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("VelJ"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("VelJ ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("VelJ"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::velL(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("VelL"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("VelL ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("VelL"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::robotMode(int* mode, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("RobotMode"), {}, errorMessage);
    if (result.ok() && !payloadToInt(result.payload, mode)) {
        setError(QStringLiteral("RobotMode returned a non-integer payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::getPose(DobotPose* pose, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("GetPose"), {}, errorMessage);
    if (result.ok() && !parseDobotPosePayload(result.payload, pose)) {
        setError(QStringLiteral("GetPose returned an invalid pose payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::getPose(
    int userIndex,
    int toolIndex,
    DobotPose* pose,
    QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("GetPose user"), userIndex, errorMessage)
        || !validateIndex(QStringLiteral("GetPose tool"), toolIndex, errorMessage)) {
        return makeLocalError(QStringLiteral("GetPose user/tool indexes must be non-negative."), errorMessage);
    }

    DobotCommandResult result = send(
        QStringLiteral("GetPose"),
        {formatDobotOption(QStringLiteral("user"), userIndex), formatDobotOption(QStringLiteral("tool"), toolIndex)},
        errorMessage);
    if (result.ok() && !parseDobotPosePayload(result.payload, pose)) {
        setError(QStringLiteral("GetPose returned an invalid pose payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::getAngle(DobotJointAngles* joints, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("GetAngle"), {}, errorMessage);
    if (result.ok() && !parseDobotJointPayload(result.payload, joints)) {
        setError(QStringLiteral("GetAngle returned an invalid joint payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::getCurrentCommandId(qint64* commandId, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("GetCurrentCommandID"), {}, errorMessage);
    if (result.ok() && !payloadToInt64(result.payload, commandId)) {
        setError(QStringLiteral("GetCurrentCommandID returned a non-integer payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::digitalInput(int index, bool* value, QString* errorMessage)
{
    return sendAndReadBool(QStringLiteral("DI"), index, value, errorMessage);
}

DobotCommandResult DobotControllerClient::digitalOutput(int index, bool* value, QString* errorMessage)
{
    return sendAndReadBool(QStringLiteral("GetDO"), index, value, errorMessage);
}

DobotCommandResult DobotControllerClient::setDigitalOutput(
    int index,
    bool on,
    int durationMs,
    QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("DO"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("DO index must be non-negative."), errorMessage);
    }
    QStringList args {QString::number(index), boolArgument(on)};
    if (durationMs >= 0) {
        args.push_back(QString::number(durationMs));
    }
    return send(QStringLiteral("DO"), args, errorMessage);
}

DobotCommandResult DobotControllerClient::setDigitalOutputInstant(int index, bool on, QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("DOInstant"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("DOInstant index must be non-negative."), errorMessage);
    }
    return send(QStringLiteral("DOInstant"), {QString::number(index), boolArgument(on)}, errorMessage);
}

DobotCommandResult DobotControllerClient::toolDigitalInput(int index, bool* value, QString* errorMessage)
{
    return sendAndReadBool(QStringLiteral("ToolDI"), index, value, errorMessage);
}

DobotCommandResult DobotControllerClient::toolDigitalOutput(int index, bool* value, QString* errorMessage)
{
    return sendAndReadBool(QStringLiteral("GetToolDO"), index, value, errorMessage);
}

DobotCommandResult DobotControllerClient::setToolDigitalOutput(int index, bool on, QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("ToolDO"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("ToolDO index must be non-negative."), errorMessage);
    }
    return send(QStringLiteral("ToolDO"), {QString::number(index), boolArgument(on)}, errorMessage);
}

DobotCommandResult DobotControllerClient::setToolDigitalOutputInstant(int index, bool on, QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("ToolDOInstant"), index, errorMessage)) {
        return makeLocalError(QStringLiteral("ToolDOInstant index must be non-negative."), errorMessage);
    }
    return send(QStringLiteral("ToolDOInstant"), {QString::number(index), boolArgument(on)}, errorMessage);
}

DobotCommandResult DobotControllerClient::movJ(
    const DobotPose& pose,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return send(QStringLiteral("MovJ"), motionArguments(formatDobotPoseArgument(pose), options, false), errorMessage);
}

DobotCommandResult DobotControllerClient::movJ(
    const DobotJointAngles& joints,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return send(QStringLiteral("MovJ"), motionArguments(formatDobotJointArgument(joints), options, false), errorMessage);
}

DobotCommandResult DobotControllerClient::movL(
    const DobotPose& pose,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return send(QStringLiteral("MovL"), motionArguments(formatDobotPoseArgument(pose), options, true), errorMessage);
}

DobotCommandResult DobotControllerClient::movL(
    const DobotJointAngles& joints,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return send(QStringLiteral("MovL"), motionArguments(formatDobotJointArgument(joints), options, true), errorMessage);
}

DobotCommandResult DobotControllerClient::send(
    const QString& commandName,
    const QStringList& arguments,
    QString* errorMessage)
{
    return m_client.sendCommand(commandName, arguments, errorMessage);
}

DobotCommandResult DobotControllerClient::sendAndReadBool(
    const QString& commandName,
    int index,
    bool* value,
    QString* errorMessage)
{
    if (!validateIndex(commandName, index, errorMessage)) {
        return makeLocalError(QStringLiteral("%1 index must be non-negative.").arg(commandName), errorMessage);
    }

    DobotCommandResult result = send(commandName, {QString::number(index)}, errorMessage);
    if (result.ok() && !payloadToBool(result.payload, value)) {
        setError(QStringLiteral("%1 returned a non-boolean payload.").arg(commandName), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::makeLocalError(const QString& message, QString* errorMessage) const
{
    setError(message, errorMessage);
    DobotCommandResult result;
    result.errorId = std::numeric_limits<int>::min();
    result.protocolError = message;
    return result;
}

QStringList DobotControllerClient::motionArguments(
    const QString& target,
    const DobotMotionOptions& options,
    bool linearMotion) const
{
    QStringList args {target};
    if (options.userIndex >= 0) {
        args.push_back(formatDobotOption(QStringLiteral("user"), options.userIndex));
    }
    if (options.toolIndex >= 0) {
        args.push_back(formatDobotOption(QStringLiteral("tool"), options.toolIndex));
    }
    if (options.accelerationPercent > 0) {
        args.push_back(formatDobotOption(QStringLiteral("a"), options.accelerationPercent));
    }
    if (linearMotion && options.speedMmPerSecond > 0.0) {
        args.push_back(formatDobotOption(QStringLiteral("speed"), options.speedMmPerSecond));
    } else if (options.velocityPercent > 0) {
        args.push_back(formatDobotOption(QStringLiteral("v"), options.velocityPercent));
    }
    if (linearMotion && options.radiusMm >= 0.0) {
        args.push_back(formatDobotOption(QStringLiteral("r"), options.radiusMm));
    } else if (options.smoothPercent >= 0) {
        args.push_back(formatDobotOption(QStringLiteral("cp"), options.smoothPercent));
    }
    return args;
}

bool DobotControllerClient::validatePercent(const QString& commandName, int value, QString* errorMessage) const
{
    if (value < 1 || value > 100) {
        return setError(QStringLiteral("%1 ratio must be in [1, 100].").arg(commandName), errorMessage);
    }
    return true;
}

bool DobotControllerClient::validateIndex(const QString& commandName, int value, QString* errorMessage) const
{
    if (value < 0) {
        return setError(QStringLiteral("%1 index must be non-negative.").arg(commandName), errorMessage);
    }
    return true;
}

bool DobotControllerClient::setError(const QString& message, QString* errorMessage) const
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace panthera::adapters::dobot
