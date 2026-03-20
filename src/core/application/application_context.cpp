#include "core/application/application_context.h"

#include "core/application/event_bus.h"
#include "core/services/audit_service.h"

namespace panthera::core {

ApplicationContext::ApplicationContext(EventBus* eventBus, AuditService* auditService, QObject* parent)
    : QObject(parent)
    , m_eventBus(eventBus)
    , m_auditService(auditService)
{
}

bool ApplicationContext::hasSelectedPatient() const
{
    return m_hasSelectedPatient;
}

const PatientRecord& ApplicationContext::selectedPatient() const
{
    return m_selectedPatient;
}

void ApplicationContext::selectPatient(const PatientRecord& patient)
{
    // 在统一入口广播患者上下文变化，保证所有页面看到的是同一份会话状态。
    m_selectedPatient = patient;
    m_hasSelectedPatient = true;

    if (m_eventBus != nullptr) {
        m_eventBus->publishPatientSelected(patient);
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("system"), QStringLiteral("patient"), QStringLiteral("选择患者：%1").arg(patient.id));
    }

    emit selectedPatientChanged(patient);
}

void ApplicationContext::clearSelectedPatient()
{
    // 是否在清空患者时同步清空方案，由更上层的流程控制代码决定。
    m_hasSelectedPatient = false;
    emit selectedPatientCleared();
}

bool ApplicationContext::hasActivePlan() const
{
    return m_hasActivePlan;
}

const TherapyPlan& ApplicationContext::activePlan() const
{
    return m_activePlan;
}

void ApplicationContext::setActivePlan(const TherapyPlan& plan)
{
    // 方案激活在这里统一审计和发布，保证预览、安全状态以及后续持久化扩展保持同步。
    m_activePlan = plan;
    m_hasActivePlan = true;

    if (m_eventBus != nullptr) {
        m_eventBus->publishPlanActivated(plan);
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("system"), QStringLiteral("plan"), QStringLiteral("激活方案：%1").arg(plan.id));
    }

    emit activePlanChanged(plan);
}

void ApplicationContext::clearActivePlan()
{
    m_hasActivePlan = false;
    emit activePlanCleared();
}

RoleType ApplicationContext::currentRole() const
{
    return m_currentRole;
}

void ApplicationContext::setCurrentRole(RoleType role)
{
    if (m_currentRole == role) {
        return;
    }

    m_currentRole = role;
    emit currentRoleChanged(role);
}

}  // panthera::core 命名空间
