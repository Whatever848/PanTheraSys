#include "modules/dashboard/device_monitor_page.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

QString boolStatus(bool ok, const QString& okText, const QString& badText)
{
    return ok ? okText : badText;
}

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

}  // namespace

DeviceMonitorPage::DeviceMonitorPage(adapters::SimulationDeviceFacade* simulationDevice, SafetyKernel* safetyKernel, QWidget* parent)
    : QWidget(parent)
    , m_simulationDevice(simulationDevice)
    , m_safetyKernel(safetyKernel)
{
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

    auto* waterCard = createMetricCard(
        QStringLiteral("水循环 / 注水泵系统状态"),
        {
            {QStringLiteral("水位"), createValueLabel()},
            {QStringLiteral("进水温度"), createValueLabel()},
            {QStringLiteral("出水温度"), createValueLabel()},
            {QStringLiteral("实际流速"), createValueLabel()},
            {QStringLiteral("水压"), createValueLabel()},
            {QStringLiteral("注水泵通信"), createValueLabel()},
            {QStringLiteral("注水泵状态"), createValueLabel()},
            {QStringLiteral("运行模式"), createValueLabel()},
            {QStringLiteral("循环方式"), createValueLabel()},
            {QStringLiteral("方向"), createValueLabel()},
            {QStringLiteral("设定转速"), createValueLabel()},
            {QStringLiteral("目标流量"), createValueLabel()},
            {QStringLiteral("实际流量"), createValueLabel()},
            {QStringLiteral("目标注水量"), createValueLabel()},
            {QStringLiteral("累计注水量"), createValueLabel()},
            {QStringLiteral("运行时间"), createValueLabel()},
            {QStringLiteral("停止时间"), createValueLabel()},
            {QStringLiteral("485地址"), createValueLabel()},
            {QStringLiteral("通信波特率"), createValueLabel()},
            {QStringLiteral("安全判定"), createValueLabel()}
        });
    topRow->addWidget(waterCard, 1);

    QVector<QPair<QString, QLabel*>> temperatureMetrics {
        {QStringLiteral("温控通信"), createValueLabel()},
        {QStringLiteral("温控采样周期"), createValueLabel()},
        {QStringLiteral("冷端温度"), createValueLabel()},
        {QStringLiteral("温控485地址"), createValueLabel()},
        {QStringLiteral("温控波特率"), createValueLabel()},
        {QStringLiteral("温控安全判定"), createValueLabel()}
    };
    for (int channelIndex = 1; channelIndex <= 6; ++channelIndex) {
        temperatureMetrics.push_back({QStringLiteral("CH%1 当前/设定").arg(channelIndex), createValueLabel()});
        temperatureMetrics.push_back({QStringLiteral("CH%1 输出/状态").arg(channelIndex), createValueLabel()});
    }
    auto* temperatureCard = createMetricCard(QStringLiteral("温度调节模块状态"), temperatureMetrics);
    topRow->addWidget(temperatureCard, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(16);
    rootLayout->addLayout(bottomRow, 1);

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
    bottomRow->addWidget(motionCard, 2);

    auto* imageCard = createMetricCard(
        QStringLiteral("影像质量与功率输出"),
        {
            {QStringLiteral("亮度"), createValueLabel()},
            {QStringLiteral("对比度"), createValueLabel()},
            {QStringLiteral("清晰度"), createValueLabel()},
            {QStringLiteral("当前输出功率"), createValueLabel()}
        });
    bottomRow->addWidget(imageCard, 1);

    auto* faultCard = new QGroupBox(QStringLiteral("故障注入 / 联锁验证"));
    auto* faultLayout = new QVBoxLayout(faultCard);

    auto* waterFault = new QCheckBox(QStringLiteral("模拟水循环故障"));
    auto* temperatureFault = new QCheckBox(QStringLiteral("模拟温控模块故障"));
    auto* powerFault = new QCheckBox(QStringLiteral("模拟功率故障"));
    auto* motionFault = new QCheckBox(QStringLiteral("模拟运动故障"));
    auto* estopFault = new QCheckBox(QStringLiteral("模拟急停"));
    auto* resetButton = new QPushButton(QStringLiteral("复位全部故障"));

    m_faultToggles = {waterFault, temperatureFault, powerFault, motionFault, estopFault};
    bindFaultToggle(waterFault, InterlockReason::WaterLoopFault);
    bindFaultToggle(temperatureFault, InterlockReason::TemperatureFault);
    bindFaultToggle(powerFault, InterlockReason::PowerFault);
    bindFaultToggle(motionFault, InterlockReason::MotionFault);
    bindFaultToggle(estopFault, InterlockReason::EmergencyStop);

    faultLayout->addWidget(waterFault);
    faultLayout->addWidget(temperatureFault);
    faultLayout->addWidget(powerFault);
    faultLayout->addWidget(motionFault);
    faultLayout->addWidget(estopFault);
    faultLayout->addStretch();
    faultLayout->addWidget(resetButton);
    bottomRow->addWidget(faultCard, 1);

    connect(resetButton, &QPushButton::clicked, this, &DeviceMonitorPage::resetFaults);
    connect(m_simulationDevice, &adapters::SimulationDeviceFacade::snapshotUpdated, this, &DeviceMonitorPage::updateSnapshot);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &DeviceMonitorPage::updateSafety);

    updateSnapshot(m_simulationDevice->latestSnapshot());
    updateSafety(m_safetyKernel->snapshot());
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

void DeviceMonitorPage::bindFaultToggle(QCheckBox* checkBox, InterlockReason reason)
{
    connect(checkBox, &QCheckBox::toggled, this, [this, reason](bool checked) {
        m_simulationDevice->injectFault(reason, checked);
    });
}

}  // panthera::modules 命名空间
