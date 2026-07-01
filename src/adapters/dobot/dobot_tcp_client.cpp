#include "adapters/dobot/dobot_tcp_client.h"

#include <QElapsedTimer>
#include <QLocale>
#include <QNetworkProxy>

#include <cmath>
#include <limits>
#include <utility>

namespace panthera::adapters::dobot {

namespace {

constexpr int kRobotPoseValueCount = 6;

int matchingBraceIndex(const QString& text, int openIndex)
{
    if (openIndex < 0 || openIndex >= text.size() || text.at(openIndex) != QLatin1Char('{')) {
        return -1;
    }

    int depth = 0;
    for (int index = openIndex; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('{')) {
            ++depth;
        } else if (ch == QLatin1Char('}')) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return -1;
}

QStringList splitTopLevelArguments(const QString& text)
{
    QStringList parts;
    int depth = 0;
    int start = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('{')) {
            ++depth;
        } else if (ch == QLatin1Char('}')) {
            depth = qMax(0, depth - 1);
        } else if (ch == QLatin1Char(',') && depth == 0) {
            parts.push_back(text.mid(start, index - start).trimmed());
            start = index + 1;
        }
    }
    parts.push_back(text.mid(start).trimmed());
    return parts;
}

QString withoutOuterBraces(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('{')) && trimmed.endsWith(QLatin1Char('}'))) {
        return trimmed.mid(1, trimmed.size() - 2).trimmed();
    }
    return trimmed;
}

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

QString errorIdHint(int errorId)
{
    if (errorId <= -30000 && errorId > -40000) {
        const int argumentIndex = qAbs(errorId) - 30000;
        if (argumentIndex > 0) {
            return QStringLiteral("parameter %1 type mismatch; DOBOT V4.6.6 motion commands expect P as pose={...} or joint={...}.")
                .arg(argumentIndex);
        }
    }
    return {};
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
    QString normalized = withoutOuterBraces(payload).trimmed();
    const QStringList parts = splitTopLevelArguments(normalized);
    if (!parts.isEmpty()) {
        normalized = parts.last().trimmed();
    }

    bool ok = false;
    const int parsed = normalized.toInt(&ok);
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

QString formatDobotFixedOption(const QString& key, double value, int precision)
{
    return QStringLiteral("%1=%2").arg(key.trimmed(), QLocale::c().toString(value, 'f', precision));
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
        if (result.raw.contains(QStringLiteral("Control Mode Is Not Tcp"), Qt::CaseInsensitive)) {
            result.protocolError = QStringLiteral("DOBOT controller is not in TCP control mode. Switch the robot to TCP/IP secondary development control mode before enabling.");
        } else {
            result.protocolError = QStringLiteral("DOBOT response does not contain an ErrorID separator: %1").arg(result.raw);
        }
        return result;
    }

    bool errorOk = false;
    result.errorId = result.raw.left(firstComma).trimmed().toInt(&errorOk);
    if (!errorOk) {
        result.protocolError = QStringLiteral("DOBOT response ErrorID is not an integer.");
        return result;
    }

    const int payloadOpen = result.raw.indexOf(QLatin1Char('{'), firstComma + 1);
    const int payloadClose = matchingBraceIndex(result.raw, payloadOpen);
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

bool parseDobotStartPosePayload(const QString& payload, DobotStartPose* startPose)
{
    const QStringList parts = splitTopLevelArguments(payload);
    if (parts.size() < 2) {
        return false;
    }

    bool pointTypeOk = false;
    const int pointType = parts.at(0).toInt(&pointTypeOk);
    if (!pointTypeOk) {
        return false;
    }

    DobotJointAngles joints;
    DobotPose pose;
    bool hasJoints = false;
    bool hasPose = false;
    int userIndex = 0;
    int toolIndex = 0;

    if (pointType == 0) {
        if (parts.size() < 5) {
            return false;
        }
        bool userOk = false;
        bool toolOk = false;
        userIndex = parts.at(2).toInt(&userOk);
        toolIndex = parts.at(3).toInt(&toolOk);
        if (!userOk || !toolOk) {
            return false;
        }
        hasJoints = parseDobotJointPayload(withoutOuterBraces(parts.at(1)), &joints);
        hasPose = parseDobotPosePayload(withoutOuterBraces(parts.at(4)), &pose);
    } else if (pointType == 1) {
        hasJoints = parseDobotJointPayload(withoutOuterBraces(parts.at(1)), &joints);
    } else if (pointType == 2) {
        hasPose = parseDobotPosePayload(withoutOuterBraces(parts.at(1)), &pose);
    } else {
        return false;
    }

    if (!hasJoints && !hasPose) {
        return false;
    }

    if (startPose != nullptr) {
        startPose->pointType = pointType;
        startPose->joints = joints;
        startPose->userIndex = userIndex;
        startPose->toolIndex = toolIndex;
        startPose->pose = pose;
        startPose->hasJoints = hasJoints;
        startPose->hasPose = hasPose;
    }
    return true;
}

DobotTcpClient::DobotTcpClient(DobotConnectionSettings settings, QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
    , m_socket(this)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);
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

    m_socket.setProxy(QNetworkProxy::NoProxy);
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

QTcpSocket* DobotTcpClient::socket()
{
    return &m_socket;
}

const QTcpSocket* DobotTcpClient::socket() const
{
    return &m_socket;
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

    const QByteArray bytes = trimmedCommand.toUtf8();

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
        QString message = QStringLiteral("DOBOT command returned ErrorID %1").arg(result.errorId);
        const QString hint = errorIdHint(result.errorId);
        if (!hint.isEmpty()) {
            message.append(QStringLiteral(" (%1)").arg(hint));
        }
        if (!result.payload.isEmpty()) {
            message.append(QStringLiteral(": %1").arg(result.payload));
        }
        if (!result.command.isEmpty()) {
            message.append(QStringLiteral(" [%1]").arg(result.command));
        }
        message.append(QLatin1Char('.'));
        setError(message, errorMessage);
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
    , m_client(settings, this)
    , m_motionClient([settings]() {
        DobotConnectionSettings motionSettings = settings;
        motionSettings.commandPort = settings.motionPort;
        return motionSettings;
    }(), this)
{
}

DobotConnectionSettings DobotControllerClient::settings() const
{
    return m_client.settings();
}

void DobotControllerClient::setSettings(const DobotConnectionSettings& settings)
{
    m_client.setSettings(settings);
    DobotConnectionSettings motionSettings = settings;
    motionSettings.commandPort = settings.motionPort;
    m_motionClient.setSettings(motionSettings);
}

bool DobotControllerClient::connectToController(QString* errorMessage)
{
    return m_client.connectToController(errorMessage);
}

void DobotControllerClient::disconnectFromController()
{
    m_client.disconnectFromController();
    m_motionClient.disconnectFromController();
}

bool DobotControllerClient::isConnected() const
{
    return m_client.isConnected();
}

QString DobotControllerClient::lastError() const
{
    return !m_client.lastError().isEmpty() ? m_client.lastError() : m_motionClient.lastError();
}

QTcpSocket* DobotControllerClient::dashboardSocket()
{
    return m_client.socket();
}

const QTcpSocket* DobotControllerClient::dashboardSocket() const
{
    return m_client.socket();
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

DobotCommandResult DobotControllerClient::runScript(const QString& projectName, QString* errorMessage)
{
    const QString normalizedProjectName = projectName.trimmed();
    if (!validatePlainArgument(QStringLiteral("RunScript projectName"), normalizedProjectName, errorMessage)) {
        return makeLocalError(QStringLiteral("RunScript projectName is invalid."), errorMessage);
    }

    return send(QStringLiteral("RunScript"), {normalizedProjectName}, errorMessage);
}

DobotCommandResult DobotControllerClient::stopScript(QString* errorMessage)
{
    return send(QStringLiteral("StopScript"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::pauseScript(QString* errorMessage)
{
    return send(QStringLiteral("PauseScript"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::continueScript(QString* errorMessage)
{
    return send(QStringLiteral("ContinueScript"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::getStartPose(
    const QString& traceName,
    DobotStartPose* startPose,
    QString* errorMessage)
{
    return getStartPose(traceName, 1, startPose, errorMessage);
}

DobotCommandResult DobotControllerClient::getStartPose(
    const QString& traceName,
    int pathType,
    DobotStartPose* startPose,
    QString* errorMessage)
{
    const QString normalizedTraceName = traceName.trimmed();
    if (!validatePlainArgument(QStringLiteral("GetStartPose traceName"), normalizedTraceName, errorMessage)) {
        return makeLocalError(QStringLiteral("GetStartPose traceName is invalid."), errorMessage);
    }
    if (pathType != 1 && pathType != 2) {
        return makeLocalError(QStringLiteral("GetStartPose pathType must be 1 or 2."), errorMessage);
    }

    DobotCommandResult result = send(QStringLiteral("GetStartPose"), {normalizedTraceName, QString::number(pathType)}, errorMessage);
    if (result.ok() && !parseDobotStartPosePayload(result.payload, startPose)) {
        setError(QStringLiteral("GetStartPose returned an invalid start pose payload."), errorMessage);
    }
    return result;
}

DobotCommandResult DobotControllerClient::startPath(const QString& traceName, QString* errorMessage)
{
    return startPath(traceName, DobotStartPathOptions {}, errorMessage);
}

DobotCommandResult DobotControllerClient::startPath(
    const QString& traceName,
    const DobotStartPathOptions& options,
    QString* errorMessage)
{
    const QString normalizedTraceName = traceName.trimmed();
    if (!validatePlainArgument(QStringLiteral("StartPath traceName"), normalizedTraceName, errorMessage)) {
        return makeLocalError(QStringLiteral("StartPath traceName is invalid."), errorMessage);
    }
    if (options.isConst != 0 && options.isConst != 1) {
        return makeLocalError(QStringLiteral("StartPath isConst must be 0 or 1."), errorMessage);
    }
    if (!std::isfinite(options.multi) || options.multi < 0.1 || options.multi > 2.0) {
        return makeLocalError(QStringLiteral("StartPath multi must be in [0.1, 2]."), errorMessage);
    }
    if (options.sample < 8 || options.sample > 1000) {
        return makeLocalError(QStringLiteral("StartPath sample must be in [8, 1000]."), errorMessage);
    }
    if (!std::isfinite(options.freq) || options.freq <= 0.0 || options.freq > 1.0) {
        return makeLocalError(QStringLiteral("StartPath freq must be in (0, 1]."), errorMessage);
    }
    if ((options.userIndex >= 0 && !validateIndex(QStringLiteral("StartPath user"), options.userIndex, errorMessage))
        || (options.toolIndex >= 0 && !validateIndex(QStringLiteral("StartPath tool"), options.toolIndex, errorMessage))) {
        return makeLocalError(QStringLiteral("StartPath user/tool indexes are invalid."), errorMessage);
    }

    QStringList arguments {
        normalizedTraceName,
        formatDobotOption(QStringLiteral("isConst"), options.isConst),
        formatDobotFixedOption(QStringLiteral("multi"), options.multi, 2),
        formatDobotOption(QStringLiteral("sample"), options.sample),
        formatDobotFixedOption(QStringLiteral("freq"), options.freq, 3)
    };
    if (options.userIndex >= 0) {
        arguments.push_back(formatDobotOption(QStringLiteral("user"), options.userIndex));
    }
    if (options.toolIndex >= 0) {
        arguments.push_back(formatDobotOption(QStringLiteral("tool"), options.toolIndex));
    }

    return sendMotion(QStringLiteral("StartPath"), arguments, errorMessage);
}

DobotCommandResult DobotControllerClient::pathRecovery(QString* errorMessage)
{
    return send(QStringLiteral("PathRecovery"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::pathRecoveryStop(QString* errorMessage)
{
    return send(QStringLiteral("PathRecoveryStop"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::pathRecoveryStatus(QString* statusPayload, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("PathRecoveryStatus"), {}, errorMessage);
    if (result.ok() && statusPayload != nullptr) {
        *statusPayload = result.payload;
    }
    return result;
}

DobotCommandResult DobotControllerClient::sync(QString* errorMessage)
{
    return sendMotion(QStringLiteral("Sync"), {}, errorMessage);
}

DobotCommandResult DobotControllerClient::offsetPara(
    double x,
    double y,
    double z,
    double rx,
    double ry,
    double rz,
    QString* errorMessage)
{
    return send(
        QStringLiteral("OffsetPara"),
        {
            formatDobotNumber(x),
            formatDobotNumber(y),
            formatDobotNumber(z),
            formatDobotNumber(rx),
            formatDobotNumber(ry),
            formatDobotNumber(rz),
        },
        errorMessage);
}

DobotCommandResult DobotControllerClient::runTo(
    const DobotStartPose& startPose,
    int accelerationPercent,
    int velocityPercent,
    QString* errorMessage)
{
    if (startPose.hasPose) {
        return runTo(
            startPose.pose,
            startPose.userIndex,
            startPose.toolIndex,
            1,
            accelerationPercent,
            velocityPercent,
            errorMessage);
    }

    if (startPose.hasJoints) {
        return runTo(startPose.joints, 0, accelerationPercent, velocityPercent, errorMessage);
    }

    return makeLocalError(QStringLiteral("Start pose does not contain a runnable pose or joint target."), errorMessage);
}

DobotCommandResult DobotControllerClient::runTo(
    const DobotPose& pose,
    int userIndex,
    int toolIndex,
    int moveType,
    int accelerationPercent,
    int velocityPercent,
    QString* errorMessage)
{
    if (!validateIndex(QStringLiteral("RunTo user"), userIndex, errorMessage)
        || !validateIndex(QStringLiteral("RunTo tool"), toolIndex, errorMessage)
        || !validatePercent(QStringLiteral("RunTo acceleration"), accelerationPercent, errorMessage)
        || !validatePercent(QStringLiteral("RunTo velocity"), velocityPercent, errorMessage)) {
        return makeLocalError(QStringLiteral("RunTo pose options are invalid."), errorMessage);
    }
    if (moveType != 0 && moveType != 1) {
        return makeLocalError(QStringLiteral("RunTo moveType must be 0 or 1."), errorMessage);
    }

    return sendMotion(
        QStringLiteral("RunTo"),
        {
            formatDobotPoseArgument(pose),
            formatDobotOption(QStringLiteral("user"), userIndex),
            formatDobotOption(QStringLiteral("tool"), toolIndex),
            formatDobotOption(QStringLiteral("moveType"), moveType),
            formatDobotOption(QStringLiteral("a"), accelerationPercent),
            formatDobotOption(QStringLiteral("v"), velocityPercent),
        },
        errorMessage);
}

DobotCommandResult DobotControllerClient::runTo(
    const DobotJointAngles& joints,
    int moveType,
    int accelerationPercent,
    int velocityPercent,
    QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("RunTo acceleration"), accelerationPercent, errorMessage)
        || !validatePercent(QStringLiteral("RunTo velocity"), velocityPercent, errorMessage)) {
        return makeLocalError(QStringLiteral("RunTo joint options are invalid."), errorMessage);
    }
    if (moveType != 0 && moveType != 1) {
        return makeLocalError(QStringLiteral("RunTo moveType must be 0 or 1."), errorMessage);
    }

    return sendMotion(
        QStringLiteral("RunTo"),
        {
            formatDobotJointArgument(joints),
            formatDobotOption(QStringLiteral("moveType"), moveType),
            formatDobotOption(QStringLiteral("a"), accelerationPercent),
            formatDobotOption(QStringLiteral("v"), velocityPercent),
        },
        errorMessage);
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

DobotCommandResult DobotControllerClient::speedJ(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("SpeedJ"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("SpeedJ ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("SpeedJ"), {QString::number(ratio)}, errorMessage);
}

DobotCommandResult DobotControllerClient::speedL(int ratio, QString* errorMessage)
{
    if (!validatePercent(QStringLiteral("SpeedL"), ratio, errorMessage)) {
        return makeLocalError(QStringLiteral("SpeedL ratio must be in [1, 100]."), errorMessage);
    }
    return send(QStringLiteral("SpeedL"), {QString::number(ratio)}, errorMessage);
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

DobotCommandResult DobotControllerClient::getErrorId(QString* alarmPayload, QString* errorMessage)
{
    DobotCommandResult result = send(QStringLiteral("GetErrorID"), {}, errorMessage);
    if (result.ok() && alarmPayload != nullptr) {
        *alarmPayload = result.payload;
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
    return sendMotion(QStringLiteral("MovJ"), motionArguments(formatDobotPoseArgument(pose), options), errorMessage);
}

DobotCommandResult DobotControllerClient::movJ(
    const DobotJointAngles& joints,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return sendMotion(QStringLiteral("MovJ"), motionArguments(formatDobotJointArgument(joints), options), errorMessage);
}

DobotCommandResult DobotControllerClient::movL(
    const DobotPose& pose,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return sendMotion(QStringLiteral("MovL"), motionArguments(formatDobotPoseArgument(pose), options), errorMessage);
}

DobotCommandResult DobotControllerClient::movL(
    const DobotJointAngles& joints,
    const DobotMotionOptions& options,
    QString* errorMessage)
{
    return sendMotion(QStringLiteral("MovL"), motionArguments(formatDobotJointArgument(joints), options), errorMessage);
}

DobotCommandResult DobotControllerClient::send(
    const QString& commandName,
    const QStringList& arguments,
    QString* errorMessage)
{
    return m_client.sendCommand(commandName, arguments, errorMessage);
}

DobotCommandResult DobotControllerClient::sendMotion(
    const QString& commandName,
    const QStringList& arguments,
    QString* errorMessage)
{
    const DobotConnectionSettings settings = m_client.settings();
    if (settings.motionPort == settings.commandPort) {
        return m_client.sendCommand(commandName, arguments, errorMessage);
    }
    return m_motionClient.sendCommand(commandName, arguments, errorMessage);
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
        const QString message = QStringLiteral("%1 returned a non-boolean payload: %2")
                                    .arg(commandName, result.payload);
        setError(message, errorMessage);
        result.protocolError = message;
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
    const QString& targetArgument,
    const DobotMotionOptions& options) const
{
    QStringList args {targetArgument};
    if (options.userIndex >= 0) {
        args.push_back(formatDobotOption(QStringLiteral("user"), options.userIndex));
    }
    if (options.toolIndex >= 0) {
        args.push_back(formatDobotOption(QStringLiteral("tool"), options.toolIndex));
    }

    if (options.accelerationPercent > 0) {
        args.push_back(formatDobotOption(QStringLiteral("a"), options.accelerationPercent));
    }
    if (options.velocityPercent > 0) {
        args.push_back(formatDobotOption(QStringLiteral("v"), options.velocityPercent));
    }

    if (options.speedMmPerSecond > 0.0) {
        args.push_back(formatDobotOption(QStringLiteral("speed"), options.speedMmPerSecond));
    }
    if (options.radiusMm >= 0.0) {
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

bool DobotControllerClient::validatePlainArgument(
    const QString& commandName,
    const QString& value,
    QString* errorMessage) const
{
    if (value.trimmed().isEmpty()) {
        return setError(QStringLiteral("%1 must not be empty.").arg(commandName), errorMessage);
    }

    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('(')) || value.contains(QLatin1Char(')'))
        || value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) {
        return setError(
            QStringLiteral("%1 must not contain command separators or parentheses.").arg(commandName),
            errorMessage);
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
