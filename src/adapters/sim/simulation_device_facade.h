#pragma once

#include <QObject>
#include <QTimer>

#include "core/device/device_interfaces.h"

namespace panthera::adapters {

// 第一阶段的仿真硬件后端，用于在接入真实运动、超声和功率设备之前，
// 先验证工作流壳层、联锁逻辑和页面交互。
// 它实现的是未来正式硬件适配器同一套接口，因此后续切换真机时，
// 上层模块可以尽量保持不变。
class SimulationDeviceFacade final
    : public QObject
    , public panthera::core::IMotionController
    , public panthera::core::IUltrasoundSource
    , public panthera::core::IPowerController
    , public panthera::core::IWaterLoopMonitor
    , public panthera::core::IDeviceHealthService
    , public panthera::core::IEmergencyStopChannel {
    Q_OBJECT

public:
    explicit SimulationDeviceFacade(QObject* parent = nullptr);

    panthera::core::Coordinate6D currentPosition() const override;
    bool moveTo(const panthera::core::Coordinate6D& target, QString* errorMessage = nullptr) override;
    bool home(QString* errorMessage = nullptr) override;

    bool isAvailable() const override;
    QString backendName() const override;

    bool isPowerReady() const override;
    bool setTreatmentOutputEnabled(bool enabled, QString* errorMessage = nullptr) override;
    double outputPowerWatts() const override;

    bool isWaterLoopHealthy() const override;
    double pressureMpa() const override;
    double flowRateLpm() const override;

    panthera::core::DeviceSnapshot latestSnapshot() const override;
    bool isEmergencyStopReleased() const override;

    void start();
    void stop();
    // 通过显式故障注入来驱动各类联锁场景，方便监控页和治疗页验证安全流程。
    void injectFault(panthera::core::InterlockReason reason, bool active);

signals:
    void snapshotUpdated(const panthera::core::DeviceSnapshot& snapshot);
    void healthSignalsChanged(bool waterHealthy, bool powerReady, bool motionReady, bool emergencyStopReleased, bool ultrasoundAvailable);

private slots:
    void tick();

private:
    panthera::core::DeviceSnapshot buildInitialSnapshot() const;

    QTimer m_timer;
    panthera::core::DeviceSnapshot m_snapshot;
    bool m_treatmentOutputEnabled {false};
    bool m_waterFaultInjected {false};
    bool m_powerFaultInjected {false};
    bool m_motionFaultInjected {false};
    bool m_emergencyStopInjected {false};
};

}  // panthera::adapters 命名空间
