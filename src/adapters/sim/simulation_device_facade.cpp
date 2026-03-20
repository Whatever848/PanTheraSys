#include "adapters/sim/simulation_device_facade.h"

#include <QDateTime>
#include <QRandomGenerator>

namespace panthera::adapters {

using namespace panthera::core;

namespace {

double jitter(QRandomGenerator* generator, double center, double amplitude)
{
    return center + ((generator->generateDouble() * 2.0) - 1.0) * amplitude;
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
    return !m_waterFaultInjected;
}

double SimulationDeviceFacade::pressureMpa() const
{
    return m_snapshot.pressureMpa;
}

double SimulationDeviceFacade::flowRateLpm() const
{
    return m_snapshot.flowRateLpm;
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
    emit healthSignalsChanged(!m_waterFaultInjected, !m_powerFaultInjected, !m_motionFaultInjected, !m_emergencyStopInjected, true);
}

DeviceSnapshot SimulationDeviceFacade::buildInitialSnapshot() const
{
    DeviceSnapshot snapshot;
    snapshot.position = Coordinate6D {20.0, -5.2, 27.9, 75.0, 25.3, 25.3};
    snapshot.imageBrightness = 65.0;
    snapshot.imageContrast = 80.0;
    snapshot.imageClarity = 88.0;
    return snapshot;
}

}  // panthera::adapters 命名空间
