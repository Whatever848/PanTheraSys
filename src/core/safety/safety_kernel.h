#pragma once

#include <QObject>

#include "core/domain/system_types.h"

namespace panthera::core {

// SafetyKernel 统一负责联锁判定和运行模式切换。
// 界面页面与设备适配层只负责上报前提条件，是否允许开始、暂停、继续或强制中止治疗，
// 一律由安全内核给出最终结论。
class SafetyKernel final : public QObject {
    Q_OBJECT

public:
    explicit SafetyKernel(QObject* parent = nullptr);

    SafetySnapshot snapshot() const;
    SystemMode mode() const;

    void setPatientSelected(bool selected);
    void setPlanApprovalState(ApprovalState approvalState);
    void setWaterLoopHealthy(bool healthy);
    void setPowerReady(bool ready);
    void setMotionReady(bool ready);
    void setEmergencyStopReleased(bool released);
    void setUltrasoundAvailable(bool available);
    void enterPlanningMode();
    void resetToIdle();

    bool canStartTreatment(QString* reason = nullptr) const;
    bool requestTreatmentStart(QString* reason = nullptr);
    bool pauseTreatment(QString* reason = nullptr);
    bool resumeTreatment(QString* reason = nullptr);
    void stopTreatment();

signals:
    void safetySnapshotChanged(const panthera::core::SafetySnapshot& snapshot);
    void systemModeChanged(panthera::core::SystemMode mode);
    void treatmentAbortRequested(const QString& reason);

private:
    // 根据最新的流程状态和硬件状态重新生成联锁判定结果。
    void rebuildSnapshot();
    void setMode(SystemMode mode);

    bool m_patientSelected {false};
    ApprovalState m_planApprovalState {ApprovalState::Draft};
    bool m_waterLoopHealthy {true};
    bool m_powerReady {true};
    bool m_motionReady {true};
    bool m_emergencyStopReleased {true};
    bool m_ultrasoundAvailable {true};
    SafetySnapshot m_snapshot;
    SystemMode m_mode {SystemMode::Startup};
};

}  // panthera::core 命名空间
