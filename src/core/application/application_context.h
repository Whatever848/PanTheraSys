#pragma once

#include <QImage>
#include <QObject>

#include "core/domain/system_types.h"

namespace panthera::core {

class EventBus;
class AuditService;

// ApplicationContext 表示工作站运行时的内存会话上下文。
// 它负责在页面之间共享“当前患者 / 当前方案 / 当前角色”等状态，
// 避免页面之间直接相互依赖。
class ApplicationContext final : public QObject {
    Q_OBJECT

public:
    ApplicationContext(EventBus* eventBus, AuditService* auditService, QObject* parent = nullptr);

    bool hasSelectedPatient() const;
    const PatientRecord& selectedPatient() const;
    void selectPatient(const PatientRecord& patient);
    void clearSelectedPatient();

    bool hasActivePlan() const;
    const TherapyPlan& activePlan() const;
    void setActivePlan(const TherapyPlan& plan);
    void clearActivePlan();

    RoleType currentRole() const;
    void setCurrentRole(RoleType role);

    void requestTreatmentLayerVisualization(const QString& planId, int layerIndex, bool treatmentActive);
    void updateTreatmentCameraFrame(const QImage& frame, const QString& cameraDescription = {});
    bool hasLatestTreatmentCameraFrame() const;
    QImage latestTreatmentCameraFrame() const;
    QString latestTreatmentCameraDescription() const;

signals:
    void selectedPatientChanged(const panthera::core::PatientRecord& patient);
    void selectedPatientCleared();
    void activePlanChanged(const panthera::core::TherapyPlan& plan);
    void activePlanCleared();
    void currentRoleChanged(panthera::core::RoleType role);
    void treatmentLayerVisualizationRequested(const QString& planId, int layerIndex, bool treatmentActive);
    void treatmentCameraFrameUpdated(const QImage& frame, const QString& cameraDescription);

private:
    EventBus* m_eventBus {nullptr};
    AuditService* m_auditService {nullptr};
    PatientRecord m_selectedPatient;
    TherapyPlan m_activePlan;
    QImage m_latestTreatmentCameraFrame;
    QString m_latestTreatmentCameraDescription;
    RoleType m_currentRole {RoleType::Physician};
    bool m_hasSelectedPatient {false};
    bool m_hasActivePlan {false};
    bool m_hasLatestTreatmentCameraFrame {false};
};

}  // panthera::core 命名空间
