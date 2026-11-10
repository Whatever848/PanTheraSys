#include "core/application/event_bus.h"

namespace panthera::core {

EventBus::EventBus(QObject* parent)
    : QObject(parent)
{
}

void EventBus::publishPatientSelected(const PatientRecord& patient)
{
    emit patientSelected(patient);
}

void EventBus::publishPlanActivated(const TherapyPlan& plan)
{
    emit planActivated(plan);
}

void EventBus::publishAuditEntry(const AuditEntry& entry)
{
    emit auditEntryPublished(entry);
}

void EventBus::publishDeviceSnapshot(const DeviceSnapshot& snapshot)
{
    emit deviceSnapshotPublished(snapshot);
}

void EventBus::publishAlarmEvent(const AlarmEvent& event)
{
    emit alarmEventPublished(event);
}

}  // panthera::core 命名空间
