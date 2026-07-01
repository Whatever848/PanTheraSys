#pragma once

#include <QObject>
#include <QString>
#include <functional>

struct DobotStartPathOptions
{
    int isConst = 0;      // 0：按录制原速，可用 multi 缩放；1：匀速复现
    double multi = 1.0;   // isConst=0 时有效，范围 [0.25, 2]
    int sample = 50;      // ms，厂家默认 50
    double freq = 0.2;    // (0,1]，越小越平滑但变形越大
    int user = -1;        // -1 表示不携带 user 参数
    int tool = -1;        // -1 表示不携带 tool 参数
};

class DobotTrajectoryRunner final : public QObject
{
    Q_OBJECT
public:
    using DashboardSendFunc = std::function<QString(const QString& command)>;

    explicit DobotTrajectoryRunner(QObject* parent = nullptr);
    explicit DobotTrajectoryRunner(DashboardSendFunc sendFunc, QObject* parent = nullptr);

    void setDashboardSendFunc(DashboardSendFunc sendFunc);

    static QString buildStartPathCommand(const QString& fileName, const DobotStartPathOptions& options = {});
    static int parseErrorId(const QString& reply, bool* ok = nullptr);
    static int parseRobotModeValue(const QString& reply, bool* ok = nullptr);

    bool startPath(const QString& fileName, const DobotStartPathOptions& options, QString* replyOrError = nullptr);
    bool stop(QString* replyOrError = nullptr);
    int robotMode(QString* replyOrError = nullptr);

signals:
    void commandSent(const QString& command, const QString& reply);
    void commandFailed(const QString& command, const QString& message);

private:
    DashboardSendFunc m_sendFunc;
};
