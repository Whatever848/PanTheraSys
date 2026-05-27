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
    case InterlockReason::TemperatureFault:
        return QStringLiteral("温控模块异常");
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

QString toDisplayString(InfusionPumpRunState state)
{
    switch (state) {
    case InfusionPumpRunState::Unknown:
        return QStringLiteral("未知");
    case InfusionPumpRunState::Stopped:
        return QStringLiteral("停止");
    case InfusionPumpRunState::Running:
        return QStringLiteral("运行");
    case InfusionPumpRunState::Fault:
        return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(InfusionPumpOperatingMode mode)
{
    switch (mode) {
    case InfusionPumpOperatingMode::Speed:
        return QStringLiteral("转速模式");
    case InfusionPumpOperatingMode::Flow:
        return QStringLiteral("流量模式");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(InfusionPumpCycleMode mode)
{
    switch (mode) {
    case InfusionPumpCycleMode::Automatic:
        return QStringLiteral("全自动循环");
    case InfusionPumpCycleMode::SemiAutomatic:
        return QStringLiteral("半自动循环");
    case InfusionPumpCycleMode::Manual:
        return QStringLiteral("手动");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(InfusionPumpDirection direction)
{
    switch (direction) {
    case InfusionPumpDirection::Clockwise:
        return QStringLiteral("顺时针");
    case InfusionPumpDirection::CounterClockwise:
        return QStringLiteral("逆时针");
    }
    return QStringLiteral("未知");
}

QString toDisplayString(TemperatureInputType type)
{
    switch (type) {
    case TemperatureInputType::ThermocoupleS:
        return QStringLiteral("S型热电偶");
    case TemperatureInputType::ThermocoupleR:
        return QStringLiteral("R型热电偶");
    case TemperatureInputType::ThermocoupleB:
        return QStringLiteral("B型热电偶");
    case TemperatureInputType::ThermocoupleK:
        return QStringLiteral("K型热电偶");
    case TemperatureInputType::ThermocoupleN:
        return QStringLiteral("N型热电偶");
    case TemperatureInputType::ThermocoupleE:
        return QStringLiteral("E型热电偶");
    case TemperatureInputType::ThermocoupleJ:
        return QStringLiteral("J型热电偶");
    case TemperatureInputType::ThermocoupleT:
        return QStringLiteral("T型热电偶");
    case TemperatureInputType::Pt100:
        return QStringLiteral("PT100热电阻");
    case TemperatureInputType::Cu50:
        return QStringLiteral("Cu50热电阻");
    case TemperatureInputType::Cu100:
        return QStringLiteral("Cu100热电阻");
    case TemperatureInputType::Millivolt:
        return QStringLiteral("自定义mV信号");
    case TemperatureInputType::Resistance:
        return QStringLiteral("自定义电阻信号");
    case TemperatureInputType::Disabled:
        return QStringLiteral("关闭");
    }
    return QStringLiteral("未知");
}

}  // panthera::core 命名空间
