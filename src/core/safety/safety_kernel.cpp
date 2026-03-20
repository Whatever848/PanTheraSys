#include "core/safety/safety_kernel.h"

#include <algorithm>

#include <QStringList>

namespace panthera::core {

namespace {

QString joinInterlocks(const QVector<InterlockReason>& reasons)
{
    QStringList labels;
    labels.reserve(reasons.size());
    for (const InterlockReason reason : reasons) {
        labels.push_back(toDisplayString(reason));
    }
    return labels.join(QStringLiteral("、"));
}

}  // 匿名命名空间

SafetyKernel::SafetyKernel(QObject* parent)
    : QObject(parent)
{
    m_mode = SystemMode::Idle;
    rebuildSnapshot();
}

SafetySnapshot SafetyKernel::snapshot() const
{
    return m_snapshot;
}

SystemMode SafetyKernel::mode() const
{
    return m_mode;
}

void SafetyKernel::setPatientSelected(bool selected)
{
    if (m_patientSelected == selected) {
        return;
    }

    m_patientSelected = selected;
    rebuildSnapshot();
}

void SafetyKernel::setPlanApprovalState(ApprovalState approvalState)
{
    if (m_planApprovalState == approvalState) {
        return;
    }

    m_planApprovalState = approvalState;
    rebuildSnapshot();
}

void SafetyKernel::setWaterLoopHealthy(bool healthy)
{
    if (m_waterLoopHealthy == healthy) {
        return;
    }

    m_waterLoopHealthy = healthy;
    rebuildSnapshot();
}

void SafetyKernel::setPowerReady(bool ready)
{
    if (m_powerReady == ready) {
        return;
    }

    m_powerReady = ready;
    rebuildSnapshot();
}

void SafetyKernel::setMotionReady(bool ready)
{
    if (m_motionReady == ready) {
        return;
    }

    m_motionReady = ready;
    rebuildSnapshot();
}

void SafetyKernel::setEmergencyStopReleased(bool released)
{
    if (m_emergencyStopReleased == released) {
        return;
    }

    m_emergencyStopReleased = released;
    rebuildSnapshot();
}

void SafetyKernel::setUltrasoundAvailable(bool available)
{
    if (m_ultrasoundAvailable == available) {
        return;
    }

    m_ultrasoundAvailable = available;
    rebuildSnapshot();
}

void SafetyKernel::enterPlanningMode()
{
    if (m_mode == SystemMode::Treating || m_mode == SystemMode::Paused) {
        return;
    }

    setMode(SystemMode::Planning);
}

void SafetyKernel::resetToIdle()
{
    if (m_mode == SystemMode::Treating || m_mode == SystemMode::Paused) {
        return;
    }

    setMode(SystemMode::Idle);
}

bool SafetyKernel::canStartTreatment(QString* reason) const
{
    if (!m_snapshot.canStartTreatment) {
        if (reason != nullptr) {
            *reason = m_snapshot.message;
        }
        return false;
    }

    if (m_mode == SystemMode::Treating) {
        if (reason != nullptr) {
            *reason = QStringLiteral("治疗已在执行");
        }
        return false;
    }

    return true;
}

bool SafetyKernel::requestTreatmentStart(QString* reason)
{
    // 所有开始治疗请求都必须经过同一个安全入口，避免界面状态绕过联锁策略。
    if (!canStartTreatment(reason)) {
        return false;
    }

    setMode(SystemMode::Treating);
    return true;
}

bool SafetyKernel::pauseTreatment(QString* reason)
{
    if (m_mode != SystemMode::Treating) {
        if (reason != nullptr) {
            *reason = QStringLiteral("当前不在治疗中");
        }
        return false;
    }

    setMode(SystemMode::Paused);
    return true;
}

bool SafetyKernel::resumeTreatment(QString* reason)
{
    if (m_mode != SystemMode::Paused) {
        if (reason != nullptr) {
            *reason = QStringLiteral("当前不在暂停状态");
        }
        return false;
    }

    if (!canStartTreatment(reason)) {
        return false;
    }

    setMode(SystemMode::Treating);
    return true;
}

void SafetyKernel::stopTreatment()
{
    setMode(SystemMode::Idle);
}

void SafetyKernel::rebuildSnapshot()
{
    SafetySnapshot nextSnapshot;

    if (!m_patientSelected) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::NoPatientSelected);
    }
    if (!(m_planApprovalState == ApprovalState::Approved || m_planApprovalState == ApprovalState::Locked)) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::PlanNotApproved);
    }
    if (!m_waterLoopHealthy) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::WaterLoopFault);
    }
    if (!m_powerReady) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::PowerFault);
    }
    if (!m_motionReady) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::MotionFault);
    }
    if (!m_emergencyStopReleased) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::EmergencyStop);
    }
    if (!m_ultrasoundAvailable) {
        nextSnapshot.activeInterlocks.push_back(InterlockReason::UltrasoundUnavailable);
    }

    if (nextSnapshot.activeInterlocks.isEmpty()) {
        nextSnapshot.state = SafetyState::Green;
        nextSnapshot.message = QStringLiteral("所有联锁均通过，可以开始治疗");
        nextSnapshot.canStartTreatment = true;
    } else {
        nextSnapshot.canStartTreatment = false;
        nextSnapshot.message = joinInterlocks(nextSnapshot.activeInterlocks);
        // 黄色表示流程前提未满足，但硬件侧没有出现危险故障。
        // 红色表示至少存在一个设备侧安全条件异常。
        const bool onlyWorkflowInterlocks = std::all_of(
            nextSnapshot.activeInterlocks.cbegin(),
            nextSnapshot.activeInterlocks.cend(),
            [](InterlockReason reason) {
                return reason == InterlockReason::NoPatientSelected || reason == InterlockReason::PlanNotApproved;
            });

        nextSnapshot.state = onlyWorkflowInterlocks ? SafetyState::Yellow : SafetyState::Red;
    }

    m_snapshot = nextSnapshot;

    if (m_mode == SystemMode::Treating || m_mode == SystemMode::Paused) {
        // “治疗中”和“已暂停”都必须遵循同一套安全姿态：出现红色联锁就必须立即中止。
        if (m_snapshot.state == SafetyState::Red) {
            setMode(SystemMode::Alarm);
            emit treatmentAbortRequested(m_snapshot.message);
        }
    } else if (m_snapshot.canStartTreatment && m_mode == SystemMode::Idle) {
        // 当所有前提条件都满足时，系统可从空闲态自动转入“治疗就绪”。
        setMode(SystemMode::TreatmentReady);
    } else if (!m_snapshot.canStartTreatment && m_mode == SystemMode::TreatmentReady) {
        setMode(SystemMode::Idle);
    }

    emit safetySnapshotChanged(m_snapshot);
}

void SafetyKernel::setMode(SystemMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    emit systemModeChanged(m_mode);
}

}  // panthera::core 命名空间
