#include "modules/dashboard/device_monitor_page.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

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
        QStringLiteral("水循环系统状态"),
        {
            {QStringLiteral("水位"), createValueLabel()},
            {QStringLiteral("进水温度"), createValueLabel()},
            {QStringLiteral("出水温度"), createValueLabel()},
            {QStringLiteral("实际流速"), createValueLabel()},
            {QStringLiteral("水压"), createValueLabel()}
        });
    topRow->addWidget(waterCard, 1);

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
    auto* powerFault = new QCheckBox(QStringLiteral("模拟功率故障"));
    auto* motionFault = new QCheckBox(QStringLiteral("模拟运动故障"));
    auto* estopFault = new QCheckBox(QStringLiteral("模拟急停"));
    auto* resetButton = new QPushButton(QStringLiteral("复位全部故障"));

    m_faultToggles = {waterFault, powerFault, motionFault, estopFault};
    bindFaultToggle(waterFault, InterlockReason::WaterLoopFault);
    bindFaultToggle(powerFault, InterlockReason::PowerFault);
    bindFaultToggle(motionFault, InterlockReason::MotionFault);
    bindFaultToggle(estopFault, InterlockReason::EmergencyStop);

    faultLayout->addWidget(waterFault);
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
    m_valueLabels.value(QStringLiteral("输入电压"))->setText(QStringLiteral("%1 V").arg(snapshot.inputVoltageVolts, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("工作电流"))->setText(QStringLiteral("%1 A").arg(snapshot.workingCurrentAmps, 0, 'f', 2));
    m_valueLabels.value(QStringLiteral("实时功率"))->setText(QStringLiteral("%1 W").arg(snapshot.realtimePowerWatts, 0, 'f', 0));
    m_valueLabels.value(QStringLiteral("水位"))->setText(QStringLiteral("%1 %").arg(snapshot.waterLevelPercent, 0, 'f', 0));
    m_valueLabels.value(QStringLiteral("进水温度"))->setText(QStringLiteral("%1 °C").arg(snapshot.inletTemperatureCelsius, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("出水温度"))->setText(QStringLiteral("%1 °C").arg(snapshot.outletTemperatureCelsius, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("实际流速"))->setText(QStringLiteral("%1 L/min").arg(snapshot.flowRateLpm, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("水压"))->setText(QStringLiteral("%1 MPa").arg(snapshot.pressureMpa, 0, 'f', 2));
    m_valueLabels.value(QStringLiteral("位置 X/Y/Z"))->setText(QStringLiteral("%1 / %2 / %3").arg(snapshot.position.x, 0, 'f', 1).arg(snapshot.position.y, 0, 'f', 1).arg(snapshot.position.z, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("姿态 A/B/C"))->setText(QStringLiteral("%1 / %2 / %3").arg(snapshot.position.a, 0, 'f', 1).arg(snapshot.position.b, 0, 'f', 1).arg(snapshot.position.c, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("负载"))->setText(QStringLiteral("%1 %").arg(snapshot.motorLoadPercent, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("精度"))->setText(QStringLiteral("%1 mm").arg(snapshot.motionAccuracyMm, 0, 'f', 2));
    m_valueLabels.value(QStringLiteral("换能器温度"))->setText(QStringLiteral("%1 °C").arg(snapshot.transducerTemperatureCelsius, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("振动频率"))->setText(QStringLiteral("%1 MHz").arg(snapshot.vibrationFrequencyMhz, 0, 'f', 2));
    m_valueLabels.value(QStringLiteral("能量效率"))->setText(QStringLiteral("%1 %").arg(snapshot.conversionEfficiencyPercent, 0, 'f', 1));
    m_valueLabels.value(QStringLiteral("亮度"))->setText(QStringLiteral("%1").arg(snapshot.imageBrightness, 0, 'f', 0));
    m_valueLabels.value(QStringLiteral("对比度"))->setText(QStringLiteral("%1").arg(snapshot.imageContrast, 0, 'f', 0));
    m_valueLabels.value(QStringLiteral("清晰度"))->setText(QStringLiteral("%1").arg(snapshot.imageClarity, 0, 'f', 0));
    m_valueLabels.value(QStringLiteral("当前输出功率"))->setText(QStringLiteral("%1 W").arg(snapshot.outputPowerWatts, 0, 'f', 0));
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
