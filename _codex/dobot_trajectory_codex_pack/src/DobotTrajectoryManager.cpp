#include "DobotTrajectoryManager.h"

#include <QComboBox>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>

DobotTrajectoryManager::DobotTrajectoryManager(QObject* parent)
    : QObject(parent)
    , m_sftp(this)
{
}

void DobotTrajectoryManager::setRobotIp(const QString& ip)
{
    auto cfg = m_sftp.config();
    cfg.host = ip.trimmed();
    m_sftp.setConfig(cfg);
}

void DobotTrajectoryManager::setSftpConfig(const DobotSftpConfig& config)
{
    m_sftp.setConfig(config);
}

DobotSftpConfig DobotTrajectoryManager::sftpConfig() const
{
    return m_sftp.config();
}

QStringList DobotTrajectoryManager::refreshTrajectoryFiles(QString* errorMessage)
{
    QString err;
    QStringList files = m_sftp.listTrajectoryCsvFiles(&err);
    if (!err.isEmpty()) {
        if (errorMessage) *errorMessage = err;
        emit refreshFailed(err);
        return {};
    }

    files = sortTrajectoryFilesLikeDobot(files);
    m_lastFiles = files;
    emit filesRefreshed(files);
    return files;
}

bool DobotTrajectoryManager::refreshComboBox(QComboBox* comboBox, QString* errorMessage, bool keepCurrent)
{
    if (!comboBox) {
        if (errorMessage) *errorMessage = QStringLiteral("轨迹下拉框指针为空");
        return false;
    }

    const QString oldText = comboBox->currentText();
    QString err;
    const QStringList files = refreshTrajectoryFiles(&err);
    if (!err.isEmpty()) {
        if (errorMessage) *errorMessage = err;
        return false;
    }

    comboBox->blockSignals(true);
    comboBox->clear();
    comboBox->addItems(files);
    if (keepCurrent && !oldText.isEmpty()) {
        const int idx = comboBox->findText(oldText);
        if (idx >= 0) comboBox->setCurrentIndex(idx);
    }
    comboBox->blockSignals(false);
    return true;
}

QStringList DobotTrajectoryManager::sortTrajectoryFilesLikeDobot(QStringList files)
{
    files.erase(std::remove_if(files.begin(), files.end(), [](const QString& file) {
                    return !file.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive);
                }),
                files.end());
    files.removeDuplicates();

    const QRegularExpression timeReg(QStringLiteral(R"(^\d{4}(-\d{2}){5})"));
    QStringList filesWithDate;
    QStringList filesWithoutDate;

    for (const QString& file : files) {
        if (timeReg.match(file).hasMatch()) {
            filesWithDate << file;
        } else {
            filesWithoutDate << file;
        }
    }

    auto fileNameToTime = [](QString fileName) -> QDateTime {
        fileName.remove(QStringLiteral(".csv"), Qt::CaseInsensitive);
        const QStringList parts = fileName.split(QLatin1Char('-'));
        if (parts.size() < 6) return {};
        const QString timeText = QStringLiteral("%1-%2-%3 %4:%5:%6")
                                     .arg(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5]);
        return QDateTime::fromString(timeText, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    };

    std::sort(filesWithDate.begin(), filesWithDate.end(), [&](const QString& a, const QString& b) {
        return fileNameToTime(a) > fileNameToTime(b);
    });

    // 厂家代码没有对普通文件二次排序，保留 SFTP 返回顺序；如项目需要稳定顺序，可取消下一行注释。
    // filesWithoutDate.sort(Qt::CaseInsensitive);
    return filesWithoutDate + filesWithDate;
}
