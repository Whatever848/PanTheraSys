#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include "core/application/application_context.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "modules/shared/mock_ultrasound_view.h"

namespace panthera::modules {

// PlanningPage 负责当前原型中的图像路径选择、方案草案生成、审批锁定和预览流程。
// 后续阶段这里的模拟规划逻辑会被真实影像分析、病灶勾画和路径规划能力替换。
class PlanningPage final : public QWidget {
    Q_OBJECT

public:
    PlanningPage(panthera::core::ApplicationContext* context, panthera::core::SafetyKernel* safetyKernel, panthera::core::AuditService* auditService, QWidget* parent = nullptr);

private slots:
    void loadDemoPatient();
    void generateDraftPlan();
    void approveCurrentPlan();
    void revertPlanToDraft();
    void updateContextSummary();

private:
    panthera::core::TherapyPlan buildPlanFromUi(panthera::core::ApprovalState approvalState) const;

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};

    QListWidget* m_pathList {nullptr};
    MockUltrasoundView* m_preview {nullptr};
    QComboBox* m_patternCombo {nullptr};
    QSpinBox* m_layerCountSpin {nullptr};
    QDoubleSpinBox* m_spacingSpin {nullptr};
    QDoubleSpinBox* m_dwellSpin {nullptr};
    QDoubleSpinBox* m_powerSpin {nullptr};
    QCheckBox* m_respiratoryTrackingCheck {nullptr};
    QLabel* m_patientSummaryLabel {nullptr};
    QLabel* m_planSummaryLabel {nullptr};
};

}  // panthera::modules 命名空间
