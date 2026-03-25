#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "core/services/clinical_data_service.h"
#include "modules/shared/mock_ultrasound_view.h"

namespace panthera::modules {

class TreatmentPage final : public QWidget {
    Q_OBJECT

public:
    TreatmentPage(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
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
    void onPlanSelectionChanged(int index);

private:
    void setButtonState(bool canStart, bool canPause, bool canResume, bool canStop);
    int totalPointCount() const;
    void appendLog(const QString& line);
    void finalizeTreatment(const QString& status);
    void refreshAvailablePlans();

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    MockUltrasoundView* m_preview {nullptr};
    QLabel* m_patientLabel {nullptr};
    QLabel* m_planLabel {nullptr};
    QLabel* m_modeLabel {nullptr};
    QLabel* m_safetyLabel {nullptr};
    QLabel* m_progressLabel {nullptr};
    QProgressBar* m_progressBar {nullptr};
    QPlainTextEdit* m_logView {nullptr};
    QComboBox* m_planCombo {nullptr};
    QPushButton* m_startButton {nullptr};
    QPushButton* m_pauseButton {nullptr};
    QPushButton* m_resumeButton {nullptr};
    QPushButton* m_stopButton {nullptr};
    QTimer m_progressTimer;
    int m_completedPointCount {0};
    double m_deliveredEnergyJ {0.0};
};

}  // namespace panthera::modules
