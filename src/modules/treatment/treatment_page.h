#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVector>
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
    void onActivePlanCleared();
    void onPatientChanged(const panthera::core::PatientRecord& patient);
    void onSafetyChanged(const panthera::core::SafetySnapshot& snapshot);
    void onAbortRequested(const QString& reason);
    void onPlanSelectionChanged(int index);
    void onLayerSelectionChanged(int index);
    void generateThreeDimensionalImage();

private:
    void setButtonState(bool canStart, bool canPause, bool canResume, bool canStop);
    bool isPlanTreatable(const panthera::core::TherapyPlan& plan) const;
    bool canTreatSelectedLayer() const;
    int layerCount(const panthera::core::TherapyPlan* plan = nullptr) const;
    int normalizedLayerIndex(const panthera::core::TherapyPlan& plan) const;
    const panthera::core::TherapySegment* selectedLayerSegment() const;
    panthera::core::TherapyPlan selectedLayerPlan(const panthera::core::TherapyPlan& plan) const;
    int totalPointCount() const;
    double pointDwellSeconds(const panthera::core::TherapyPoint& point, const panthera::core::TherapyPlan& plan) const;
    double layerPlannedDurationSeconds(const panthera::core::TherapySegment& segment, const panthera::core::TherapyPlan& plan) const;
    double layerElapsedDurationSeconds(const panthera::core::TherapySegment& segment, const panthera::core::TherapyPlan& plan) const;
    double planElapsedDurationSeconds(const panthera::core::TherapyPlan& plan) const;
    double planPlannedDurationSeconds(const panthera::core::TherapyPlan& plan) const;
    int completedPointCountForLayer(int layerIndex, const panthera::core::TherapyPlan& plan) const;
    void ensureLayerProgressStorage(const panthera::core::TherapyPlan& plan);
    void updateVolumeButtonState();
    QString planComboText(const panthera::core::TherapyPlan& plan) const;
    bool hasSelectablePlans() const;
    void syncPlanComboEntry(const panthera::core::TherapyPlan& plan);
    void updatePlanSummary(const panthera::core::TherapyPlan* plan);
    void configureLayerSelector(const panthera::core::TherapyPlan* plan);
    void updateLayerNavigationButtons();
    void updateLayerPreview();
    void updateProgressText();
    void appendLog(const QString& line);
    void finalizeTreatment(const QString& status);
    void refreshAvailablePlans(bool keepSelectionBlank = false);

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    MockUltrasoundView* m_preview {nullptr};
    QLabel* m_patientLabel {nullptr};
    QLabel* m_planSummaryLabel {nullptr};
    QLabel* m_layerLabel {nullptr};
    QLabel* m_modeLabel {nullptr};
    QLabel* m_safetyLabel {nullptr};
    QLabel* m_progressLabel {nullptr};
    QLabel* m_timeSummaryLabel {nullptr};
    QLabel* m_layerRemainingValueLabel {nullptr};
    QLabel* m_layerTotalValueLabel {nullptr};
    QLabel* m_planRemainingValueLabel {nullptr};
    QLabel* m_planTotalValueLabel {nullptr};
    QProgressBar* m_progressBar {nullptr};
    QPlainTextEdit* m_logView {nullptr};
    QComboBox* m_planCombo {nullptr};
    QSlider* m_layerSlider {nullptr};
    QToolButton* m_previousLayerButton {nullptr};
    QToolButton* m_nextLayerButton {nullptr};
    QPushButton* m_startButton {nullptr};
    QPushButton* m_pauseButton {nullptr};
    QPushButton* m_stopButton {nullptr};
    QPushButton* m_generate3dButton {nullptr};
    QTimer m_progressTimer;
    int m_selectedLayerIndex {0};
    int m_completedPointCount {0};
    double m_deliveredEnergyJ {0.0};
    QVector<int> m_layerCompletedPointCounts;
    bool m_deferStartupPlanSelection {true};
};

}  // namespace panthera::modules
