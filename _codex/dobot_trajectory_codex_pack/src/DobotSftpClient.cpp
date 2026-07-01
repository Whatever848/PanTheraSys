#include "DobotSftpClient.h"

#include <QFileInfo>
#include <QSet>
#include <QDebug>

#ifdef Q_OS_WIN
#include <winsock2.h>
#endif

namespace {
bool ensureLibssh2Initialized(QString* errorMessage)
{
    static bool initialized = false;
    static int initCode = 0;
    if (!initialized) {
        initCode = libssh2_init(0);
        initialized = true;
    }
    if (initCode != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("libssh2 初始化失败，错误码=%1").arg(initCode);
        }
        return false;
    }
    return true;
}
} // namespace

DobotSftpClient::DobotSftpClient(QObject* parent)
    : QObject(parent)
{
}

DobotSftpClient::~DobotSftpClient()
{
    disconnectSession();
}

void DobotSftpClient::setConfig(const DobotSftpConfig& config)
{
    m_config = config;
}

DobotSftpConfig DobotSftpClient::config() const
{
    return m_config;
}

QStringList DobotSftpClient::listTrajectoryCsvFiles(QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();

    if (!connectSession(errorMessage)) {
        return {};
    }

    QStringList files;
    QSet<QString> seen;

    const QByteArray remoteDir = m_config.trajectoryDir.toUtf8();
    LIBSSH2_SFTP_HANDLE* dirHandle = libssh2_sftp_opendir(m_sftp, remoteDir.constData());
    if (!dirHandle) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("打开控制器轨迹目录失败：%1；目录=%2")
                                .arg(sessionLastError(), m_config.trajectoryDir);
        }
        disconnectSession();
        return {};
    }

    char buffer[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        memset(&attrs, 0, sizeof(attrs));
        const int rc = libssh2_sftp_readdir_ex(dirHandle,
                                               buffer,
                                               sizeof(buffer),
                                               nullptr,
                                               0,
                                               &attrs);
        if (rc > 0) {
            QString name = QString::fromUtf8(buffer, rc).trimmed();
            if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
                continue;
            }
            // 厂家列表最终展示文件名；这里过滤 csv，避免目录或其他临时文件进入下拉框。
            if (name.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive) && !seen.contains(name)) {
                seen.insert(name);
                files.append(name);
            }
            continue;
        }
        if (rc == 0) {
            break; // 目录读取结束
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取控制器轨迹目录失败：%1").arg(sessionLastError());
        }
        break;
    }

    libssh2_sftp_closedir(dirHandle);
    disconnectSession();
    return files;
}

bool DobotSftpClient::connectSession(QString* errorMessage)
{
    disconnectSession();

    if (!ensureLibssh2Initialized(errorMessage)) {
        return false;
    }

    m_socket = new QTcpSocket();
    m_socket->connectToHost(m_config.host, m_config.port);
    if (!m_socket->waitForConnected(m_config.connectTimeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SFTP TCP 连接失败：%1:%2，%3")
                                .arg(m_config.host)
                                .arg(m_config.port)
                                .arg(m_socket->errorString());
        }
        disconnectSession();
        return false;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        if (errorMessage) *errorMessage = QStringLiteral("创建 libssh2 session 失败");
        disconnectSession();
        return false;
    }
    libssh2_session_set_blocking(m_session, 1);
    libssh2_session_set_timeout(m_session, m_config.operationTimeoutMs);

    const auto socketDescriptor = m_socket->socketDescriptor();
    if (socketDescriptor == -1) {
        if (errorMessage) *errorMessage = QStringLiteral("无法获取 TCP socketDescriptor");
        disconnectSession();
        return false;
    }

    if (libssh2_session_handshake(m_session, static_cast<libssh2_socket_t>(socketDescriptor)) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SSH 握手失败：%1").arg(sessionLastError());
        }
        disconnectSession();
        return false;
    }

    const QByteArray username = m_config.username.toUtf8();
    const QByteArray password = m_config.password.toUtf8();
    if (libssh2_userauth_password(m_session, username.constData(), password.constData()) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SSH 用户名或密码认证失败：%1").arg(sessionLastError());
        }
        disconnectSession();
        return false;
    }

    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("初始化 SFTP 失败：%1").arg(sessionLastError());
        }
        disconnectSession();
        return false;
    }

    return true;
}

void DobotSftpClient::disconnectSession()
{
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal Shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_socket) {
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->disconnectFromHost();
            m_socket->waitForDisconnected(500);
        }
        delete m_socket;
        m_socket = nullptr;
    }
}

QString DobotSftpClient::sessionLastError() const
{
    if (!m_session) return QStringLiteral("无 session 错误信息");
    char* error = nullptr;
    int len = 0;
    const int code = libssh2_session_last_error(m_session, &error, &len, 0);
    if (error && len > 0) {
        return QStringLiteral("code=%1, %2").arg(code).arg(QString::fromUtf8(error, len));
    }
    return QStringLiteral("code=%1").arg(code);
}
