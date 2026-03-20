#pragma once

#include <QObject>

#include "core/domain/system_types.h"

namespace panthera::core {

class EventBus final : public QObject {
    Q_OBJECT

public:
    explicit EventBus(QObject* parent = nullptr);

public slots:
    void publishPatientSelected(const PatientRecord& patient);
    void publishPlanActivated(const TherapyPlan& plan);
    void publishAuditEntry(const AuditEntry& entry);
    void publishDeviceSnapshot(const DeviceSnapshot& snapshot);

signals:
    void patientSelected(const PatientRecord& patient);
    void planActivated(const TherapyPlan& plan);
    void auditEntryPublished(const AuditEntry& entry);
    void deviceSnapshotPublished(const DeviceSnapshot& snapshot);
};

}  // panthera::core 命名空间
