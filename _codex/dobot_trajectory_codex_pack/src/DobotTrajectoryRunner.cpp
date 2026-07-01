#include "DobotTrajectoryRunner.h"

#include <QRegularExpression>
#include <QLocale>

DobotTrajectoryRunner::DobotTrajectoryRunner(QObject* parent)
    : QObject(parent)
{
}

DobotTrajectoryRunner::DobotTrajectoryRunner(DashboardSendFunc sendFunc, QObject* parent)
    : QObject(parent)
    , m_sendFunc(std::move(sendFunc))
{
}

void DobotTrajectoryRunner::setDashboardSendFunc(DashboardSendFunc sendFunc)
{
    m_sendFunc = std::move(sendFunc);
}

QString DobotTrajectoryRunner::buildStartPathCommand(const QString& fileName, const DobotStartPathOptions& options)
{
    const QLocale c = QLocale::c();
    QString cmd = QStringLiteral("StartPath(%1,isConst=%2,multi=%3,sample=%4,freq=%5)")
                      .arg(fileName.trimmed())
                      .arg(options.isConst)
                      .arg(c.toString(options.multi, 'f', 2))
                      .arg(options.sample)
                      .arg(c.toString(options.freq, 'f', 3));

    // 若你们项目需要固定用户/工具坐标系，可打开这两项。格式按 DOBOT TCP/IP 文档：user=index,tool=index。
    if (options.user >= 0 || options.tool >= 0) {
        cmd.chop(1); // remove ')'
        if (options.user >= 0) cmd += QStringLiteral(",user=%1").arg(options.user);
        if (options.tool >= 0) cmd += QStringLiteral(",tool=%1").arg(options.tool);
        cmd += QLatin1Char(')');
    }
    return cmd;
}

int DobotTrajectoryRunner::parseErrorId(const QString& reply, bool* ok)
{
    const int comma = reply.indexOf(QLatin1Char(','));
    bool localOk = false;
    const int value = reply.left(comma >= 0 ? comma : reply.size()).trimmed().toInt(&localOk);
    if (ok) *ok = localOk;
    return localOk ? value : 999999;
}

int DobotTrajectoryRunner::parseRobotModeValue(const QString& reply, bool* ok)
{
    // 示例：0,{7},RobotMode();
    const QRegularExpression re(QStringLiteral(R"(^\s*[-\d]+\s*,\s*\{\s*(-?\d+)\s*\}\s*,\s*RobotMode\s*\()"),
                                QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(reply);
    bool localOk = false;
    int value = 0;
    if (match.hasMatch()) {
        value = match.captured(1).toInt(&localOk);
    }
    if (ok) *ok = localOk;
    return localOk ? value : -1;
}

bool DobotTrajectoryRunner::startPath(const QString& fileName, const DobotStartPathOptions& options, QString* replyOrError)
{
    if (!m_sendFunc) {
        if (replyOrError) *replyOrError = QStringLiteral("未设置 Dashboard 发送函数");
        return false;
    }
    if (fileName.trimmed().isEmpty()) {
        if (replyOrError) *replyOrError = QStringLiteral("轨迹文件名为空");
        return false;
    }

    const QString cmd = buildStartPathCommand(fileName, options);
    const QString reply = m_sendFunc(cmd);
    emit commandSent(cmd, reply);

    bool ok = false;
    const int errorId = parseErrorId(reply, &ok);
    if (!ok || errorId != 0) {
        const QString msg = QStringLiteral("StartPath 失败，reply=%1").arg(reply);
        if (replyOrError) *replyOrError = msg;
        emit commandFailed(cmd, msg);
        return false;
    }
    if (replyOrError) *replyOrError = reply;
    return true;
}

bool DobotTrajectoryRunner::stop(QString* replyOrError)
{
    if (!m_sendFunc) {
        if (replyOrError) *replyOrError = QStringLiteral("未设置 Dashboard 发送函数");
        return false;
    }
    const QString cmd = QStringLiteral("Stop()");
    const QString reply = m_sendFunc(cmd);
    emit commandSent(cmd, reply);

    bool ok = false;
    const int errorId = parseErrorId(reply, &ok);
    if (!ok || errorId != 0) {
        const QString msg = QStringLiteral("Stop 失败，reply=%1").arg(reply);
        if (replyOrError) *replyOrError = msg;
        emit commandFailed(cmd, msg);
        return false;
    }
    if (replyOrError) *replyOrError = reply;
    return true;
}

int DobotTrajectoryRunner::robotMode(QString* replyOrError)
{
    if (!m_sendFunc) {
        if (replyOrError) *replyOrError = QStringLiteral("未设置 Dashboard 发送函数");
        return -1;
    }
    const QString cmd = QStringLiteral("RobotMode()");
    const QString reply = m_sendFunc(cmd);
    emit commandSent(cmd, reply);

    bool errOk = false;
    const int errorId = parseErrorId(reply, &errOk);
    if (!errOk || errorId != 0) {
        if (replyOrError) *replyOrError = QStringLiteral("RobotMode 失败，reply=%1").arg(reply);
        return -1;
    }

    bool modeOk = false;
    const int mode = parseRobotModeValue(reply, &modeOk);
    if (!modeOk) {
        if (replyOrError) *replyOrError = QStringLiteral("RobotMode 返回值解析失败，reply=%1").arg(reply);
        return -1;
    }
    if (replyOrError) *replyOrError = reply;
    return mode;
}
