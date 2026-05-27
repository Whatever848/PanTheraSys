#pragma once

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointF>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "core/services/clinical_data_service.h"
#include "modules/shared/energy_output_chart_widget.h"
#include "modules/shared/mock_ultrasound_view.h"
#include "modules/shared/therapy_imaging_algorithms.h"

class QHBoxLayout;

namespace panthera::modules {

class PlanningPage final : public QWidget {
    Q_OBJECT

public:
    PlanningPage(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        QWidget* parent = nullptr);

    [[nodiscard]] QStringList personalizationProfileNames() const;
    [[nodiscard]] QString activePersonalizationProfileName() const;
    void applyPersonalizationProfile(const QString& profileName);
    bool saveCurrentPersonalizationProfile(const QString& profileName);
    bool deletePersonalizationProfile(const QString& profileName);

public slots:
    void showTreatmentComparisonLayer(const QString& planId, int layerIndex, bool treatmentActive);

private slots:
    void loadDemoPatient(bool refreshHistoricalImages = true);
    void generateDraftPlan();
    void generateTargetsForCurrentSlice();
    void generateAssessmentForCurrentPlan();
    void approveCurrentPlan();
    void revertPlanToDraft();
    void updateContextSummary();
    void addPathItem();
    void removeCurrentPathItem();
    void simulateImageAcquisition();
    void generateThreeDimensionalImage();
    void previewCurrentPlan();
    void editCurrentPlan();
    void toggleAnnotationPanel();
    void onPathSelectionChanged(int row);
    void onStagedSliceSelectionChanged(int row);
    void onPreviewAnnotationsChanged();
    void onRespiratoryTrackingToggled(bool enabled);
    void onDeviceSnapshotUpdated(const panthera::core::DeviceSnapshot& snapshot);
    void refreshDerivedMetrics();
    void storeCapturedImages();
    void loadStoredImages();
    void showHistoryPreviewMaximized();
    void showCurrentPreviewMaximized();
    void markComparisonSyncAnchor(bool historySide, bool startPoint);
    void resetComparisonSyncCalibration();
    void updateComparisonSyncState();
    bool hasComparisonSyncCalibration() const;
    bool isComparisonSyncActive() const;
    void applyComparisonSyncSliderValue(int value);
    int comparisonSyncRange() const;
    int mappedComparisonSliceIndex(int syncValue, int startIndex, int endIndex) const;

private:
    struct StagedSliceState {
        panthera::core::ImageSeriesRecord image;
        QVector<AnnotationStroke> annotations;
        QVector<panthera::core::TherapyPoint> targets;
        QString label;
        panthera::core::TreatmentPattern pattern {panthera::core::TreatmentPattern::Point};
        double spacingMm {0.0};
        double dwellSeconds {0.0};
        double powerWatts {0.0};
        bool respiratoryTrackingEnabled {false};
        QString deliveryMode;
        double annotatedAreaMm2 {0.0};
        double estimatedVolumeCm3 {0.0};
        double ablatedVolumeCm3 {0.0};
        QVector<panthera::core::TherapyPoint> respiratoryAdjustedTargets;
        QRectF respiratoryCalibrationBoxMm;
        QPointF respiratoryBaselineCentroidMm;
        QPointF respiratoryLiveCentroidMm;
        QPointF respiratoryOffsetMm;
        QString respiratorySummary;
        bool edited {false};
        bool targetsGenerated {false};
        bool respiratoryTrackingCalibrated {false};
    };

    struct PathEditingState {
        QVector<panthera::core::ImageSeriesRecord> stagedImageSeries;
        QVector<StagedSliceState> stagedSlices;
        QString assessmentText;
        QString planPreviewText;
        panthera::core::TherapyPlan activePlan;
        QDateTime lastAcquisitionAt;
        int currentStagedSliceIndex {-1};
        bool annotationPanelExpanded {false};
        bool hasActivePlan {false};
    };

    panthera::core::TherapyPlan buildPlanFromUi(panthera::core::ApprovalState approvalState) const;
    panthera::core::TherapyPlan buildPlanFromSlices(panthera::core::ApprovalState approvalState) const;
    void applyPlanToUi(const panthera::core::TherapyPlan& plan);
    void populatePatientSelector();
    void refreshImagingPaths(const QString& patientId);
    void syncPatientSelector(const QString& patientId);
    void refreshContextSummary(bool refreshHistoricalImages);
    void updateAssessmentText(const panthera::core::TherapyPlan* plan);
    void updatePlanPreviewText(const panthera::core::TherapyPlan* plan);
    void populateDefaultScanChannels();
    QString currentChannelLabel() const;
    QString currentChannelCoordinate() const;
    void updateAcquisitionSummary(const QString& title, const QStringList& lines);
    QListWidgetItem* createPathListItem(int index);
    QString pathStateKeyForRow(int row) const;
    void saveCurrentPathState();
    void loadPathState(int row);
    void rebuildModelList();
    void resetActivePathWorkspace();
    void activatePlanningWorkspace();
    void clearStartupDisplay();
    bool hasActivePathSelection() const;
    void updatePathActionState();
    void updatePlanApprovalButtonState();
    void refreshPowerCurve();
    double currentRealtimeTransducerPowerWatts() const;
    void loadHistoricalImages(bool announce = false, bool forceReload = false);
    void loadHistoricalFiles(const QStringList& filePaths);
    void loadHistoricalSlice(int row, bool announce = false);
    void clearHistoricalComparison(const QString& overlayText);
    void updateHistoryMaximizeButtonState();
    void updateSliceNavigationButtons();
    void persistCurrentSliceAnnotations();
    void loadStagedSlice(int row);
    QPixmap renderCurrentSlicePixmap(int row, const QSize& size) const;
    void refreshCurrentSliceVisualization();
    void configureCurrentPreviewView(MockUltrasoundView* preview, int row, bool useActivePreviewAnnotations = false) const;
    void applyCurrentControlsToAllSlices();
    void restoreSliceControls(int row);
    void storeCurrentSliceControls();
    void invalidateCurrentSliceTargets(const QString& title = {}, const QString& detail = {});
    void invalidateAllSliceTargets(const QString& title = {}, const QString& detail = {});
    void clearSliceTargets(bool clearAnnotations);
    void updateSliceAssessmentMetrics();
    void updateAssessmentMetricsPanel(double estimatedVolumeCm3, double ablatedVolumeCm3);
    bool hasGeneratedSliceTargets() const;
    void clearRespiratoryTrackingState(StagedSliceState& slice);
    void recalculateRespiratoryTrackingForSlice(int row);
    void showThreeDimensionalPreviewDialog(const VolumeReconstructionResult& result) const;
    void registerCollapseSection(const QString& key, QToolButton* button);
    [[nodiscard]] QVariantMap currentCollapseStateMap() const;
    void applyCollapseStateMap(const QVariantMap& stateMap);
    [[nodiscard]] QVariantMap loadSavedCollapseStateMap(const QString& profileName) const;
    void setActivePersonalizationProfileName(const QString& profileName);
    void clearActivePersonalizationProfileName();
    void restoreLastPersonalizationProfile();
    void updateAnnotationColorButtonSelection(const QColor& color);
    void setTreatmentComparisonFocusMode(bool enabled);
    void configureTreatmentComparisonSyncForLayer(int layerIndex);
    int syncValueForCurrentSliceIndex(int currentIndex) const;

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    QComboBox* m_patientCombo {nullptr};
    QListWidget* m_pathList {nullptr};
    QListWidget* m_modelList {nullptr};
    MockUltrasoundView* m_historyPreview {nullptr};
    MockUltrasoundView* m_preview {nullptr};
    QLabel* m_historyPreviewOverlayLabel {nullptr};
    QLabel* m_previewOverlayLabel {nullptr};
    QToolButton* m_historyPrevSliceButton {nullptr};
    QSlider* m_historySliceSlider {nullptr};
    QToolButton* m_historyNextSliceButton {nullptr};
    QLabel* m_historySliceSummaryLabel {nullptr};
    QToolButton* m_currentPrevSliceButton {nullptr};
    QSlider* m_currentSliceSlider {nullptr};
    QToolButton* m_currentNextSliceButton {nullptr};
    QLabel* m_currentSliceSummaryLabel {nullptr};
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
    QLabel* m_totalLengthValueLabel {nullptr};
    QLabel* m_totalDurationValueLabel {nullptr};
    QLabel* m_powerValueLabel {nullptr};
    QLabel* m_patientSummaryLabel {nullptr};
    QLabel* m_planSummaryLabel {nullptr};
    QLabel* m_chartSummaryLabel {nullptr};
    EnergyOutputChartWidget* m_energyOutputChart {nullptr};
    QLabel* m_estimatedVolumeValueLabel {nullptr};
    QLabel* m_ablatedVolumeValueLabel {nullptr};
    QLabel* m_coverageRatioValueLabel {nullptr};
    QProgressBar* m_coverageProgressBar {nullptr};
    QPlainTextEdit* m_assessmentPreview {nullptr};
    QPlainTextEdit* m_planPreview {nullptr};
    QPushButton* m_generateTargetsButton {nullptr};
    QPushButton* m_generateAssessmentButton {nullptr};
    QPushButton* m_previewPlanButton {nullptr};
    QToolButton* m_editPlanButton {nullptr};
    QToolButton* m_historyMaximizeButton {nullptr};
    QToolButton* m_currentMaximizeButton {nullptr};
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
    QCheckBox* m_comparisonSyncCheck {nullptr};
    QPushButton* m_historySyncStartButton {nullptr};
    QPushButton* m_historySyncEndButton {nullptr};
    QPushButton* m_currentSyncStartButton {nullptr};
    QPushButton* m_currentSyncEndButton {nullptr};
    QPushButton* m_resetComparisonSyncButton {nullptr};
    QToolButton* m_comparisonSyncPrevButton {nullptr};
    QToolButton* m_comparisonSyncNextButton {nullptr};
    QSlider* m_comparisonSyncSlider {nullptr};
    QHBoxLayout* m_rootLayout {nullptr};
    QWidget* m_leftColumnHost {nullptr};
    QWidget* m_centerColumnHost {nullptr};
    QWidget* m_rightColumnHost {nullptr};
    QFrame* m_previewFrame {nullptr};

    QVector<panthera::core::ImageSeriesRecord> m_historyImageSeries;
    QVector<QPixmap> m_historyPixmaps;
    QVector<panthera::core::ImageSeriesRecord> m_stagedImageSeries;
    QVector<StagedSliceState> m_stagedSlices;
    QHash<QString, PathEditingState> m_pathEditingStates;
    QString m_activePathStateKey;
    QString m_loadedHistoryPatientId;
    bool m_historyLoadedFromLocalFiles {false};
    int m_currentHistorySliceIndex {-1};
    int m_currentStagedSliceIndex {-1};
    int m_nextPathStateId {1};
    bool m_initializingUi {true};
    bool m_deferStartupContextSummary {true};
    bool m_suppressNextPatientHistoryRefresh {false};
    QDateTime m_lastAcquisitionAt;
    panthera::core::DeviceSnapshot m_latestDeviceSnapshot;
    bool m_hasDeviceSnapshot {false};
    QHash<QString, QToolButton*> m_collapseButtonsByKey;
    QString m_activePersonalizationProfileName;
    bool m_applyingPersonalizationProfile {false};
    bool m_hasHistorySyncStartPoint {false};
    bool m_hasHistorySyncEndPoint {false};
    bool m_hasCurrentSyncStartPoint {false};
    bool m_hasCurrentSyncEndPoint {false};
    int m_historySyncStartIndex {-1};
    int m_historySyncEndIndex {-1};
    int m_currentSyncStartIndex {-1};
    int m_currentSyncEndIndex {-1};
    bool m_applyingComparisonSync {false};
    bool m_treatmentComparisonFocusMode {false};
    QColor m_activeAnnotationColor {QColor(201, 71, 51)};
};

}  // namespace panthera::modules
