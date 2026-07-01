#include "adapters/dobot/dobot_trajectory_sftp_client.h"

#include <QByteArray>
#include <QDateTime>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTcpSocket>

#ifdef Q_OS_WIN
#include <winsock2.h>
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>

namespace panthera::adapters::dobot {

namespace {

class Libssh2Runtime final {
public:
    Libssh2Runtime()
        : m_ok(libssh2_init(0) == 0)
    {
    }

    ~Libssh2Runtime()
    {
        if (m_ok) {
            libssh2_exit();
        }
    }

    [[nodiscard]] bool ok() const
    {
        return m_ok;
    }

private:
    bool m_ok {false};
};

Libssh2Runtime& libssh2Runtime()
{
    static Libssh2Runtime runtime;
    return runtime;
}

void setError(const QString& message, QString* errorMessage)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString libssh2SessionError(LIBSSH2_SESSION* session, const QString& fallback)
{
    if (session == nullptr) {
        return fallback;
    }

    char* message = nullptr;
    int messageLength = 0;
    const int code = libssh2_session_last_error(session, &message, &messageLength, 0);
    if (message != nullptr && messageLength > 0) {
        return QStringLiteral("%1 (libssh2 error %2)")
            .arg(QString::fromUtf8(message, messageLength))
            .arg(code);
    }
    return QStringLiteral("%1 (libssh2 error %2)").arg(fallback).arg(code);
}

bool isTimestampTrajectoryName(const QString& fileName, QDateTime* timestamp = nullptr)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(\d{4})-(\d{2})-(\d{2})-(\d{2})-(\d{2})-(\d{2})\.csv$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(fileName.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    if (timestamp != nullptr) {
        const QString isoText = QStringLiteral("%1-%2-%3T%4:%5:%6")
            .arg(match.captured(1), match.captured(2), match.captured(3), match.captured(4), match.captured(5), match.captured(6));
        *timestamp = QDateTime::fromString(isoText, Qt::ISODate);
    }
    return true;
}

void appendUnique(QStringList* values, const QString& value)
{
    const QString normalized = value.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    for (const QString& existing : *values) {
        if (existing.compare(normalized, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    values->push_back(normalized);
}

}  // namespace

QStringList DobotTrajectorySftpClient::sortTrajectoryCsvFiles(const QStringList& fileNames)
{
    QStringList ordinaryFiles;
    QStringList timestampFiles;
    for (const QString& fileName : fileNames) {
        if (isTimestampTrajectoryName(fileName)) {
            appendUnique(&timestampFiles, fileName);
        } else {
            appendUnique(&ordinaryFiles, fileName);
        }
    }

    std::sort(timestampFiles.begin(), timestampFiles.end(), [](const QString& left, const QString& right) {
        QDateTime leftTime;
        QDateTime rightTime;
        isTimestampTrajectoryName(left, &leftTime);
        isTimestampTrajectoryName(right, &rightTime);
        if (leftTime.isValid() && rightTime.isValid() && leftTime != rightTime) {
            return leftTime > rightTime;
        }
        return QString::localeAwareCompare(left, right) < 0;
    });

    ordinaryFiles.append(timestampFiles);
    return ordinaryFiles;
}

QStringList DobotTrajectorySftpClient::listTrajectoryCsvFiles(
    const DobotTrajectorySftpSettings& settings,
    QString* errorMessage)
{
    if (!libssh2Runtime().ok()) {
        setError(QStringLiteral("libssh2 初始化失败"), errorMessage);
        return {};
    }

    const QString host = settings.host.trimmed();
    if (host.isEmpty()) {
        setError(QStringLiteral("SFTP host 不能为空"), errorMessage);
        return {};
    }

    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(host, settings.port);
    if (!socket.waitForConnected(settings.timeoutMs)) {
        setError(
            QStringLiteral("连接 SFTP %1:%2 失败：%3")
                .arg(host)
                .arg(settings.port)
                .arg(socket.errorString()),
            errorMessage);
        return {};
    }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (session == nullptr) {
        setError(QStringLiteral("创建 libssh2 session 失败"), errorMessage);
        return {};
    }
    libssh2_session_set_timeout(session, settings.timeoutMs);

    const auto closeSession = [&]() {
        libssh2_session_disconnect(session, "PanTheraSys trajectory list finished");
        libssh2_session_free(session);
    };

    if (libssh2_session_handshake(session, static_cast<libssh2_socket_t>(socket.socketDescriptor())) != 0) {
        setError(libssh2SessionError(session, QStringLiteral("SFTP 握手失败")), errorMessage);
        closeSession();
        return {};
    }

    const QByteArray username = settings.username.toUtf8();
    const QByteArray password = settings.password.toUtf8();
    if (libssh2_userauth_password(session, username.constData(), password.constData()) != 0) {
        setError(libssh2SessionError(session, QStringLiteral("SFTP 用户名或密码认证失败")), errorMessage);
        closeSession();
        return {};
    }

    LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
    if (sftp == nullptr) {
        setError(libssh2SessionError(session, QStringLiteral("初始化 SFTP 子系统失败")), errorMessage);
        closeSession();
        return {};
    }

    const QByteArray remoteDirectory = settings.remoteDirectory.toUtf8();
    LIBSSH2_SFTP_HANDLE* directory = libssh2_sftp_opendir(sftp, remoteDirectory.constData());
    if (directory == nullptr) {
        const QString message = libssh2SessionError(
            session,
            QStringLiteral("打开远端轨迹目录失败：%1").arg(settings.remoteDirectory));
        setError(message, errorMessage);
        libssh2_sftp_shutdown(sftp);
        closeSession();
        return {};
    }

    QStringList csvFiles;
    char nameBuffer[1024];
    char longEntryBuffer[2048];
    LIBSSH2_SFTP_ATTRIBUTES attributes;
    for (;;) {
        const int readCount = libssh2_sftp_readdir_ex(
            directory,
            nameBuffer,
            sizeof(nameBuffer),
            longEntryBuffer,
            sizeof(longEntryBuffer),
            &attributes);
        if (readCount > 0) {
            const QString fileName = QString::fromUtf8(nameBuffer, readCount).trimmed();
            if (fileName != QStringLiteral(".")
                && fileName != QStringLiteral("..")
                && fileName.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
                appendUnique(&csvFiles, fileName);
            }
            continue;
        }
        if (readCount == 0) {
            break;
        }

        setError(libssh2SessionError(session, QStringLiteral("读取远端轨迹目录失败")), errorMessage);
        libssh2_sftp_closedir(directory);
        libssh2_sftp_shutdown(sftp);
        closeSession();
        return {};
    }

    libssh2_sftp_closedir(directory);
    libssh2_sftp_shutdown(sftp);
    closeSession();

    if (csvFiles.isEmpty()) {
        setError(QStringLiteral("远端轨迹目录中没有 CSV 文件：%1").arg(settings.remoteDirectory), errorMessage);
        return {};
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return sortTrajectoryCsvFiles(csvFiles);
}

}  // namespace panthera::adapters::dobot
