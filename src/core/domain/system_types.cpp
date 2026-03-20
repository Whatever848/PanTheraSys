#include "core/domain/system_types.h"

namespace panthera::core {

QString toDisplayString(SystemMode mode)
{
    switch (mode) {
    case SystemMode::Startup:
        return QStringLiteral("启动中");
    case SystemMode::Idle:
        return QStringLiteral("空闲");
    case SystemMode::Planning:
        return QStringLiteral("方案设计");
    case SystemMode::TreatmentReady:
        return QStringLiteral("治疗就绪");
    case SystemMode::Treating:
        return QStringLiteral("治疗中");
    case SystemMode::Paused:
        return QStringLiteral("已暂停");
    case SystemMode::Alarm:
        return QStringLiteral("报警");
    case SystemMode::Maintenance:
        return QStringLiteral("维护");
    case SystemMode::Shutdown:
        return QStringLiteral("关机");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(SafetyState state)
{
    switch (state) {
    case SafetyState::Green:
        return QStringLiteral("绿色");
    case SafetyState::Yellow:
        return QStringLiteral("黄色");
    case SafetyState::Red:
        return QStringLiteral("红色");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(AlarmLevel level)
{
    switch (level) {
    case AlarmLevel::Info:
        return QStringLiteral("提示");
    case AlarmLevel::Warning:
        return QStringLiteral("警告");
    case AlarmLevel::Critical:
        return QStringLiteral("严重");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(InterlockReason reason)
{
    switch (reason) {
    case InterlockReason::None:
        return QStringLiteral("无");
    case InterlockReason::NoPatientSelected:
        return QStringLiteral("未选择患者");
    case InterlockReason::PlanNotApproved:
        return QStringLiteral("方案未审批");
    case InterlockReason::WaterLoopFault:
        return QStringLiteral("水循环异常");
    case InterlockReason::PowerFault:
        return QStringLiteral("功率链路异常");
    case InterlockReason::MotionFault:
        return QStringLiteral("运动系统异常");
    case InterlockReason::EmergencyStop:
        return QStringLiteral("急停触发");
    case InterlockReason::UltrasoundUnavailable:
        return QStringLiteral("超声源不可用");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(ApprovalState state)
{
    switch (state) {
    case ApprovalState::Draft:
        return QStringLiteral("草案");
    case ApprovalState::UnderReview:
        return QStringLiteral("审核中");
    case ApprovalState::Approved:
        return QStringLiteral("已审批");
    case ApprovalState::Locked:
        return QStringLiteral("已锁定");
    case ApprovalState::Superseded:
        return QStringLiteral("已替代");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(RoleType role)
{
    switch (role) {
    case RoleType::Operator:
        return QStringLiteral("操作员");
    case RoleType::Physician:
        return QStringLiteral("医生");
    case RoleType::Engineer:
        return QStringLiteral("工程师");
    case RoleType::Administrator:
        return QStringLiteral("管理员");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(TreatmentPattern pattern)
{
    switch (pattern) {
    case TreatmentPattern::Point:
        return QStringLiteral("点治疗");
    case TreatmentPattern::Line:
        return QStringLiteral("线治疗");
    case TreatmentPattern::Segmented:
        return QStringLiteral("分段治疗");
    }
    return QStringLiteral("未知");
}

}  // panthera::core 命名空间
