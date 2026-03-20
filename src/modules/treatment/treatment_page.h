#pragma once

#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "modules/shared/mock_ultrasound_view.h"

namespace panthera::modules {

// TreatmentPage 负责已审批方案的治疗执行演示。
// 当前原型使用定时器驱动进度推进；后续接入真实硬件后，应改为响应设备回调、
// 能量反馈和安全确认信号。
class TreatmentPage final : public QWidget {
    Q_OBJECT

public:
    TreatmentPage(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        QWidget* parent = nullptr);

private slots:
    void startTreatment();
    void pauseTreatment();
    void resumeTreatment();
    void stopTreatment();
    void advanceProgress();
    void onActivePlanChanged(const panthera::core::TherapyPlan& plan);
    void onPatientChanged(const panthera::core::PatientRecord& patient);
    void onSafetyChanged(const panthera::core::SafetySnapshot& snapshot);
    void onAbortRequested(const QString& reason);

private:
    void setButtonState(bool canStart, bool canPause, bool canResume, bool canStop);
    int totalPointCount() const;
    void appendLog(const QString& line);
    void finalizeTreatment(const QString& status);

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    MockUltrasoundView* m_preview {nullptr};
    QLabel* m_patientLabel {nullptr};
    QLabel* m_planLabel {nullptr};
    QLabel* m_modeLabel {nullptr};
    QLabel* m_safetyLabel {nullptr};
    QLabel* m_progressLabel {nullptr};
    QProgressBar* m_progressBar {nullptr};
    QPlainTextEdit* m_logView {nullptr};
    QPushButton* m_startButton {nullptr};
    QPushButton* m_pauseButton {nullptr};
    QPushButton* m_resumeButton {nullptr};
    QPushButton* m_stopButton {nullptr};
    QTimer m_progressTimer;
    int m_completedPointCount {0};
    double m_deliveredEnergyJ {0.0};
};

}  // panthera::modules 命名空间
