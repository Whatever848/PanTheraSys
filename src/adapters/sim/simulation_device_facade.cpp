#include "adapters/sim/simulation_device_facade.h"

#include "adapters/anthone/lu926_temperature_protocol.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QRandomGenerator>

namespace panthera::adapters {

using namespace panthera::core;
using panthera::adapters::anthone::Lu926TemperatureProtocol;

namespace {

constexpr double kTemperatureSetpointMinimumCelsius = 0.0;
constexpr double kTemperatureSetpointMaximumCelsius = 45.0;
constexpr double kTemperatureProcessMinimumCelsius = 0.0;
constexpr double kTemperatureProcessMaximumCelsius = 50.0;

double jitter(QRandomGenerator* generator, double center, double amplitude)
{
    return center + ((generator->generateDouble() * 2.0) - 1.0) * amplitude;
}

bool requireRange(double value, double minimum, double maximum, const QString& label, QString* errorMessage)
{
    if (value < minimum || value > maximum) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1超出允许范围：%2 - %3")
                                .arg(label)
                                .arg(minimum, 0, 'f', 1)
                                .arg(maximum, 0, 'f', 1);
        }
        return false;
    }
    return true;
}

bool requireTemperatureChannel(int channelIndex, QString* errorMessage)
{
    if (!Lu926TemperatureProtocol::isValidChannelIndex(channelIndex)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("温控通道必须在1-6之间");
        }
        return false;
    }
    return true;
}

bool requireAnyBaudRate(int baudRate, QString* errorMessage)
{
    switch (baudRate) {
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return true;
    default:
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("注水泵波特率不在说明书支持范围内");
        }
        return false;
    }
}

bool requireTemperatureBaudRate(int baudRate, QString* errorMessage)
{
    if (Lu926TemperatureProtocol::isSupportedBaudRate(baudRate)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("温控模块波特率不在LU-926UT6Y说明书支持范围内");
    }
    return false;
}

bool isChannelTemperatureSafe(const TemperatureChannelTelemetry& channel)
{
    return channel.enabled
        && channel.dataValid
        && !channel.alarmActive
        && !channel.faultActive
        && channel.processTemperatureCelsius >= channel.lowerSafetyLimitCelsius
        && channel.processTemperatureCelsius <= channel.upperSafetyLimitCelsius
        && channel.setpointTemperatureCelsius >= kTemperatureSetpointMinimumCelsius
        && channel.setpointTemperatureCelsius <= kTemperatureSetpointMaximumCelsius
        && channel.outputPercent >= 0.0
        && channel.outputPercent <= 100.0;
}

}  // 匿名命名空间

SimulationDeviceFacade::SimulationDeviceFacade(QObject* parent)
    : QObject(parent)
    , m_snapshot(buildInitialSnapshot())
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SimulationDeviceFacade::tick);
}

Coordinate6D SimulationDeviceFacade::currentPosition() const
{
    return m_snapshot.position;
}

bool SimulationDeviceFacade::moveTo(const Coordinate6D& target, QString* errorMessage)
{
    if (m_motionFaultInjected) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("模拟运动故障已激活");
        }
        return false;
    }

    m_snapshot.position = target;
    return true;
}

bool SimulationDeviceFacade::home(QString* errorMessage)
{
    return moveTo(Coordinate6D {}, errorMessage);
}

bool SimulationDeviceFacade::isAvailable() const
{
    return true;
}

QString SimulationDeviceFacade::backendName() const
{
    return QStringLiteral("simulation");
}

bool SimulationDeviceFacade::isPowerReady() const
{
    return !m_powerFaultInjected;
}

bool SimulationDeviceFacade::setTreatmentOutputEnabled(bool enabled, QString* errorMessage)
{
    if (enabled && m_powerFaultInjected) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("模拟功率故障已激活");
        }
        return false;
    }

    m_treatmentOutputEnabled = enabled;
    return true;
}

double SimulationDeviceFacade::outputPowerWatts() const
{
    return m_snapshot.outputPowerWatts;
}

bool SimulationDeviceFacade::isWaterLoopHealthy() const
{
    return !m_waterFaultInjected && m_snapshot.infusionPump.safetyLimitsOk;
}

double SimulationDeviceFacade::pressureMpa() const
{
    return m_snapshot.pressureMpa;
}

double SimulationDeviceFacade::flowRateLpm() const
{
    return m_snapshot.flowRateLpm;
}

InfusionPumpTelemetry SimulationDeviceFacade::latestInfusionPumpTelemetry() const
{
    return m_snapshot.infusionPump;
}

bool SimulationDeviceFacade::setInfusionPumpEnabled(bool enabled, QString* errorMessage)
{
    if (enabled && m_waterFaultInjected) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("水循环故障状态下禁止启动注水泵");
        }
        return false;
    }

    m_infusionPumpEnabled = enabled;
    m_snapshot.infusionPump.runState = enabled ? InfusionPumpRunState::Running : InfusionPumpRunState::Stopped;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpOperatingMode(InfusionPumpOperatingMode mode, QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    m_snapshot.infusionPump.operatingMode = mode;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpDirection(InfusionPumpDirection direction, QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    m_snapshot.infusionPump.direction = direction;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpSpeedRpm(double rpm, QString* errorMessage)
{
    if (!requireRange(rpm, 0.1, 400.0, QStringLiteral("注水泵转速"), errorMessage)) {
        return false;
    }

    m_snapshot.infusionPump.speedRpm = rpm;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpFlowMlPerMin(double flowMlPerMin, QString* errorMessage)
{
    if (!requireRange(flowMlPerMin, 0.0, 1500.0, QStringLiteral("注水泵流量"), errorMessage)) {
        return false;
    }

    m_snapshot.infusionPump.targetFlowMlPerMin = flowMlPerMin;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpTargetVolumeMl(double volumeMl, QString* errorMessage)
{
    if (!requireRange(volumeMl, 0.0, 9999.0, QStringLiteral("注水泵添加量"), errorMessage)) {
        return false;
    }

    m_snapshot.infusionPump.targetVolumeMl = volumeMl;
    tick();
    return true;
}

bool SimulationDeviceFacade::setInfusionPumpCycleTiming(double runSeconds, double stopSeconds, QString* errorMessage)
{
    if (!requireRange(runSeconds, 0.0, 999.0, QStringLiteral("注水泵运行时间"), errorMessage)
        || !requireRange(stopSeconds, 0.0, 999.0, QStringLiteral("注水泵停止时间"), errorMessage)) {
        return false;
    }

    m_snapshot.infusionPump.runTimeSeconds = runSeconds;
    m_snapshot.infusionPump.stopTimeSeconds = stopSeconds;
    tick();
    return true;
}

bool SimulationDeviceFacade::configureInfusionPumpCommunication(int modbusAddress, int baudRate, QString* errorMessage)
{
    if (modbusAddress < 1 || modbusAddress > 247) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("注水泵485地址必须在1-247之间");
        }
        return false;
    }
    if (!requireAnyBaudRate(baudRate, errorMessage)) {
        return false;
    }

    m_snapshot.infusionPump.modbusAddress = modbusAddress;
    m_snapshot.infusionPump.baudRate = baudRate;
    tick();
    return true;
}

bool SimulationDeviceFacade::resetInfusionPumpFault(QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    m_waterFaultInjected = false;
    m_snapshot.infusionPump.lastError.clear();
    tick();
    return true;
}

TemperatureModuleTelemetry SimulationDeviceFacade::latestTemperatureTelemetry() const
{
    return m_snapshot.temperatureModule;
}

bool SimulationDeviceFacade::setTemperatureControlEnabled(bool enabled, QString* errorMessage)
{
    if (enabled && m_temperatureFaultInjected) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("温控模块故障状态下禁止启用温度控制");
        }
        return false;
    }

    m_temperatureControlEnabled = enabled;
    tick();
    return true;
}

bool SimulationDeviceFacade::setTemperatureChannelSetpoint(int channelIndex, double celsius, QString* errorMessage)
{
    if (!requireTemperatureChannel(channelIndex, errorMessage)
        || !requireRange(celsius, kTemperatureSetpointMinimumCelsius, kTemperatureSetpointMaximumCelsius, QStringLiteral("温控设定值"), errorMessage)) {
        return false;
    }

    auto& channels = m_snapshot.temperatureModule.channels;
    channels[channelIndex - 1].setpointTemperatureCelsius = celsius;
    tick();
    return true;
}

bool SimulationDeviceFacade::configureTemperatureInput(int channelIndex, TemperatureInputType inputType, QString* errorMessage)
{
    if (!requireTemperatureChannel(channelIndex, errorMessage)) {
        return false;
    }

    auto& channel = m_snapshot.temperatureModule.channels[channelIndex - 1];
    channel.inputType = inputType;
    channel.enabled = inputType != TemperatureInputType::Disabled;
    tick();
    return true;
}

bool SimulationDeviceFacade::configureTemperatureCommunication(int modbusAddress, int baudRate, QString* errorMessage)
{
    if (modbusAddress < 1 || modbusAddress > 247) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("温控模块485地址必须在1-247之间");
        }
        return false;
    }
    if (!requireTemperatureBaudRate(baudRate, errorMessage)) {
        return false;
    }

    m_snapshot.temperatureModule.modbusAddress = modbusAddress;
    m_snapshot.temperatureModule.baudRate = baudRate;
    tick();
    return true;
}

bool SimulationDeviceFacade::resetTemperatureFault(QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    m_temperatureFaultInjected = false;
    m_snapshot.temperatureModule.faultCode = 0;
    m_snapshot.temperatureModule.lastError.clear();
    tick();
    return true;
}

DeviceSnapshot SimulationDeviceFacade::latestSnapshot() const
{
    return m_snapshot;
}

bool SimulationDeviceFacade::isEmergencyStopReleased() const
{
    return !m_emergencyStopInjected;
}

void SimulationDeviceFacade::start()
{
    if (!m_timer.isActive()) {
        m_timer.start();
        tick();
    }
}

void SimulationDeviceFacade::stop()
{
    m_timer.stop();
}

void SimulationDeviceFacade::injectFault(InterlockReason reason, bool active)
{
    // 显式映射每一种故障类型，保证每个注入动作都对应明确的联锁路径。
    switch (reason) {
    case InterlockReason::WaterLoopFault:
        m_waterFaultInjected = active;
        break;
    case InterlockReason::PowerFault:
        m_powerFaultInjected = active;
        break;
    case InterlockReason::MotionFault:
        m_motionFaultInjected = active;
        break;
    case InterlockReason::TemperatureFault:
        m_temperatureFaultInjected = active;
        break;
    case InterlockReason::EmergencyStop:
        m_emergencyStopInjected = active;
        break;
    default:
        break;
    }

    tick();
}

void SimulationDeviceFacade::tick()
{
    // 每个时钟周期发布一份完整一致的遥测快照。
    // 界面层和安全内核都基于这份快照工作，这也对应未来真实后端的状态分发方式。
    auto* generator = QRandomGenerator::global();

    m_snapshot.capturedAt = QDateTime::currentDateTime();
    m_snapshot.inputVoltageVolts = jitter(generator, 220.0, 3.0);
    m_snapshot.workingCurrentAmps = m_treatmentOutputEnabled ? jitter(generator, 5.3, 0.3) : jitter(generator, 1.2, 0.1);
    m_snapshot.realtimePowerWatts = m_snapshot.inputVoltageVolts * m_snapshot.workingCurrentAmps;
    m_snapshot.waterLevelPercent = 98.0;
    m_snapshot.inletTemperatureCelsius = jitter(generator, 18.5, 0.4);
    m_snapshot.outletTemperatureCelsius = jitter(generator, 22.3, 0.4);
    m_snapshot.flowRateLpm = m_waterFaultInjected ? 0.5 : jitter(generator, 12.4, 0.8);
    m_snapshot.pressureMpa = m_waterFaultInjected ? 0.05 : jitter(generator, 0.40, 0.02);
    auto& pump = m_snapshot.infusionPump;
    pump.updatedAt = m_snapshot.capturedAt;
    pump.connected = !m_waterFaultInjected;
    pump.dataStale = false;
    pump.dataValid = pump.connected;
    pump.runState = m_waterFaultInjected ? InfusionPumpRunState::Fault
        : (m_infusionPumpEnabled ? InfusionPumpRunState::Running : InfusionPumpRunState::Stopped);
    pump.speedRpm = std::clamp(pump.speedRpm, 0.1, 400.0);
    pump.targetFlowMlPerMin = std::clamp(pump.targetFlowMlPerMin, 0.0, 1500.0);
    pump.actualFlowMlPerMin = m_waterFaultInjected ? 0.0
        : (m_infusionPumpEnabled ? jitter(generator, pump.targetFlowMlPerMin, 8.0) : 0.0);
    pump.actualFlowMlPerMin = std::clamp(pump.actualFlowMlPerMin, 0.0, 1500.0);
    pump.deliveredVolumeMl = m_infusionPumpEnabled
        ? std::min(pump.targetVolumeMl, pump.deliveredVolumeMl + (pump.actualFlowMlPerMin / 60.0))
        : pump.deliveredVolumeMl;
    pump.safetyLimitsOk = pump.connected
        && pump.dataValid
        && !pump.dataStale
        && pump.speedRpm >= 0.1
        && pump.speedRpm <= 400.0
        && pump.targetFlowMlPerMin >= 0.0
        && pump.targetFlowMlPerMin <= 1500.0
        && pump.actualFlowMlPerMin >= 0.0
        && pump.actualFlowMlPerMin <= 1500.0
        && pump.targetVolumeMl >= 0.0
        && pump.targetVolumeMl <= 9999.0
        && pump.runTimeSeconds >= 0.0
        && pump.runTimeSeconds <= 999.0
        && pump.stopTimeSeconds >= 0.0
        && pump.stopTimeSeconds <= 999.0
        && pump.modbusAddress >= 1
        && pump.modbusAddress <= 247;
    pump.lastError = pump.safetyLimitsOk ? QString() : QStringLiteral("注水泵遥测或参数越界");

    auto& temperature = m_snapshot.temperatureModule;
    temperature.updatedAt = m_snapshot.capturedAt;
    temperature.connected = !m_temperatureFaultInjected;
    temperature.dataStale = false;
    temperature.dataValid = temperature.connected;
    temperature.coldJunctionTemperatureCelsius = m_temperatureFaultInjected ? 0.0 : jitter(generator, 24.0, 0.2);
    temperature.faultCode = m_temperatureFaultInjected ? 0x0001U : 0U;

    for (auto& channel : temperature.channels) {
        if (m_temperatureFaultInjected) {
            channel.dataValid = false;
            channel.alarmActive = true;
            channel.faultActive = true;
            channel.outputPercent = 0.0;
            channel.statusMessage = QStringLiteral("通信/采样异常");
            continue;
        }

        channel.dataValid = channel.enabled;
        channel.faultActive = !channel.enabled;
        if (!channel.enabled) {
            channel.outputPercent = 0.0;
            channel.alarmActive = true;
            channel.statusMessage = QStringLiteral("通道关闭");
            temperature.faultCode |= 0x0002U;
            continue;
        }

        const double controlTarget = m_temperatureControlEnabled
            ? channel.setpointTemperatureCelsius
            : channel.processTemperatureCelsius;
        const double drift = (controlTarget - channel.processTemperatureCelsius) * 0.16;
        channel.processTemperatureCelsius += drift + jitter(generator, 0.0, 0.05);
        const double errorCelsius = std::abs(channel.setpointTemperatureCelsius - channel.processTemperatureCelsius);
        channel.outputPercent = m_temperatureControlEnabled
            ? std::clamp((errorCelsius * 10.0) + jitter(generator, 8.0, 3.0), 0.0, 100.0)
            : 0.0;
        channel.alarmActive = channel.processTemperatureCelsius < channel.lowerSafetyLimitCelsius
            || channel.processTemperatureCelsius > channel.upperSafetyLimitCelsius;
        channel.statusMessage = channel.alarmActive ? QStringLiteral("越界报警") : QStringLiteral("正常");
        if (channel.alarmActive) {
            temperature.faultCode |= 0x0004U;
        }
    }

    const bool allTemperatureChannelsSafe = std::all_of(
        temperature.channels.cbegin(),
        temperature.channels.cend(),
        [](const TemperatureChannelTelemetry& channel) {
            return isChannelTemperatureSafe(channel);
        });
    temperature.safetyLimitsOk = temperature.connected
        && temperature.dataValid
        && !temperature.dataStale
        && temperature.coldJunctionTemperatureCelsius >= -10.0
        && temperature.coldJunctionTemperatureCelsius <= 60.0
        && temperature.modbusAddress >= 1
        && temperature.modbusAddress <= 247
        && Lu926TemperatureProtocol::isSupportedBaudRate(temperature.baudRate)
        && allTemperatureChannelsSafe;
    temperature.lastError = temperature.safetyLimitsOk ? QString() : QStringLiteral("温控模块通信、报警或温度越界");

    m_snapshot.transducerTemperatureCelsius = m_treatmentOutputEnabled ? jitter(generator, 31.0, 0.6) : jitter(generator, 28.5, 0.3);
    m_snapshot.vibrationFrequencyMhz = jitter(generator, 1.25, 0.02);
    m_snapshot.conversionEfficiencyPercent = m_powerFaultInjected ? 40.0 : jitter(generator, 92.5, 1.2);
    m_snapshot.motorLoadPercent = m_motionFaultInjected ? 91.0 : jitter(generator, 35.0, 5.0);
    m_snapshot.motionAccuracyMm = m_motionFaultInjected ? 2.5 : jitter(generator, 0.18, 0.09);
    m_snapshot.imageBrightness = jitter(generator, 65.0, 2.0);
    m_snapshot.imageContrast = jitter(generator, 80.0, 2.0);
    m_snapshot.imageClarity = jitter(generator, 88.0, 2.5);
    m_snapshot.outputPowerWatts = m_treatmentOutputEnabled && !m_powerFaultInjected ? jitter(generator, 400.0, 15.0) : 0.0;
    m_snapshot.coolerOn = !m_waterFaultInjected;
    m_snapshot.heaterOn = !m_waterFaultInjected;
    m_snapshot.waterPumpOn = !m_waterFaultInjected;
    m_snapshot.emergencyStopEngaged = m_emergencyStopInjected;

    emit snapshotUpdated(m_snapshot);
    emit healthSignalsChanged(!m_waterFaultInjected && pump.safetyLimitsOk, !m_powerFaultInjected, !m_motionFaultInjected, temperature.safetyLimitsOk, !m_emergencyStopInjected, true);
}

DeviceSnapshot SimulationDeviceFacade::buildInitialSnapshot() const
{
    DeviceSnapshot snapshot;
    snapshot.position = Coordinate6D {20.0, -5.2, 27.9, 75.0, 25.3, 25.3};
    snapshot.imageBrightness = 65.0;
    snapshot.imageContrast = 80.0;
    snapshot.imageClarity = 88.0;
    snapshot.infusionPump.connected = true;
    snapshot.infusionPump.dataValid = true;
    snapshot.infusionPump.dataStale = false;
    snapshot.infusionPump.safetyLimitsOk = true;
    snapshot.infusionPump.backendName = QStringLiteral("DIP 1500 V2 仿真接口");
    snapshot.infusionPump.runState = InfusionPumpRunState::Stopped;
    snapshot.infusionPump.operatingMode = InfusionPumpOperatingMode::Flow;
    snapshot.infusionPump.cycleMode = InfusionPumpCycleMode::Automatic;
    snapshot.infusionPump.direction = InfusionPumpDirection::Clockwise;
    snapshot.infusionPump.speedRpm = 180.0;
    snapshot.infusionPump.targetFlowMlPerMin = 720.0;
    snapshot.infusionPump.actualFlowMlPerMin = 0.0;
    snapshot.infusionPump.targetVolumeMl = 20.0;
    snapshot.infusionPump.deliveredVolumeMl = 0.0;
    snapshot.infusionPump.runTimeSeconds = 30.0;
    snapshot.infusionPump.stopTimeSeconds = 0.0;
    snapshot.infusionPump.modbusAddress = 192;
    snapshot.infusionPump.baudRate = 9600;

    snapshot.temperatureModule.connected = true;
    snapshot.temperatureModule.dataValid = true;
    snapshot.temperatureModule.dataStale = false;
    snapshot.temperatureModule.safetyLimitsOk = true;
    snapshot.temperatureModule.backendName = QStringLiteral("LU-926UT6Y 六路温控仿真接口");
    snapshot.temperatureModule.modbusAddress = Lu926TemperatureProtocol::kDefaultAddress;
    snapshot.temperatureModule.baudRate = Lu926TemperatureProtocol::kDefaultBaudRate;
    snapshot.temperatureModule.samplePeriodSeconds = Lu926TemperatureProtocol::kSamplePeriodSeconds;
    snapshot.temperatureModule.coldJunctionTemperatureCelsius = 24.0;
    auto makeTemperatureChannel = [](int channelIndex, const QString& label, double processCelsius, double setpointCelsius) {
        TemperatureChannelTelemetry channel;
        channel.channelIndex = channelIndex;
        channel.label = label;
        channel.enabled = true;
        channel.dataValid = true;
        channel.inputType = TemperatureInputType::Pt100;
        channel.processTemperatureCelsius = processCelsius;
        channel.setpointTemperatureCelsius = setpointCelsius;
        channel.outputPercent = 0.0;
        channel.lowerSafetyLimitCelsius = kTemperatureProcessMinimumCelsius;
        channel.upperSafetyLimitCelsius = kTemperatureProcessMaximumCelsius;
        channel.statusMessage = QStringLiteral("正常");
        return channel;
    };
    snapshot.temperatureModule.channels = {
        makeTemperatureChannel(1, QStringLiteral("CH1 水箱"), 18.5, 20.0),
        makeTemperatureChannel(2, QStringLiteral("CH2 出水"), 22.3, 22.0),
        makeTemperatureChannel(3, QStringLiteral("CH3 换能器"), 28.5, 29.0),
        makeTemperatureChannel(4, QStringLiteral("CH4 治疗头"), 36.5, 37.0),
        makeTemperatureChannel(5, QStringLiteral("CH5 机箱"), 30.0, 30.0),
        makeTemperatureChannel(6, QStringLiteral("CH6 备用"), 25.0, 25.0)
    };
    return snapshot;
}

}  // panthera::adapters 命名空间
