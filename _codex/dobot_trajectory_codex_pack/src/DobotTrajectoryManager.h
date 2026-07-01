#pragma once

#include "DobotSftpClient.h"

#include <QObject>
#include <QStringList>

class QComboBox;

class DobotTrajectoryManager final : public QObject
{
    Q_OBJECT
public:
    explicit DobotTrajectoryManager(QObject* parent = nullptr);

    void setRobotIp(const QString& ip);
    void setSftpConfig(const DobotSftpConfig& config);
    DobotSftpConfig sftpConfig() const;

    // 同步刷新。若担心 UI 卡顿，可在项目中用 QThread/QtConcurrent 包一层调用。
    QStringList refreshTrajectoryFiles(QString* errorMessage = nullptr);

    // 直接刷新到 UI 下拉框；keepCurrent=true 时会尽量保持原选择项。
    bool refreshComboBox(QComboBox* comboBox, QString* errorMessage = nullptr, bool keepCurrent = true);

    // 完全复刻厂家排序：时间格式文件 yyyy-MM-dd-HH-mm-ss 放在普通文件之后，且时间倒序。
    static QStringList sortTrajectoryFilesLikeDobot(QStringList files);

signals:
    void filesRefreshed(const QStringList& files);
    void refreshFailed(const QString& message);

private:
    DobotSftpClient m_sftp;
    QStringList m_lastFiles;
};
