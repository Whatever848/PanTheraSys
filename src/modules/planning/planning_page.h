#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVector>
#include <QWidget>

#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "core/services/clinical_data_service.h"
#include "modules/shared/mock_ultrasound_view.h"

namespace panthera::modules {

class PlanningPage final : public QWidget {
    Q_OBJECT

public:
    PlanningPage(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        QWidget* parent = nullptr);

private slots:
    void loadDemoPatient();
    void generateDraftPlan();
    void approveCurrentPlan();
    void revertPlanToDraft();
    void updateContextSummary();
    void addPathItem();
    void removeCurrentPathItem();
    void simulateImageAcquisition();
    void generateThreeDimensionalImage();
    void previewCurrentPlan();
    void editCurrentPlan();
    void saveCurrentPlan();
    void deleteCurrentPlan();
    void toggleAnnotationPanel();
    void onStagedSliceSelectionChanged(int row);
    void onPreviewAnnotationsChanged();
    void refreshDerivedMetrics();
    void storeCapturedImages();
    void loadStoredImages();

private:
    struct StagedSliceState {
        panthera::core::ImageSeriesRecord image;
        QVector<AnnotationStroke> annotations;
        QString label;
        bool edited {false};
    };

    panthera::core::TherapyPlan buildPlanFromUi(panthera::core::ApprovalState approvalState) const;
    void applyPlanToUi(const panthera::core::TherapyPlan& plan);
    void populatePatientSelector();
    void refreshImagingPaths(const QString& patientId);
    void syncPatientSelector(const QString& patientId);
    void updateAssessmentText(const panthera::core::TherapyPlan* plan);
    void updatePlanPreviewText(const panthera::core::TherapyPlan* plan);
    void populateDefaultScanChannels();
    QString currentChannelLabel() const;
    QString currentChannelCoordinate() const;
    void updateAcquisitionSummary(const QString& title, const QStringList& lines);
    void persistCurrentSliceAnnotations();
    void loadStagedSlice(int row);

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;

    QComboBox* m_patientCombo {nullptr};
    QListWidget* m_pathList {nullptr};
    QListWidget* m_modelList {nullptr};
    MockUltrasoundView* m_preview {nullptr};
    QLabel* m_previewOverlayLabel {nullptr};
    QSpinBox* m_layerCountSpin {nullptr};
    QSpinBox* m_stepSpin {nullptr};
    QDoubleSpinBox* m_spacingSpin {nullptr};
    QDoubleSpinBox* m_dwellSpin {nullptr};
    QDoubleSpinBox* m_powerSpin {nullptr};
    QSlider* m_powerSlider {nullptr};
    QCheckBox* m_respiratoryTrackingCheck {nullptr};
    QRadioButton* m_directTreatmentRadio {nullptr};
    QRadioButton* m_segmentedTreatmentRadio {nullptr};
    QRadioButton* m_pointTreatmentRadio {nullptr};
    QRadioButton* m_lineTreatmentRadio {nullptr};
    QLabel* m_totalDurationValueLabel {nullptr};
    QLabel* m_powerValueLabel {nullptr};
    QLabel* m_patientSummaryLabel {nullptr};
    QLabel* m_planSummaryLabel {nullptr};
    QLabel* m_chartSummaryLabel {nullptr};
    QPlainTextEdit* m_assessmentPreview {nullptr};
    QPlainTextEdit* m_planPreview {nullptr};
    QPushButton* m_generateTargetsButton {nullptr};
    QPushButton* m_generateAssessmentButton {nullptr};
    QPushButton* m_previewPlanButton {nullptr};
    QPushButton* m_addPlanButton {nullptr};
    QPushButton* m_deletePlanButton {nullptr};
    QToolButton* m_editPlanButton {nullptr};
    QPushButton* m_addPathButton {nullptr};
    QPushButton* m_removePathButton {nullptr};
    QPushButton* m_acquireImageButton {nullptr};
    QPushButton* m_generate3dButton {nullptr};
    QPushButton* m_storeImageButton {nullptr};
    QPushButton* m_loadImageButton {nullptr};
    QToolButton* m_annotationButton {nullptr};
    QFrame* m_annotationPanel {nullptr};
    QToolButton* m_annotationRedButton {nullptr};
    QToolButton* m_annotationBlueButton {nullptr};
    QToolButton* m_annotationGreenButton {nullptr};
    QToolButton* m_annotationOrangeButton {nullptr};
    QToolButton* m_annotationUndoButton {nullptr};
    QToolButton* m_annotationClearButton {nullptr};
    QToolButton* m_annotationCollapseButton {nullptr};

    QVector<panthera::core::ImageSeriesRecord> m_stagedImageSeries;
    QVector<StagedSliceState> m_stagedSlices;
    int m_currentStagedSliceIndex {-1};
    QDateTime m_lastAcquisitionAt;
};

}  // namespace panthera::modules
