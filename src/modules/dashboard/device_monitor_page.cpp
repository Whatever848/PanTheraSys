#include "modules/dashboard/device_monitor_page.h"

#include "adapters/anthone/lu926_temperature_protocol.h"

#include <algorithm>
#include <cmath>

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr int kRobotSafeDebugPercent = 10;
constexpr int kRobotZAxisAlignMotionPercent = 10;
constexpr int kRobotZAxisAlignWaitTimeoutMs = 120000;
constexpr int kRobotZAxisAlignPollIntervalMs = 200;
constexpr int kRobotZAxisAlignStableSamples = 3;
constexpr int kRobotSafeWallDefaultPollIntervalMs = 1000;
constexpr int kRobotPhysicalDragButtonDiIndex = 13;
constexpr int kRobotPhysicalDragButtonPollIntervalMs = 80;
constexpr int kRobotPhysicalPowerButtonHoldMs = 2500;
constexpr int kRobotModeEnable = 5;
constexpr int kRobotModeBackdrive = 6;
constexpr int kRobotModeRunning = 7;
constexpr int kRobotModeSingleMove = 8;
constexpr int kRobotModeError = 9;
constexpr int kRobotDefaultTrajectoryPathType = 1;
constexpr int kRobotDefaultUserIndex = 0;
constexpr int kRobotDefaultToolIndex = 0;
constexpr double kRobotDefaultZAxisAlignRx = 180.0;
constexpr double kRobotDefaultZAxisAlignRy = 0.0;
constexpr double kRobotDefaultZAxisAlignRz = 0.0;
constexpr double kRobotDefaultZAxisAlignMaxJointDeltaDeg = 180.0;
constexpr double kRobotZAxisAlignJointToleranceDeg = 1.0;
constexpr int kThreeAxisStepsPerTurn = 3200;
constexpr double kThreeAxisLinearStepsPerCentimeter = 6400.0;
constexpr double kThreeAxisSwingStepsPerDegree = 1777.8;
constexpr int kThreeAxisAxis6MinimumSteps = -19290;
constexpr int kThreeAxisAxis6MaximumSteps = 19062;
constexpr int kThreeAxisAxis7MinimumSteps = 0;
constexpr int kThreeAxisAxis7MaximumSteps = 74461;
constexpr int kThreeAxisAxis8MinimumSteps = 0;
constexpr int kThreeAxisAxis8MaximumSteps = 152314;
constexpr int kThreeAxisLargeMoveConfirmSteps = kThreeAxisStepsPerTurn * 5;
constexpr int kThreeAxisDefaultSpeed = 2400;
constexpr int kThreeAxisRefreshIntervalMs = 1000;
constexpr int kThreeAxisSensorDecelerateToStopAction = 3;
constexpr int kTank2FillPollIntervalMs = 1000;
constexpr int kTemperatureRealtimeIntervalMs = 2000;
constexpr double kTank2FillDefaultTargetCentimeters = 30.0;
constexpr double kTank2FillMaximumTargetCentimeters = 43.0;
constexpr const char* kSharedRs485PortName = "COM3";
constexpr int kSharedRs485BaudRate = 9600;
// UI rows map directly to CAN node ids: row 6 controls node 6, row 7 controls node 7, row 8 controls node 8.
const std::array<int, 3> kThreeAxisNodeIds {6, 7, 8};
constexpr int kWaterTankHighLevelAxisIndex = 0;  // 6号电机上限位
constexpr int kWaterTankLowLevelAxisIndex = 1;   // 7号电机下限位
constexpr int kWaterTankUpperLimitSensorIndex = 3;  // Node 6 S3
constexpr int kWaterTankLowerLimitSensorIndex = 3;  // Node 7 S3
constexpr int kWaterTankLimitActiveDebounceSamples = 3;
const std::array<double, 3> kThreeAxisCurrentAmps {1.0, 1.0, 1.0};
const std::array<const char*, 3> kThreeAxisTitles {"6号", "7号", "8号"};
const std::array<const char*, 3> kThreeAxisNegativeActions {"左摆", "左移", "上移"};
const std::array<const char*, 3> kThreeAxisPositiveActions {"右摆", "右移", "下移"};
const std::array<int, 3> kThreeAxisNegativeButtonDirections {1, -1, -1};
const std::array<int, 3> kThreeAxisPositiveButtonDirections {-1, 1, 1};
const adapters::dobot::DobotPose kRobotDefaultSafeOriginPose {
    568.4855,
    -652.5055,
    753.2592,
    -179.9999,
    0.0,
    -152.9531,
};

QStringList defaultRobotTrajectoryFiles()
{
    return {
        QStringLiteral("从下往上.csv"),
        QStringLiteral("test1.csv"),
        QStringLiteral("open.csv"),
        QStringLiteral("2026-06-04-19-13-15.csv")
    };
}

void setStableColumnWidget(QWidget* widget, int width)
{
    if (widget == nullptr) {
        return;
    }
    widget->setFixedWidth(width);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void setExpandingColumnWidget(QWidget* widget, int minimumWidth)
{
    if (widget == nullptr) {
        return;
    }
    widget->setMinimumWidth(minimumWidth);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void setStableElidedText(QLabel* label, const QString& text)
{
    if (label == nullptr) {
        return;
    }
    const int maximumWidth = label->maximumWidth();
    const int measuredWidth = maximumWidth > 0 && maximumWidth < QWIDGETSIZE_MAX ? maximumWidth : label->width();
    const int availableWidth = std::max(12, measuredWidth - 8);
    label->setToolTip(text);
    label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, availableWidth));
}

QString boolStatus(bool ok, const QString& okText, const QString& badText)
{
    return ok ? okText : badText;
}

QLineEdit* createFixedSerialText(const QString& text, QWidget* parent)
{
    auto* edit = new QLineEdit(text, parent);
    edit->setReadOnly(true);
    edit->setClearButtonEnabled(false);
    edit->setMinimumWidth(110);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return edit;
}

class ScopedBusyFlag final {
public:
    explicit ScopedBusyFlag(bool& flag)
        : m_flag(flag)
    {
        m_flag = true;
    }

    ~ScopedBusyFlag()
    {
        m_flag = false;
    }

    ScopedBusyFlag(const ScopedBusyFlag&) = delete;
    ScopedBusyFlag& operator=(const ScopedBusyFlag&) = delete;

private:
    bool& m_flag;
};

QString secondsText(double seconds)
{
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', 0);
}

QString celsiusText(double value)
{
    return QStringLiteral("%1 °C").arg(value, 0, 'f', 1);
}

QString percentText(double value)
{
    return QStringLiteral("%1 %").arg(value, 0, 'f', 1);
}

QString mlText(double value)
{
    return QStringLiteral("%1 mL").arg(value, 0, 'f', 1);
}

QString mlPerMinuteText(double value)
{
    return QStringLiteral("%1 mL/min").arg(value, 0, 'f', 1);
}

QString waterPumpResponseText(const QByteArray& response)
{
    return response.isEmpty()
        ? QStringLiteral("未回包（已发送命令）")
        : panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response);
}

QString temperatureFailureText(const QString& action, const QString& errorMessage)
{
    const QString normalized = errorMessage.trimmed();
    if (normalized.isEmpty()) {
        return QStringLiteral("%1失败").arg(action);
    }
    if (normalized.contains(QStringLiteral("未连接"))) {
        return QStringLiteral("%1失败：串口未连接").arg(action);
    }
    if (normalized.contains(QStringLiteral("超时"))) {
        return QStringLiteral("%1失败：%2").arg(action, normalized);
    }
    if (normalized.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)) {
        return QStringLiteral("%1失败：CRC错误").arg(action);
    }
    if (normalized.contains(QStringLiteral("设备未原样返回"))) {
        return QStringLiteral("%1失败：设备未原样返回").arg(action);
    }
    return QStringLiteral("%1失败：%2").arg(action, normalized);
}

QString statusPanelStyle(bool alarm)
{
    return alarm
        ? QStringLiteral(
              "QLabel { padding: 14px 16px; border: 2px solid #ff3b3b; border-radius: 8px; "
              "background: #3a1014; color: #ffd7d7; font-weight: 800; }")
        : QStringLiteral(
              "QLabel { padding: 14px 16px; border: 1px solid #1e5d91; border-radius: 8px; "
              "background: #0e2943; color: #ffffff; font-weight: 600; }");
}

bool motorSensorTriggered(const diji::adapters::uim::UimMotorSnapshot& snapshot, int sensorIndex)
{
    if (sensorIndex == 1) {
        return !snapshot.sensor1;
    }
    if (sensorIndex == 2) {
        return !snapshot.sensor2;
    }
    if (sensorIndex == 3) {
        return !snapshot.sensor3;
    }
    return false;
}

bool debouncedWaterTankLimitActive(QObject* owner, const char* propertyName, bool active)
{
    if (owner == nullptr || propertyName == nullptr) {
        return active;
    }

    int activeSamples = owner->property(propertyName).toInt();
    if (!active) {
        owner->setProperty(propertyName, 0);
        return false;
    }

    activeSamples = std::min(activeSamples + 1, kWaterTankLimitActiveDebounceSamples);
    owner->setProperty(propertyName, activeSamples);
    return activeSamples >= kWaterTankLimitActiveDebounceSamples;
}

bool waterPumpStatusNeedsModalAlert(const QString& message)
{
    const QStringList markers {
        QStringLiteral("CRC"),
        QStringLiteral("长度不足"),
        QStringLiteral("长度异常"),
        QStringLiteral("数据不完整"),
        QStringLiteral("粘包"),
        QStringLiteral("Modbus 异常码")
    };
    for (const QString& marker : markers) {
        if (message.contains(marker, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString resolveRuntimePath(const QString& relativePath)
{
    const QString projectDefaultsPath = QStringLiteral("D:/PanSoftware/PanTheraSys/config/defaults.ini");
    if (relativePath == QStringLiteral("config/defaults.ini") && QFileInfo::exists(projectDefaultsPath)) {
        return QDir::cleanPath(projectDefaultsPath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QList<QDir> baseDirectories {
        QDir::current(),
        QDir(appDir)
    };
    const QStringList relativeCandidates {
        relativePath,
        QStringLiteral("../%1").arg(relativePath),
        QStringLiteral("../../%1").arg(relativePath),
        QStringLiteral("../../../%1").arg(relativePath),
        QStringLiteral("../../../../%1").arg(relativePath),
        QStringLiteral("../../../../../%1").arg(relativePath)
    };

    for (const QDir& baseDirectory : baseDirectories) {
        for (const QString& relativeCandidate : relativeCandidates) {
            const QString candidate = baseDirectory.absoluteFilePath(relativeCandidate);
            if (QFileInfo::exists(candidate)) {
                return QDir::cleanPath(candidate);
            }
        }
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath(relativePath));
}

int safePort(int value, int fallback)
{
    return value >= 1 && value <= 65535 ? value : fallback;
}

int safePathType(int value, int fallback)
{
    return value == 1 || value == 2 ? value : fallback;
}

int safeCoordinateIndex(int value, int fallback)
{
    return value >= 0 && value <= 50 ? value : fallback;
}

void appendUniqueString(QStringList* values, const QString& value)
{
    const QString normalizedValue = value.trimmed();
    if (normalizedValue.isEmpty()) {
        return;
    }

    for (const QString& existing : *values) {
        if (existing.compare(normalizedValue, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    values->push_back(normalizedValue);
}

QStringList splitConfiguredList(const QString& rawValue)
{
    QStringList values;
    const QStringList parts = rawValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        appendUniqueString(&values, part);
    }
    return values;
}

bool dobotAlarmPayloadIsClear(const QString& payload)
{
    const QString normalized = payload.trimmed();
    return normalized.isEmpty() || normalized == QStringLiteral("[]");
}

bool isClearableRobotCommandErrorId(int errorId)
{
    return errorId == -2 || errorId == -3;
}

int dobotCommandErrorIdFromText(const QString& text)
{
    const QString marker = QStringLiteral("ErrorID=");
    const int markerIndex = text.indexOf(marker);
    if (markerIndex < 0) {
        return 0;
    }

    const int startIndex = markerIndex + marker.size();
    int endIndex = startIndex;
    if (endIndex < text.size() && (text.at(endIndex) == QLatin1Char('-') || text.at(endIndex) == QLatin1Char('+'))) {
        ++endIndex;
    }
    while (endIndex < text.size() && text.at(endIndex).isDigit()) {
        ++endIndex;
    }

    bool ok = false;
    const int errorId = text.mid(startIndex, endIndex - startIndex).toInt(&ok);
    return ok ? errorId : 0;
}

QStringList configuredRobotTrajectoryFilesFromDefaults()
{
    const QString defaultsIniPath = resolveRuntimePath(QStringLiteral("config/defaults.ini"));
    if (!QFileInfo::exists(defaultsIniPath)) {
        return defaultRobotTrajectoryFiles();
    }

    QSettings settings(defaultsIniPath, QSettings::IniFormat);
    const QString configuredTrajectories = settings.value(QStringLiteral("dobot/preset_trajectories")).toString().trimmed();
    const QStringList trajectoryNames = splitConfiguredList(configuredTrajectories);
    return trajectoryNames.isEmpty() ? defaultRobotTrajectoryFiles() : trajectoryNames;
}

QString poseSummary(const adapters::dobot::DobotPose& pose)
{
    return QStringLiteral("{x=%1,y=%2,z=%3,rx=%4,ry=%5,rz=%6}")
        .arg(adapters::dobot::formatDobotNumber(pose.x))
        .arg(adapters::dobot::formatDobotNumber(pose.y))
        .arg(adapters::dobot::formatDobotNumber(pose.z))
        .arg(adapters::dobot::formatDobotNumber(pose.rx))
        .arg(adapters::dobot::formatDobotNumber(pose.ry))
        .arg(adapters::dobot::formatDobotNumber(pose.rz));
}

QString jointSummary(const adapters::dobot::DobotJointAngles& joints)
{
    return QStringLiteral("{j1=%1,j2=%2,j3=%3,j4=%4,j5=%5,j6=%6}")
        .arg(adapters::dobot::formatDobotNumber(joints.j1))
        .arg(adapters::dobot::formatDobotNumber(joints.j2))
        .arg(adapters::dobot::formatDobotNumber(joints.j3))
        .arg(adapters::dobot::formatDobotNumber(joints.j4))
        .arg(adapters::dobot::formatDobotNumber(joints.j5))
        .arg(adapters::dobot::formatDobotNumber(joints.j6));
}

QVector<double> jointVector(const adapters::dobot::DobotJointAngles& joints)
{
    return {joints.j1, joints.j2, joints.j3, joints.j4, joints.j5, joints.j6};
}

QString jointSummary(const QVector<double>& joints)
{
    QStringList values;
    values.reserve(joints.size());
    for (double joint : joints) {
        values.push_back(adapters::dobot::formatDobotNumber(joint));
    }
    return QStringLiteral("{%1}").arg(values.join(QLatin1Char(',')));
}

QString startPoseSummary(const adapters::dobot::DobotStartPose& startPose)
{
    QStringList fields {
        QStringLiteral("pointType=%1").arg(startPose.pointType),
        QStringLiteral("user=%1").arg(startPose.userIndex),
        QStringLiteral("tool=%1").arg(startPose.toolIndex)
    };
    if (startPose.hasJoints) {
        fields.push_back(QStringLiteral("joints=%1").arg(jointSummary(startPose.joints)));
    }
    if (startPose.hasPose) {
        fields.push_back(QStringLiteral("pose=%1").arg(poseSummary(startPose.pose)));
    }
    return fields.join(QStringLiteral(" | "));
}

QString commandResultSummary(
    const adapters::dobot::DobotCommandResult& result,
    const QString& commandError)
{
    QStringList fields {
        QStringLiteral("ErrorID=%1").arg(result.errorId)
    };
    if (!result.command.isEmpty()) {
        fields.push_back(QStringLiteral("command=%1").arg(result.command));
    }
    if (!result.payload.isEmpty()) {
        fields.push_back(QStringLiteral("payload=%1").arg(result.payload));
    }
    if (!result.protocolError.isEmpty()) {
        fields.push_back(QStringLiteral("protocol=%1").arg(result.protocolError));
    }
    if (!commandError.isEmpty()) {
        fields.push_back(QStringLiteral("error=%1").arg(commandError));
    }
    if (!result.raw.isEmpty()) {
        fields.push_back(QStringLiteral("raw=%1").arg(result.raw));
    }
    return fields.join(QStringLiteral(" | "));
}

}  // namespace

DeviceMonitorPage::DeviceMonitorPage(adapters::SimulationDeviceFacade* simulationDevice, SafetyKernel* safetyKernel, QWidget* parent)
    : QWidget(parent)
    , m_simulationDevice(simulationDevice)
    , m_safetyKernel(safetyKernel)
    , m_robotZAxisAligner(m_robotArmClient.dashboardSocket())
{
    loadRobotArmSettings();
    applyRobotArmSettingsToClient();
    m_robotArmSafetyWallTimer.setInterval(m_robotSafeWallPollIntervalMs);
    connect(&m_robotArmSafetyWallTimer, &QTimer::timeout, this, &DeviceMonitorPage::pollRobotArmSafetyWall);
    m_robotPhysicalDragPollTimer.setTimerType(Qt::PreciseTimer);
    m_robotPhysicalDragPollTimer.setInterval(kRobotPhysicalDragButtonPollIntervalMs);
    connect(&m_robotPhysicalDragPollTimer, &QTimer::timeout, this, &DeviceMonitorPage::pollRobotArmPhysicalDragButton);
    m_tank2FillTimer.setInterval(kTank2FillPollIntervalMs);
    connect(&m_tank2FillTimer, &QTimer::timeout, this, &DeviceMonitorPage::pollTank2FillLevel);
    m_temperatureRealtimeTimer.setInterval(kTemperatureRealtimeIntervalMs);
    connect(&m_temperatureRealtimeTimer, &QTimer::timeout, this, &DeviceMonitorPage::updateRealtimeTemperature);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(16);
    rootLayout->addLayout(topRow, 1);

    auto* statusCard = new QGroupBox(QStringLiteral("设备实时状态"));
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setSpacing(10);

    m_safetyStateLabel = new QLabel(QStringLiteral("安全状态：--"));
    m_interlockLabel = new QLabel(QStringLiteral("联锁信息：--"));
    statusLayout->addWidget(m_safetyStateLabel);
    statusLayout->addWidget(m_interlockLabel);
    statusLayout->addStretch();
    topRow->addWidget(statusCard, 1);

    auto* powerCard = createMetricCard(
        QStringLiteral("电源系统状态"),
        {
            {QStringLiteral("输入电压"), createValueLabel()},
            {QStringLiteral("工作电流"), createValueLabel()},
            {QStringLiteral("实时功率"), createValueLabel()}
        });
    topRow->addWidget(powerCard, 1);

    auto* waterCard = createWaterLoopControlCard();
    topRow->addWidget(waterCard, 1);

    topRow->addWidget(createTemperatureControlCard(), 1);

    auto* bottomGrid = new QGridLayout();
    bottomGrid->setHorizontalSpacing(16);
    bottomGrid->setVerticalSpacing(16);
    rootLayout->addLayout(bottomGrid, 1);

    auto* motionCard = createMetricCard(
        QStringLiteral("运动与换能器状态"),
        {
            {QStringLiteral("位置 X/Y/Z"), createValueLabel()},
            {QStringLiteral("姿态 A/B/C"), createValueLabel()},
            {QStringLiteral("负载"), createValueLabel()},
            {QStringLiteral("精度"), createValueLabel()},
            {QStringLiteral("换能器温度"), createValueLabel()},
            {QStringLiteral("振动频率"), createValueLabel()},
            {QStringLiteral("能量效率"), createValueLabel()}
        });
    bottomGrid->addWidget(motionCard, 0, 0);

    bottomGrid->addWidget(createRobotArmControlCard(), 0, 1);

    bottomGrid->addWidget(createLiquidLevelSensorCard(), 0, 2);
    bottomGrid->addWidget(createThreeAxisMotorControlCard(), 1, 0, 1, 3);
    bottomGrid->setColumnStretch(0, 2);
    bottomGrid->setColumnStretch(1, 1);
    bottomGrid->setColumnStretch(2, 1);
    bottomGrid->setRowStretch(0, 1);

    connect(m_simulationDevice, &adapters::SimulationDeviceFacade::snapshotUpdated, this, &DeviceMonitorPage::updateSnapshot);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &DeviceMonitorPage::updateSafety);

    updateSnapshot(m_simulationDevice->latestSnapshot());
    updateSafety(m_safetyKernel->snapshot());
    refreshWaterPumpSerialPorts();
    refreshWaterPumpUi();
    refreshTemperatureSerialPorts();
    refreshTemperatureUi();
    refreshLiquidLevelSerialPorts();
    refreshLiquidLevelUi();
    refreshRobotArmUi();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::updateSnapshot(const DeviceSnapshot& snapshot)
{
    const auto setValue = [this](const QString& key, const QString& text) {
        if (QLabel* label = m_valueLabels.value(key, nullptr)) {
            label->setText(text);
        }
    };

    setValue(QStringLiteral("输入电压"), QStringLiteral("%1 V").arg(snapshot.inputVoltageVolts, 0, 'f', 1));
    setValue(QStringLiteral("工作电流"), QStringLiteral("%1 A").arg(snapshot.workingCurrentAmps, 0, 'f', 2));
    setValue(QStringLiteral("实时功率"), QStringLiteral("%1 W").arg(snapshot.realtimePowerWatts, 0, 'f', 0));
    setValue(QStringLiteral("水位"), QStringLiteral("%1 %").arg(snapshot.waterLevelPercent, 0, 'f', 0));
    setValue(QStringLiteral("进水温度"), celsiusText(snapshot.inletTemperatureCelsius));
    setValue(QStringLiteral("出水温度"), celsiusText(snapshot.outletTemperatureCelsius));
    setValue(QStringLiteral("实际流速"), QStringLiteral("%1 L/min").arg(snapshot.flowRateLpm, 0, 'f', 1));
    setValue(QStringLiteral("水压"), QStringLiteral("%1 MPa").arg(snapshot.pressureMpa, 0, 'f', 2));
    const auto& pump = snapshot.infusionPump;
    setValue(QStringLiteral("注水泵通信"), boolStatus(pump.connected && pump.dataValid && !pump.dataStale, QStringLiteral("正常"), QStringLiteral("异常")));
    setValue(QStringLiteral("注水泵状态"), toDisplayString(pump.runState));
    setValue(QStringLiteral("运行模式"), toDisplayString(pump.operatingMode));
    setValue(QStringLiteral("循环方式"), toDisplayString(pump.cycleMode));
    setValue(QStringLiteral("方向"), toDisplayString(pump.direction));
    setValue(QStringLiteral("设定转速"), QStringLiteral("%1 rpm").arg(pump.speedRpm, 0, 'f', 1));
    setValue(QStringLiteral("目标流量"), mlPerMinuteText(pump.targetFlowMlPerMin));
    setValue(QStringLiteral("实际流量"), mlPerMinuteText(pump.actualFlowMlPerMin));
    setValue(QStringLiteral("目标注水量"), mlText(pump.targetVolumeMl));
    setValue(QStringLiteral("累计注水量"), mlText(pump.deliveredVolumeMl));
    setValue(QStringLiteral("运行时间"), secondsText(pump.runTimeSeconds));
    setValue(QStringLiteral("停止时间"), secondsText(pump.stopTimeSeconds));
    setValue(QStringLiteral("485地址"), QString::number(pump.modbusAddress));
    setValue(QStringLiteral("通信波特率"), QStringLiteral("%1 bps").arg(pump.baudRate));
    setValue(QStringLiteral("安全判定"), boolStatus(pump.safetyLimitsOk, QStringLiteral("通过"), pump.lastError.isEmpty() ? QStringLiteral("未通过") : pump.lastError));
    const auto& temperature = snapshot.temperatureModule;
    setValue(QStringLiteral("温控通信"), boolStatus(temperature.connected && temperature.dataValid && !temperature.dataStale, QStringLiteral("正常"), QStringLiteral("异常")));
    setValue(QStringLiteral("温控采样周期"), secondsText(temperature.samplePeriodSeconds));
    setValue(QStringLiteral("冷端温度"), celsiusText(temperature.coldJunctionTemperatureCelsius));
    setValue(QStringLiteral("温控485地址"), QString::number(temperature.modbusAddress));
    setValue(QStringLiteral("温控波特率"), QStringLiteral("%1 bps").arg(temperature.baudRate));
    setValue(QStringLiteral("温控安全判定"), boolStatus(temperature.safetyLimitsOk, QStringLiteral("通过"), temperature.lastError.isEmpty() ? QStringLiteral("未通过") : temperature.lastError));
    for (const auto& channel : temperature.channels) {
        const QString currentKey = QStringLiteral("CH%1 当前/设定").arg(channel.channelIndex);
        const QString stateKey = QStringLiteral("CH%1 输出/状态").arg(channel.channelIndex);
        setValue(
            currentKey,
            QStringLiteral("%1：%2 / %3")
                .arg(channel.label.isEmpty() ? QStringLiteral("CH%1").arg(channel.channelIndex) : channel.label,
                     celsiusText(channel.processTemperatureCelsius),
                     celsiusText(channel.setpointTemperatureCelsius)));
        setValue(
            stateKey,
            QStringLiteral("%1 | %2 | %3")
                .arg(percentText(channel.outputPercent), toDisplayString(channel.inputType), channel.statusMessage));
    }
    setValue(QStringLiteral("位置 X/Y/Z"), QStringLiteral("%1 / %2 / %3").arg(snapshot.position.x, 0, 'f', 1).arg(snapshot.position.y, 0, 'f', 1).arg(snapshot.position.z, 0, 'f', 1));
    setValue(QStringLiteral("姿态 A/B/C"), QStringLiteral("%1 / %2 / %3").arg(snapshot.position.a, 0, 'f', 1).arg(snapshot.position.b, 0, 'f', 1).arg(snapshot.position.c, 0, 'f', 1));
    setValue(QStringLiteral("负载"), QStringLiteral("%1 %").arg(snapshot.motorLoadPercent, 0, 'f', 1));
    setValue(QStringLiteral("精度"), QStringLiteral("%1 mm").arg(snapshot.motionAccuracyMm, 0, 'f', 2));
    setValue(QStringLiteral("换能器温度"), celsiusText(snapshot.transducerTemperatureCelsius));
    setValue(QStringLiteral("振动频率"), QStringLiteral("%1 MHz").arg(snapshot.vibrationFrequencyMhz, 0, 'f', 2));
    setValue(QStringLiteral("能量效率"), QStringLiteral("%1 %").arg(snapshot.conversionEfficiencyPercent, 0, 'f', 1));
    setValue(QStringLiteral("亮度"), QStringLiteral("%1").arg(snapshot.imageBrightness, 0, 'f', 0));
    setValue(QStringLiteral("对比度"), QStringLiteral("%1").arg(snapshot.imageContrast, 0, 'f', 0));
    setValue(QStringLiteral("清晰度"), QStringLiteral("%1").arg(snapshot.imageClarity, 0, 'f', 0));
    setValue(QStringLiteral("当前输出功率"), QStringLiteral("%1 W").arg(snapshot.outputPowerWatts, 0, 'f', 0));
}

void DeviceMonitorPage::updateSafety(const SafetySnapshot& snapshot)
{
    m_safetyStateLabel->setText(QStringLiteral("安全状态：%1").arg(toDisplayString(snapshot.state)));
    m_interlockLabel->setText(QStringLiteral("联锁信息：%1").arg(snapshot.message));
}

void DeviceMonitorPage::resetFaults()
{
    for (QCheckBox* toggle : m_faultToggles) {
        toggle->setChecked(false);
    }
    if (m_threeAxisEmergencyStopActive && m_faultToggles.size() >= 5 && m_faultToggles.at(4) != nullptr) {
        m_faultToggles.at(4)->setChecked(true);
        setThreeAxisStatus(QStringLiteral("三电机急停仍处于锁定状态，请使用解除急停"));
    }
}

QLabel* DeviceMonitorPage::createValueLabel()
{
    auto* label = new QLabel(QStringLiteral("--"));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

QWidget* DeviceMonitorPage::createMetricCard(const QString& title, const QVector<QPair<QString, QLabel*>>& metrics)
{
    auto* groupBox = new QGroupBox(title);
    auto* layout = new QFormLayout(groupBox);
    layout->setLabelAlignment(Qt::AlignLeft);

    for (const auto& metric : metrics) {
        layout->addRow(metric.first, metric.second);
        m_valueLabels.insert(metric.first, metric.second);
    }

    return groupBox;
}

QWidget* DeviceMonitorPage::createWaterLoopControlCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("水循环 / 三泵控制"));
    groupBox->setMinimumWidth(0);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(groupBox);
    layout->setSpacing(10);

    auto* serialLayout = new QGridLayout();
    serialLayout->setHorizontalSpacing(8);
    serialLayout->setVerticalSpacing(8);
    serialLayout->setColumnStretch(1, 1);
    m_waterPumpPortCombo = createFixedSerialText(QString::fromLatin1(kSharedRs485PortName), groupBox);
    m_waterPumpRefreshPortsButton = new QPushButton(QStringLiteral("固定"), groupBox);
    m_waterPumpRefreshPortsButton->setEnabled(false);
    m_waterPumpBaudCombo = createFixedSerialText(QString::number(kSharedRs485BaudRate), groupBox);
    m_waterPumpConnectionButton = new QPushButton(QStringLiteral("连接485"), groupBox);

    serialLayout->addWidget(new QLabel(QStringLiteral("串口"), groupBox), 0, 0);
    serialLayout->addWidget(m_waterPumpPortCombo, 0, 1);
    serialLayout->addWidget(m_waterPumpRefreshPortsButton, 0, 2);
    serialLayout->addWidget(new QLabel(QStringLiteral("波特率"), groupBox), 1, 0);
    serialLayout->addWidget(m_waterPumpBaudCombo, 1, 1);
    serialLayout->addWidget(m_waterPumpConnectionButton, 1, 2);
    layout->addLayout(serialLayout);

    auto* flowLayout = new QGridLayout();
    flowLayout->setHorizontalSpacing(8);
    flowLayout->setVerticalSpacing(8);
    flowLayout->setColumnStretch(1, 1);
    m_waterPumpFlowSpin = new QDoubleSpinBox(groupBox);
    m_waterPumpFlowSpin->setRange(
        panthera::adapters::waterpump::WaterPumpModbusClient::kMinimumFlowMlPerMin,
        panthera::adapters::waterpump::WaterPumpModbusClient::kMaximumFlowMlPerMin);
    m_waterPumpFlowSpin->setDecimals(1);
    m_waterPumpFlowSpin->setSingleStep(10.0);
    m_waterPumpFlowSpin->setValue(600.0);
    m_waterPumpFlowSpin->setSuffix(QStringLiteral(" mL/min"));
    m_waterPumpFlowSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto* setLoopFlowButton = new QPushButton(QStringLiteral("设置 02/03 流速"), groupBox);
    flowLayout->addWidget(new QLabel(QStringLiteral("循环流速"), groupBox), 0, 0);
    flowLayout->addWidget(m_waterPumpFlowSpin, 0, 1);
    flowLayout->addWidget(setLoopFlowButton, 0, 2);
    layout->addLayout(flowLayout);
    m_waterPumpCommandWidgets.push_back(setLoopFlowButton);

    auto* tank2FillLayout = new QGridLayout();
    tank2FillLayout->setHorizontalSpacing(8);
    tank2FillLayout->setVerticalSpacing(8);
    tank2FillLayout->setColumnStretch(1, 1);
    m_tank2FillTargetLevelSpin = new QDoubleSpinBox(groupBox);
    m_tank2FillTargetLevelSpin->setRange(0.0, kTank2FillMaximumTargetCentimeters);
    m_tank2FillTargetLevelSpin->setDecimals(1);
    m_tank2FillTargetLevelSpin->setSingleStep(1.0);
    m_tank2FillTargetLevelSpin->setValue(kTank2FillDefaultTargetCentimeters);
    m_tank2FillTargetLevelSpin->setSuffix(QStringLiteral(" cm"));
    m_tank2FillTargetLevelSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tank2FillButton = new QPushButton(QStringLiteral("上水"), groupBox);
    tank2FillLayout->addWidget(new QLabel(QStringLiteral("上水目标"), groupBox), 0, 0);
    tank2FillLayout->addWidget(m_tank2FillTargetLevelSpin, 0, 1);
    tank2FillLayout->addWidget(m_tank2FillButton, 0, 2);
    layout->addLayout(tank2FillLayout);
    m_waterPumpCommandWidgets.push_back(m_tank2FillButton);

    auto* loopButtonsLayout = new QHBoxLayout();
    loopButtonsLayout->setSpacing(8);
    auto* startLoopButton = new QPushButton(QStringLiteral("启动循环"), groupBox);
    auto* stopLoopButton = new QPushButton(QStringLiteral("停止循环"), groupBox);
    loopButtonsLayout->addWidget(startLoopButton, 1);
    loopButtonsLayout->addWidget(stopLoopButton, 1);
    layout->addLayout(loopButtonsLayout);
    m_waterPumpCommandWidgets.push_back(startLoopButton);
    m_waterPumpCommandWidgets.push_back(stopLoopButton);

    auto* pumpLayout = new QGridLayout();
    pumpLayout->setHorizontalSpacing(8);
    pumpLayout->setVerticalSpacing(8);
    pumpLayout->setColumnStretch(0, 1);
    pumpLayout->setColumnStretch(1, 1);
    pumpLayout->setColumnStretch(2, 1);

    for (int index = 0; index < 2; ++index) {
        const int row = index;
        auto* nameLabel = new QLabel(QStringLiteral("%1  %2")
                                         .arg(waterPumpAddressText(index), waterPumpName(index)),
                                     groupBox);
        nameLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; }"));
        auto* startButton = new QPushButton(QStringLiteral("启动"), groupBox);
        auto* stopButton = new QPushButton(QStringLiteral("停止"), groupBox);
        pumpLayout->addWidget(nameLabel, row, 0);
        pumpLayout->addWidget(startButton, row, 1);
        pumpLayout->addWidget(stopButton, row, 2);

        const QVector<QWidget*> commandWidgets {
            startButton,
            stopButton
        };
        for (QWidget* widget : commandWidgets) {
            m_waterPumpCommandWidgets.push_back(widget);
        }

        connect(startButton, &QPushButton::clicked, this, [this, index]() { startWaterPump(index); });
        connect(stopButton, &QPushButton::clicked, this, [this, index]() { stopWaterPump(index); });
    }
    auto* robotPumpLabel = new QLabel(QStringLiteral("RO  第三水泵"), groupBox);
    robotPumpLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; }"));
    auto* robotForwardButton = new QPushButton(QStringLiteral("注水"), groupBox);
    auto* robotReverseButton = new QPushButton(QStringLiteral("出水"), groupBox);
    auto* robotStopButton = new QPushButton(QStringLiteral("停止"), groupBox);
    const int robotRow = 2;
    pumpLayout->addWidget(robotPumpLabel, robotRow, 0);
    pumpLayout->addWidget(robotForwardButton, robotRow, 1);
    pumpLayout->addWidget(robotReverseButton, robotRow, 2);
    pumpLayout->addWidget(robotStopButton, robotRow, 3);
    layout->addLayout(pumpLayout);

    auto* logTitleLabel = new QLabel(QStringLiteral("日志输出"), groupBox);
    logTitleLabel->setStyleSheet(QStringLiteral(
        "QLabel { padding: 2px 4px; background: #020b12; color: #ffffff; font-weight: 500; }"));
    layout->addWidget(logTitleLabel);

    m_waterPumpLogEdit = new QPlainTextEdit(groupBox);
    m_waterPumpLogEdit->setReadOnly(true);
    m_waterPumpLogEdit->setMinimumHeight(240);
    m_waterPumpLogEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_waterPumpLogEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  padding: 14px 16px;"
        "  border: 1px solid #1e5d91;"
        "  border-radius: 8px;"
        "  background: #0e2943;"
        "  color: #ffffff;"
        "  selection-background-color: #1f6ca8;"
        "}"));
    layout->addWidget(m_waterPumpLogEdit, 1);

    connect(m_waterPumpRefreshPortsButton, &QPushButton::clicked, this, &DeviceMonitorPage::refreshWaterPumpSerialPorts);
    connect(m_waterPumpConnectionButton, &QPushButton::clicked, this, &DeviceMonitorPage::toggleWaterPumpConnection);
    connect(setLoopFlowButton, &QPushButton::clicked, this, &DeviceMonitorPage::setWaterLoopFlow);
    connect(m_tank2FillButton, &QPushButton::clicked, this, &DeviceMonitorPage::toggleTank2Fill);
    connect(startLoopButton, &QPushButton::clicked, this, &DeviceMonitorPage::startWaterLoop);
    connect(stopLoopButton, &QPushButton::clicked, this, &DeviceMonitorPage::stopWaterLoop);
    connect(robotForwardButton, &QPushButton::clicked, this, &DeviceMonitorPage::startRobotPumpForward);
    connect(robotReverseButton, &QPushButton::clicked, this, &DeviceMonitorPage::startRobotPumpReverse);
    connect(robotStopButton, &QPushButton::clicked, this, &DeviceMonitorPage::stopRobotPump);

    refreshWaterPumpUi();
    setWaterPumpStatus(QStringLiteral("水循环待连接：02/03 通过 485，第三泵通过机械臂 RO。"), true);
    return groupBox;
}

QWidget* DeviceMonitorPage::createTemperatureControlCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("温度调节模块控制"));
    groupBox->setMinimumWidth(0);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(groupBox);
    layout->setSpacing(10);

    auto* serialLayout = new QGridLayout();
    serialLayout->setHorizontalSpacing(8);
    serialLayout->setVerticalSpacing(8);
    serialLayout->setColumnStretch(1, 1);
    m_temperaturePortCombo = createFixedSerialText(QString::fromLatin1(kSharedRs485PortName), groupBox);
    m_temperatureRefreshPortsButton = new QPushButton(QStringLiteral("固定"), groupBox);
    m_temperatureRefreshPortsButton->setEnabled(false);
    m_temperatureBaudCombo = createFixedSerialText(QString::number(kSharedRs485BaudRate), groupBox);
    m_temperatureConnectionButton = new QPushButton(QStringLiteral("连接485"), groupBox);

    serialLayout->addWidget(new QLabel(QStringLiteral("串口"), groupBox), 0, 0);
    serialLayout->addWidget(m_temperaturePortCombo, 0, 1);
    serialLayout->addWidget(m_temperatureRefreshPortsButton, 0, 2);
    serialLayout->addWidget(new QLabel(QStringLiteral("波特率"), groupBox), 1, 0);
    serialLayout->addWidget(m_temperatureBaudCombo, 1, 1);
    serialLayout->addWidget(m_temperatureConnectionButton, 1, 2);
    layout->addLayout(serialLayout);

    auto* commandLayout = new QGridLayout();
    commandLayout->setHorizontalSpacing(8);
    commandLayout->setVerticalSpacing(8);
    commandLayout->setColumnStretch(1, 1);
    m_temperatureChannelCombo = new QComboBox(groupBox);
    m_temperatureChannelCombo->clear();
    m_temperatureChannelCombo->addItem(QStringLiteral("CH1"), 1);
    m_temperatureChannelCombo->setCurrentIndex(0);
    m_temperatureChannelCombo->setEditable(false);
    m_temperatureChannelCombo->setEnabled(false);
    m_temperatureSetpointSpin = new QDoubleSpinBox(groupBox);
    m_temperatureSetpointSpin->setRange(
        panthera::adapters::anthone::Lu926TemperatureProtocol::kMinimumTemperatureRegister / 10.0,
        panthera::adapters::anthone::Lu926TemperatureProtocol::kMaximumTemperatureRegister / 10.0);
    m_temperatureSetpointSpin->setDecimals(1);
    m_temperatureSetpointSpin->setSingleStep(0.5);
    m_temperatureSetpointSpin->setValue(20.0);
    m_temperatureSetpointSpin->setSuffix(QStringLiteral(" °C"));
    m_temperatureSetpointSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_temperatureCurrentDisplay = new QLineEdit(QStringLiteral("--.- \u2103"), groupBox);
    m_temperatureCurrentDisplay->setReadOnly(true);
    m_temperatureCurrentDisplay->setClearButtonEnabled(false);
    m_temperatureCurrentDisplay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_temperatureCurrentDisplay->setMinimumWidth(110);
    m_temperatureCurrentDisplay->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_temperatureSetButton = new QPushButton(QStringLiteral("设定温度"), groupBox);
    m_temperatureReadButton = new QPushButton(QStringLiteral("读取温度"), groupBox);

    commandLayout->addWidget(new QLabel(QStringLiteral("通道"), groupBox), 0, 0);
    commandLayout->addWidget(m_temperatureChannelCombo, 0, 1);
    commandLayout->addWidget(new QLabel(QStringLiteral("设定温度"), groupBox), 1, 0);
    commandLayout->addWidget(m_temperatureSetpointSpin, 1, 1);
    commandLayout->addWidget(m_temperatureSetButton, 1, 2);
    commandLayout->addWidget(new QLabel(QStringLiteral("当前温度"), groupBox), 2, 0);
    commandLayout->addWidget(m_temperatureCurrentDisplay, 2, 1);
    commandLayout->addWidget(m_temperatureReadButton, 2, 2);
    layout->addLayout(commandLayout);

    m_temperatureResultLabel = new QLabel(QStringLiteral("温控测试待连接：地址 04，固定 CH1，SET1 写设定温度，PV1 读当前温度。"), groupBox);
    m_temperatureResultLabel->setWordWrap(true);
    m_temperatureResultLabel->setMinimumHeight(180);
    m_temperatureResultLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_temperatureResultLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  padding: 14px 16px;"
        "  border: 1px solid #1e5d91;"
        "  border-radius: 8px;"
        "  background: #0e2943;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"));
    layout->addWidget(m_temperatureResultLabel, 1);

    connect(m_temperatureRefreshPortsButton, &QPushButton::clicked, this, &DeviceMonitorPage::refreshTemperatureSerialPorts);
    connect(m_temperatureConnectionButton, &QPushButton::clicked, this, &DeviceMonitorPage::toggleTemperatureConnection);
    connect(m_temperatureSetButton, &QPushButton::clicked, this, &DeviceMonitorPage::setTemperatureSetpoint);
    connect(m_temperatureReadButton, &QPushButton::clicked, this, &DeviceMonitorPage::readTemperatureValue);

    refreshTemperatureUi();
    return groupBox;
}

QWidget* DeviceMonitorPage::createLiquidLevelSensorCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("液位传感器测试"));
    groupBox->setMinimumWidth(0);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(groupBox);
    layout->setSpacing(10);

    auto* serialLayout = new QGridLayout();
    serialLayout->setHorizontalSpacing(8);
    serialLayout->setVerticalSpacing(8);
    serialLayout->setColumnStretch(1, 1);
    m_liquidLevelPortCombo = createFixedSerialText(QString::fromLatin1(kSharedRs485PortName), groupBox);
    m_liquidLevelPortCombo->setMinimumWidth(190);
    m_liquidLevelPortCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_liquidLevelRefreshPortsButton = new QPushButton(QStringLiteral("固定"), groupBox);
    m_liquidLevelRefreshPortsButton->setEnabled(false);
    m_liquidLevelBaudCombo = createFixedSerialText(QString::number(kSharedRs485BaudRate), groupBox);
    m_liquidLevelConnectionButton = new QPushButton(QStringLiteral("连接485"), groupBox);

    serialLayout->addWidget(new QLabel(QStringLiteral("串口"), groupBox), 0, 0);
    serialLayout->addWidget(m_liquidLevelPortCombo, 0, 1);
    serialLayout->addWidget(m_liquidLevelRefreshPortsButton, 0, 2);
    serialLayout->addWidget(new QLabel(QStringLiteral("波特率"), groupBox), 1, 0);
    serialLayout->addWidget(m_liquidLevelBaudCombo, 1, 1);
    serialLayout->addWidget(m_liquidLevelConnectionButton, 1, 2);
    layout->addLayout(serialLayout);

    auto* commandLayout = new QGridLayout();
    commandLayout->setHorizontalSpacing(8);
    commandLayout->setVerticalSpacing(8);
    commandLayout->setColumnStretch(1, 1);
    m_liquidLevelAddressEdit = new QLineEdit(QStringLiteral("01"), groupBox);
    m_liquidLevelAddressEdit->setReadOnly(true);
    m_liquidLevelAddressEdit->setClearButtonEnabled(false);
    m_liquidLevelReadButton = new QPushButton(QStringLiteral("读取液位"), groupBox);
    commandLayout->addWidget(new QLabel(QStringLiteral("通讯地址"), groupBox), 0, 0);
    commandLayout->addWidget(m_liquidLevelAddressEdit, 0, 1);
    commandLayout->addWidget(new QLabel(QStringLiteral("当前液位"), groupBox), 1, 0);
    commandLayout->addWidget(m_liquidLevelReadButton, 1, 2);
    layout->addLayout(commandLayout);

    m_waterTankLimitStatusLabel = new QLabel(QStringLiteral("水箱限位保护：等待三电机限位反馈"), groupBox);
    m_waterTankLimitStatusLabel->setWordWrap(true);
    m_waterTankLimitStatusLabel->setMinimumHeight(72);
    m_waterTankLimitStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_waterTankLimitStatusLabel->setStyleSheet(statusPanelStyle(false));
    layout->addWidget(m_waterTankLimitStatusLabel);

    m_liquidLevelResultLabel = new QLabel(QStringLiteral("液位传感器待连接：地址 01，读取当前液位值。"), groupBox);
    m_liquidLevelResultLabel->setWordWrap(true);
    m_liquidLevelResultLabel->setMinimumHeight(180);
    m_liquidLevelResultLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_liquidLevelResultLabel->setStyleSheet(statusPanelStyle(false));
    layout->addWidget(m_liquidLevelResultLabel, 1);

    connect(m_liquidLevelRefreshPortsButton, &QPushButton::clicked, this, &DeviceMonitorPage::refreshLiquidLevelSerialPorts);
    connect(m_liquidLevelConnectionButton, &QPushButton::clicked, this, &DeviceMonitorPage::toggleLiquidLevelConnection);
    connect(m_liquidLevelReadButton, &QPushButton::clicked, this, &DeviceMonitorPage::readLiquidLevelValue);

    refreshLiquidLevelUi();
    updateWaterTankLimitStatus();
    return groupBox;
}

QWidget* DeviceMonitorPage::createRobotArmControlCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("机械臂控制"));
    groupBox->setMinimumWidth(0);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(groupBox);
    layout->setSpacing(12);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(12);

    m_robotArmHostEdit = new QLineEdit(m_robotArmSettings.host);
    m_robotArmHostEdit->setReadOnly(true);
    m_robotArmHostEdit->setClearButtonEnabled(false);
    formLayout->addRow(QStringLiteral("控制器 IP"), m_robotArmHostEdit);

    const auto createSwitch = [](const QString& offText = QStringLiteral("OFF"), const QString& onText = QStringLiteral("ON")) {
        auto* checkBox = new QCheckBox(offText);
        checkBox->setObjectName(QStringLiteral("robotArmSwitch"));
        checkBox->setCursor(Qt::PointingHandCursor);
        checkBox->setStyleSheet(QStringLiteral(
            "QCheckBox#robotArmSwitch { color: #ffffff; font-weight: 600; spacing: 8px; }"
            "QCheckBox#robotArmSwitch::indicator { width: 58px; height: 30px; border-radius: 15px; background: #d9dee3; }"
            "QCheckBox#robotArmSwitch::indicator:checked { background: #39b97f; }"));
        connect(checkBox, &QCheckBox::toggled, checkBox, [checkBox, offText, onText](bool checked) {
            checkBox->setText(checked ? onText : offText);
        });
        return checkBox;
    };

    m_robotPhysicalPowerButtonSwitch = createSwitch();
    formLayout->addRow(QStringLiteral("实体电源按钮："), m_robotPhysicalPowerButtonSwitch);

    m_robotArmConnectionSwitch = createSwitch();
    formLayout->addRow(QStringLiteral("连接机械臂："), m_robotArmConnectionSwitch);
    m_robotArmEnableSwitch = createSwitch();
    m_robotArmDragSwitch = createSwitch();
    formLayout->addRow(QStringLiteral("使能："), m_robotArmEnableSwitch);
    formLayout->addRow(QStringLiteral("拖拽："), m_robotArmDragSwitch);
    m_robotArmPhysicalDragButtonSwitch = createSwitch();
    formLayout->addRow(QStringLiteral("拖拽(实体按钮)："), m_robotArmPhysicalDragButtonSwitch);

    layout->addLayout(formLayout);

    auto* trajectoryControlLabel = new QLabel(QStringLiteral("轨迹控制"));
    trajectoryControlLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    layout->addWidget(trajectoryControlLabel);

    auto* trajectoryControlLayout = new QGridLayout();
    trajectoryControlLayout->setHorizontalSpacing(10);
    trajectoryControlLayout->setVerticalSpacing(10);
    m_robotArmSafeOriginButton = new QPushButton(QStringLiteral("安全原点"));
    m_robotArmZAxisAlignButton = new QPushButton(QStringLiteral("Z轴对齐"));
    trajectoryControlLayout->addWidget(m_robotArmSafeOriginButton, 0, 0);
    trajectoryControlLayout->addWidget(m_robotArmZAxisAlignButton, 0, 1);
    layout->addLayout(trajectoryControlLayout);

    auto* presetTrajectoryLabel = new QLabel(QStringLiteral("预设轨迹"));
    presetTrajectoryLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    layout->addWidget(presetTrajectoryLabel);

    m_robotArmTrajectoryCombo = new QComboBox();
    for (const QString& traceName : m_robotTrajectoryFiles) {
        m_robotArmTrajectoryCombo->addItem(traceName, traceName);
    }
    m_robotArmRefreshTrajectoriesButton = new QPushButton(QStringLiteral("刷新"));
    auto* trajectorySelectLayout = new QHBoxLayout();
    trajectorySelectLayout->setSpacing(8);
    trajectorySelectLayout->addWidget(m_robotArmTrajectoryCombo, 1);
    trajectorySelectLayout->addWidget(m_robotArmRefreshTrajectoriesButton);
    layout->addLayout(trajectorySelectLayout);

    m_robotArmReplayPresetButton = new QPushButton(QStringLiteral("轨迹复现"));
    layout->addWidget(m_robotArmReplayPresetButton);

    m_robotArmStatusLabel = new QLabel(QStringLiteral("未连接"));
    m_robotArmStatusLabel->setWordWrap(true);
    m_robotArmStatusLabel->setMinimumWidth(0);
    m_robotArmStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_robotArmStatusLabel);

    auto* logLabel = new QLabel(QStringLiteral("日志输出"));
    logLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    layout->addWidget(logLabel);

    m_robotArmLogEdit = new QPlainTextEdit();
    m_robotArmLogEdit->setObjectName(QStringLiteral("robotArmLogView"));
    m_robotArmLogEdit->setReadOnly(true);
    m_robotArmLogEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_robotArmLogEdit->setMinimumWidth(0);
    m_robotArmLogEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_robotArmLogEdit->setMaximumBlockCount(500);
    m_robotArmLogEdit->setMinimumHeight(140);
    layout->addWidget(m_robotArmLogEdit);

    layout->addStretch();

    connect(m_robotArmConnectionSwitch, &QCheckBox::toggled, this, &DeviceMonitorPage::toggleRobotArmConnection);
    connect(m_robotArmEnableSwitch, &QCheckBox::toggled, this, &DeviceMonitorPage::toggleRobotArmEnable);
    connect(m_robotArmDragSwitch, &QCheckBox::toggled, this, &DeviceMonitorPage::toggleRobotArmDrag);
    connect(m_robotArmPhysicalDragButtonSwitch, &QCheckBox::toggled, this, &DeviceMonitorPage::toggleRobotArmPhysicalDragButton);
    connect(m_robotPhysicalPowerButtonSwitch, &QCheckBox::toggled, this, &DeviceMonitorPage::toggleRobotPhysicalPowerButton);
    connect(m_robotArmSafeOriginButton, &QPushButton::clicked, this, &DeviceMonitorPage::moveRobotToSafeOrigin);
    connect(m_robotArmZAxisAlignButton, &QPushButton::clicked, this, &DeviceMonitorPage::alignRobotZAxis);
    connect(m_robotArmRefreshTrajectoriesButton, &QPushButton::clicked, this, &DeviceMonitorPage::refreshRobotTrajectoryFiles);
    connect(m_robotArmReplayPresetButton, &QPushButton::clicked, this, &DeviceMonitorPage::replaySelectedPresetTrajectory);

    refreshRobotArmUi();
    return groupBox;
}

QWidget* DeviceMonitorPage::createThreeAxisMotorControlCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("三电机控制"));
    groupBox->setMinimumWidth(0);
    groupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* layout = new QVBoxLayout(groupBox);
    layout->setSpacing(10);

    auto* sdkLayout = new QGridLayout();
    sdkLayout->setHorizontalSpacing(10);
    sdkLayout->setVerticalSpacing(8);
    sdkLayout->setColumnStretch(1, 1);
    m_threeAxisSdkPathEdit = new QLineEdit(defaultThreeAxisSdkPath(), groupBox);
    m_threeAxisSdkPathEdit->setMinimumWidth(120);
    m_threeAxisSdkPathEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_threeAxisLoadSdkButton = new QPushButton(QStringLiteral("加载"), groupBox);
    m_threeAxisSearchButton = new QPushButton(QStringLiteral("搜索"), groupBox);
    m_threeAxisGatewayButton = new QPushButton(QStringLiteral("打开"), groupBox);
    m_threeAxisDeviceCombo = new QComboBox(groupBox);
    m_threeAxisDeviceCombo->setMinimumWidth(0);
    m_threeAxisDeviceCombo->setMinimumContentsLength(18);
    m_threeAxisDeviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_threeAxisDeviceCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_threeAxisDeviceCombo->addItem(QStringLiteral("未搜索到 USB-CAN 网关"), QVariant());
    sdkLayout->addWidget(new QLabel(QStringLiteral("SDK"), groupBox), 0, 0);
    sdkLayout->addWidget(m_threeAxisSdkPathEdit, 0, 1, 1, 5);
    sdkLayout->addWidget(m_threeAxisLoadSdkButton, 0, 6);
    sdkLayout->addWidget(new QLabel(QStringLiteral("网关"), groupBox), 1, 0);
    sdkLayout->addWidget(m_threeAxisDeviceCombo, 1, 1, 1, 4);
    sdkLayout->addWidget(m_threeAxisSearchButton, 1, 5);
    sdkLayout->addWidget(m_threeAxisGatewayButton, 1, 6);
    layout->addLayout(sdkLayout);

    auto* commandLayout = new QHBoxLayout();
    commandLayout->setSpacing(8);
    m_threeAxisEnableAllButton = new QPushButton(QStringLiteral("全部上电"), groupBox);
    m_threeAxisDisableAllButton = new QPushButton(QStringLiteral("全部断电"), groupBox);
    m_threeAxisEmergencyStopButton = new QPushButton(QStringLiteral("急停"), groupBox);
    m_threeAxisReleaseEmergencyStopButton = new QPushButton(QStringLiteral("解除急停"), groupBox);
    const std::array<QPushButton*, 4> commandButtons {
        m_threeAxisEnableAllButton,
        m_threeAxisDisableAllButton,
        m_threeAxisEmergencyStopButton,
        m_threeAxisReleaseEmergencyStopButton
    };
    for (QPushButton* button : commandButtons) {
        button->setMinimumHeight(44);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    m_threeAxisEmergencyStopButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #b91c1c; color: white; font-weight: 700; }"
        "QPushButton:disabled { background: #7f1d1d; color: #fca5a5; }"));
    commandLayout->addWidget(m_threeAxisEnableAllButton, 1);
    commandLayout->addWidget(m_threeAxisDisableAllButton, 1);
    commandLayout->addWidget(m_threeAxisEmergencyStopButton, 1);
    commandLayout->addWidget(m_threeAxisReleaseEmergencyStopButton, 1);
    layout->addLayout(commandLayout);

    auto* axisLayout = new QGridLayout();
    axisLayout->setHorizontalSpacing(6);
    axisLayout->setVerticalSpacing(6);
    axisLayout->setColumnStretch(1, 0);
    axisLayout->setColumnStretch(2, 1);
    axisLayout->setColumnMinimumWidth(2, 330);
    axisLayout->addWidget(new QLabel(QStringLiteral("电机名称"), groupBox), 0, 0);
    axisLayout->addWidget(new QLabel(QStringLiteral("节点"), groupBox), 0, 1);
    axisLayout->addWidget(new QLabel(QStringLiteral("当前位置（范围）"), groupBox), 0, 2);
    axisLayout->addWidget(new QLabel(QStringLiteral("移动位置"), groupBox), 0, 3);
    axisLayout->addWidget(new QLabel(QStringLiteral("点动距离"), groupBox), 0, 5);
    axisLayout->addWidget(new QLabel(QStringLiteral("点动"), groupBox), 0, 6, 1, 2);

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        auto* axisLabel = new QLabel(threeAxisAxisTitle(index), groupBox);
        setStableColumnWidget(axisLabel, 64);
        m_threeAxisNodeLabels[static_cast<size_t>(index)] = new QLabel(QStringLiteral("未发现"), groupBox);
        setStableColumnWidget(m_threeAxisNodeLabels[static_cast<size_t>(index)], 190);
        m_threeAxisSoftPositionLabels[static_cast<size_t>(index)] = new QLabel(QStringLiteral("未查询"), groupBox);
        m_threeAxisSoftPositionLabels[static_cast<size_t>(index)]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setExpandingColumnWidget(m_threeAxisSoftPositionLabels[static_cast<size_t>(index)], 330);

        auto* targetSpin = new QDoubleSpinBox(groupBox);
        targetSpin->setDecimals(threeAxisDisplayDecimals(index));
        targetSpin->setRange(threeAxisMinimumDisplayUnits(index), threeAxisMaximumDisplayUnits(index));
        targetSpin->setSingleStep(index == 0 ? 0.1 : 0.1);
        targetSpin->setValue(index == 0 ? 1.0 : 0.0);
        targetSpin->setSuffix(QStringLiteral(" %1").arg(threeAxisDisplayUnitText(index)));
        targetSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        setStableColumnWidget(targetSpin, 128);
        targetSpin->setToolTip(QStringLiteral("%1 可输入范围：%2，超出范围将被输入框限制")
                                   .arg(threeAxisAxisTitle(index), threeAxisPositionRangeText(index)));
        targetSpin->setStatusTip(targetSpin->toolTip());
        m_threeAxisTargetPositionSpins[static_cast<size_t>(index)] = targetSpin;

        m_threeAxisMoveToButtons[static_cast<size_t>(index)] = new QPushButton(QStringLiteral("移动"), groupBox);
        setStableColumnWidget(m_threeAxisMoveToButtons[static_cast<size_t>(index)], 62);

        auto* jogDistanceSpin = new QDoubleSpinBox(groupBox);
        jogDistanceSpin->setDecimals(2);
        jogDistanceSpin->setRange(0.01, threeAxisMaximumDisplayUnits(index) - threeAxisMinimumDisplayUnits(index));
        jogDistanceSpin->setSingleStep(index == 0 ? 0.1 : 0.1);
        jogDistanceSpin->setValue(1.0);
        jogDistanceSpin->setSuffix(QStringLiteral(" %1").arg(threeAxisDisplayUnitText(index)));
        jogDistanceSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        setStableColumnWidget(jogDistanceSpin, 98);
        m_threeAxisJogDistanceSpins[static_cast<size_t>(index)] = jogDistanceSpin;

        m_threeAxisNegativeButtons[static_cast<size_t>(index)] = new QPushButton(QString::fromUtf8(kThreeAxisNegativeActions.at(static_cast<size_t>(index))), groupBox);
        m_threeAxisPositiveButtons[static_cast<size_t>(index)] = new QPushButton(QString::fromUtf8(kThreeAxisPositiveActions.at(static_cast<size_t>(index))), groupBox);
        setStableColumnWidget(m_threeAxisNegativeButtons[static_cast<size_t>(index)], 76);
        setStableColumnWidget(m_threeAxisPositiveButtons[static_cast<size_t>(index)], 76);

        const int row = index + 1;
        axisLayout->addWidget(axisLabel, row, 0);
        axisLayout->addWidget(m_threeAxisNodeLabels[static_cast<size_t>(index)], row, 1);
        axisLayout->addWidget(m_threeAxisSoftPositionLabels[static_cast<size_t>(index)], row, 2);
        axisLayout->addWidget(targetSpin, row, 3);
        axisLayout->addWidget(m_threeAxisMoveToButtons[static_cast<size_t>(index)], row, 4);
        axisLayout->addWidget(jogDistanceSpin, row, 5);
        axisLayout->addWidget(m_threeAxisNegativeButtons[static_cast<size_t>(index)], row, 6);
        axisLayout->addWidget(m_threeAxisPositiveButtons[static_cast<size_t>(index)], row, 7);

        connect(m_threeAxisNegativeButtons[static_cast<size_t>(index)], &QPushButton::clicked, this, [this, index]() {
            moveThreeAxisMotor(index, kThreeAxisNegativeButtonDirections.at(static_cast<size_t>(index)));
        });
        connect(m_threeAxisPositiveButtons[static_cast<size_t>(index)], &QPushButton::clicked, this, [this, index]() {
            moveThreeAxisMotor(index, kThreeAxisPositiveButtonDirections.at(static_cast<size_t>(index)));
        });
        connect(m_threeAxisMoveToButtons[static_cast<size_t>(index)], &QPushButton::clicked, this, [this, index]() {
            moveThreeAxisMotorToAbsolute(index);
        });
        connect(targetSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() {
            refreshThreeAxisUi();
        });
        connect(jogDistanceSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() {
            refreshThreeAxisUi();
        });
    }
    layout->addLayout(axisLayout);

    m_threeAxisLogEdit = new QPlainTextEdit(groupBox);
    m_threeAxisLogEdit->setReadOnly(true);
    m_threeAxisLogEdit->setMinimumWidth(0);
    m_threeAxisLogEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_threeAxisLogEdit->setMaximumHeight(78);
    m_threeAxisLogEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_threeAxisLogEdit);

    connect(m_threeAxisLoadSdkButton, &QPushButton::clicked, this, &DeviceMonitorPage::loadThreeAxisSdk);
    connect(m_threeAxisSearchButton, &QPushButton::clicked, this, &DeviceMonitorPage::searchThreeAxisGateways);
    connect(m_threeAxisGatewayButton, &QPushButton::clicked, this, &DeviceMonitorPage::toggleThreeAxisGateway);
    connect(m_threeAxisEnableAllButton, &QPushButton::clicked, this, &DeviceMonitorPage::enableAllThreeAxisMotors);
    connect(m_threeAxisDisableAllButton, &QPushButton::clicked, this, &DeviceMonitorPage::disableAllThreeAxisMotors);
    connect(m_threeAxisEmergencyStopButton, &QPushButton::clicked, this, &DeviceMonitorPage::emergencyStopThreeAxisMotors);
    connect(m_threeAxisReleaseEmergencyStopButton, &QPushButton::clicked, this, &DeviceMonitorPage::releaseThreeAxisEmergencyStop);
    connect(&m_threeAxisGateway, &diji::adapters::uim::UimMotorGateway::errorOccurred, this, [this](const QString& message) {
        setThreeAxisStatus(message);
    });
    connect(&m_threeAxisGateway, &diji::adapters::uim::UimMotorGateway::nodesChanged, this, [this]() {
        m_threeAxisNodes = m_threeAxisGateway.nodes();
        updateThreeAxisNodeStatus();
    });
    connect(&m_threeAxisGateway, &diji::adapters::uim::UimMotorGateway::snapshotChanged, this, &DeviceMonitorPage::updateThreeAxisSnapshot);
    connect(&m_threeAxisRefreshTimer, &QTimer::timeout, this, &DeviceMonitorPage::refreshThreeAxisMotors);
    m_threeAxisRefreshTimer.setInterval(kThreeAxisRefreshIntervalMs);
    m_threeAxisRefreshTimer.start();

    refreshThreeAxisUi();
    return groupBox;
}

void DeviceMonitorPage::loadRobotArmSettings()
{
    m_robotArmSettings = panthera::adapters::dobot::DobotConnectionSettings {};
    m_robotArmSettings.host = QStringLiteral("192.168.5.1");
    m_robotArmSettings.commandPort = 29999;
    m_robotArmSettings.motionPort = 29999;
    m_robotArmSettings.timeoutMs = 3000;
    m_robotSafeOriginPose = kRobotDefaultSafeOriginPose;
    m_robotSafeOriginUserIndex = kRobotDefaultUserIndex;
    m_robotSafeOriginToolIndex = kRobotDefaultToolIndex;
    m_robotTrajectoryFiles = defaultRobotTrajectoryFiles();
    m_robotConfiguredTrajectoryFiles = m_robotTrajectoryFiles;
    m_robotTrajectorySftpSettings = panthera::adapters::dobot::DobotTrajectorySftpSettings {};
    m_robotTrajectorySftpSettings.host = m_robotArmSettings.host;
    m_robotTrajectoryPathType = kRobotDefaultTrajectoryPathType;
    m_robotStartPathOptions = panthera::adapters::dobot::DobotStartPathOptions {};
    m_robotZAxisAlignUserIndex = kRobotDefaultUserIndex;
    m_robotZAxisAlignToolIndex = kRobotDefaultToolIndex;
    m_robotZAxisAlignRx = kRobotDefaultZAxisAlignRx;
    m_robotZAxisAlignRy = kRobotDefaultZAxisAlignRy;
    m_robotZAxisAlignRz = kRobotDefaultZAxisAlignRz;
    m_robotZAxisAlignKeepCurrentRz = true;
    m_robotZAxisAlignMaxJointDeltaDeg = kRobotDefaultZAxisAlignMaxJointDeltaDeg;
    m_robotSafeSpeedPercent = kRobotSafeDebugPercent;
    m_robotSafeAccelerationPercent = 1;
    m_robotSafeWallName = QStringLiteral("testSafe");
    m_robotSafeWallIndex = 1;
    m_robotSafeWallMonitorEnabled = true;
    m_robotSafeWallPollIntervalMs = kRobotSafeWallDefaultPollIntervalMs;
    m_robotPayloadApplyBeforeEnable = false;
    m_robotPayloadOptions = panthera::adapters::dobot::DobotPayloadPresetOptions {};

    const QString defaultsIniPath = resolveRuntimePath(QStringLiteral("config/defaults.ini"));
    if (!QFileInfo::exists(defaultsIniPath)) {
        return;
    }

    QSettings settings(defaultsIniPath, QSettings::IniFormat);
    const QString configuredHost = settings.value(QStringLiteral("dobot/host"), m_robotArmSettings.host).toString().trimmed();
    if (!configuredHost.isEmpty()) {
        m_robotArmSettings.host = configuredHost;
    }
    m_robotArmSettings.commandPort = static_cast<quint16>(
        safePort(settings.value(QStringLiteral("dobot/dashboard_port"), static_cast<int>(m_robotArmSettings.commandPort)).toInt(), m_robotArmSettings.commandPort));
    m_robotArmSettings.motionPort = static_cast<quint16>(
        safePort(settings.value(QStringLiteral("dobot/motion_port"), static_cast<int>(m_robotArmSettings.motionPort)).toInt(), m_robotArmSettings.motionPort));
    m_robotArmSettings.timeoutMs = qBound(1000, settings.value(QStringLiteral("dobot/timeout_ms"), m_robotArmSettings.timeoutMs).toInt(), 10000);

    m_robotSafeOriginPose.x = settings.value(QStringLiteral("dobot/safe_origin_x"), m_robotSafeOriginPose.x).toDouble();
    m_robotSafeOriginPose.y = settings.value(QStringLiteral("dobot/safe_origin_y"), m_robotSafeOriginPose.y).toDouble();
    m_robotSafeOriginPose.z = settings.value(QStringLiteral("dobot/safe_origin_z"), m_robotSafeOriginPose.z).toDouble();
    m_robotSafeOriginPose.rx = settings.value(QStringLiteral("dobot/safe_origin_rx"), m_robotSafeOriginPose.rx).toDouble();
    m_robotSafeOriginPose.ry = settings.value(QStringLiteral("dobot/safe_origin_ry"), m_robotSafeOriginPose.ry).toDouble();
    m_robotSafeOriginPose.rz = settings.value(QStringLiteral("dobot/safe_origin_rz"), m_robotSafeOriginPose.rz).toDouble();
    m_robotSafeOriginUserIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/safe_origin_user"), m_robotSafeOriginUserIndex).toInt(),
        m_robotSafeOriginUserIndex);
    m_robotSafeOriginToolIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/safe_origin_tool"), m_robotSafeOriginToolIndex).toInt(),
        m_robotSafeOriginToolIndex);

    const QString configuredTrajectories = settings.value(QStringLiteral("dobot/preset_trajectories")).toString().trimmed();
    if (!configuredTrajectories.isEmpty()) {
        const QStringList trajectoryNames = splitConfiguredList(configuredTrajectories);
        if (!trajectoryNames.isEmpty()) {
            m_robotTrajectoryFiles = trajectoryNames;
            m_robotConfiguredTrajectoryFiles = trajectoryNames;
        }
    }
    m_robotTrajectorySftpSettings.host = m_robotArmSettings.host;
    m_robotTrajectorySftpSettings.port = static_cast<quint16>(
        safePort(settings.value(QStringLiteral("dobot/trajectory_sftp_port"), static_cast<int>(m_robotTrajectorySftpSettings.port)).toInt(), m_robotTrajectorySftpSettings.port));
    const QString trajectorySftpUser = settings.value(QStringLiteral("dobot/trajectory_sftp_user"), m_robotTrajectorySftpSettings.username).toString().trimmed();
    if (!trajectorySftpUser.isEmpty()) {
        m_robotTrajectorySftpSettings.username = trajectorySftpUser;
    }
    const QString trajectorySftpPassword = settings.value(QStringLiteral("dobot/trajectory_sftp_password"), m_robotTrajectorySftpSettings.password).toString();
    if (!trajectorySftpPassword.isEmpty()) {
        m_robotTrajectorySftpSettings.password = trajectorySftpPassword;
    }
    const QString trajectorySftpRemoteDir =
        settings.value(QStringLiteral("dobot/trajectory_sftp_remote_dir"), m_robotTrajectorySftpSettings.remoteDirectory).toString().trimmed();
    if (!trajectorySftpRemoteDir.isEmpty()) {
        m_robotTrajectorySftpSettings.remoteDirectory = trajectorySftpRemoteDir;
    }
    m_robotTrajectoryPathType = safePathType(
        settings.value(QStringLiteral("dobot/trajectory_path_type"), m_robotTrajectoryPathType).toInt(),
        m_robotTrajectoryPathType);
    m_robotStartPathOptions.isConst =
        settings.value(QStringLiteral("dobot/trajectory_is_const"), m_robotStartPathOptions.isConst != 0).toBool() ? 1 : 0;
    m_robotStartPathOptions.multi = qBound(
        0.1,
        settings.value(QStringLiteral("dobot/trajectory_multi"), m_robotStartPathOptions.multi).toDouble(),
        2.0);
    m_robotStartPathOptions.sample = qBound(
        8,
        settings.value(QStringLiteral("dobot/trajectory_sample"), m_robotStartPathOptions.sample).toInt(),
        1000);
    m_robotStartPathOptions.freq = qBound(
        0.001,
        settings.value(QStringLiteral("dobot/trajectory_freq"), m_robotStartPathOptions.freq).toDouble(),
        1.0);
    m_robotStartPathOptions.userIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/trajectory_user"), m_robotStartPathOptions.userIndex).toInt(),
        m_robotStartPathOptions.userIndex);
    m_robotStartPathOptions.toolIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/trajectory_tool"), m_robotStartPathOptions.toolIndex).toInt(),
        m_robotStartPathOptions.toolIndex);
    m_robotZAxisAlignUserIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/z_axis_align_user"), m_robotZAxisAlignUserIndex).toInt(),
        m_robotZAxisAlignUserIndex);
    m_robotZAxisAlignToolIndex = safeCoordinateIndex(
        settings.value(QStringLiteral("dobot/z_axis_align_tool"), m_robotZAxisAlignToolIndex).toInt(),
        m_robotZAxisAlignToolIndex);
    m_robotZAxisAlignRx = settings.value(QStringLiteral("dobot/z_axis_align_rx"), m_robotZAxisAlignRx).toDouble();
    m_robotZAxisAlignRy = settings.value(QStringLiteral("dobot/z_axis_align_ry"), m_robotZAxisAlignRy).toDouble();
    m_robotZAxisAlignRz = settings.value(QStringLiteral("dobot/z_axis_align_rz"), m_robotZAxisAlignRz).toDouble();
    m_robotZAxisAlignKeepCurrentRz = settings.value(QStringLiteral("dobot/z_axis_align_keep_current_rz"), m_robotZAxisAlignKeepCurrentRz).toBool();
    m_robotZAxisAlignMaxJointDeltaDeg = qMax(
        0.0,
        settings.value(QStringLiteral("dobot/z_axis_align_max_joint_delta_deg"), m_robotZAxisAlignMaxJointDeltaDeg).toDouble());

    m_robotSafeSpeedPercent = qBound(
        1,
        settings.value(QStringLiteral("dobot/safety_speed_percent"), kRobotSafeDebugPercent).toInt(),
        kRobotSafeDebugPercent);
    m_robotSafeAccelerationPercent = qBound(
        1,
        settings.value(QStringLiteral("dobot/safety_acceleration_percent"), 1).toInt(),
        kRobotSafeDebugPercent);
    const QString configuredSafeWallName =
        settings.value(QStringLiteral("dobot/safe_wall_name"), m_robotSafeWallName).toString().trimmed();
    if (!configuredSafeWallName.isEmpty()) {
        m_robotSafeWallName = configuredSafeWallName;
    }
    m_robotSafeWallIndex = qMax(
        0,
        settings.value(QStringLiteral("dobot/safe_wall_index"), m_robotSafeWallIndex).toInt());
    m_robotSafeWallMonitorEnabled =
        settings.value(QStringLiteral("dobot/safe_wall_monitor_enabled"), m_robotSafeWallMonitorEnabled).toBool();
    m_robotSafeWallPollIntervalMs = qBound(
        500,
        settings.value(QStringLiteral("dobot/safe_wall_poll_interval_ms"), m_robotSafeWallPollIntervalMs).toInt(),
        5000);
    const QString payloadPresetName =
        settings.value(QStringLiteral("dobot/payload_preset_name"), m_robotPayloadOptions.presetName).toString().trimmed();
    m_robotPayloadApplyBeforeEnable =
        settings.value(QStringLiteral("dobot/payload_apply_before_enable"), m_robotPayloadApplyBeforeEnable).toBool();
    m_robotPayloadOptions.presetName = payloadPresetName.isEmpty() ? QStringLiteral("TEST") : payloadPresetName;
    m_robotPayloadOptions.usePresetName =
        settings.value(QStringLiteral("dobot/payload_use_preset_name"), m_robotPayloadOptions.usePresetName).toBool();
    m_robotPayloadOptions.fallbackToExplicitEnableRobot =
        settings.value(QStringLiteral("dobot/payload_fallback_to_explicit"), m_robotPayloadOptions.fallbackToExplicitEnableRobot).toBool();
    m_robotPayloadOptions.loadKg =
        qMax(0.0, settings.value(QStringLiteral("dobot/payload_load_kg"), m_robotPayloadOptions.loadKg).toDouble());
    m_robotPayloadOptions.centerX =
        settings.value(QStringLiteral("dobot/payload_center_x"), m_robotPayloadOptions.centerX).toDouble();
    m_robotPayloadOptions.centerY =
        settings.value(QStringLiteral("dobot/payload_center_y"), m_robotPayloadOptions.centerY).toDouble();
    m_robotPayloadOptions.centerZ =
        settings.value(QStringLiteral("dobot/payload_center_z"), m_robotPayloadOptions.centerZ).toDouble();
    m_robotPayloadOptions.enableLoadCheck =
        settings.value(QStringLiteral("dobot/payload_enable_check"), m_robotPayloadOptions.enableLoadCheck).toBool();
}

void DeviceMonitorPage::applyRobotArmSettingsToClient()
{
    m_robotArmClient.setSettings(m_robotArmSettings);
    m_robotZAxisAligner.setDashboardSocket(m_robotArmClient.dashboardSocket());
    m_robotZAxisAligner.setMaxJointDeltaDeg(m_robotZAxisAlignMaxJointDeltaDeg);
}

void DeviceMonitorPage::connectRobotArm()
{
    const QString host = m_robotArmHostEdit != nullptr ? m_robotArmHostEdit->text().trimmed() : m_robotArmSettings.host;
    if (host.isEmpty()) {
        setRobotArmStatus(QStringLiteral("机械臂 IP 不能为空"));
        return;
    }

    m_robotArmSettings.host = host;
    applyRobotArmSettingsToClient();

    QString errorMessage;
    if (!m_robotArmClient.connectToController(&errorMessage)) {
        m_robotEnabled = false;
        setRobotArmStatus(QStringLiteral("连接失败：%1").arg(errorMessage));
        return;
    }

    setRobotArmStatus(QStringLiteral("已连接 %1:%2").arg(m_robotArmSettings.host).arg(m_robotArmSettings.commandPort));
    if (m_robotSafeWallMonitorEnabled) {
        m_robotArmSafetyWallTimer.start();
        pollRobotArmSafetyWall();
    }
}

void DeviceMonitorPage::disconnectRobotArm()
{
    m_robotArmSafetyWallTimer.stop();
    resetRobotArmPhysicalDragButtonState();
    if (m_robotArmClient.isConnected() && m_robotEnabled) {
        QString ignoredError;
        m_robotArmClient.stopScript(&ignoredError);
        m_robotArmClient.stop(&ignoredError);
        m_robotArmClient.disableRobot(&ignoredError);
    }

    m_robotEnabled = false;
    m_robotArmClient.disconnectFromController();
    m_robotArmDragging = false;
    m_robotSafeWallRecoveryMode = false;
    m_robotSafeWallAlarmLatched = false;
    m_robotAlarmClearDialogActive = false;
    m_robotAlarmClearPromptDismissed = false;
    m_robotLastSafeWallAlarmPayload.clear();
    m_robotLastSafeWallMode = -1;
    m_hasRobotZAxisAlignTarget = false;
    setRobotArmStatus(QStringLiteral("已断开"));
}

void DeviceMonitorPage::toggleRobotArmConnection(bool checked)
{
    if (m_updatingRobotArmSwitches) {
        return;
    }

    if (checked) {
        connectRobotArm();
    } else {
        disconnectRobotArm();
    }
    syncRobotArmSwitches();
}

void DeviceMonitorPage::toggleRobotArmEnable(bool checked)
{
    if (m_updatingRobotArmSwitches) {
        return;
    }

    if (checked) {
        enableRobotArm();
    } else {
        disableRobotArm();
    }
    syncRobotArmSwitches();
}

void DeviceMonitorPage::enableRobotArm()
{
    if (!m_robotArmClient.isConnected()) {
        connectRobotArm();
        if (!m_robotArmClient.isConnected()) {
            return;
        }
    }

    const auto isClearableRobotAlarm = [](const panthera::adapters::dobot::DobotCommandResult& result) {
        return isClearableRobotCommandErrorId(result.errorId);
    };
    const auto confirmClearForCommandError = [this](const QString& action, const panthera::adapters::dobot::DobotCommandResult& result) {
        const QString alarmSummary = QStringLiteral("%1 ErrorID=%2").arg(action).arg(result.errorId);
        noteRobotArmSafetyWallAlarm(-1, alarmSummary);
        return confirmAndClearRobotArmAlarm(-1, alarmSummary, result.errorId, true);
    };

    QString errorMessage;
    panthera::adapters::dobot::DobotCommandResult controlResult = m_robotArmClient.requestControl(&errorMessage);
    logRobotArmCommand(QStringLiteral("RequestControl()"), controlResult, errorMessage);
    if (!controlResult.ok()) {
        if (isClearableRobotAlarm(controlResult)) {
            if (confirmClearForCommandError(QStringLiteral("RequestControl()"), controlResult)) {
                errorMessage.clear();
                controlResult = m_robotArmClient.requestControl(&errorMessage);
                logRobotArmCommand(QStringLiteral("RequestControl()"), controlResult, errorMessage);
            }
        }
        if (!controlResult.ok()) {
            if (errorMessage.isEmpty()) {
                errorMessage = controlResult.protocolError;
            }
            setRobotArmStatus(QStringLiteral("请求 TCP 控制权失败：%1").arg(errorMessage));
            return;
        }
    }

    int robotMode = -1;
    panthera::adapters::dobot::DobotCommandResult modeResult = m_robotArmClient.robotMode(&robotMode, &errorMessage);
    if (!modeResult.ok()) {
        if (isClearableRobotAlarm(modeResult)) {
            if (confirmClearForCommandError(QStringLiteral("RobotMode()"), modeResult)) {
                errorMessage.clear();
                modeResult = m_robotArmClient.robotMode(&robotMode, &errorMessage);
            }
        }
        if (!modeResult.ok()) {
            setRobotArmStatus(QStringLiteral("读取 RobotMode 失败：%1").arg(errorMessage.isEmpty() ? modeResult.protocolError : errorMessage));
            return;
        }
    }

    QString alarmPayload;
    panthera::adapters::dobot::DobotCommandResult alarmResult = m_robotArmClient.getErrorId(&alarmPayload, &errorMessage);
    if (!alarmResult.ok()) {
        if (isClearableRobotAlarm(alarmResult)) {
            if (confirmClearForCommandError(QStringLiteral("GetErrorID()"), alarmResult)) {
                errorMessage.clear();
                alarmResult = m_robotArmClient.getErrorId(&alarmPayload, &errorMessage);
            }
        }
        if (!alarmResult.ok()) {
            setRobotArmStatus(QStringLiteral("读取 GetErrorID 失败：%1").arg(errorMessage.isEmpty() ? alarmResult.protocolError : errorMessage));
            return;
        }
    }

    const bool hadAlarmBeforeEnable = robotMode == kRobotModeError || !dobotAlarmPayloadIsClear(alarmPayload);
    if (hadAlarmBeforeEnable) {
        noteRobotArmSafetyWallAlarm(robotMode, alarmPayload);
        if (!confirmAndClearRobotArmAlarm(robotMode, alarmPayload, 0, true)) {
            return;
        }
    }

    panthera::adapters::dobot::DobotCommandResult powerResult = m_robotArmClient.powerOn(&errorMessage);
    logRobotArmCommand(QStringLiteral("PowerOn()"), powerResult, errorMessage);
    if (!powerResult.ok()) {
        if (isClearableRobotAlarm(powerResult)) {
            if (confirmClearForCommandError(QStringLiteral("PowerOn()"), powerResult)) {
                errorMessage.clear();
                powerResult = m_robotArmClient.powerOn(&errorMessage);
                logRobotArmCommand(QStringLiteral("PowerOn()"), powerResult, errorMessage);
            }
        }
        if (!powerResult.ok()) {
            setRobotArmStatus(QStringLiteral("PowerOn 失败：%1").arg(errorMessage.isEmpty() ? powerResult.protocolError : errorMessage));
            return;
        }
    }

    if (m_robotPayloadApplyBeforeEnable) {
        bool enableRobotAlreadySent = false;
        panthera::adapters::dobot::DobotPayloadEnableService payloadService(
            [this](const QString& command) {
                QString commandError;
                const panthera::adapters::dobot::DobotCommandResult result =
                    m_robotArmClient.rawCommand(command, &commandError);
                if (!result.raw.trimmed().isEmpty()) {
                    return result.raw.trimmed();
                }
                if (!commandError.trimmed().isEmpty()) {
                    return commandError.trimmed();
                }
                if (!result.protocolError.trimmed().isEmpty()) {
                    return result.protocolError.trimmed();
                }
                return QStringLiteral("No Dashboard response.");
            },
            [this](const QString& message) {
                appendRobotArmLog(message);
            });
        payloadService.setOptions(m_robotPayloadOptions);

        QString payloadError;
        bool payloadApplied = payloadService.applyPayloadBeforeEnable(&enableRobotAlreadySent, &payloadError);
        if (!payloadApplied) {
            const int payloadCommandErrorId = dobotCommandErrorIdFromText(payloadError);
            if (isClearableRobotCommandErrorId(payloadCommandErrorId)
                && confirmAndClearRobotArmAlarm(-1,
                    QStringLiteral("EnableRobot() ErrorID=%1").arg(payloadCommandErrorId),
                    payloadCommandErrorId,
                    true)) {
                enableRobotAlreadySent = false;
                payloadError.clear();
                payloadApplied = payloadService.applyPayloadBeforeEnable(&enableRobotAlreadySent, &payloadError);
            }
        }
        if (!payloadApplied) {
            setRobotArmStatus(QStringLiteral("机械臂负载参数应用失败：%1").arg(payloadError));
            return;
        }

        if (enableRobotAlreadySent) {
            m_robotEnabled = true;
            QStringList fallbackArguments {
                panthera::adapters::dobot::formatDobotNumber(m_robotPayloadOptions.loadKg),
                panthera::adapters::dobot::formatDobotNumber(m_robotPayloadOptions.centerX),
                panthera::adapters::dobot::formatDobotNumber(m_robotPayloadOptions.centerY),
                panthera::adapters::dobot::formatDobotNumber(m_robotPayloadOptions.centerZ)
            };
            if (m_robotPayloadOptions.enableLoadCheck) {
                fallbackArguments.push_back(QStringLiteral("1"));
            }
            const QString enableCommand = QStringLiteral("EnableRobot(%1)").arg(fallbackArguments.join(QLatin1Char(',')));
            if (m_robotSafeWallAlarmLatched) {
                m_robotSafeWallRecoveryMode = true;
                setRobotArmStatus(QStringLiteral("安全墙恢复模式：已发送 %1。请手动将机械臂移动回 %2 内；软件将通过 RobotMode()/GetErrorID() 自动确认恢复完成。")
                                      .arg(enableCommand, robotSafeWallLabel()));
                return;
            }
            setRobotArmStatus(QStringLiteral("已发送 %1，机械臂已上使能").arg(enableCommand));
            return;
        }
    }

    panthera::adapters::dobot::DobotCommandResult enableResult = m_robotArmClient.enableRobot(&errorMessage);
    logRobotArmCommand(QStringLiteral("EnableRobot()"), enableResult, errorMessage);
    if (!enableResult.ok()) {
        if (isClearableRobotAlarm(enableResult)) {
            if (confirmClearForCommandError(QStringLiteral("EnableRobot()"), enableResult)) {
                errorMessage.clear();
                enableResult = m_robotArmClient.enableRobot(&errorMessage);
                logRobotArmCommand(QStringLiteral("EnableRobot()"), enableResult, errorMessage);
            }
        }
        if (!enableResult.ok()) {
            setRobotArmStatus(QStringLiteral("上使能失败：%1").arg(errorMessage.isEmpty() ? enableResult.protocolError : errorMessage));
            return;
        }
    }

    m_robotEnabled = true;
    if (m_robotSafeWallAlarmLatched) {
        m_robotSafeWallRecoveryMode = true;
        setRobotArmStatus(QStringLiteral("安全墙恢复模式：已发送 EnableRobot()。请手动将机械臂移动回 %1 内；软件将通过 RobotMode()/GetErrorID() 自动确认恢复完成。")
                              .arg(robotSafeWallLabel()));
        return;
    }
    setRobotArmStatus(QStringLiteral("已应用机械臂负载参数并发送 EnableRobot()，机械臂已上使能"));
}

void DeviceMonitorPage::disableRobotArm()
{
    if (!m_robotArmClient.isConnected()) {
        m_robotEnabled = false;
        resetRobotArmPhysicalDragButtonState();
        setRobotArmStatus(QStringLiteral("机械臂未连接"));
        return;
    }

    QString ignoredError;
    m_robotArmClient.requestControl(&ignoredError);

    QString errorMessage;
    const panthera::adapters::dobot::DobotCommandResult result = m_robotArmClient.disableRobot(&errorMessage);
    if (!result.ok()) {
        setRobotArmStatus(QStringLiteral("下使能失败：%1").arg(errorMessage.isEmpty() ? result.protocolError : errorMessage));
        return;
    }

    m_robotEnabled = false;
    m_robotArmDragging = false;
    resetRobotArmPhysicalDragButtonState();
    setRobotArmStatus(QStringLiteral("已发送 DisableRobot()，机械臂已下使能"));
}

bool DeviceMonitorPage::requestRobotArmControl(QString* errorMessage, bool logCommand)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const panthera::adapters::dobot::DobotCommandResult controlResult = m_robotArmClient.requestControl(errorMessage);
    if (logCommand) {
        logRobotArmCommand(QStringLiteral("RequestControl()"), controlResult, errorMessage != nullptr ? *errorMessage : QString());
    }
    if (!controlResult.ok()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = controlResult.protocolError;
        }
        return false;
    }
    return true;
}

QString DeviceMonitorPage::robotSafeWallLabel() const
{
    return QStringLiteral("%1(index=%2)").arg(m_robotSafeWallName).arg(m_robotSafeWallIndex);
}

void DeviceMonitorPage::noteRobotArmSafetyWallAlarm(int robotMode, const QString& alarmPayload)
{
    const bool shouldLog = !m_robotSafeWallAlarmLatched
        || m_robotLastSafeWallMode != robotMode
        || m_robotLastSafeWallAlarmPayload != alarmPayload;

    if (shouldLog) {
        m_robotAlarmClearPromptDismissed = false;
    }
    m_robotSafeWallAlarmLatched = true;
    m_robotSafeWallRecoveryMode = true;
    m_robotLastSafeWallMode = robotMode;
    m_robotLastSafeWallAlarmPayload = alarmPayload;
    m_robotEnabled = false;
    m_robotArmDragging = false;
    resetRobotArmPhysicalDragButtonState();

    if (shouldLog) {
        setRobotArmStatus(QStringLiteral("检测到机械臂报警，按安全墙 %1 恢复流程处理：RobotMode=%2，GetErrorID=%3。清除报警后再次点击使能进入恢复模式。")
                              .arg(robotSafeWallLabel())
                              .arg(robotMode)
                              .arg(alarmPayload));
    } else {
        refreshRobotArmUi();
    }
}

bool DeviceMonitorPage::confirmAndClearRobotArmAlarm(int robotMode, const QString& alarmPayload, int commandErrorId, bool forcePrompt)
{
    if (!m_robotArmClient.isConnected()) {
        setRobotArmStatus(QStringLiteral("机械臂未连接，无法清除报警"));
        return false;
    }
    if (m_robotAlarmClearDialogActive) {
        return false;
    }
    if (!forcePrompt && m_robotAlarmClearPromptDismissed) {
        return false;
    }

    const bool timerWasActive = m_robotArmSafetyWallTimer.isActive();
    if (timerWasActive) {
        m_robotArmSafetyWallTimer.stop();
    }
    const auto restorePolling = [this, timerWasActive]() {
        if (timerWasActive && m_robotSafeWallMonitorEnabled && m_robotArmClient.isConnected()) {
            m_robotArmSafetyWallTimer.start();
        }
    };

    m_robotAlarmClearDialogActive = true;
    appendRobotArmLog(QStringLiteral("等待医生确认清除机械臂报警：commandErrorId=%1, RobotMode=%2, GetErrorID=%3")
                          .arg(commandErrorId)
                          .arg(robotMode)
                          .arg(alarmPayload));

    QString dialogTitle = QStringLiteral("机械臂安全报警");
    QString dialogText = QStringLiteral("机械臂触发安全保护。");
    QString dialogInfo = QStringLiteral("确认现场安全后，点击“清除报警”。");
    if (commandErrorId == -2) {
        dialogTitle = QStringLiteral("机械臂报警");
        dialogText = QStringLiteral("机械臂处于报警状态。");
        dialogInfo = QStringLiteral("确认现场安全后，点击“清除报警”。");
    } else if (commandErrorId == -3) {
        dialogTitle = QStringLiteral("急停已触发");
        dialogText = QStringLiteral("请先手动松开急停按钮。");
        dialogInfo = QStringLiteral("确认急停按钮已松开后，点击“清除报警”。");
    }

    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(dialogTitle);
    messageBox.setText(dialogText);
    messageBox.setInformativeText(dialogInfo);
    QPushButton* clearButton = messageBox.addButton(QStringLiteral("清除报警"), QMessageBox::AcceptRole);
    QPushButton* cancelButton = messageBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    messageBox.setDefaultButton(cancelButton);
    messageBox.exec();
    const bool confirmed = messageBox.clickedButton() == clearButton;
    m_robotAlarmClearDialogActive = false;

    if (!confirmed) {
        m_robotAlarmClearPromptDismissed = true;
        setRobotArmStatus(QStringLiteral("用户未确认清除机械臂报警，未发送 ClearError()"));
        restorePolling();
        return false;
    }

    m_robotAlarmClearPromptDismissed = false;

    QString commandError;
    if (!requestRobotArmControl(&commandError)) {
        setRobotArmStatus(QStringLiteral("清除报警前请求 TCP 控制权失败：%1").arg(commandError));
        restorePolling();
        return false;
    }

    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult clearResult =
        m_robotArmClient.clearError(&commandError);
    logRobotArmCommand(QStringLiteral("ClearError()"), clearResult, commandError);
    if (!clearResult.ok()) {
        m_robotAlarmClearPromptDismissed = true;
        setRobotArmStatus(QStringLiteral("ClearError 失败：%1。若为实体急停，请确认按钮已手动释放后再重试。")
                              .arg(commandError.isEmpty() ? clearResult.protocolError : commandError));
        restorePolling();
        return false;
    }

    int modeAfterClear = -1;
    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult modeResult =
        m_robotArmClient.robotMode(&modeAfterClear, &commandError);
    if (!modeResult.ok()) {
        setRobotArmStatus(QStringLiteral("ClearError 后读取 RobotMode 失败：%1")
                              .arg(commandError.isEmpty() ? modeResult.protocolError : commandError));
        restorePolling();
        return false;
    }

    QString alarmAfterClear;
    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult alarmResult =
        m_robotArmClient.getErrorId(&alarmAfterClear, &commandError);
    if (!alarmResult.ok()) {
        setRobotArmStatus(QStringLiteral("ClearError 后读取 GetErrorID 失败：%1")
                              .arg(commandError.isEmpty() ? alarmResult.protocolError : commandError));
        restorePolling();
        return false;
    }

    if (modeAfterClear == kRobotModeError || !dobotAlarmPayloadIsClear(alarmAfterClear)) {
        m_robotAlarmClearPromptDismissed = true;
        setRobotArmStatus(QStringLiteral("报警未清除，RobotMode=%1，GetErrorID=%2。请确认实体急停已释放或安全墙报警条件已处理后重试。")
                              .arg(modeAfterClear)
                              .arg(alarmAfterClear));
        restorePolling();
        return false;
    }

    m_robotSafeWallRecoveryMode = true;
    m_robotSafeWallAlarmLatched = true;
    m_robotLastSafeWallMode = modeAfterClear;
    m_robotLastSafeWallAlarmPayload.clear();
    m_robotEnabled = false;
    m_robotArmDragging = false;
    setRobotArmStatus(QStringLiteral("机械臂报警已清除：RobotMode=%1，GetErrorID=[]。请继续上电/使能并按恢复流程操作。")
                          .arg(modeAfterClear));
    restorePolling();
    return true;
}

void DeviceMonitorPage::pollRobotArmSafetyWall()
{
    if (!m_robotSafeWallMonitorEnabled || !m_robotArmClient.isConnected()) {
        return;
    }

    QString errorMessage;
    int robotMode = -1;
    const panthera::adapters::dobot::DobotCommandResult modeResult =
        m_robotArmClient.robotMode(&robotMode, &errorMessage);
    if (!modeResult.ok()) {
        if (m_robotSafeWallRecoveryMode) {
            appendRobotArmLog(QStringLiteral("安全墙恢复轮询 RobotMode() 失败：%1")
                                  .arg(errorMessage.isEmpty() ? modeResult.protocolError : errorMessage));
        }
        return;
    }

    QString alarmPayload;
    errorMessage.clear();
    const panthera::adapters::dobot::DobotCommandResult alarmResult =
        m_robotArmClient.getErrorId(&alarmPayload, &errorMessage);
    if (!alarmResult.ok()) {
        if (m_robotSafeWallRecoveryMode) {
            appendRobotArmLog(QStringLiteral("安全墙恢复轮询 GetErrorID() 失败：%1")
                                  .arg(errorMessage.isEmpty() ? alarmResult.protocolError : errorMessage));
        }
        return;
    }

    const bool alarmActive = robotMode == kRobotModeError || !dobotAlarmPayloadIsClear(alarmPayload);
    if (alarmActive) {
        noteRobotArmSafetyWallAlarm(robotMode, alarmPayload);
        confirmAndClearRobotArmAlarm(robotMode, alarmPayload, 0, false);
        return;
    }

    if (m_robotSafeWallRecoveryMode) {
        if (robotMode == kRobotModeEnable) {
            m_robotSafeWallRecoveryMode = false;
            m_robotSafeWallAlarmLatched = false;
            m_robotLastSafeWallAlarmPayload.clear();
            m_robotLastSafeWallMode = robotMode;
            m_robotEnabled = true;
            m_robotArmDragging = false;
            setRobotArmStatus(QStringLiteral("安全墙 %1 恢复完成：RobotMode=5，GetErrorID=[]，已退出恢复模式。")
                                  .arg(robotSafeWallLabel()));
            return;
        }

        if (robotMode != m_robotLastSafeWallMode) {
            appendRobotArmLog(QStringLiteral("安全墙恢复中：RobotMode=%1，GetErrorID=[]。等待控制器回到 RobotMode=5 后自动退出恢复模式。")
                                  .arg(robotMode));
            m_robotLastSafeWallMode = robotMode;
        }
        return;
    }

    if (robotMode == kRobotModeEnable || robotMode == kRobotModeBackdrive
        || robotMode == kRobotModeRunning || robotMode == kRobotModeSingleMove) {
        if (!m_robotEnabled) {
            m_robotEnabled = true;
            refreshRobotArmUi();
        }
    } else if (m_robotEnabled) {
        m_robotEnabled = false;
        m_robotArmDragging = false;
        refreshRobotArmUi();
    }
}

bool DeviceMonitorPage::prepareRobotArmForSafeMotion(QString* errorMessage)
{
    if (!m_robotArmClient.isConnected()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("机械臂未连接");
        }
        return false;
    }

    const auto requireOk = [errorMessage](const QString& action, const panthera::adapters::dobot::DobotCommandResult& result, const QString& commandError) {
        if (result.ok()) {
            return true;
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1失败：%2").arg(action, commandError.isEmpty() ? result.protocolError : commandError);
        }
        return false;
    };

    QString commandError;
    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult controlResult = m_robotArmClient.requestControl(&commandError);
    logRobotArmCommand(QStringLiteral("RequestControl()"), controlResult, commandError);
    if (!requireOk(QStringLiteral("请求控制权"), controlResult, commandError)) {
        return false;
    }

    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult clearResult = m_robotArmClient.clearError(&commandError);
    logRobotArmCommand(QStringLiteral("ClearError()"), clearResult, commandError);
    if (!requireOk(QStringLiteral("清错"), clearResult, commandError)) {
        return false;
    }
    if (!applyRobotArmSafeSpeed(errorMessage)) {
        return false;
    }
    return true;
}

bool DeviceMonitorPage::applyRobotArmSafeSpeed(QString* errorMessage)
{
    const auto requireOk = [errorMessage](const QString& action, const panthera::adapters::dobot::DobotCommandResult& result, const QString& commandError) {
        if (result.ok()) {
            return true;
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1失败：%2").arg(action, commandError.isEmpty() ? result.protocolError : commandError);
        }
        return false;
    };

    QString commandError;
    commandError.clear();
    const panthera::adapters::dobot::DobotCommandResult speedFactorResult = m_robotArmClient.speedFactor(m_robotSafeSpeedPercent, &commandError);
    logRobotArmCommand(QStringLiteral("SpeedFactor(%1)").arg(m_robotSafeSpeedPercent), speedFactorResult, commandError);
    if (!requireOk(QStringLiteral("设置全局速度"), speedFactorResult, commandError)) {
        return false;
    }

    return true;
}

bool DeviceMonitorPage::moveRobotToPose(
    const panthera::adapters::dobot::DobotPose& pose,
    int userIndex,
    int toolIndex,
    QString* errorMessage)
{
    appendRobotArmLog(QStringLiteral("安全原点位姿运动开始：pose=%1，user=%2，tool=%3")
                          .arg(poseSummary(pose))
                          .arg(userIndex)
                          .arg(toolIndex));
    if (!prepareRobotArmForSafeMotion(errorMessage)) {
        return false;
    }

    panthera::adapters::dobot::DobotMotionOptions motionOptions;
    motionOptions.userIndex = userIndex;
    motionOptions.toolIndex = toolIndex;
    motionOptions.accelerationPercent = m_robotSafeAccelerationPercent;
    motionOptions.velocityPercent = m_robotSafeSpeedPercent;

    QString commandError;
    appendRobotArmLog(QStringLiteral("准备调用 MovJ(pose)，a=%1，v=%2")
                          .arg(m_robotSafeAccelerationPercent)
                          .arg(m_robotSafeSpeedPercent));
    const panthera::adapters::dobot::DobotCommandResult moveResult =
        m_robotArmClient.movJ(pose, motionOptions, &commandError);
    logRobotArmCommand(QStringLiteral("MovJ(pose,%1)").arg(poseSummary(pose)), moveResult, commandError);
    if (!moveResult.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("MovJ(pose) 安全原点运动失败：%1")
                                .arg(commandError.isEmpty() ? moveResult.protocolError : commandError);
        }
        return false;
    }

    appendRobotArmLog(QStringLiteral("安全原点 MovJ(pose) 已被控制器接受，机械臂开始移动。"));
    return true;
}

QString DeviceMonitorPage::currentPresetTrajectoryName() const
{
    if (m_robotArmTrajectoryCombo == nullptr) {
        return QString();
    }

    const QString dataName = m_robotArmTrajectoryCombo->currentData().toString().trimmed();
    return !dataName.isEmpty() ? dataName : m_robotArmTrajectoryCombo->currentText().trimmed();
}

void DeviceMonitorPage::updateRobotTrajectoryCombo(const QStringList& traceNames)
{
    QStringList normalizedTraceNames;
    for (const QString& traceName : traceNames) {
        appendUniqueString(&normalizedTraceNames, traceName);
    }
    if (normalizedTraceNames.isEmpty()) {
        return;
    }

    const QString previousSelection = currentPresetTrajectoryName();
    m_robotTrajectoryFiles = normalizedTraceNames;

    if (m_robotArmTrajectoryCombo != nullptr) {
        const QSignalBlocker blocker(m_robotArmTrajectoryCombo);
        m_robotArmTrajectoryCombo->clear();
        for (const QString& traceName : m_robotTrajectoryFiles) {
            m_robotArmTrajectoryCombo->addItem(traceName, traceName);
        }

        const int previousIndex = m_robotArmTrajectoryCombo->findData(previousSelection);
        if (previousIndex >= 0) {
            m_robotArmTrajectoryCombo->setCurrentIndex(previousIndex);
        } else if (m_robotArmTrajectoryCombo->count() > 0) {
            m_robotArmTrajectoryCombo->setCurrentIndex(0);
        }
    }

    refreshRobotArmUi();
}

void DeviceMonitorPage::refreshRobotTrajectoryFiles()
{
    if (m_robotTrajectoryRefreshRunning) {
        setRobotArmStatus(QStringLiteral("轨迹列表正在刷新，请稍候"));
        return;
    }

    const QString host = (m_robotArmHostEdit != nullptr ? m_robotArmHostEdit->text() : m_robotArmSettings.host).trimmed();
    if (host.isEmpty()) {
        setRobotArmStatus(QStringLiteral("机械臂 IP 不能为空，无法刷新轨迹目录"));
        return;
    }

    panthera::adapters::dobot::DobotTrajectorySftpSettings sftpSettings = m_robotTrajectorySftpSettings;
    sftpSettings.host = host;
    m_robotArmSettings.host = host;
    m_robotTrajectorySftpSettings.host = host;

    appendRobotArmLog(QStringLiteral("开始通过 SFTP 刷新机械臂轨迹列表：host=%1，port=%2，remoteDir=%3")
                          .arg(sftpSettings.host)
                          .arg(sftpSettings.port)
                          .arg(sftpSettings.remoteDirectory));

    m_robotTrajectoryRefreshRunning = true;
    refreshRobotArmUi();

    QPointer<DeviceMonitorPage> page(this);
    QThread* refreshThread = QThread::create([page, sftpSettings]() {
        QString errorMessage;
        const QStringList foundFileNames =
            panthera::adapters::dobot::DobotTrajectorySftpClient::listTrajectoryCsvFiles(sftpSettings, &errorMessage);

        DeviceMonitorPage* targetPage = page.data();
        if (targetPage == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(targetPage, [page, foundFileNames, errorMessage, sftpSettings]() {
            if (page == nullptr) {
                return;
            }

            page->m_robotTrajectoryRefreshRunning = false;
            if (!foundFileNames.isEmpty()) {
                page->updateRobotTrajectoryCombo(foundFileNames);
                qDebug() << "DOBOT SFTP 轨迹列表刷新成功:" << sftpSettings.host << sftpSettings.remoteDirectory << foundFileNames;
                page->setRobotArmStatus(QStringLiteral("已通过 SFTP 刷新 %1 条轨迹：%2")
                                            .arg(foundFileNames.size())
                                            .arg(sftpSettings.remoteDirectory));
                return;
            }

            const QString message = errorMessage.isEmpty()
                ? QStringLiteral("SFTP 未读取到 CSV 文件")
                : errorMessage;
            page->appendRobotArmLog(QStringLiteral("SFTP 刷新轨迹列表失败：%1").arg(message));
            page->setRobotArmStatus(QStringLiteral("SFTP 刷新轨迹列表失败：%1").arg(message));
        }, Qt::QueuedConnection);
    });
    connect(refreshThread, &QThread::finished, refreshThread, &QObject::deleteLater);
    refreshThread->start();
}

bool DeviceMonitorPage::moveRobotToTrajectoryStart(const QString& traceName, int pathType, QString* errorMessage)
{
    appendRobotArmLog(QStringLiteral("轨迹初始点流程开始：traceName=%1，pathType=%2").arg(traceName).arg(pathType));
    if (!prepareRobotArmForSafeMotion(errorMessage)) {
        return false;
    }

    panthera::adapters::dobot::DobotStartPose startPose;
    QString commandError;
    appendRobotArmLog(QStringLiteral("准备调用 GetStartPose(%1,%2)").arg(traceName).arg(pathType));
    const panthera::adapters::dobot::DobotCommandResult startPoseResult = m_robotArmClient.getStartPose(traceName, pathType, &startPose, &commandError);
    logRobotArmCommand(QStringLiteral("GetStartPose(%1,%2)").arg(traceName).arg(pathType), startPoseResult, commandError);
    if (!startPoseResult.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("GetStartPose(%1,%2) 失败：%3")
                                .arg(traceName)
                                .arg(pathType)
                                .arg(commandError.isEmpty() ? startPoseResult.protocolError : commandError);
        }
        return false;
    }
    appendRobotArmLog(QStringLiteral("GetStartPose 初始点解析成功：%1").arg(startPoseSummary(startPose)));

    if (!startPose.hasJoints) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("GetStartPose(%1,%2) 未返回关节起点，无法用 GetAngle 确认到位")
                                .arg(traceName)
                                .arg(pathType);
        }
        return false;
    }

    commandError.clear();
    panthera::adapters::dobot::DobotMotionOptions motionOptions;
    motionOptions.accelerationPercent = m_robotSafeAccelerationPercent;
    motionOptions.velocityPercent = m_robotSafeSpeedPercent;

    appendRobotArmLog(QStringLiteral("准备调用 MovJ(joint) 初始点，a=%1，v=%2").arg(m_robotSafeAccelerationPercent).arg(m_robotSafeSpeedPercent));
    const panthera::adapters::dobot::DobotCommandResult moveResult =
        m_robotArmClient.movJ(startPose.joints, motionOptions, &commandError);
    logRobotArmCommand(QStringLiteral("MovJ(joint,%1 初始点)").arg(traceName), moveResult, commandError);
    if (!moveResult.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("MovJ(joint,%1 初始点) 失败：%2").arg(traceName, commandError.isEmpty() ? moveResult.protocolError : commandError);
        }
        return false;
    }

    const QVector<double> targetJoint = jointVector(startPose.joints);
    commandError.clear();
    appendRobotArmLog(QStringLiteral("等待轨迹初始点运动到位：GetAngle轮询，timeout=%1ms，tolerance=%2°")
                          .arg(kRobotZAxisAlignWaitTimeoutMs)
                          .arg(kRobotZAxisAlignJointToleranceDeg, 0, 'f', 1));
    const bool arrivedOk = m_robotZAxisAligner.waitForJointTarget(
        targetJoint,
        kRobotZAxisAlignWaitTimeoutMs,
        kRobotZAxisAlignJointToleranceDeg,
        kRobotZAxisAlignStableSamples,
        kRobotZAxisAlignPollIntervalMs,
        &commandError);
    appendRobotArmLog(arrivedOk ? QStringLiteral("GetAngle轮询等待轨迹初始点到位 => OK")
                                : QStringLiteral("GetAngle轮询等待轨迹初始点到位 => %1").arg(commandError));
    if (!arrivedOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("等待轨迹初始点到位失败：%1").arg(commandError);
        }
        return false;
    }
    return true;
}

bool DeviceMonitorPage::setRobotArmDragMode(bool checked, QString* errorMessage, bool logDiagnostics)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QString command = checked ? QStringLiteral("StartDrag()") : QStringLiteral("StopDrag()");
    if (checked && !requestRobotArmControl(errorMessage, logDiagnostics)) {
        return false;
    }

    QString commandError;
    const panthera::adapters::dobot::DobotCommandResult result = m_robotArmClient.rawCommand(command, &commandError);
    if (logDiagnostics) {
        logRobotArmCommand(command, result, commandError);
    }
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = commandError.isEmpty() ? result.protocolError : commandError;
        }
        return false;
    }

    m_robotArmDragging = checked;

    if (!logDiagnostics) {
        return true;
    }

    int robotMode = -1;
    QString modeError;
    const panthera::adapters::dobot::DobotCommandResult modeResult =
        m_robotArmClient.robotMode(&robotMode, &modeError);
    logRobotArmCommand(QStringLiteral("%1 后 RobotMode()").arg(command), modeResult, modeError);
    if (modeResult.ok()) {
        const bool modeMatchesDrag = checked ? robotMode == kRobotModeBackdrive : robotMode != kRobotModeBackdrive;
        appendRobotArmLog(QStringLiteral("%1 确认：目标拖拽=%2，RobotMode=%3%4")
                              .arg(command,
                                   checked ? QStringLiteral("ON") : QStringLiteral("OFF"))
                              .arg(robotMode)
                              .arg(modeMatchesDrag ? QString() : QStringLiteral("（未观察到期望模式变化）")));
    }
    return true;
}

void DeviceMonitorPage::resetRobotArmPhysicalDragButtonState()
{
    m_robotPhysicalDragMonitorEnabled = false;
    m_robotPhysicalDragButtonPressed = false;
    m_robotPhysicalDragButtonStateKnown = false;
    m_robotPhysicalDragPollTimer.stop();
}

void DeviceMonitorPage::toggleRobotArmDrag(bool checked)
{
    if (m_updatingRobotArmSwitches) {
        return;
    }

    if (!m_robotArmClient.isConnected()) {
        setRobotArmStatus(QStringLiteral("机械臂未连接"));
        syncRobotArmSwitches();
        return;
    }
    if (!m_robotEnabled) {
        setRobotArmStatus(QStringLiteral("请先上使能机械臂"));
        syncRobotArmSwitches();
        return;
    }

    QString errorMessage;
    if (!setRobotArmDragMode(checked, &errorMessage)) {
        setRobotArmStatus(QStringLiteral("%1拖拽模式失败：%2")
                              .arg(checked ? QStringLiteral("进入") : QStringLiteral("退出"),
                                   errorMessage));
        syncRobotArmSwitches();
        return;
    }

    setRobotArmStatus(checked ? QStringLiteral("已发送 StartDrag()，机械臂进入拖拽模式")
                              : QStringLiteral("已发送 StopDrag()，机械臂退出拖拽模式"));
    syncRobotArmSwitches();
}

void DeviceMonitorPage::toggleRobotArmPhysicalDragButton(bool checked)
{
    if (m_updatingRobotArmSwitches) {
        return;
    }

    if (!checked) {
        const bool shouldStopDrag = m_robotArmClient.isConnected()
            && m_robotEnabled
            && m_robotPhysicalDragMonitorEnabled
            && (m_robotPhysicalDragButtonPressed || m_robotArmDragging);
        resetRobotArmPhysicalDragButtonState();

        if (shouldStopDrag) {
            QString errorMessage;
            if (!setRobotArmDragMode(false, &errorMessage, false)) {
                setRobotArmStatus(QStringLiteral("已关闭实体拖拽按钮监控，退出拖拽失败：%1").arg(errorMessage));
                refreshRobotArmUi();
                return;
            }
        }

        setRobotArmStatus(QStringLiteral("已关闭实体拖拽按钮监控"));
        refreshRobotArmUi();
        return;
    }

    if (!m_robotArmClient.isConnected()) {
        setRobotArmStatus(QStringLiteral("机械臂未连接"));
        syncRobotArmSwitches();
        return;
    }
    if (!m_robotEnabled) {
        setRobotArmStatus(QStringLiteral("请先上使能机械臂"));
        syncRobotArmSwitches();
        return;
    }

    m_robotPhysicalDragMonitorEnabled = true;
    m_robotPhysicalDragButtonPressed = false;
    m_robotPhysicalDragButtonStateKnown = false;
    m_robotPhysicalDragPollTimer.start();
    setRobotArmStatus(QStringLiteral("已开启实体拖拽按钮监控"));
    refreshRobotArmUi();
    pollRobotArmPhysicalDragButton();
}

void DeviceMonitorPage::pollRobotArmPhysicalDragButton()
{
    if (!m_robotPhysicalDragMonitorEnabled) {
        return;
    }

    if (!m_robotArmClient.isConnected() || !m_robotEnabled) {
        resetRobotArmPhysicalDragButtonState();
        setRobotArmStatus(QStringLiteral("实体拖拽按钮监控已停止，请确认机械臂已连接并上使能"));
        refreshRobotArmUi();
        return;
    }

    bool pressed = false;
    QString errorMessage;
    const panthera::adapters::dobot::DobotCommandResult inputResult =
        m_robotArmClient.digitalInput(kRobotPhysicalDragButtonDiIndex, &pressed, &errorMessage);
    if (!inputResult.ok()) {
        resetRobotArmPhysicalDragButtonState();

        const QString readError = errorMessage.isEmpty() ? inputResult.protocolError : errorMessage;
        if (m_robotArmDragging && m_robotArmClient.isConnected()) {
            QString stopError;
            if (!setRobotArmDragMode(false, &stopError, false)) {
                setRobotArmStatus(QStringLiteral("实体拖拽按钮读取失败，已停止监控，退出拖拽失败：%1").arg(stopError));
                refreshRobotArmUi();
                return;
            }
        }

        setRobotArmStatus(QStringLiteral("实体拖拽按钮读取失败，已停止监控：%1").arg(readError));
        refreshRobotArmUi();
        return;
    }

    if (!m_robotPhysicalDragButtonStateKnown) {
        if (!pressed && !m_robotArmDragging) {
            m_robotPhysicalDragButtonStateKnown = true;
            m_robotPhysicalDragButtonPressed = false;
            return;
        }
    } else if (pressed == m_robotPhysicalDragButtonPressed) {
        return;
    }

    QString dragError;
    if (!setRobotArmDragMode(pressed, &dragError, false)) {
        setRobotArmStatus(QStringLiteral("%1，%2失败：%3")
                              .arg(pressed ? QStringLiteral("已按下实体拖拽按钮") : QStringLiteral("已松开实体拖拽按钮"),
                                   pressed ? QStringLiteral("进入拖拽") : QStringLiteral("退出拖拽"),
                                   dragError));
        syncRobotArmSwitches();
        return;
    }

    m_robotPhysicalDragButtonStateKnown = true;
    m_robotPhysicalDragButtonPressed = pressed;
    setRobotArmStatus(pressed ? QStringLiteral("已按下实体拖拽按钮，机械臂已进入拖拽模式")
                              : QStringLiteral("已松开实体拖拽按钮，机械臂已退出拖拽模式"));
    syncRobotArmSwitches();
}

void DeviceMonitorPage::moveRobotToSafeOrigin()
{
    if (!m_robotEnabled) {
        setRobotArmStatus(QStringLiteral("请先连接并上使能机械臂"));
        return;
    }

    QString errorMessage;
    if (!moveRobotToPose(m_robotSafeOriginPose, m_robotSafeOriginUserIndex, m_robotSafeOriginToolIndex, &errorMessage)) {
        setRobotArmStatus(errorMessage);
        return;
    }
    setRobotArmStatus(QStringLiteral("已发送安全原点 MovJ(pose) 指令，机械臂开始移动"));
}

void DeviceMonitorPage::alignRobotZAxis()
{
    if (!m_robotEnabled) {
        setRobotArmStatus(QStringLiteral("请先连接并上使能机械臂"));
        return;
    }

    appendRobotArmLog(QStringLiteral("Z轴对齐流程开始：工具Z轴指向地面(-Z)，自动计算目标点并低速运动。"));
    appendRobotArmLog(QStringLiteral("Z轴对齐安全阈值：单关节最大变化 %1°，acc=%2，vel=%3")
                          .arg(m_robotZAxisAlignMaxJointDeltaDeg, 0, 'f', 1)
                          .arg(kRobotZAxisAlignMotionPercent)
                          .arg(kRobotZAxisAlignMotionPercent));

    QString errorMessage;
    if (!prepareRobotArmForSafeMotion(&errorMessage)) {
        setRobotArmStatus(errorMessage);
        return;
    }

    m_hasRobotZAxisAlignTarget = false;
    errorMessage.clear();
    if (!m_robotZAxisAligner.calculateZAlignTarget(
            m_robotZAxisAlignUserIndex,
            m_robotZAxisAlignToolIndex,
            m_robotZAxisAlignTargetPose,
            m_robotZAxisAlignTargetJoint,
            &errorMessage)) {
        qDebug() << "DOBOT Z轴对齐计算失败:" << errorMessage;
        setRobotArmStatus(QStringLiteral("Z轴对齐计算失败：%1").arg(errorMessage));
        return;
    }

    m_hasRobotZAxisAlignTarget = true;
    qDebug() << "DOBOT Z轴对齐 targetPose=" << poseSummary(m_robotZAxisAlignTargetPose)
             << "targetJoint=" << jointSummary(m_robotZAxisAlignTargetJoint);
    appendRobotArmLog(QStringLiteral("Z轴对齐目标位姿：%1").arg(poseSummary(m_robotZAxisAlignTargetPose)));
    appendRobotArmLog(QStringLiteral("Z轴对齐目标关节：%1").arg(jointSummary(m_robotZAxisAlignTargetJoint)));

    errorMessage.clear();
    qDebug() << "DOBOT Z轴对齐准备运动 targetPose=" << poseSummary(m_robotZAxisAlignTargetPose)
             << "targetJoint=" << jointSummary(m_robotZAxisAlignTargetJoint);
    appendRobotArmLog(QStringLiteral("准备调用 MovJ(joint) Z轴对齐，a=%1，v=%2")
                          .arg(kRobotZAxisAlignMotionPercent)
                          .arg(kRobotZAxisAlignMotionPercent));
    const bool moveOk = m_robotZAxisAligner.runToJoint(
        m_robotZAxisAlignTargetJoint,
        kRobotZAxisAlignMotionPercent,
        kRobotZAxisAlignMotionPercent,
        &errorMessage);
    appendRobotArmLog(moveOk ? QStringLiteral("MovJ(joint,Z轴对齐目标) => OK")
                             : QStringLiteral("MovJ(joint,Z轴对齐目标) => %1").arg(errorMessage));
    if (!moveOk) {
        qDebug() << "DOBOT Z轴对齐运动失败:" << errorMessage;
        setRobotArmStatus(QStringLiteral("Z轴对齐运动失败：%1").arg(errorMessage));
        return;
    }

    setRobotArmStatus(QStringLiteral("已发送 Z轴对齐 MovJ(joint) 低速运动指令"));

    errorMessage.clear();
    appendRobotArmLog(QStringLiteral("等待 Z轴对齐运动到位：GetAngle轮询，timeout=%1ms，tolerance=%2°")
                          .arg(kRobotZAxisAlignWaitTimeoutMs)
                          .arg(kRobotZAxisAlignJointToleranceDeg, 0, 'f', 1));
    const bool arrivedOk = m_robotZAxisAligner.waitForJointTarget(
        m_robotZAxisAlignTargetJoint,
        kRobotZAxisAlignWaitTimeoutMs,
        kRobotZAxisAlignJointToleranceDeg,
        kRobotZAxisAlignStableSamples,
        kRobotZAxisAlignPollIntervalMs,
        &errorMessage);
    appendRobotArmLog(arrivedOk ? QStringLiteral("GetAngle轮询等待 Z轴对齐运动到位 => OK")
                                : QStringLiteral("GetAngle轮询等待 Z轴对齐运动到位 => %1").arg(errorMessage));
    if (!arrivedOk) {
        qDebug() << "DOBOT Z轴对齐等待到位失败:" << errorMessage;
        QString stopError;
        const bool stopOk = m_robotZAxisAligner.stop(&stopError);
        appendRobotArmLog(stopOk ? QStringLiteral("Stop() Z轴对齐异常停止 => OK")
                                 : QStringLiteral("Stop() Z轴对齐异常停止 => %1").arg(stopError));
        setRobotArmStatus(QStringLiteral("Z轴对齐等待到位失败：%1").arg(errorMessage));
        return;
    }

    errorMessage.clear();
    const bool stopOk = m_robotZAxisAligner.stop(&errorMessage);
    appendRobotArmLog(stopOk ? QStringLiteral("Stop() Z轴对齐 => OK")
                             : QStringLiteral("Stop() Z轴对齐 => %1").arg(errorMessage));
    if (!stopOk) {
        qDebug() << "DOBOT Z轴对齐停止失败:" << errorMessage;
        setRobotArmStatus(QStringLiteral("Z轴对齐停止失败：%1").arg(errorMessage));
        return;
    }

    m_hasRobotZAxisAlignTarget = false;
    setRobotArmStatus(QStringLiteral("机械臂已完成Z轴对齐操作"));
}

void DeviceMonitorPage::replaySelectedPresetTrajectory()
{
    if (!m_robotEnabled) {
        setRobotArmStatus(QStringLiteral("请先连接并上使能机械臂"));
        return;
    }

    const QString traceName = currentPresetTrajectoryName();
    if (traceName.isEmpty()) {
        setRobotArmStatus(QStringLiteral("请选择预设轨迹文件"));
        return;
    }
    appendRobotArmLog(QStringLiteral("预设轨迹复现流程开始：traceName=%1，pathType=%2").arg(traceName).arg(m_robotTrajectoryPathType));

    const QMessageBox::StandardButton moveAnswer = QMessageBox::question(
        this,
        QStringLiteral("轨迹初始位置"),
        QStringLiteral("是否移动到轨迹初始位置？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (moveAnswer != QMessageBox::Yes) {
        appendRobotArmLog(QStringLiteral("用户取消移动到轨迹初始位置：%1").arg(traceName));
        return;
    }

    QString errorMessage;
    if (!moveRobotToTrajectoryStart(traceName, m_robotTrajectoryPathType, &errorMessage)) {
        setRobotArmStatus(errorMessage);
        return;
    }
    appendRobotArmLog(QStringLiteral("机械臂已到达轨迹初始位置：%1").arg(traceName));
    QMessageBox::information(
        this,
        QStringLiteral("轨迹初始位置"),
        QStringLiteral("机械臂已到达轨迹初始位置。"));

    const QMessageBox::StandardButton replayAnswer = QMessageBox::question(
        this,
        QStringLiteral("预设轨迹复现"),
        QStringLiteral("是否开始预设轨迹复现？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (replayAnswer != QMessageBox::Yes) {
        appendRobotArmLog(QStringLiteral("用户取消预设轨迹复现：%1").arg(traceName));
        setRobotArmStatus(QStringLiteral("已移动到轨迹初始位置，未开始复现：%1").arg(traceName));
        return;
    }

    if (!prepareRobotArmForSafeMotion(&errorMessage)) {
        setRobotArmStatus(errorMessage);
        return;
    }

    errorMessage.clear();
    panthera::adapters::dobot::DobotStartPathOptions startPathOptions;
    startPathOptions.isConst = 0;
    startPathOptions.multi = 1.0;
    startPathOptions.sample = 50;
    startPathOptions.freq = 0.2;
    startPathOptions.userIndex = -1;
    startPathOptions.toolIndex = -1;
    appendRobotArmLog(
        QStringLiteral("准备调用 StartPath 轨迹复现：traceName=%1，isConst=%2，multi=%3，sample=%4，freq=%5")
            .arg(traceName)
            .arg(startPathOptions.isConst)
            .arg(QString::number(startPathOptions.multi, 'f', 2))
            .arg(startPathOptions.sample)
            .arg(QString::number(startPathOptions.freq, 'f', 3)));
    const panthera::adapters::dobot::DobotCommandResult result =
        m_robotArmClient.startPath(traceName, startPathOptions, &errorMessage);
    logRobotArmCommand(QStringLiteral("StartPath(%1)").arg(traceName), result, errorMessage);
    if (!result.ok()) {
        setRobotArmStatus(QStringLiteral("StartPath 轨迹复现失败：%1").arg(errorMessage.isEmpty() ? result.protocolError : errorMessage));
        return;
    }

    setRobotArmStatus(QStringLiteral("已发送 StartPath 轨迹复现：%1").arg(traceName));
}

void DeviceMonitorPage::runSelectedRobotTrajectory()
{
    replaySelectedPresetTrajectory();
}

void DeviceMonitorPage::pauseRobotTrajectory()
{
    if (!m_robotArmClient.isConnected()) {
        setRobotArmStatus(QStringLiteral("机械臂未连接"));
        return;
    }

    QString errorMessage;
    const panthera::adapters::dobot::DobotCommandResult result = m_robotArmClient.pause(&errorMessage);
    logRobotArmCommand(QStringLiteral("Pause() 轨迹复现"), result, errorMessage);
    if (!result.ok()) {
        setRobotArmStatus(QStringLiteral("暂停轨迹复现失败：%1").arg(errorMessage.isEmpty() ? result.protocolError : errorMessage));
        return;
    }
    setRobotArmStatus(QStringLiteral("轨迹复现已暂停"));
}

void DeviceMonitorPage::releaseThreeAxisGatewayForSharedUse()
{
    if (!m_threeAxisGateway.isGatewayOpen()) {
        return;
    }

    m_threeAxisGateway.closeGateway();
    m_threeAxisNodes.clear();
    setThreeAxisStatus(QStringLiteral("三电机网关已释放，当前位置保持不变，可在治疗方案页从当前位置开始图像采集"));
    updateThreeAxisNodeStatus();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::loadThreeAxisSdk()
{
    const QString sdkPath = m_threeAxisSdkPathEdit != nullptr ? m_threeAxisSdkPathEdit->text().trimmed() : QString();
    if (sdkPath.isEmpty()) {
        setThreeAxisStatus(QStringLiteral("请选择 UISimCanFunc.dll"));
        return;
    }

    QString errorMessage;
    if (!m_threeAxisGateway.loadSdk(sdkPath, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("UIM SDK 加载失败：%1").arg(errorMessage));
        refreshThreeAxisUi();
        return;
    }

    setThreeAxisStatus(QStringLiteral("UIM SDK 已加载：%1").arg(sdkPath));
    refreshThreeAxisUi();
}

void DeviceMonitorPage::searchThreeAxisGateways()
{
    if (!m_threeAxisGateway.isSdkLoaded()) {
        loadThreeAxisSdk();
        if (!m_threeAxisGateway.isSdkLoaded()) {
            return;
        }
    }

    QString errorMessage;
    m_threeAxisDevices = m_threeAxisGateway.searchGateways(&errorMessage);
    if (m_threeAxisDeviceCombo != nullptr) {
        m_threeAxisDeviceCombo->clear();
        for (const diji::adapters::uim::UimDeviceInfo& device : m_threeAxisDevices) {
            const QString name = device.name.trimmed().isEmpty() ? QStringLiteral("USB-CAN") : device.name.trimmed();
            m_threeAxisDeviceCombo->addItem(
                QStringLiteral("%1 | index %2 | COM%3 | %4")
                    .arg(name)
                    .arg(device.deviceIndex)
                    .arg(device.comIndex)
                    .arg(device.baudRate),
                device.deviceIndex);
        }
        if (m_threeAxisDevices.isEmpty()) {
            m_threeAxisDeviceCombo->addItem(QStringLiteral("未搜索到 USB-CAN 网关"), QVariant());
        }
    }

    if (m_threeAxisDevices.isEmpty()) {
        setThreeAxisStatus(errorMessage.isEmpty() ? QStringLiteral("未搜索到 USB-CAN 网关") : errorMessage);
    } else {
        setThreeAxisStatus(QStringLiteral("已搜索到 %1 个 USB-CAN 网关").arg(m_threeAxisDevices.size()));
    }
    refreshThreeAxisUi();
}

void DeviceMonitorPage::toggleThreeAxisGateway()
{
    if (m_threeAxisGateway.isGatewayOpen()) {
        disableAllThreeAxisMotors();
        m_threeAxisGateway.closeGateway();
        m_threeAxisNodes.clear();
        m_waterTankHighLevelKnown = false;
        m_waterTankHighLevelActive = false;
        setProperty("waterTankHighLevelActiveSamples", 0);
        m_waterTankLowLevelKnown = false;
        m_waterTankLowLevelActive = false;
        setProperty("waterTankLowLevelActiveSamples", 0);
        m_waterTankLowLevelAlarmLatched = false;
        updateWaterTankLimitStatus();
        setThreeAxisStatus(QStringLiteral("UIM 网关已关闭"));
        updateThreeAxisNodeStatus();
        refreshThreeAxisUi();
        return;
    }

    if (!m_threeAxisGateway.isSdkLoaded()) {
        loadThreeAxisSdk();
        if (!m_threeAxisGateway.isSdkLoaded()) {
            return;
        }
    }
    if (m_threeAxisDevices.isEmpty()) {
        searchThreeAxisGateways();
        if (m_threeAxisDevices.isEmpty()) {
            return;
        }
    }

    const QVariant selectedDeviceIndex = m_threeAxisDeviceCombo != nullptr ? m_threeAxisDeviceCombo->currentData() : QVariant();
    if (!selectedDeviceIndex.isValid()) {
        setThreeAxisStatus(QStringLiteral("请选择 USB-CAN 网关"));
        return;
    }

    QString errorMessage;
    if (!m_threeAxisGateway.openGateway(selectedDeviceIndex.toUInt(), &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("UIM 网关打开失败：%1").arg(errorMessage));
        refreshThreeAxisUi();
        return;
    }

    m_threeAxisNodes = m_threeAxisGateway.nodes();
    updateThreeAxisNodeStatus();
    const bool parametersConfigured = configureThreeAxisMotorParameters();
    setThreeAxisStatus(parametersConfigured
            ? QStringLiteral("UIM 网关已打开，CAN 节点 %1 个，电流、固定速度和 S1/S2 下降沿动作已设置").arg(m_threeAxisNodes.size())
            : QStringLiteral("UIM 网关已打开，CAN 节点 %1 个，部分电机参数设置失败").arg(m_threeAxisNodes.size()));
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::enableAllThreeAxisMotors()
{
    if (m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("急停未解除，禁止上电"));
        return;
    }

    bool issued = false;
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (threeAxisNodeAvailable(index)) {
            enableThreeAxisMotor(index);
            issued = true;
        }
    }
    if (!issued) {
        setThreeAxisStatus(QStringLiteral("未发现 6/7/8 号电机节点"));
    }
}

void DeviceMonitorPage::disableAllThreeAxisMotors()
{
    bool issued = false;
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (threeAxisNodeAvailable(index)) {
            disableThreeAxisMotor(index);
            issued = true;
        }
    }
    if (!issued && m_threeAxisGateway.isGatewayOpen()) {
        setThreeAxisStatus(QStringLiteral("未发现 6/7/8 号电机节点，无法断电"));
    }
}

void DeviceMonitorPage::emergencyStopThreeAxisMotors()
{
    if (m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("三电机急停已处于触发状态"));
        return;
    }

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        m_threeAxisPreEmergencySpeeds[static_cast<size_t>(index)] = threeAxisSpeedForAxis(index);
    }

    m_threeAxisEmergencyStopActive = true;
    appendThreeAxisLog(QStringLiteral("急停触发：开始将 6/7/8 号电机速度置 0，并取消残余移动，电机保持上电状态"));

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (!threeAxisNodeAvailable(index)) {
            appendThreeAxisLog(QStringLiteral("%1 急停跳过：未发现节点").arg(threeAxisAxisTitle(index)));
            continue;
        }
        QString errorMessage;
        if (!cancelThreeAxisMotorMotion(index, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 急停取消残余移动失败：%2").arg(threeAxisAxisTitle(index), errorMessage));
        } else {
            appendThreeAxisLog(QStringLiteral("%1 急停速度已置 0，残余移动已取消").arg(threeAxisAxisTitle(index)));
        }
    }

    if (m_faultToggles.size() >= 5 && m_faultToggles.at(4) != nullptr) {
        m_faultToggles.at(4)->setChecked(true);
    } else {
        m_simulationDevice->injectFault(InterlockReason::EmergencyStop, true);
        m_safetyKernel->setEmergencyStopReleased(false);
    }
    setThreeAxisStatus(QStringLiteral("三电机急停已触发，固定速度已置 0，残余移动已取消，电机未断电"));
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::releaseThreeAxisEmergencyStop()
{
    if (!m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("三电机急停未处于触发状态"));
        return;
    }

    bool cancelFailed = false;
    appendThreeAxisLog(QStringLiteral("解除急停：先保持速度 0 并取消 6/7/8 号电机残余移动"));
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (!m_threeAxisGateway.isGatewayOpen()) {
            appendThreeAxisLog(QStringLiteral("%1 取消残余移动跳过：UIM 网关未打开").arg(threeAxisAxisTitle(index)));
            continue;
        }
        if (!threeAxisNodeAvailable(index)) {
            appendThreeAxisLog(QStringLiteral("%1 取消残余移动失败：未发现节点").arg(threeAxisAxisTitle(index)));
            cancelFailed = true;
            continue;
        }

        QString errorMessage;
        if (!cancelThreeAxisMotorMotion(index, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 取消残余移动失败：%2").arg(threeAxisAxisTitle(index), errorMessage));
            cancelFailed = true;
            continue;
        }
        appendThreeAxisLog(QStringLiteral("%1 残余移动已取消").arg(threeAxisAxisTitle(index)));
    }

    if (cancelFailed) {
        setThreeAxisStatus(QStringLiteral("三电机解除急停失败，部分电机残余移动未取消，已保持速度 0"));
        refreshThreeAxisUi();
        return;
    }

    bool restoreFailed = false;
    appendThreeAxisLog(QStringLiteral("解除急停：残余移动已取消，开始恢复固定速度"));
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        const int speed = m_threeAxisPreEmergencySpeeds[static_cast<size_t>(index)] > 0
            ? m_threeAxisPreEmergencySpeeds[static_cast<size_t>(index)]
            : kThreeAxisDefaultSpeed;

        if (!m_threeAxisGateway.isGatewayOpen()) {
            appendThreeAxisLog(QStringLiteral("%1 恢复固定速度 %2：UIM 网关未打开")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(speed));
            continue;
        }
        if (!threeAxisNodeAvailable(index)) {
            appendThreeAxisLog(QStringLiteral("%1 恢复速度失败：未发现节点").arg(threeAxisAxisTitle(index)));
            restoreFailed = true;
            continue;
        }

        QString errorMessage;
        if (!selectThreeAxisNode(index, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 恢复速度选中失败：%2").arg(threeAxisAxisTitle(index), errorMessage));
            restoreFailed = true;
            continue;
        }
        if (!m_threeAxisGateway.setSpeed(speed, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 恢复速度 %2 失败：%3")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(speed)
                                   .arg(errorMessage));
            restoreFailed = true;
            continue;
        }
        appendThreeAxisLog(QStringLiteral("%1 恢复速度 %2 => OK")
                               .arg(threeAxisAxisTitle(index))
                               .arg(speed));
    }

    if (restoreFailed) {
        setThreeAxisStatus(QStringLiteral("三电机解除急停失败，残余移动已取消，但部分电机速度未恢复，请检查节点后重试"));
        refreshThreeAxisUi();
        return;
    }

    m_threeAxisEmergencyStopActive = false;
    if (m_faultToggles.size() >= 5 && m_faultToggles.at(4) != nullptr) {
        m_faultToggles.at(4)->setChecked(false);
    } else {
        m_simulationDevice->injectFault(InterlockReason::EmergencyStop, false);
        m_safetyKernel->setEmergencyStopReleased(true);
    }
    setThreeAxisStatus(QStringLiteral("三电机急停已解除，残余移动已取消，已恢复固定速度"));
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::refreshThreeAxisMotors()
{
    if (!m_threeAxisGateway.isGatewayOpen()) {
        return;
    }

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (!threeAxisNodeAvailable(index)) {
            continue;
        }
        QString errorMessage;
        if (!selectThreeAxisNode(index, &errorMessage)) {
            continue;
        }
        errorMessage.clear();
        m_threeAxisGateway.refreshSnapshot(&errorMessage);
        errorMessage.clear();
        m_threeAxisGateway.refreshSensorFeedback(&errorMessage);
    }
}

void DeviceMonitorPage::moveThreeAxisMotor(int axisIndex, int direction)
{
    if (m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("急停未解除，禁止运动"));
        return;
    }
    if (direction == 0) {
        return;
    }

    const double stepAmount = threeAxisJogAmountForAxis(axisIndex);
    const int requestedSteps = threeAxisMoveSteps(axisIndex, stepAmount);
    if (requestedSteps <= 0) {
        setThreeAxisStatus(QStringLiteral("点动量必须大于 0"));
        return;
    }

    const int signedSteps = direction > 0 ? requestedSteps : -requestedSteps;

    QString errorMessage;
    if (!selectThreeAxisNode(axisIndex, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }
    errorMessage.clear();
    if (!m_threeAxisGateway.refreshSnapshot(&errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 读取实际绝对位置失败：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }

    const diji::adapters::uim::UimMotorSnapshot snapshot = m_threeAxisGateway.latestSnapshot();
    if (!snapshot.hasPosition) {
        setThreeAxisStatus(QStringLiteral("%1 未读到实际绝对位置，已拒绝运动").arg(threeAxisAxisTitle(axisIndex)));
        refreshThreeAxisUi();
        return;
    }
    const int currentSteps = snapshot.position;
    m_threeAxisSoftPositionSteps[static_cast<size_t>(axisIndex)] = currentSteps;
    m_threeAxisPositionKnown[static_cast<size_t>(axisIndex)] = true;
    const int projectedSteps = currentSteps + signedSteps;
    if (!threeAxisAbsoluteTargetAllowed(axisIndex, projectedSteps, &errorMessage)) {
        setThreeAxisStatus(errorMessage);
        refreshThreeAxisUi();
        return;
    }
    if (!threeAxisSensorLimitAllowsMove(axisIndex, signedSteps, &errorMessage)) {
        setThreeAxisStatus(errorMessage);
        refreshThreeAxisUi();
        return;
    }

    if (std::abs(signedSteps) >= kThreeAxisLargeMoveConfirmSteps) {
        appendThreeAxisLog(QStringLiteral("%1 大步点动已通过范围和传感器检查，直接执行")
                               .arg(threeAxisAxisTitle(axisIndex)));
    }

    const QString actionName = QStringLiteral("%1 %2 %3 / %4 步")
                                   .arg(threeAxisJogActionTitle(axisIndex, direction))
                                   .arg(stepAmount, 0, 'f', 2)
                                   .arg(threeAxisDisplayUnitText(axisIndex))
                                   .arg(std::abs(signedSteps));

    errorMessage.clear();
    if (!selectThreeAxisNode(axisIndex, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }
    if (!m_threeAxisGateway.setSpeed(threeAxisSpeedForAxis(axisIndex), &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 %2 失败：%3").arg(threeAxisAxisTitle(axisIndex), actionName, errorMessage));
        refreshThreeAxisUi();
        return;
    }
    if (!m_threeAxisGateway.setStep(signedSteps, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 %2 失败：%3").arg(threeAxisAxisTitle(axisIndex), actionName, errorMessage));
        refreshThreeAxisUi();
        return;
    }

    appendThreeAxisLog(QStringLiteral("%1：%2，当前位置 %3，目标 %4 => OK")
                           .arg(threeAxisAxisTitle(axisIndex))
                           .arg(actionName)
                           .arg(currentSteps)
                           .arg(projectedSteps));
    m_threeAxisSoftPositionSteps[static_cast<size_t>(axisIndex)] = projectedSteps;
    if (m_threeAxisSoftPositionLabels[static_cast<size_t>(axisIndex)] != nullptr) {
        setStableElidedText(m_threeAxisSoftPositionLabels[static_cast<size_t>(axisIndex)], threeAxisPositionText(axisIndex, projectedSteps));
    }
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::moveThreeAxisMotorToAbsolute(int axisIndex)
{
    if (m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("急停未解除，禁止运动"));
        return;
    }
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        setThreeAxisStatus(QStringLiteral("三电机轴索引无效"));
        return;
    }

    QDoubleSpinBox* targetSpin = m_threeAxisTargetPositionSpins[static_cast<size_t>(axisIndex)];
    if (targetSpin == nullptr) {
        return;
    }

    QString errorMessage;
    if (!selectThreeAxisNode(axisIndex, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }
    errorMessage.clear();
    if (!m_threeAxisGateway.refreshSnapshot(&errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 读取实际绝对位置失败：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }

    const diji::adapters::uim::UimMotorSnapshot snapshot = m_threeAxisGateway.latestSnapshot();
    if (!snapshot.hasPosition) {
        setThreeAxisStatus(QStringLiteral("%1 未读到实际绝对位置，已拒绝移动").arg(threeAxisAxisTitle(axisIndex)));
        refreshThreeAxisUi();
        return;
    }

    const int currentSteps = snapshot.position;
    m_threeAxisSoftPositionSteps[static_cast<size_t>(axisIndex)] = currentSteps;
    m_threeAxisPositionKnown[static_cast<size_t>(axisIndex)] = true;
    const double targetValue = targetSpin->value();
    const int targetSteps = threeAxisDisplayUnitsToSteps(axisIndex, targetValue);
    if (!threeAxisAbsoluteTargetAllowed(axisIndex, targetSteps, &errorMessage)) {
        setThreeAxisStatus(errorMessage);
        refreshThreeAxisUi();
        return;
    }

    const int deltaSteps = targetSteps - currentSteps;
    if (deltaSteps == 0) {
        setThreeAxisStatus(QStringLiteral("%1 已在当前位置 %2")
                               .arg(threeAxisAxisTitle(axisIndex), threeAxisPositionText(axisIndex, targetSteps)));
        refreshThreeAxisUi();
        return;
    }
    if (!threeAxisSensorLimitAllowsMove(axisIndex, deltaSteps, &errorMessage)) {
        setThreeAxisStatus(errorMessage);
        refreshThreeAxisUi();
        return;
    }

    if (std::abs(deltaSteps) >= kThreeAxisLargeMoveConfirmSteps) {
        appendThreeAxisLog(QStringLiteral("%1 大步绝对移动已通过范围和传感器检查，直接执行")
                               .arg(threeAxisAxisTitle(axisIndex)));
    }

    const QString actionName = QStringLiteral("移动到 %1（差值 %2 步）")
                                   .arg(threeAxisPositionText(axisIndex, targetSteps))
                                   .arg(deltaSteps);
    errorMessage.clear();
    if (!selectThreeAxisNode(axisIndex, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return;
    }
    if (!m_threeAxisGateway.setSpeed(threeAxisSpeedForAxis(axisIndex), &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 %2 失败：%3").arg(threeAxisAxisTitle(axisIndex), actionName, errorMessage));
        refreshThreeAxisUi();
        return;
    }
    if (!m_threeAxisGateway.setStep(deltaSteps, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 %2 失败：%3").arg(threeAxisAxisTitle(axisIndex), actionName, errorMessage));
        refreshThreeAxisUi();
        return;
    }

    appendThreeAxisLog(QStringLiteral("%1：%2，当前位置 %3 => OK")
                           .arg(threeAxisAxisTitle(axisIndex))
                           .arg(actionName)
                           .arg(threeAxisPositionText(axisIndex, currentSteps)));
    m_threeAxisSoftPositionSteps[static_cast<size_t>(axisIndex)] = targetSteps;
    if (m_threeAxisSoftPositionLabels[static_cast<size_t>(axisIndex)] != nullptr) {
        setStableElidedText(m_threeAxisSoftPositionLabels[static_cast<size_t>(axisIndex)], threeAxisPositionText(axisIndex, targetSteps));
    }
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
}

void DeviceMonitorPage::enableThreeAxisMotor(int axisIndex)
{
    if (m_threeAxisEmergencyStopActive) {
        setThreeAxisStatus(QStringLiteral("急停未解除，禁止上电"));
        return;
    }

    runThreeAxisCommand(axisIndex, QStringLiteral("上电"), [this](QString* error) {
        return m_threeAxisGateway.enableMotor(error);
    });
}

void DeviceMonitorPage::disableThreeAxisMotor(int axisIndex)
{
    runThreeAxisCommand(axisIndex, QStringLiteral("断电"), [this](QString* error) {
        return m_threeAxisGateway.disableMotor(error);
    });
}

bool DeviceMonitorPage::configureThreeAxisMotorParameters()
{
    bool issued = false;
    bool allOk = true;

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (!threeAxisNodeAvailable(index)) {
            appendThreeAxisLog(QStringLiteral("%1 参数初始化跳过：未发现节点").arg(threeAxisAxisTitle(index)));
            allOk = false;
            continue;
        }

        QString errorMessage;
        if (!selectThreeAxisNode(index, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 参数初始化选中失败：%2").arg(threeAxisAxisTitle(index), errorMessage));
            allOk = false;
            continue;
        }
        issued = true;

        const double currentAmps = kThreeAxisCurrentAmps.at(static_cast<size_t>(index));
        errorMessage.clear();
        if (!m_threeAxisGateway.setCurrentAmps(currentAmps, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 电流设置为 %2 A 失败：%3")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(currentAmps, 0, 'f', 1)
                                   .arg(errorMessage));
            allOk = false;
        } else {
            appendThreeAxisLog(QStringLiteral("%1 电流设置为 %2 A => OK")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(currentAmps, 0, 'f', 1));
        }

        errorMessage.clear();
        if (!m_threeAxisGateway.setSpeed(kThreeAxisDefaultSpeed, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 固定期望速度设置为 %2 失败：%3")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(kThreeAxisDefaultSpeed)
                                   .arg(errorMessage));
            allOk = false;
        } else {
            appendThreeAxisLog(QStringLiteral("%1 固定期望速度设置为 %2 => OK")
                                   .arg(threeAxisAxisTitle(index))
                                   .arg(kThreeAxisDefaultSpeed));
        }

        diji::adapters::uim::UimSensorActionConfig actionConfig {};
        errorMessage.clear();
        if (!m_threeAxisGateway.readSensorActionConfig(&actionConfig, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 读取 S1/S2 动作失败，将使用上升沿无动作继续写入：%2")
                                   .arg(threeAxisAxisTitle(index), errorMessage));
        }

        actionConfig.sensor1FallingAction = kThreeAxisSensorDecelerateToStopAction;
        actionConfig.sensor2FallingAction = kThreeAxisSensorDecelerateToStopAction;
        errorMessage.clear();
        if (!m_threeAxisGateway.setSensorActionConfig(actionConfig, &errorMessage)) {
            appendThreeAxisLog(QStringLiteral("%1 S1/S2 下降沿绑定“减速直到停止”失败：%2")
                                   .arg(threeAxisAxisTitle(index), errorMessage));
            allOk = false;
        } else {
            appendThreeAxisLog(QStringLiteral("%1 S1/S2 下降沿已绑定“减速直到停止”")
                                   .arg(threeAxisAxisTitle(index)));
        }
    }

    if (!issued) {
        setThreeAxisStatus(QStringLiteral("未发现 6/7/8 号电机节点，无法初始化电流、固定速度和传感器动作"));
        return false;
    }
    return allOk;
}

bool DeviceMonitorPage::cancelThreeAxisMotorMotion(int axisIndex, QString* errorMessage)
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("三电机轴索引无效");
        }
        return false;
    }

    QString localError;
    if (!selectThreeAxisNode(axisIndex, &localError)) {
        if (errorMessage != nullptr) {
            *errorMessage = localError;
        }
        return false;
    }

    localError.clear();
    if (!m_threeAxisGateway.setSpeed(0, &localError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("速度置 0 失败：%1").arg(localError);
        }
        return false;
    }

    localError.clear();
    if (!m_threeAxisGateway.setStep(0, &localError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("清除期望位移失败：%1").arg(localError);
        }
        return false;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

bool DeviceMonitorPage::runThreeAxisCommand(int axisIndex, const QString& action, const std::function<bool(QString*)>& command)
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        setThreeAxisStatus(QStringLiteral("三电机轴索引无效"));
        return false;
    }

    QString errorMessage;
    if (!selectThreeAxisNode(axisIndex, &errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1：%2").arg(threeAxisAxisTitle(axisIndex), errorMessage));
        refreshThreeAxisUi();
        return false;
    }

    if (!command(&errorMessage)) {
        setThreeAxisStatus(QStringLiteral("%1 %2 失败：%3").arg(threeAxisAxisTitle(axisIndex), action, errorMessage));
        refreshThreeAxisUi();
        return false;
    }

    appendThreeAxisLog(QStringLiteral("%1：%2 => OK").arg(threeAxisAxisTitle(axisIndex), action));
    refreshThreeAxisMotors();
    refreshThreeAxisUi();
    return true;
}

bool DeviceMonitorPage::selectThreeAxisNode(int axisIndex, QString* errorMessage)
{
    if (!m_threeAxisGateway.isGatewayOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("UIM 网关尚未打开");
        }
        return false;
    }
    if (!threeAxisNodeAvailable(axisIndex)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未发现节点 %1").arg(kThreeAxisNodeIds.at(static_cast<size_t>(axisIndex)));
        }
        return false;
    }
    return m_threeAxisGateway.selectNode(static_cast<quint32>(kThreeAxisNodeIds.at(static_cast<size_t>(axisIndex))), errorMessage);
}

bool DeviceMonitorPage::threeAxisNodeAvailable(int axisIndex) const
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        return false;
    }
    const int nodeId = kThreeAxisNodeIds.at(static_cast<size_t>(axisIndex));
    return std::any_of(m_threeAxisNodes.cbegin(), m_threeAxisNodes.cend(), [nodeId](const diji::adapters::uim::UimNodeInfo& node) {
        return static_cast<int>(node.nodeId) == nodeId;
    });
}

int DeviceMonitorPage::threeAxisMinimumSteps(int axisIndex) const
{
    if (axisIndex == 0) {
        return kThreeAxisAxis6MinimumSteps;
    }
    if (axisIndex == 1) {
        return kThreeAxisAxis7MinimumSteps;
    }
    if (axisIndex == 2) {
        return kThreeAxisAxis8MinimumSteps;
    }
    return 0;
}

int DeviceMonitorPage::threeAxisMaximumSteps(int axisIndex) const
{
    if (axisIndex == 0) {
        return kThreeAxisAxis6MaximumSteps;
    }
    if (axisIndex == 1) {
        return kThreeAxisAxis7MaximumSteps;
    }
    if (axisIndex == 2) {
        return kThreeAxisAxis8MaximumSteps;
    }
    return 0;
}

int DeviceMonitorPage::threeAxisMoveSteps(int axisIndex, double amount) const
{
    const double safeAmount = std::max(0.0, amount);
    if (axisIndex == 0) {
        return static_cast<int>(std::lround(safeAmount * kThreeAxisSwingStepsPerDegree));
    }
    return static_cast<int>(std::lround(safeAmount * kThreeAxisLinearStepsPerCentimeter));
}

double DeviceMonitorPage::threeAxisStepsToDisplayUnits(int axisIndex, int steps) const
{
    if (axisIndex == 0) {
        return static_cast<double>(steps) / kThreeAxisSwingStepsPerDegree;
    }
    return static_cast<double>(steps) / kThreeAxisLinearStepsPerCentimeter;
}

int DeviceMonitorPage::threeAxisDisplayUnitsToSteps(int axisIndex, double value) const
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        return 0;
    }
    const double stepsPerUnit = axisIndex == 0 ? kThreeAxisSwingStepsPerDegree : kThreeAxisLinearStepsPerCentimeter;
    const int steps = static_cast<int>(std::lround(value * stepsPerUnit));
    return std::max(threeAxisMinimumSteps(axisIndex), std::min(threeAxisMaximumSteps(axisIndex), steps));
}

double DeviceMonitorPage::threeAxisMinimumDisplayUnits(int axisIndex) const
{
    return threeAxisStepsToDisplayUnits(axisIndex, threeAxisMinimumSteps(axisIndex));
}

double DeviceMonitorPage::threeAxisMaximumDisplayUnits(int axisIndex) const
{
    return threeAxisStepsToDisplayUnits(axisIndex, threeAxisMaximumSteps(axisIndex));
}

int DeviceMonitorPage::threeAxisDisplayDecimals(int axisIndex) const
{
    if (axisIndex == 0) {
        return 2;
    }
    return 3;
}

int DeviceMonitorPage::threeAxisSpeedForAxis(int axisIndex) const
{
    Q_UNUSED(axisIndex);
    return kThreeAxisDefaultSpeed;
}

double DeviceMonitorPage::threeAxisJogAmountForAxis(int axisIndex) const
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(m_threeAxisJogDistanceSpins.size())) {
        return 0.0;
    }
    QDoubleSpinBox* jogSpin = m_threeAxisJogDistanceSpins[static_cast<size_t>(axisIndex)];
    return jogSpin != nullptr ? jogSpin->value() : 0.0;
}

QString DeviceMonitorPage::threeAxisJogActionTitle(int axisIndex, int direction) const
{
    if (axisIndex == 1) {
        return direction > 0 ? QStringLiteral("右移") : QStringLiteral("左移");
    }
    if (axisIndex == 2) {
        return direction > 0 ? QStringLiteral("下移") : QStringLiteral("上移");
    }
    return direction > 0 ? QStringLiteral("左摆") : QStringLiteral("右摆");
}

bool DeviceMonitorPage::threeAxisAbsoluteTargetAllowed(int axisIndex, int targetSteps, QString* errorMessage) const
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("三电机轴索引无效");
        }
        return false;
    }

    const int minimumSteps = threeAxisMinimumSteps(axisIndex);
    const int maximumSteps = threeAxisMaximumSteps(axisIndex);
    const int decimals = threeAxisDisplayDecimals(axisIndex);
    const QString rangeText = QStringLiteral("S2=%1 %2 到 S1=%3 %2（%4 到 %5 步）")
                                  .arg(threeAxisMinimumDisplayUnits(axisIndex), 0, 'f', decimals)
                                  .arg(threeAxisDisplayUnitText(axisIndex))
                                  .arg(threeAxisMaximumDisplayUnits(axisIndex), 0, 'f', decimals)
                                  .arg(minimumSteps)
                                  .arg(maximumSteps);
    if (targetSteps < minimumSteps || targetSteps > maximumSteps) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 目标位置 %2 超出安全范围：%3，已拒绝运动")
                                .arg(threeAxisAxisTitle(axisIndex))
                                .arg(threeAxisPositionText(axisIndex, targetSteps))
                                .arg(rangeText);
        }
        return false;
    }
    return true;
}

bool DeviceMonitorPage::threeAxisSensorLimitAllowsMove(int axisIndex, int deltaSteps, QString* errorMessage)
{
    if (deltaSteps == 0) {
        return true;
    }
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisNodeIds.size())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("三电机轴索引无效");
        }
        return false;
    }

    QString sensorError;
    if (!m_threeAxisGateway.refreshSensorFeedback(&sensorError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 传感器查询失败：%2，已拒绝运动")
                                .arg(threeAxisAxisTitle(axisIndex), sensorError);
        }
        return false;
    }

    const diji::adapters::uim::UimMotorSnapshot snapshot = m_threeAxisGateway.latestSnapshot();
    if (!snapshot.hasSensorFeedback) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 未取到 S1/S2 传感器状态，已拒绝运动")
                                .arg(threeAxisAxisTitle(axisIndex));
        }
        return false;
    }

    const bool movingTowardS1 = deltaSteps > 0;
    const bool movingTowardS2 = deltaSteps < 0;
    if (movingTowardS1 && !snapshot.sensor1) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 S1=0，当前位置已在 S1 限位，禁止继续向 S1 移动")
                                .arg(threeAxisAxisTitle(axisIndex));
        }
        appendThreeAxisLog(QStringLiteral("%1 限位保护：S1=0, S2=%2, S3=%3, delta=%4")
                               .arg(threeAxisAxisTitle(axisIndex))
                               .arg(snapshot.sensor2 ? 1 : 0)
                               .arg(snapshot.sensor3 ? 1 : 0)
                               .arg(deltaSteps));
        return false;
    }
    if (movingTowardS2 && !snapshot.sensor2) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 S2=0，当前位置已在 S2 限位，禁止继续向 S2 移动")
                                .arg(threeAxisAxisTitle(axisIndex));
        }
        appendThreeAxisLog(QStringLiteral("%1 限位保护：S1=%2, S2=0, S3=%3, delta=%4")
                               .arg(threeAxisAxisTitle(axisIndex))
                               .arg(snapshot.sensor1 ? 1 : 0)
                               .arg(snapshot.sensor3 ? 1 : 0)
                               .arg(deltaSteps));
        return false;
    }

    appendThreeAxisLog(QStringLiteral("%1 传感器检查通过：S1=%2, S2=%3, S3=%4")
                           .arg(threeAxisAxisTitle(axisIndex))
                           .arg(snapshot.sensor1 ? 1 : 0)
                           .arg(snapshot.sensor2 ? 1 : 0)
                           .arg(snapshot.sensor3 ? 1 : 0));
    return true;
}

QString DeviceMonitorPage::threeAxisDisplayUnitText(int axisIndex) const
{
    return axisIndex == 0 ? QStringLiteral("°") : QStringLiteral("cm");
}

QString DeviceMonitorPage::threeAxisPositionRangeText(int axisIndex) const
{
    const QString unitText = threeAxisDisplayUnitText(axisIndex);
    const int decimals = threeAxisDisplayDecimals(axisIndex);
    return QStringLiteral("%1-%2 %3")
        .arg(threeAxisMinimumDisplayUnits(axisIndex), 0, 'f', decimals)
        .arg(threeAxisMaximumDisplayUnits(axisIndex), 0, 'f', decimals)
        .arg(unitText);
}

QString DeviceMonitorPage::threeAxisPositionText(int axisIndex, int positionSteps) const
{
    const int decimals = threeAxisDisplayDecimals(axisIndex);
    return QStringLiteral("%1 %2（%3）")
        .arg(threeAxisStepsToDisplayUnits(axisIndex, positionSteps), 0, 'f', decimals)
        .arg(threeAxisDisplayUnitText(axisIndex), threeAxisPositionRangeText(axisIndex));
}

QString DeviceMonitorPage::threeAxisAxisTitle(int axisIndex) const
{
    if (axisIndex < 0 || axisIndex >= static_cast<int>(kThreeAxisTitles.size())) {
        return QStringLiteral("未知轴");
    }
    return QString::fromUtf8(kThreeAxisTitles.at(static_cast<size_t>(axisIndex)));
}

void DeviceMonitorPage::updateThreeAxisSnapshot(const diji::adapters::uim::UimMotorSnapshot& snapshot)
{
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        if (static_cast<int>(snapshot.nodeId) != kThreeAxisNodeIds.at(static_cast<size_t>(index))) {
            continue;
        }

        QLabel* nodeLabel = m_threeAxisNodeLabels[static_cast<size_t>(index)];
        if (nodeLabel != nullptr) {
            QStringList parts {
                QStringLiteral("节点 %1").arg(snapshot.nodeId),
                snapshot.enabled ? QStringLiteral("已上电") : QStringLiteral("已断电")
            };
            if (snapshot.hasSensorFeedback) {
                parts.push_back(QStringLiteral("S:%1%2%3")
                                    .arg(snapshot.sensor1 ? QStringLiteral("1") : QStringLiteral("-"))
                                    .arg(snapshot.sensor2 ? QStringLiteral("2") : QStringLiteral("-"))
                                    .arg(snapshot.sensor3 ? QStringLiteral("3") : QStringLiteral("-")));
            }
            setStableElidedText(nodeLabel, parts.join(QStringLiteral(" / ")));
        }
        if (snapshot.hasPosition) {
            m_threeAxisSoftPositionSteps[static_cast<size_t>(index)] = snapshot.position;
            m_threeAxisPositionKnown[static_cast<size_t>(index)] = true;
            QLabel* positionLabel = m_threeAxisSoftPositionLabels[static_cast<size_t>(index)];
            if (positionLabel != nullptr) {
                setStableElidedText(positionLabel, threeAxisPositionText(index, snapshot.position));
            }
        } else {
            m_threeAxisPositionKnown[static_cast<size_t>(index)] = false;
            QLabel* positionLabel = m_threeAxisSoftPositionLabels[static_cast<size_t>(index)];
            if (positionLabel != nullptr) {
                setStableElidedText(positionLabel, QStringLiteral("未查询"));
            }
        }
        break;
    }
    handleWaterTankLevelSensors(snapshot);
    refreshThreeAxisUi();
}

void DeviceMonitorPage::updateThreeAxisNodeStatus()
{
    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        QLabel* nodeLabel = m_threeAxisNodeLabels[static_cast<size_t>(index)];
        if (nodeLabel == nullptr) {
            continue;
        }
        const bool available = threeAxisNodeAvailable(index);
        setStableElidedText(nodeLabel,
            available
                ? QStringLiteral("节点 %1 已发现").arg(kThreeAxisNodeIds.at(static_cast<size_t>(index)))
                : QStringLiteral("未发现"));
        if (!available) {
            m_threeAxisPositionKnown[static_cast<size_t>(index)] = false;
            if (m_threeAxisSoftPositionLabels[static_cast<size_t>(index)] != nullptr) {
                setStableElidedText(m_threeAxisSoftPositionLabels[static_cast<size_t>(index)], QStringLiteral("未查询"));
            }
        }
    }
    refreshThreeAxisUi();
}

void DeviceMonitorPage::setThreeAxisStatus(const QString& message)
{
    appendThreeAxisLog(message);
}

void DeviceMonitorPage::appendThreeAxisLog(const QString& message)
{
    if (m_threeAxisLogEdit == nullptr || message.trimmed().isEmpty()) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_threeAxisLogEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void DeviceMonitorPage::refreshThreeAxisUi()
{
    const bool sdkLoaded = m_threeAxisGateway.isSdkLoaded();
    const bool gatewayOpen = m_threeAxisGateway.isGatewayOpen();
    const bool hasDevice = !m_threeAxisDevices.isEmpty();

    if (m_threeAxisSdkPathEdit != nullptr) {
        m_threeAxisSdkPathEdit->setEnabled(!gatewayOpen);
    }
    if (m_threeAxisLoadSdkButton != nullptr) {
        m_threeAxisLoadSdkButton->setEnabled(!gatewayOpen);
    }
    if (m_threeAxisSearchButton != nullptr) {
        m_threeAxisSearchButton->setEnabled(sdkLoaded && !gatewayOpen);
    }
    if (m_threeAxisDeviceCombo != nullptr) {
        m_threeAxisDeviceCombo->setEnabled(!gatewayOpen && hasDevice);
    }
    if (m_threeAxisGatewayButton != nullptr) {
        m_threeAxisGatewayButton->setText(gatewayOpen ? QStringLiteral("关闭") : QStringLiteral("打开"));
        m_threeAxisGatewayButton->setEnabled(gatewayOpen || hasDevice);
    }
    if (m_threeAxisEnableAllButton != nullptr) {
        m_threeAxisEnableAllButton->setEnabled(gatewayOpen && !m_threeAxisEmergencyStopActive);
    }
    if (m_threeAxisDisableAllButton != nullptr) {
        m_threeAxisDisableAllButton->setEnabled(gatewayOpen);
    }
    if (m_threeAxisEmergencyStopButton != nullptr) {
        m_threeAxisEmergencyStopButton->setEnabled(!m_threeAxisEmergencyStopActive);
    }
    if (m_threeAxisReleaseEmergencyStopButton != nullptr) {
        m_threeAxisReleaseEmergencyStopButton->setEnabled(m_threeAxisEmergencyStopActive);
    }

    for (int index = 0; index < static_cast<int>(kThreeAxisNodeIds.size()); ++index) {
        const bool available = gatewayOpen && threeAxisNodeAvailable(index);
        const bool canMove = available && !m_threeAxisEmergencyStopActive;
        const size_t arrayIndex = static_cast<size_t>(index);

        const bool positionKnown = m_threeAxisPositionKnown[arrayIndex];
        const int currentSteps = m_threeAxisSoftPositionSteps[arrayIndex];
        bool targetAllowed = false;
        QDoubleSpinBox* targetSpin = m_threeAxisTargetPositionSpins[arrayIndex];
        if (targetSpin != nullptr) {
            targetSpin->setEnabled(canMove);
            const double targetValue = targetSpin->value();
            const double displayTolerance = std::pow(10.0, -threeAxisDisplayDecimals(index)) * 0.5;
            targetAllowed = targetValue >= threeAxisMinimumDisplayUnits(index) - displayTolerance
                && targetValue <= threeAxisMaximumDisplayUnits(index) + displayTolerance;
            if (targetAllowed) {
                const int targetSteps = threeAxisDisplayUnitsToSteps(index, targetValue);
                targetAllowed = targetSteps >= threeAxisMinimumSteps(index)
                    && targetSteps <= threeAxisMaximumSteps(index);
            }
        }
        if (m_threeAxisMoveToButtons[arrayIndex] != nullptr) {
            m_threeAxisMoveToButtons[arrayIndex]->setEnabled(canMove && positionKnown && targetAllowed);
        }

        bool negativeAllowed = false;
        bool positiveAllowed = false;
        QDoubleSpinBox* jogSpin = m_threeAxisJogDistanceSpins[arrayIndex];
        if (jogSpin != nullptr) {
            jogSpin->setEnabled(canMove);
            const int jogSteps = threeAxisMoveSteps(index, jogSpin->value());
            if (positionKnown && jogSteps > 0) {
                negativeAllowed = threeAxisAbsoluteTargetAllowed(
                    index,
                    currentSteps + kThreeAxisNegativeButtonDirections.at(arrayIndex) * jogSteps,
                    nullptr);
                positiveAllowed = threeAxisAbsoluteTargetAllowed(
                    index,
                    currentSteps + kThreeAxisPositiveButtonDirections.at(arrayIndex) * jogSteps,
                    nullptr);
            }
        }
        if (m_threeAxisNegativeButtons[arrayIndex] != nullptr) {
            m_threeAxisNegativeButtons[arrayIndex]->setEnabled(canMove && negativeAllowed);
        }
        if (m_threeAxisPositiveButtons[arrayIndex] != nullptr) {
            m_threeAxisPositiveButtons[arrayIndex]->setEnabled(canMove && positiveAllowed);
        }
    }
}

QString DeviceMonitorPage::defaultThreeAxisSdkPath()
{
    const QStringList candidates {
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("UISimCanFunc.dll")),
        QStringLiteral("D:/PanSoftware/UIMDemo/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DIANJIDEMO2/build/mingw/apps/three_axis_motor/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DianJi/电机控制/UIMDemoNew/UIMDemo20170523/example/VC/UIMVCDemo/DLL/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DianJi/电机控制/UIMDemo20170523/example/VC/UIMVCDemo/DLL/UISimCanFunc.dll")
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return candidates.constFirst();
}

void DeviceMonitorPage::setRobotArmStatus(const QString& message)
{
    if (m_robotArmStatusLabel != nullptr) {
        m_robotArmStatusLabel->setText(message);
    }
    appendRobotArmLog(message);
    refreshRobotArmUi();
}

void DeviceMonitorPage::appendRobotArmLog(const QString& message)
{
    if (m_robotArmLogEdit == nullptr || message.trimmed().isEmpty()) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_robotArmLogEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void DeviceMonitorPage::logRobotArmCommand(
    const QString& action,
    const panthera::adapters::dobot::DobotCommandResult& result,
    const QString& commandError)
{
    appendRobotArmLog(QStringLiteral("%1 => %2").arg(action, commandResultSummary(result, commandError)));
}

void DeviceMonitorPage::refreshRobotArmUi()
{
    const bool connected = m_robotArmClient.isConnected();
    if ((!connected || !m_robotEnabled) && m_robotPhysicalDragMonitorEnabled) {
        resetRobotArmPhysicalDragButtonState();
    }
    if (m_robotArmHostEdit != nullptr) {
        m_robotArmHostEdit->setText(m_robotArmSettings.host);
        m_robotArmHostEdit->setEnabled(true);
    }
    if (m_robotArmEnableSwitch != nullptr) {
        m_robotArmEnableSwitch->setEnabled(connected);
    }
    if (m_robotArmDragSwitch != nullptr) {
        m_robotArmDragSwitch->setEnabled(connected && m_robotEnabled && !m_robotPhysicalDragMonitorEnabled);
    }
    if (m_robotArmPhysicalDragButtonSwitch != nullptr) {
        m_robotArmPhysicalDragButtonSwitch->setEnabled(connected && m_robotEnabled);
    }
    if (m_robotArmSafeOriginButton != nullptr) {
        m_robotArmSafeOriginButton->setEnabled(connected && m_robotEnabled);
    }
    if (m_robotArmZAxisAlignButton != nullptr) {
        m_robotArmZAxisAlignButton->setEnabled(connected && m_robotEnabled);
    }
    if (m_robotArmTrajectoryCombo != nullptr) {
        m_robotArmTrajectoryCombo->setEnabled(!m_robotTrajectoryFiles.isEmpty());
    }
    if (m_robotArmRefreshTrajectoriesButton != nullptr) {
        m_robotArmRefreshTrajectoriesButton->setEnabled(!m_robotArmSettings.host.trimmed().isEmpty() && !m_robotTrajectoryRefreshRunning);
    }
    if (m_robotArmReplayPresetButton != nullptr) {
        m_robotArmReplayPresetButton->setEnabled(connected && m_robotEnabled && !currentPresetTrajectoryName().isEmpty());
    }
    if (m_robotPhysicalPowerButtonSwitch != nullptr) {
        m_robotPhysicalPowerButtonSwitch->setEnabled(true);
    }
    syncRobotArmSwitches();
}

void DeviceMonitorPage::syncRobotArmSwitches()
{
    m_updatingRobotArmSwitches = true;
    if (m_robotArmConnectionSwitch != nullptr) {
        m_robotArmConnectionSwitch->setChecked(m_robotArmClient.isConnected());
        m_robotArmConnectionSwitch->setText(m_robotArmClient.isConnected() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    if (m_robotArmEnableSwitch != nullptr) {
        m_robotArmEnableSwitch->setChecked(m_robotEnabled);
        m_robotArmEnableSwitch->setText(m_robotEnabled ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    if (m_robotArmDragSwitch != nullptr) {
        m_robotArmDragSwitch->setChecked(m_robotArmDragging);
        m_robotArmDragSwitch->setText(m_robotArmDragging ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    if (m_robotArmPhysicalDragButtonSwitch != nullptr) {
        m_robotArmPhysicalDragButtonSwitch->setChecked(m_robotPhysicalDragMonitorEnabled);
        m_robotArmPhysicalDragButtonSwitch->setText(m_robotPhysicalDragMonitorEnabled ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    if (m_robotPhysicalPowerButtonSwitch != nullptr) {
        m_robotPhysicalPowerButtonSwitch->setChecked(m_robotPhysicalPowerSwitchOn);
        m_robotPhysicalPowerButtonSwitch->setText(m_robotPhysicalPowerSwitchOn ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    m_updatingRobotArmSwitches = false;
}

bool DeviceMonitorPage::prepareRobotPhysicalPowerMotorGateway(QString* errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (!m_threeAxisGateway.isSdkLoaded()) {
        const QString sdkPath = m_threeAxisSdkPathEdit != nullptr && !m_threeAxisSdkPathEdit->text().trimmed().isEmpty()
            ? m_threeAxisSdkPathEdit->text().trimmed()
            : defaultThreeAxisSdkPath();
        if (m_threeAxisSdkPathEdit != nullptr && m_threeAxisSdkPathEdit->text().trimmed().isEmpty()) {
            m_threeAxisSdkPathEdit->setText(sdkPath);
        }

        QString loadError;
        if (!m_threeAxisGateway.loadSdk(sdkPath, &loadError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("UIM SDK 加载失败：%1").arg(loadError);
            }
            appendThreeAxisLog(QStringLiteral("实体电源按钮准备失败：UIM SDK 加载失败：%1").arg(loadError));
            refreshThreeAxisUi();
            return false;
        }
        appendThreeAxisLog(QStringLiteral("实体电源按钮已加载 UIM SDK：%1").arg(sdkPath));
    }

    if (!m_threeAxisGateway.isGatewayOpen()) {
        if (m_threeAxisDevices.isEmpty()) {
            searchThreeAxisGateways();
        }
        if (m_threeAxisDevices.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("未搜索到 USB-CAN 网关");
            }
            return false;
        }

        QVariant selectedDeviceIndex = m_threeAxisDeviceCombo != nullptr ? m_threeAxisDeviceCombo->currentData() : QVariant();
        if (!selectedDeviceIndex.isValid()) {
            selectedDeviceIndex = m_threeAxisDevices.constFirst().deviceIndex;
        }

        QString openError;
        if (!m_threeAxisGateway.openGateway(selectedDeviceIndex.toUInt(), &openError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("UIM 网关打开失败：%1").arg(openError);
            }
            appendThreeAxisLog(QStringLiteral("实体电源按钮准备失败：UIM 网关打开失败：%1").arg(openError));
            refreshThreeAxisUi();
            return false;
        }

        m_threeAxisNodes = m_threeAxisGateway.nodes();
        appendThreeAxisLog(QStringLiteral("实体电源按钮已打开 UIM 网关：CAN 节点 %1 个").arg(m_threeAxisNodes.size()));
        updateThreeAxisNodeStatus();
    }

    if (!threeAxisNodeAvailable(0) || !threeAxisNodeAvailable(1)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未发现 6/7 号电机节点，无法控制 P4 输出");
        }
        refreshThreeAxisUi();
        return false;
    }

    return true;
}

bool DeviceMonitorPage::setRobotPhysicalPowerPins(bool node6High, bool node7High, const QString& action, QString* errorMessage)
{
    if (!prepareRobotPhysicalPowerMotorGateway(errorMessage)) {
        return false;
    }

    const quint32 previousNodeId = m_threeAxisGateway.selectedNodeId();
    const auto restorePreviousNode = [this, previousNodeId]() {
        if (previousNodeId == 0) {
            return;
        }
        const bool stillAvailable = std::any_of(
            m_threeAxisNodes.cbegin(),
            m_threeAxisNodes.cend(),
            [previousNodeId](const diji::adapters::uim::UimNodeInfo& node) {
                return node.nodeId == previousNodeId;
            });
        if (stillAvailable) {
            QString ignoredError;
            m_threeAxisGateway.selectNode(previousNodeId, &ignoredError);
        }
    };

    const auto writeP4 = [this, &action, errorMessage](int axisIndex, bool high) {
        QString localError;
        if (!selectThreeAxisNode(axisIndex, &localError)) {
            if (errorMessage != nullptr) {
                *errorMessage = localError;
            }
            return false;
        }
        if (!m_threeAxisGateway.setDigitalOutput(high, &localError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 P4=%2 失败：%3")
                                    .arg(threeAxisAxisTitle(axisIndex))
                                    .arg(high ? 1 : 0)
                                    .arg(localError);
            }
            return false;
        }

        bool readbackHigh = false;
        const bool readbackOk = m_threeAxisGateway.readDigitalOutput(&readbackHigh, &localError);
        if (readbackOk && readbackHigh != high) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 P4 写入后读回不一致：写入 %2，读回 %3")
                                    .arg(threeAxisAxisTitle(axisIndex))
                                    .arg(high ? 1 : 0)
                                    .arg(readbackHigh ? 1 : 0);
            }
            return false;
        }

        const QString message = readbackOk
            ? QStringLiteral("%1：%2 P4=%3，读回=%4")
                  .arg(action)
                  .arg(threeAxisAxisTitle(axisIndex))
                  .arg(high ? 1 : 0)
                  .arg(readbackHigh ? 1 : 0)
            : QStringLiteral("%1：%2 P4=%3，读回失败：%4")
                  .arg(action)
                  .arg(threeAxisAxisTitle(axisIndex))
                  .arg(high ? 1 : 0)
                  .arg(localError);
        appendRobotArmLog(message);
        appendThreeAxisLog(message);
        return true;
    };

    const auto writeWithGap = [writeP4](int axisIndex, bool high) {
        if (!writeP4(axisIndex, high)) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(40);
        return true;
    };

    bool ok = false;
    if (!node6High && node7High) {
        ok = writeWithGap(0, false) && writeP4(1, true);
    } else if (node6High && !node7High) {
        ok = writeWithGap(1, false) && writeP4(0, true);
    } else {
        ok = writeWithGap(0, node6High) && writeP4(1, node7High);
    }

    restorePreviousNode();
    refreshThreeAxisUi();
    return ok;
}

void DeviceMonitorPage::toggleRobotPhysicalPowerButton(bool checked)
{
    if (m_updatingRobotArmSwitches) {
        return;
    }

    const QString action = checked
        ? QStringLiteral("实体电源按钮点按（打开）")
        : QStringLiteral("实体电源按钮点按（关闭）");
    QString errorMessage;

    if (m_robotPhysicalPowerButtonSwitch != nullptr) {
        m_robotPhysicalPowerButtonSwitch->setEnabled(false);
    }

    if (!setRobotPhysicalPowerPins(true, false, QStringLiteral("%1：按下 10").arg(action), &errorMessage)) {
        setRobotArmStatus(QStringLiteral("%1失败：按下失败，%2").arg(action, errorMessage));
        syncRobotArmSwitches();
        if (m_robotPhysicalPowerButtonSwitch != nullptr) {
            m_robotPhysicalPowerButtonSwitch->setEnabled(true);
        }
        return;
    }

    QCoreApplication::processEvents();
    QThread::msleep(kRobotPhysicalPowerButtonHoldMs);

    if (!setRobotPhysicalPowerPins(false, true, QStringLiteral("%1：回位 01").arg(action), &errorMessage)) {
        setRobotArmStatus(QStringLiteral("%1失败：回位失败，%2").arg(action, errorMessage));
        syncRobotArmSwitches();
        if (m_robotPhysicalPowerButtonSwitch != nullptr) {
            m_robotPhysicalPowerButtonSwitch->setEnabled(true);
        }
        return;
    }

    m_robotPhysicalPowerSwitchOn = checked;
    setRobotArmStatus(QStringLiteral("%1完成：按下 10，保持 %2 ms，回位 01")
                          .arg(action)
                          .arg(kRobotPhysicalPowerButtonHoldMs));
    syncRobotArmSwitches();
    if (m_robotPhysicalPowerButtonSwitch != nullptr) {
        m_robotPhysicalPowerButtonSwitch->setEnabled(true);
    }
}

bool DeviceMonitorPage::sharedRs485Connected() const
{
    return m_waterPumpClient.isOpen()
        || m_temperatureClient.isOpen()
        || m_liquidLevelClient.isOpen();
}

void DeviceMonitorPage::closeSharedRs485Clients()
{
    m_temperatureRealtimeTimer.stop();
    if (m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
    }
    m_waterPumpClient.close();
    m_temperatureClient.close();
    m_liquidLevelClient.close();
}

void DeviceMonitorPage::refreshSharedRs485Ui()
{
    refreshWaterPumpUi();
    refreshTemperatureUi();
    refreshLiquidLevelUi();
}

void DeviceMonitorPage::setSharedRs485ConnectedStatus(const QString& source)
{
    const QString message = QStringLiteral("%1：485 总线已连接：%2 @ %3 bps；水泵 02/03，温控 04，液位 01。")
                                .arg(source, QString::fromLatin1(kSharedRs485PortName))
                                .arg(kSharedRs485BaudRate);
    m_liquidLevelAddress = panthera::adapters::liquidlevel::LiquidLevelModbusClient::kDefaultAddress;
    setWaterPumpStatus(message, true);
    setTemperatureStatus(message, true);
    setLiquidLevelStatus(message, true);
}

void DeviceMonitorPage::setSharedRs485DisconnectedStatus(const QString& source)
{
    if (m_tank2FillActive) {
        stopTank2Fill(false, QStringLiteral("485 总线断开"));
    }
    closeSharedRs485Clients();
    const QString message = QStringLiteral("%1：485 总线已断开：%2 @ %3 bps。")
                                .arg(source, QString::fromLatin1(kSharedRs485PortName))
                                .arg(kSharedRs485BaudRate);
    setWaterPumpStatus(message, false);
    setTemperatureStatus(message, false);
    setLiquidLevelStatus(message, false);
    refreshSharedRs485Ui();
}

bool DeviceMonitorPage::openSharedRs485ForWaterPump(QString* errorMessage)
{
    if (m_waterPumpClient.isOpen()) {
        return true;
    }

    m_temperatureRealtimeTimer.stop();
    if (m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
    }
    m_temperatureClient.close();
    m_liquidLevelClient.close();
    panthera::adapters::waterpump::WaterPumpSerialSettings settings;
    settings.portName = QString::fromLatin1(kSharedRs485PortName);
    settings.baudRate = kSharedRs485BaudRate;
    return m_waterPumpClient.open(settings, errorMessage);
}

bool DeviceMonitorPage::openSharedRs485ForTemperature(QString* errorMessage)
{
    if (m_temperatureClient.isOpen()) {
        return true;
    }

    m_waterPumpClient.close();
    m_liquidLevelClient.close();
    panthera::adapters::anthone::Lu926TemperatureSerialSettings settings;
    settings.portName = m_temperaturePortCombo != nullptr
        ? m_temperaturePortCombo->text().trimmed()
        : QString::fromLatin1(kSharedRs485PortName);
    bool baudOk = false;
    const int baudRate = m_temperatureBaudCombo != nullptr
        ? m_temperatureBaudCombo->text().trimmed().toInt(&baudOk)
        : kSharedRs485BaudRate;
    settings.baudRate = baudOk ? baudRate : kSharedRs485BaudRate;
    settings.responseTimeoutMs = 2000;
    return m_temperatureClient.open(settings, errorMessage);
}

bool DeviceMonitorPage::openSharedRs485ForLiquidLevel(QString* errorMessage)
{
    using LiquidLevelClient = panthera::adapters::liquidlevel::LiquidLevelModbusClient;

    if (m_liquidLevelClient.isOpen()) {
        return true;
    }

    m_waterPumpClient.close();
    m_temperatureRealtimeTimer.stop();
    if (m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
    }
    m_temperatureClient.close();
    panthera::adapters::liquidlevel::LiquidLevelSerialSettings settings;
    settings.portName = QString::fromLatin1(kSharedRs485PortName);
    settings.baudRate = kSharedRs485BaudRate;
    settings.responseTimeoutMs = 800;
    if (!m_liquidLevelClient.open(settings, errorMessage)) {
        m_liquidLevelAddress = LiquidLevelClient::kDefaultAddress;
        return false;
    }

    m_liquidLevelAddress = LiquidLevelClient::kDefaultAddress;
    return true;
}

void DeviceMonitorPage::refreshTemperatureSerialPorts()
{
    if (m_temperaturePortCombo == nullptr) {
        return;
    }

    m_temperaturePortCombo->setText(QString::fromLatin1(kSharedRs485PortName));
    if (m_temperatureBaudCombo != nullptr) {
        m_temperatureBaudCombo->setText(QString::number(kSharedRs485BaudRate));
    }
}

void DeviceMonitorPage::toggleTemperatureConnection()
{
    if (sharedRs485Connected()) {
        setSharedRs485DisconnectedStatus(QStringLiteral("温控"));
        return;
    }

    QString errorMessage;
    if (!openSharedRs485ForTemperature(&errorMessage)) {
        m_temperatureRealtimeTimer.stop();
        if (m_temperatureCurrentDisplay != nullptr) {
            m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
        }
        setTemperatureStatus(QStringLiteral("485连接失败：%1").arg(errorMessage), false);
        refreshSharedRs485Ui();
        return;
    }

    setSharedRs485ConnectedStatus(QStringLiteral("温控"));
    refreshSharedRs485Ui();
    if (m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("--.- \u2103"));
    }
    m_temperatureRealtimeTimer.start();
    setTemperatureStatus(QStringLiteral("485连接成功：%1, %2,8,N,1, RTS=%3, DTR=%4")
                             .arg(m_temperatureClient.portName())
                             .arg(m_temperatureClient.baudRate())
                             .arg(m_temperatureClient.requestToSend() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(m_temperatureClient.dataTerminalReady() ? QStringLiteral("true") : QStringLiteral("false")),
                         true);
}

bool DeviceMonitorPage::ensureTemperatureConnection()
{
    QString errorMessage;
    if (openSharedRs485ForTemperature(&errorMessage)) {
        refreshSharedRs485Ui();
        if (m_temperatureClient.isOpen()) {
            if (m_temperatureCurrentDisplay != nullptr && m_temperatureCurrentDisplay->text() == QStringLiteral("未连接")) {
                m_temperatureCurrentDisplay->setText(QStringLiteral("--.- \u2103"));
            }
            if (!m_temperatureRealtimeTimer.isActive()) {
                m_temperatureRealtimeTimer.start();
            }
        }
        return true;
    }

    m_temperatureRealtimeTimer.stop();
    if (m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
    }
    setTemperatureStatus(QStringLiteral("485连接失败：%1").arg(errorMessage), false);
    refreshSharedRs485Ui();
    return false;
}

void DeviceMonitorPage::setTemperatureStatus(const QString& message, bool ok)
{
    if (m_temperatureResultLabel == nullptr) {
        return;
    }

    m_temperatureResultLabel->setText(message);
    m_temperatureResultLabel->setStyleSheet(ok
        ? QStringLiteral(
              "QLabel { padding: 14px 16px; border: 1px solid #1e5d91; border-radius: 8px; "
              "background: #0e2943; color: #ffffff; font-weight: 600; }")
        : QStringLiteral(
              "QLabel { padding: 14px 16px; border: 1px solid #d94a4a; border-radius: 8px; "
              "background: #3a1014; color: #ffd7d7; font-weight: 700; }"));
}

void DeviceMonitorPage::refreshTemperatureUi()
{
    const bool connected = sharedRs485Connected();
    if (m_temperaturePortCombo != nullptr) {
        m_temperaturePortCombo->setEnabled(true);
    }
    if (m_temperatureBaudCombo != nullptr) {
        m_temperatureBaudCombo->setEnabled(true);
    }
    if (m_temperatureRefreshPortsButton != nullptr) {
        m_temperatureRefreshPortsButton->setEnabled(false);
    }
    if (m_temperatureConnectionButton != nullptr) {
        m_temperatureConnectionButton->setText(connected ? QStringLiteral("断开485") : QStringLiteral("连接485"));
    }
    if (m_temperatureChannelCombo != nullptr) {
        if (m_temperatureChannelCombo->count() != 1 || m_temperatureChannelCombo->itemText(0) != QStringLiteral("CH1")) {
            m_temperatureChannelCombo->clear();
            m_temperatureChannelCombo->addItem(QStringLiteral("CH1"), 1);
        }
        m_temperatureChannelCombo->setCurrentIndex(0);
        m_temperatureChannelCombo->setEditable(false);
        m_temperatureChannelCombo->setEnabled(false);
    }
    if (m_temperatureSetButton != nullptr) {
        m_temperatureSetButton->setEnabled(connected);
    }
    if (m_temperatureReadButton != nullptr) {
        m_temperatureReadButton->setEnabled(connected);
    }
}

void DeviceMonitorPage::setTemperatureSetpoint()
{
    if (!ensureTemperatureConnection() || m_temperatureSetpointSpin == nullptr) {
        return;
    }

    const double celsius = m_temperatureSetpointSpin->value();
    if (!std::isfinite(celsius)) {
        setTemperatureStatus(QStringLiteral("设定温度失败：温度输入无效"), false);
        return;
    }

    if (m_temperatureRequestBusy) {
        setTemperatureStatus(temperatureFailureText(QStringLiteral("设定温度"), QStringLiteral("温控串口忙")), false);
        return;
    }
    ScopedBusyFlag busyGuard(m_temperatureRequestBusy);

    const QByteArray request = panthera::adapters::anthone::Lu926TemperatureModbusClient::buildWriteSet1Frame(celsius);
    QString errorMessage;
    QByteArray response;
    if (!m_temperatureClient.setChannel1Setpoint(celsius, &errorMessage, &response)) {
        QStringList lines = m_temperatureClient.lastDebugLog();
        if (lines.isEmpty()) {
            lines = {
                QStringLiteral("TX len=%1").arg(request.size()),
                QStringLiteral("TX: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(request))
            };
        }
        lines.push_back(temperatureFailureText(QStringLiteral("设定温度"), errorMessage));
        setTemperatureStatus(lines.join(QLatin1Char('\n')), false);
        return;
    }

    QStringList lines {
        QStringLiteral("TX: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(request)),
        QStringLiteral("RX: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(response)),
        QStringLiteral("CH1 设定温度 SET1 写入成功：%1 °C").arg(celsius, 0, 'f', 1)
    };

    const QByteArray readbackRequest = panthera::adapters::anthone::Lu926TemperatureModbusClient::buildReadSet1Frame();
    QByteArray readbackResponse;
    double confirmedCelsius = 0.0;
    if (!m_temperatureClient.readChannel1Setpoint(&confirmedCelsius, &errorMessage, &readbackResponse)) {
        lines.push_back(QStringLiteral("TX: %1").arg(
            panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(readbackRequest)));
        if (!readbackResponse.isEmpty()) {
            lines.push_back(QStringLiteral("RX: %1").arg(
                panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(readbackResponse)));
        }
        lines.push_back(temperatureFailureText(QStringLiteral("SET1 读回确认"), errorMessage));
        setTemperatureStatus(lines.join(QLatin1Char('\n')), false);
        return;
    }

    lines.push_back(QStringLiteral("TX: %1").arg(
        panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(readbackRequest)));
    lines.push_back(QStringLiteral("RX: %1").arg(
        panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(readbackResponse)));
    lines.push_back(QStringLiteral("CH1 设定温度 SET1 读回确认：%1 °C").arg(confirmedCelsius, 0, 'f', 1));
    setTemperatureStatus(lines.join(QLatin1Char('\n')), true);
}

bool DeviceMonitorPage::readCurrentPv1Temperature(double& temperature, QString& errorMessage, bool verboseLog)
{
    const QByteArray request = panthera::adapters::anthone::Lu926TemperatureModbusClient::buildReadPv1Frame();

    if (m_temperatureRequestBusy) {
        errorMessage = QStringLiteral("温控串口忙");
        if (verboseLog) {
            setTemperatureStatus(temperatureFailureText(QStringLiteral("读取温度"), errorMessage), false);
        }
        return false;
    }

    ScopedBusyFlag busyGuard(m_temperatureRequestBusy);

    if (!m_temperatureClient.isOpen()) {
        errorMessage = QStringLiteral("温控串口未连接");
        if (verboseLog) {
            setTemperatureStatus(temperatureFailureText(QStringLiteral("读取温度"), errorMessage), false);
        }
        return false;
    }

    QByteArray response;
    if (!m_temperatureClient.readChannel1Temperature(&temperature, &errorMessage, &response)) {
        if (verboseLog) {
            QStringList lines = m_temperatureClient.lastDebugLog();
            if (lines.isEmpty()) {
                lines = {
                    QStringLiteral("TX len=%1").arg(request.size()),
                    QStringLiteral("TX: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(request))
                };
            }
            lines.push_back(temperatureFailureText(QStringLiteral("读取温度"), errorMessage));
            setTemperatureStatus(lines.join(QLatin1Char('\n')), false);
        }
        return false;
    }

    if (verboseLog) {
        const QStringList lines {
            QStringLiteral("TX len=%1").arg(request.size()),
            QStringLiteral("TX: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(request)),
            QStringLiteral("RX total len=%1").arg(response.size()),
            QStringLiteral("RX total: %1").arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(response)),
            QStringLiteral("CH1 当前温度 PV1：%1 °C").arg(temperature, 0, 'f', 1)
        };
        setTemperatureStatus(lines.join(QLatin1Char('\n')), true);
    }
    return true;
}

void DeviceMonitorPage::updateRealtimeTemperature()
{
    if (m_temperatureCurrentDisplay == nullptr) {
        return;
    }

    if (!m_temperatureClient.isOpen()) {
        m_temperatureRealtimeTimer.stop();
        m_temperatureCurrentDisplay->setText(QStringLiteral("未连接"));
        return;
    }

    double celsius = 0.0;
    QString errorMessage;
    if (readCurrentPv1Temperature(celsius, errorMessage, false)) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("%1 \u2103").arg(celsius, 0, 'f', 1));
        return;
    }

    if (errorMessage == QStringLiteral("温控串口忙")) {
        return;
    }
    m_temperatureCurrentDisplay->setText(QStringLiteral("读取失败"));
}

void DeviceMonitorPage::readTemperatureValue()
{
    if (!ensureTemperatureConnection()) {
        return;
    }

    double celsius = 0.0;
    QString errorMessage;
    if (readCurrentPv1Temperature(celsius, errorMessage, true) && m_temperatureCurrentDisplay != nullptr) {
        m_temperatureCurrentDisplay->setText(QStringLiteral("%1 \u2103").arg(celsius, 0, 'f', 1));
    }
}

void DeviceMonitorPage::refreshLiquidLevelSerialPorts()
{
    if (m_liquidLevelPortCombo == nullptr) {
        return;
    }

    m_liquidLevelPortCombo->setText(QString::fromLatin1(kSharedRs485PortName));
    if (m_liquidLevelBaudCombo != nullptr) {
        m_liquidLevelBaudCombo->setText(QString::number(kSharedRs485BaudRate));
    }
}

void DeviceMonitorPage::toggleLiquidLevelConnection()
{
    using LiquidLevelClient = panthera::adapters::liquidlevel::LiquidLevelModbusClient;

    if (sharedRs485Connected()) {
        m_liquidLevelAddress = LiquidLevelClient::kDefaultAddress;
        setSharedRs485DisconnectedStatus(QStringLiteral("液位"));
        return;
    }

    QString errorMessage;
    if (!openSharedRs485ForLiquidLevel(&errorMessage)) {
        m_liquidLevelAddress = LiquidLevelClient::kDefaultAddress;
        setLiquidLevelStatus(QStringLiteral("液位传感器 485 连接失败：%1").arg(errorMessage), false);
        refreshSharedRs485Ui();
        return;
    }

    m_liquidLevelAddress = LiquidLevelClient::kDefaultAddress;
    setSharedRs485ConnectedStatus(QStringLiteral("液位"));
    refreshSharedRs485Ui();
}

bool DeviceMonitorPage::ensureLiquidLevelConnection()
{
    QString errorMessage;
    if (openSharedRs485ForLiquidLevel(&errorMessage)) {
        refreshSharedRs485Ui();
        return true;
    }

    setLiquidLevelStatus(QStringLiteral("液位传感器 485 连接失败：%1").arg(errorMessage), false);
    refreshSharedRs485Ui();
    return false;
}

void DeviceMonitorPage::setLiquidLevelStatus(const QString& message, bool ok)
{
    if (m_liquidLevelResultLabel == nullptr) {
        return;
    }

    m_liquidLevelResultLabel->setText(message);
    m_liquidLevelResultLabel->setStyleSheet(ok
        ? QStringLiteral(
              "QLabel { padding: 14px 16px; border: 1px solid #1e5d91; border-radius: 8px; "
              "background: #0e2943; color: #ffffff; font-weight: 600; }")
        : QStringLiteral(
              "QLabel { padding: 14px 16px; border: 1px solid #d94a4a; border-radius: 8px; "
              "background: #3a1014; color: #ffd7d7; font-weight: 700; }"));
}

void DeviceMonitorPage::refreshLiquidLevelUi()
{
    const bool connected = sharedRs485Connected();
    if (m_liquidLevelPortCombo != nullptr) {
        m_liquidLevelPortCombo->setEnabled(true);
    }
    if (m_liquidLevelBaudCombo != nullptr) {
        m_liquidLevelBaudCombo->setEnabled(true);
    }
    if (m_liquidLevelRefreshPortsButton != nullptr) {
        m_liquidLevelRefreshPortsButton->setEnabled(false);
    }
    if (m_liquidLevelConnectionButton != nullptr) {
        m_liquidLevelConnectionButton->setText(connected ? QStringLiteral("断开485") : QStringLiteral("连接485"));
    }
    if (m_liquidLevelAddressEdit != nullptr) {
        m_liquidLevelAddressEdit->setEnabled(true);
        m_liquidLevelAddressEdit->setText(QStringLiteral("%1").arg(static_cast<int>(m_liquidLevelAddress), 2, 10, QLatin1Char('0')));
    }
    if (m_liquidLevelReadButton != nullptr) {
        m_liquidLevelReadButton->setEnabled(connected);
    }
}

void DeviceMonitorPage::readLiquidLevelValue()
{
    using LiquidLevelClient = panthera::adapters::liquidlevel::LiquidLevelModbusClient;

    if (!ensureLiquidLevelConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    double centimeters = 0.0;
    if (!m_liquidLevelClient.readLevelMillimeters(
            m_liquidLevelAddress,
            &centimeters,
            &errorMessage,
            &response)) {
        setLiquidLevelStatus(QStringLiteral("读取液位失败：%1（%2 @ %3 bps）")
                                 .arg(errorMessage)
                                 .arg(m_liquidLevelClient.portName())
                                 .arg(m_liquidLevelClient.baudRate()),
                             false);
        return;
    }

    const QByteArray readFrame = LiquidLevelClient::buildReadLevelFrame(m_liquidLevelAddress);
    setLiquidLevelStatus(QStringLiteral("当前液位：%1 cm\n地址：%2\n发送 %3 字节：%4\n响应 %5 字节：%6")
                             .arg(centimeters, 0, 'f', 1)
                             .arg(static_cast<int>(m_liquidLevelAddress), 2, 10, QLatin1Char('0'))
                             .arg(readFrame.size())
                             .arg(LiquidLevelClient::frameToHex(readFrame))
                             .arg(response.size())
                             .arg(LiquidLevelClient::frameToHex(response)),
                         true);
}

void DeviceMonitorPage::handleWaterTankLevelSensors(const diji::adapters::uim::UimMotorSnapshot& snapshot)
{
    if (!snapshot.hasSensorFeedback) {
        return;
    }

    const int nodeId = static_cast<int>(snapshot.nodeId);
    if (nodeId == kThreeAxisNodeIds.at(static_cast<size_t>(kWaterTankHighLevelAxisIndex))) {
        m_waterTankHighLevelKnown = true;
        const bool highLevelActive = motorSensorTriggered(snapshot, kWaterTankUpperLimitSensorIndex);
        m_waterTankHighLevelActive =
            debouncedWaterTankLimitActive(this, "waterTankHighLevelActiveSamples", highLevelActive);
        updateWaterTankLimitStatus();
        return;
    }

    if (nodeId != kThreeAxisNodeIds.at(static_cast<size_t>(kWaterTankLowLevelAxisIndex))) {
        return;
    }

    m_waterTankLowLevelKnown = true;
    const bool lowLevelActive = motorSensorTriggered(snapshot, kWaterTankLowerLimitSensorIndex);
    m_waterTankLowLevelActive =
        debouncedWaterTankLimitActive(this, "waterTankLowLevelActiveSamples", lowLevelActive);
    if (m_waterTankLowLevelActive) {
        if (!m_waterTankLowLevelAlarmLatched) {
            triggerWaterTankLowLevelAlarm();
        }
    } else if (!lowLevelActive && m_waterTankLowLevelAlarmLatched) {
        m_waterTankLowLevelAlarmLatched = false;
        appendThreeAxisLog(QStringLiteral("水箱下限位已恢复：7号电机 S3 下限位传感器恢复正常"));
    }
    updateWaterTankLimitStatus();
}

void DeviceMonitorPage::updateWaterTankLimitStatus()
{
    if (m_waterTankLimitStatusLabel == nullptr) {
        return;
    }

    const QString highText = m_waterTankHighLevelKnown
        ? (m_waterTankHighLevelActive ? QStringLiteral("已触发") : QStringLiteral("正常"))
        : QStringLiteral("未读取");
    const QString lowText = m_waterTankLowLevelKnown
        ? (m_waterTankLowLevelActive ? QStringLiteral("报警") : QStringLiteral("正常"))
        : QStringLiteral("未读取");

    const QString prefix = m_waterTankLowLevelActive
        ? QStringLiteral("水箱下限位报警：液位过低")
        : QStringLiteral("水箱限位保护");
    m_waterTankLimitStatusLabel->setText(QStringLiteral("%1\n6号 S3 上限位：%2\n7号 S3 下限位：%3")
                                             .arg(prefix, highText, lowText));
    m_waterTankLimitStatusLabel->setStyleSheet(statusPanelStyle(m_waterTankLowLevelActive));
}

void DeviceMonitorPage::triggerWaterTankLowLevelAlarm()
{
    m_waterTankLowLevelAlarmLatched = true;

    const QString temperatureResult = setTemperatureSetpointsToZeroForWaterTankAlarm();
    const QString message = QStringLiteral("水箱下限位报警：液位过低。\n%1")
                                .arg(temperatureResult);
    appendThreeAxisLog(message);
    updateWaterTankLimitStatus();
    QMessageBox::critical(this, QStringLiteral("水箱下限位报警"), message);
}

QString DeviceMonitorPage::setTemperatureSetpointsToZeroForWaterTankAlarm()
{
    if (m_temperatureSetpointSpin != nullptr) {
        m_temperatureSetpointSpin->setValue(0.0);
    }

    if (!ensureTemperatureConnection()) {
        const QString message = QStringLiteral("温控 485 未连接，无法自动将加热棒设定为 0°C。");
        setTemperatureStatus(QStringLiteral("水箱下限位报警：%1").arg(message), false);
        return message;
    }

    if (m_temperatureRequestBusy) {
        const QString message = QStringLiteral("温控串口忙，无法自动将加热棒设定为 0°C。");
        setTemperatureStatus(QStringLiteral("水箱下限位报警：%1").arg(message), false);
        return message;
    }
    ScopedBusyFlag busyGuard(m_temperatureRequestBusy);

    QString errorMessage;
    QByteArray response;
    if (m_temperatureClient.setChannel1Setpoint(0.0, &errorMessage, &response)) {
        const QString message = QStringLiteral("已将温控 CH1 设定为 0°C。");
        setTemperatureStatus(QStringLiteral("水箱下限位报警：%1").arg(message), false);
        return message;
    }

    const QString message = QStringLiteral("温控 CH1 设定 0°C 失败：%1").arg(errorMessage);
    setTemperatureStatus(QStringLiteral("水箱下限位报警：%1").arg(message), false);
    return message;
}

void DeviceMonitorPage::refreshWaterPumpSerialPorts()
{
    if (m_waterPumpPortCombo == nullptr) {
        return;
    }

    m_waterPumpPortCombo->setText(QString::fromLatin1(kSharedRs485PortName));
    if (m_waterPumpBaudCombo != nullptr) {
        m_waterPumpBaudCombo->setText(QString::number(kSharedRs485BaudRate));
    }
}

void DeviceMonitorPage::toggleWaterPumpConnection()
{
    if (sharedRs485Connected()) {
        setSharedRs485DisconnectedStatus(QStringLiteral("水泵"));
        return;
    }

    QString errorMessage;
    if (!openSharedRs485ForWaterPump(&errorMessage)) {
        setWaterPumpStatus(QStringLiteral("水泵 485 连接失败：%1").arg(errorMessage), false);
        refreshSharedRs485Ui();
        return;
    }

    setSharedRs485ConnectedStatus(QStringLiteral("水泵"));
    refreshSharedRs485Ui();
}

quint8 DeviceMonitorPage::waterPumpAddress(int pumpIndex) const
{
    return pumpIndex == 0
        ? panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress
        : panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress;
}

QString DeviceMonitorPage::waterPumpName(int pumpIndex) const
{
    return pumpIndex == 0 ? QStringLiteral("抽入水泵") : QStringLiteral("抽回水泵");
}

QString DeviceMonitorPage::waterPumpAddressText(int pumpIndex) const
{
    return QStringLiteral("%1").arg(static_cast<int>(waterPumpAddress(pumpIndex)), 2, 10, QLatin1Char('0'));
}

bool DeviceMonitorPage::ensureWaterPumpConnection()
{
    QString errorMessage;
    if (openSharedRs485ForWaterPump(&errorMessage)) {
        refreshSharedRs485Ui();
        return true;
    }

    setWaterPumpStatus(QStringLiteral("水泵 485 连接失败：%1").arg(errorMessage), false);
    refreshSharedRs485Ui();
    return false;
}

void DeviceMonitorPage::setWaterPumpStatus(const QString& message, bool ok)
{
    if (m_waterPumpLogEdit == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_waterPumpLogEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
    m_waterPumpLogEdit->ensureCursorVisible();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!ok
        && waterPumpStatusNeedsModalAlert(message)
        && nowMs - m_lastWaterPumpAlertTimestampMs > 15000) {
        m_lastWaterPumpAlertTimestampMs = nowMs;
        m_lastWaterPumpAlertMessage = message;
        QMessageBox::warning(this, QStringLiteral("水循环异常"), message);
    }
}

void DeviceMonitorPage::refreshWaterPumpUi()
{
    const bool connected = sharedRs485Connected();
    if (m_waterPumpPortCombo != nullptr) {
        m_waterPumpPortCombo->setEnabled(true);
    }
    if (m_waterPumpBaudCombo != nullptr) {
        m_waterPumpBaudCombo->setEnabled(true);
    }
    if (m_waterPumpRefreshPortsButton != nullptr) {
        m_waterPumpRefreshPortsButton->setEnabled(false);
    }
    if (m_waterPumpConnectionButton != nullptr) {
        m_waterPumpConnectionButton->setText(connected ? QStringLiteral("断开485") : QStringLiteral("连接485"));
    }
    for (QWidget* widget : m_waterPumpCommandWidgets) {
        if (widget != nullptr) {
            widget->setEnabled(connected);
        }
    }
    if (m_tank2FillTargetLevelSpin != nullptr) {
        m_tank2FillTargetLevelSpin->setEnabled(!m_tank2FillActive);
    }
    if (m_tank2FillButton != nullptr) {
        m_tank2FillButton->setEnabled(connected);
        m_tank2FillButton->setText(m_tank2FillActive ? QStringLiteral("停止上水") : QStringLiteral("上水"));
    }
}

void DeviceMonitorPage::setWaterLoopFlow()
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QStringList responses;
    QStringList failures;
    if (!applySharedWaterPumpFlow(&responses, &failures)) {
        setWaterPumpStatus(QStringLiteral("02/03 流速未全部设置成功：%1").arg(failures.join(QStringLiteral("；"))), false);
        return;
    }

    setWaterPumpStatus(QStringLiteral("02/03 流速已同步：%1 mL/min；%2")
                           .arg(m_waterPumpFlowSpin != nullptr ? m_waterPumpFlowSpin->value() : 0.0, 0, 'f', 1)
                           .arg(responses.join(QStringLiteral("；"))),
                       true);
}

bool DeviceMonitorPage::applySharedWaterPumpFlow(QStringList* responses, QStringList* failures)
{
    if (m_waterPumpFlowSpin == nullptr) {
        if (failures != nullptr) {
            failures->push_back(QStringLiteral("流速输入框未初始化"));
        }
        return false;
    }

    const double flow = m_waterPumpFlowSpin->value();
    bool allOk = true;
    for (int index = 0; index < 2; ++index) {
        QString responseText;
        QString errorMessage;
        if (setWaterPumpFlowValue(index, flow, &responseText, &errorMessage)) {
            if (responses != nullptr) {
                responses->push_back(responseText);
            }
        } else {
            allOk = false;
            if (failures != nullptr) {
                failures->push_back(QStringLiteral("%1（%2）：%3")
                                        .arg(waterPumpName(index), waterPumpAddressText(index), errorMessage));
            }
        }
    }
    return allOk;
}

bool DeviceMonitorPage::setWaterPumpFlowValue(int pumpIndex, double flow, QString* responseText, QString* errorMessage)
{
    QString commandError;
    QByteArray response;
    if (!m_waterPumpClient.setFlowMlPerMin(waterPumpAddress(pumpIndex), flow, &commandError, &response)) {
        if (errorMessage != nullptr) {
            *errorMessage = commandError;
        }
        return false;
    }

    if (responseText != nullptr) {
        *responseText = QStringLiteral("%1 %2")
                            .arg(waterPumpAddressText(pumpIndex),
                                 panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response));
    }
    return true;
}

void DeviceMonitorPage::setWaterPumpFlow(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }
    QDoubleSpinBox* flowSpin = m_waterPumpFlowSpins[static_cast<size_t>(pumpIndex)];
    if (flowSpin == nullptr) {
        flowSpin = m_waterPumpFlowSpin;
    }
    if (flowSpin == nullptr) {
        return;
    }

    const quint8 address = waterPumpAddress(pumpIndex);
    const double flow = flowSpin->value();
    QString errorMessage;
    QByteArray response;
    if (!m_waterPumpClient.setFlowMlPerMin(address, flow, &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）流速设置失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }

    setWaterPumpStatus(QStringLiteral("%1（%2）流速设置成功：%3 mL/min，响应 %4")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex))
                           .arg(flow, 0, 'f', 1)
                           .arg(panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response)),
                       true);
}

void DeviceMonitorPage::readWaterPumpFlow(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    const quint8 address = waterPumpAddress(pumpIndex);
    QString errorMessage;
    QByteArray response;
    double flow = 0.0;
    if (!m_waterPumpClient.readFlowMlPerMin(address, &flow, &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）流速读取失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }

    if (m_waterPumpFlowSpins[static_cast<size_t>(pumpIndex)] != nullptr) {
        m_waterPumpFlowSpins[static_cast<size_t>(pumpIndex)]->setValue(flow);
    }
    setWaterPumpStatus(QStringLiteral("%1（%2）当前设定流速：%3 mL/min，响应 %4")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex))
                           .arg(flow, 0, 'f', 1)
                           .arg(panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response)),
                       true);
}

void DeviceMonitorPage::setWaterPumpRunDuration(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }
    QSpinBox* durationSpin = m_waterPumpRunDurationSpins[static_cast<size_t>(pumpIndex)];
    if (durationSpin == nullptr) {
        return;
    }

    const quint8 address = waterPumpAddress(pumpIndex);
    const int seconds = durationSpin->value();
    QString errorMessage;
    QByteArray response;
    if (!m_waterPumpClient.setRunDurationSeconds(address, seconds, &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）运行时长设置失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }

    const QString durationText = seconds == 0 ? QStringLiteral("一直运行") : QStringLiteral("%1 s").arg(seconds);
    setWaterPumpStatus(QStringLiteral("%1（%2）运行时长设置成功：%3，响应 %4")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), durationText,
                                panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response)),
                       true);
}

void DeviceMonitorPage::readWaterPumpConfiguredRunDuration(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    const quint8 address = waterPumpAddress(pumpIndex);
    QString errorMessage;
    QByteArray response;
    double seconds = 0.0;
    if (!m_waterPumpClient.readConfiguredRunDurationSeconds(address, &seconds, &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）设定时长读取失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }

    if (m_waterPumpRunDurationSpins[static_cast<size_t>(pumpIndex)] != nullptr) {
        m_waterPumpRunDurationSpins[static_cast<size_t>(pumpIndex)]->setValue(static_cast<int>(std::round(seconds)));
    }
    const QString durationText = seconds <= 0.0 ? QStringLiteral("一直运行") : QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
    setWaterPumpStatus(QStringLiteral("%1（%2）设定运行时长：%3，响应 %4")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), durationText,
                                panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response)),
                       true);
}

void DeviceMonitorPage::readWaterPumpRealtimeRunDuration(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    const quint8 address = waterPumpAddress(pumpIndex);
    QString errorMessage;
    QByteArray response;
    double seconds = 0.0;
    if (!m_waterPumpClient.readRealtimeRunDurationSeconds(address, &seconds, &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）实时运行时长读取失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }

    setWaterPumpStatus(QStringLiteral("%1（%2）实时运行时长：%3 s，响应 %4")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex))
                           .arg(seconds, 0, 'f', 1)
                           .arg(panthera::adapters::waterpump::WaterPumpModbusClient::frameToHex(response)),
                       true);
}

void DeviceMonitorPage::startWaterPump(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!m_waterPumpClient.startPump(waterPumpAddress(pumpIndex), &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）启动失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("%1（%2）启动成功，响应 %3")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex),
                                waterPumpResponseText(response)),
                       true);
}

void DeviceMonitorPage::stopWaterPump(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!m_waterPumpClient.stopPump(waterPumpAddress(pumpIndex), &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）停止失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("%1（%2）停止成功，响应 %3")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex),
                                waterPumpResponseText(response)),
                       true);
}

void DeviceMonitorPage::setWaterPumpClockwise(int pumpIndex)
{
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!m_waterPumpClient.setClockwise(waterPumpAddress(pumpIndex), &errorMessage, &response)) {
        setWaterPumpStatus(QStringLiteral("%1（%2）顺时针设置失败：%3")
                               .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex), errorMessage),
                           false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("%1（%2）顺时针设置成功，响应 %3")
                           .arg(waterPumpName(pumpIndex), waterPumpAddressText(pumpIndex),
                                waterPumpResponseText(response)),
                       true);
}

void DeviceMonitorPage::toggleTank2Fill()
{
    if (m_tank2FillActive) {
        stopTank2Fill(false, QStringLiteral("手动停止"));
        return;
    }
    if (m_tank2FillTargetLevelSpin == nullptr) {
        setWaterPumpStatus(QStringLiteral("上水失败：目标液位输入框未初始化"), false);
        return;
    }

    const double target = m_tank2FillTargetLevelSpin->value();
    if (target <= 0.0 || target > kTank2FillMaximumTargetCentimeters) {
        setWaterPumpStatus(QStringLiteral("上水失败：目标液位需在 0-%1 cm 内")
                               .arg(kTank2FillMaximumTargetCentimeters, 0, 'f', 1),
                           false);
        return;
    }

    if (!ensureLiquidLevelConnection()) {
        setWaterPumpStatus(QStringLiteral("上水失败：液位传感器 01 未连接"), false);
        return;
    }

    QString errorMessage;
    QByteArray levelResponse;
    double currentLevel = 0.0;
    if (!m_liquidLevelClient.readLevelMillimeters(
            panthera::adapters::liquidlevel::LiquidLevelModbusClient::kDefaultAddress,
            &currentLevel,
            &errorMessage,
            &levelResponse)) {
        setWaterPumpStatus(QStringLiteral("上水失败：读取液位传感器 01 失败：%1").arg(errorMessage), false);
        setLiquidLevelStatus(QStringLiteral("上水前读取液位失败：%1").arg(errorMessage), false);
        return;
    }

    setLiquidLevelStatus(QStringLiteral("上水前液位：%1 cm，目标：%2 cm\n地址：01，响应：%3")
                             .arg(currentLevel, 0, 'f', 1)
                             .arg(target, 0, 'f', 1)
                             .arg(panthera::adapters::liquidlevel::LiquidLevelModbusClient::frameToHex(levelResponse)),
                         true);
    if (currentLevel >= target) {
        setWaterPumpStatus(QStringLiteral("无需上水：水箱2当前液位 %1 cm 已达到目标 %2 cm")
                               .arg(currentLevel, 0, 'f', 1)
                               .arg(target, 0, 'f', 1),
                           true);
        return;
    }

    if (!ensureWaterPumpConnection()) {
        setWaterPumpStatus(QStringLiteral("上水失败：03 抽入水泵 485 未连接"), false);
        return;
    }

    QByteArray startResponse;
    if (!m_waterPumpClient.startPump(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            &errorMessage,
            &startResponse)) {
        setWaterPumpStatus(QStringLiteral("上水失败：03 抽入水泵启动失败：%1").arg(errorMessage), false);
        return;
    }

    m_tank2FillTargetLevelCentimeters = target;
    m_tank2FillActive = true;
    m_tank2FillTimer.start();
    refreshWaterPumpUi();
    setWaterPumpStatus(QStringLiteral("上水已启动：03 抽入水泵从水箱1向水箱2注水，当前 %1 cm，目标 %2 cm；响应 %3")
                           .arg(currentLevel, 0, 'f', 1)
                           .arg(target, 0, 'f', 1)
                           .arg(waterPumpResponseText(startResponse)),
                       true);
}

void DeviceMonitorPage::pollTank2FillLevel()
{
    if (!m_tank2FillActive) {
        m_tank2FillTimer.stop();
        return;
    }

    if (!ensureLiquidLevelConnection()) {
        stopTank2Fill(false, QStringLiteral("液位传感器 01 连接失败"));
        return;
    }

    QString errorMessage;
    QByteArray response;
    double currentLevel = 0.0;
    if (!m_liquidLevelClient.readLevelMillimeters(
            panthera::adapters::liquidlevel::LiquidLevelModbusClient::kDefaultAddress,
            &currentLevel,
            &errorMessage,
            &response)) {
        stopTank2Fill(false, QStringLiteral("读取液位传感器 01 失败：%1").arg(errorMessage));
        setLiquidLevelStatus(QStringLiteral("上水中读取液位失败：%1").arg(errorMessage), false);
        return;
    }

    setLiquidLevelStatus(QStringLiteral("上水中液位：%1 cm，目标：%2 cm\n地址：01，响应：%3")
                             .arg(currentLevel, 0, 'f', 1)
                             .arg(m_tank2FillTargetLevelCentimeters, 0, 'f', 1)
                             .arg(panthera::adapters::liquidlevel::LiquidLevelModbusClient::frameToHex(response)),
                         true);

    if (currentLevel >= m_tank2FillTargetLevelCentimeters) {
        stopTank2Fill(true, QStringLiteral("当前液位 %1 cm 已达到目标 %2 cm")
                                .arg(currentLevel, 0, 'f', 1)
                                .arg(m_tank2FillTargetLevelCentimeters, 0, 'f', 1));
        return;
    }

    setWaterPumpStatus(QStringLiteral("上水中：水箱2液位 %1 / %2 cm，03 抽入水泵运行中")
                           .arg(currentLevel, 0, 'f', 1)
                           .arg(m_tank2FillTargetLevelCentimeters, 0, 'f', 1),
                       true);
}

void DeviceMonitorPage::stopTank2Fill(bool reachedTarget, const QString& reason)
{
    const bool wasActive = m_tank2FillActive;
    m_tank2FillTimer.stop();
    m_tank2FillActive = false;
    refreshWaterPumpUi();

    QString errorMessage;
    if (!openSharedRs485ForWaterPump(&errorMessage)) {
        setWaterPumpStatus(QStringLiteral("上水停止失败：无法切回水泵 485，03 抽入水泵未确认停止：%1").arg(errorMessage), false);
        return;
    }

    QByteArray response;
    if (!m_waterPumpClient.stopPump(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            &errorMessage,
            &response)) {
        setWaterPumpStatus(QStringLiteral("上水停止失败：03 抽入水泵停止失败：%1").arg(errorMessage), false);
        return;
    }

    const QString action = reachedTarget ? QStringLiteral("上水完成") : QStringLiteral("上水已停止");
    const QString detail = reason.trimmed().isEmpty() ? QString() : QStringLiteral("：%1").arg(reason.trimmed());
    setWaterPumpStatus(QStringLiteral("%1%2，03 抽入水泵已停止，响应 %3")
                           .arg(action, detail,
                                waterPumpResponseText(response)),
                       reachedTarget || wasActive);
}

void DeviceMonitorPage::startWaterLoop()
{
    if (m_tank2FillActive) {
        setWaterPumpStatus(QStringLiteral("启动循环失败：请先停止当前上水流程"), false);
        return;
    }
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray supplyResponse;
    if (!m_waterPumpClient.startPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &supplyResponse)) {
        setWaterPumpStatus(QStringLiteral("启动循环失败：抽入水泵（03）%1").arg(errorMessage), false);
        return;
    }

    QByteArray returnResponse;
    if (!m_waterPumpClient.startPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &returnResponse)) {
        QString stopError;
        QByteArray stopResponse;
        m_waterPumpClient.stopPump(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            &stopError,
            &stopResponse);
        setWaterPumpStatus(QStringLiteral("启动循环失败：抽回水泵（02）%1").arg(errorMessage), false);
        return;
    }

    setWaterPumpStatus(QStringLiteral("水循环启动成功：沿用当前水泵流速；抽入 03 响应 %1；抽回 02 响应 %2")
                           .arg(waterPumpResponseText(supplyResponse))
                           .arg(waterPumpResponseText(returnResponse)),
                       true);
}

void DeviceMonitorPage::stopWaterLoop()
{
    if (m_tank2FillActive) {
        m_tank2FillTimer.stop();
        m_tank2FillActive = false;
        refreshWaterPumpUi();
    }
    if (!ensureWaterPumpConnection()) {
        return;
    }

    QStringList failures;
    QStringList responses;
    for (int index = 0; index < 2; ++index) {
        QString errorMessage;
        QByteArray response;
        if (m_waterPumpClient.stopPump(waterPumpAddress(index), &errorMessage, &response)) {
            responses.push_back(QStringLiteral("%1 %2")
                                    .arg(waterPumpAddressText(index),
                                         waterPumpResponseText(response)));
        } else {
            failures.push_back(QStringLiteral("%1（%2）：%3")
                                   .arg(waterPumpName(index), waterPumpAddressText(index), errorMessage));
        }
    }

    if (!failures.isEmpty()) {
        setWaterPumpStatus(QStringLiteral("停止循环未全部成功：%1").arg(failures.join(QStringLiteral("；"))), false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("水循环停止成功：%1").arg(responses.join(QStringLiteral("；"))), true);
}

bool DeviceMonitorPage::ensureRobotPumpControl(QString* errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (!m_robotArmClient.isConnected()) {
        QString connectError;
        if (!m_robotArmClient.connectToController(&connectError)) {
            if (errorMessage != nullptr) {
                *errorMessage = connectError;
            }
            setRobotArmStatus(QStringLiteral("第三泵连接机械臂失败：%1").arg(connectError));
            refreshRobotArmUi();
            return false;
        }
        setRobotArmStatus(QStringLiteral("第三泵已连接机械臂 %1:%2").arg(m_robotArmSettings.host).arg(m_robotArmSettings.commandPort));
        refreshRobotArmUi();
    }

    QString controlError;
    if (!requestRobotArmControl(&controlError)) {
        if (errorMessage != nullptr) {
            *errorMessage = controlError.isEmpty() ? QStringLiteral("请求机械臂控制权失败") : controlError;
        }
        return false;
    }
    return true;
}

bool DeviceMonitorPage::sendRobotPumpDo(int index, bool on, const QString& action, QString* errorMessage)
{
    QString commandError;
    const panthera::adapters::dobot::DobotCommandResult result =
        m_robotArmClient.setDigitalOutputInstant(index, on, &commandError);
    logRobotArmCommand(
        QStringLiteral("%1：DOInstant(%2,%3)").arg(action).arg(index).arg(on ? 1 : 0),
        result,
        commandError);
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = commandError.isEmpty() ? result.protocolError : commandError;
        }
        return false;
    }
    return true;
}

bool DeviceMonitorPage::setRobotPumpMode(bool do13On, bool do14On, const QString& action, QString* errorMessage)
{
    if (!ensureRobotPumpControl(errorMessage)) {
        return false;
    }

    if (do13On && do14On) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("禁止同时打开 DO13 和 DO14");
        }
        return false;
    }

    if (do13On) {
        if (!sendRobotPumpDo(14, false, action, errorMessage)) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(80);
        return sendRobotPumpDo(13, true, action, errorMessage);
    }

    if (do14On) {
        if (!sendRobotPumpDo(13, false, action, errorMessage)) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(80);
        return sendRobotPumpDo(14, true, action, errorMessage);
    }

    return sendRobotPumpDo(13, false, action, errorMessage)
        && sendRobotPumpDo(14, false, action, errorMessage);
}

void DeviceMonitorPage::startRobotPumpForward()
{
    QString errorMessage;
    if (!setRobotPumpMode(true, false, QStringLiteral("第三泵注水"), &errorMessage)) {
        setWaterPumpStatus(QStringLiteral("第三泵注水启动失败：%1").arg(errorMessage), false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("第三泵注水启动成功：DO13=ON / DO14=OFF"), true);
}

void DeviceMonitorPage::startRobotPumpReverse()
{
    QString errorMessage;
    if (!setRobotPumpMode(false, true, QStringLiteral("第三泵出水"), &errorMessage)) {
        setWaterPumpStatus(QStringLiteral("第三泵出水启动失败：%1").arg(errorMessage), false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("第三泵出水启动成功：DO13=OFF / DO14=ON"), true);
}

void DeviceMonitorPage::stopRobotPump()
{
    QString errorMessage;
    if (!setRobotPumpMode(false, false, QStringLiteral("第三泵停止"), &errorMessage)) {
        setWaterPumpStatus(QStringLiteral("第三泵停止失败：%1").arg(errorMessage), false);
        return;
    }
    setWaterPumpStatus(QStringLiteral("第三泵已停止：DO13=OFF / DO14=OFF"), true);
}

void DeviceMonitorPage::bindFaultToggle(QCheckBox* checkBox, InterlockReason reason)
{
    connect(checkBox, &QCheckBox::toggled, this, [this, reason](bool checked) {
        m_simulationDevice->injectFault(reason, checked);
    });
}

}  // panthera::modules 命名空间
