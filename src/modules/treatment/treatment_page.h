#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QWidget>

#include "adapters/anthone/lu926_temperature_modbus_client.h"
#include "adapters/dobot/dobot_tcp_client.h"
#include "adapters/liquidlevel/liquid_level_modbus_client.h"
#include "adapters/uim/uim_motor_gateway.h"
#include "adapters/sim/simulation_device_facade.h"
#include "adapters/waterpump/water_pump_modbus_client.h"
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
    ~TreatmentPage() override;

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
    void refreshFluidSerialPorts();
    void toggleWaterPumpConnection();
    void toggleTemperatureConnection();
    void toggleLiquidLevelConnection();
    void startRobotPumpFill();
    void startRobotPumpDrain();
    void stopRobotPumpFromUi();
    void confirmTank2Fill();
    void startWaterCycle();
    void stopWaterCycle();
    void stopFluidDevicesFromUi();
    void startHeating();
    void stopHeating();
    void onFluidControlTick();
    void onTemperatureRefreshTick();

private:
    enum class FluidWorkflowState {
        Idle,
        FillingTank1,
        WaitingTank2Confirm,
        FillingTank2,
        ReadyToCycle,
        Cycling
    };

    enum class CycleBalanceMode {
        Unknown,
        BothPumps,
        FillingTank2,
        DrainingTank2
    };

    void setButtonState(bool canStart, bool canPause, bool canResume, bool canStop);
    QWidget* createFluidControlCard();
    bool isPlanTreatable(const panthera::core::TherapyPlan& plan) const;
    bool canTreatSelectedLayer() const;
    int layerCount(const panthera::core::TherapyPlan* plan = nullptr) const;
    int normalizedLayerIndex(const panthera::core::TherapyPlan& plan) const;
    int visualizationSliceIndexForSelectedLayer(const panthera::core::TherapyPlan& plan) const;
    const panthera::core::TherapySegment* selectedLayerSegment() const;
    panthera::core::TherapyPlan selectedLayerPlan(const panthera::core::TherapyPlan& plan) const;
    bool selectedLayerHasSourceImage(const panthera::core::TherapyPlan& plan) const;
    bool applySelectedLayerPreviewImage(const panthera::core::TherapyPlan& plan);
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
    bool prepareTreatmentMotorGateway(QString* errorMessage);
    bool selectTreatmentMotor(quint32 nodeId, QString* errorMessage);
    bool readTreatmentMotorSnapshot(
        quint32 nodeId,
        diji::adapters::uim::UimMotorSnapshot* snapshot,
        QString* errorMessage);
    bool readTreatmentMotorPosition(quint32 nodeId, int* positionSteps, QString* errorMessage);
    bool moveTreatmentMotorToAbsolute(
        quint32 nodeId,
        int targetPositionSteps,
        int minimumPositionSteps,
        int maximumPositionSteps,
        const QString& axisLabel,
        QString* errorMessage);
    bool prepareSelectedLayerTreatmentMotors(
        const panthera::core::TherapyPlan& plan,
        const panthera::core::TherapySegment& segment,
        QString* errorMessage);
    bool moveTreatmentPointMotors(const panthera::core::TherapySegment& segment, int pointIndex, QString* errorMessage);
    void waitForTreatmentMotor(int milliseconds);
    void loadTreatmentRobotPumpSettings();
    void applyTreatmentRobotPumpSettings();
    bool ensureRobotPumpControl(QString* errorMessage);
    bool sendRobotPumpDo(int index, bool on, const QString& action, QString* errorMessage);
    bool setRobotPumpMode(bool do13On, bool do14On, const QString& action, QString* errorMessage);
    bool ensureWaterPumpConnection();
    bool ensureTemperatureConnection();
    bool ensureLiquidLevelConnection();
    bool setWaterPumpFlow(quint8 address, double flowMlPerMin, QString* errorMessage, QByteArray* response = nullptr);
    bool startWaterPump(quint8 address, QString* errorMessage, QByteArray* response = nullptr);
    bool stopWaterPump(quint8 address, QString* errorMessage, QByteArray* response = nullptr);
    bool stopAllFluidDevices(bool resetWorkflow, bool stopHeating, QString* summary = nullptr);
    bool readTank1UpperLimit(bool* active, QString* errorMessage);
    bool tank1UpperLimitDebounced(bool active);
    void resetTank1UpperLimitDebounce();
    bool readTank2Level(double* millimeters, QString* errorMessage, QByteArray* response = nullptr);
    int selectedTemperatureChannel() const;
    bool setTemperatureSetpoint(double celsius, QString* errorMessage, QByteArray* response = nullptr);
    void stopHeatingForAlarm(const QString& reason);
    void setFluidStatus(const QString& message, bool ok);
    void setTemperatureStatus(const QString& message, bool ok);
    void refreshFluidUi();
    void refreshTemperatureUi();
    void updateTank2LevelDisplay(double millimeters);
    void setFluidWorkflowState(FluidWorkflowState state);
    void handleTank1UpperLimitReached();
    void startTank2FillInternal();
    void setCycleBalanceMode(CycleBalanceMode mode);
    QString fluidWorkflowStateText() const;

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};
    panthera::adapters::dobot::DobotControllerClient m_robotPumpClient;
    panthera::adapters::dobot::DobotConnectionSettings m_robotPumpSettings;
    panthera::adapters::anthone::Lu926TemperatureModbusClient m_temperatureClient;
    panthera::adapters::liquidlevel::LiquidLevelModbusClient m_liquidLevelClient;
    panthera::adapters::waterpump::WaterPumpModbusClient m_waterPumpClient;
    diji::adapters::uim::UimMotorGateway m_treatmentMotorGateway;
    QVector<diji::adapters::uim::UimDeviceInfo> m_treatmentMotorDevices;
    QVector<diji::adapters::uim::UimNodeInfo> m_treatmentMotorNodes;

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
    QLabel* m_fluidStatusLabel {nullptr};
    QLabel* m_temperatureStatusLabel {nullptr};
    QLabel* m_tank2LevelLabel {nullptr};
    QComboBox* m_waterPumpPortCombo {nullptr};
    QComboBox* m_waterPumpBaudCombo {nullptr};
    QPushButton* m_waterPumpConnectionButton {nullptr};
    QComboBox* m_temperaturePortCombo {nullptr};
    QComboBox* m_temperatureBaudCombo {nullptr};
    QPushButton* m_temperatureConnectionButton {nullptr};
    QComboBox* m_temperatureChannelCombo {nullptr};
    QDoubleSpinBox* m_temperatureSetpointSpin {nullptr};
    QPushButton* m_temperatureStartButton {nullptr};
    QPushButton* m_temperatureStopButton {nullptr};
    QComboBox* m_liquidLevelPortCombo {nullptr};
    QComboBox* m_liquidLevelBaudCombo {nullptr};
    QPushButton* m_liquidLevelConnectionButton {nullptr};
    QLineEdit* m_liquidLevelAddressEdit {nullptr};
    QDoubleSpinBox* m_tank2TargetLevelSpin {nullptr};
    QPushButton* m_robotPumpFillButton {nullptr};
    QPushButton* m_robotPumpDrainButton {nullptr};
    QPushButton* m_robotPumpStopButton {nullptr};
    QPushButton* m_confirmTank2FillButton {nullptr};
    QPushButton* m_startCycleButton {nullptr};
    QPushButton* m_stopCycleButton {nullptr};
    QPushButton* m_stopFluidDevicesButton {nullptr};
    QTimer m_progressTimer;
    QTimer m_fluidControlTimer;
    QTimer m_temperatureRefreshTimer;
    QElapsedTimer m_tank1UpperLimitDebounceTimer;
    FluidWorkflowState m_fluidWorkflowState {FluidWorkflowState::Idle};
    CycleBalanceMode m_cycleBalanceMode {CycleBalanceMode::Unknown};
    bool m_tank1UpperLimitRawActive {false};
    bool m_robotPumpFillingTank1 {false};
    bool m_hasTank2Level {false};
    double m_lastTank2LevelMillimeters {0.0};
    double m_activeTemperatureTargetCelsius {0.0};
    bool m_temperatureAlarmDisplayed {false};
    int m_selectedLayerIndex {0};
    int m_completedPointCount {0};
    double m_deliveredEnergyJ {0.0};
    QVector<int> m_layerCompletedPointCounts;
    int m_treatmentSwingCenterSteps {0};
    bool m_hasTreatmentSwingCenter {false};
    bool m_deferStartupPlanSelection {true};
};

}  // namespace panthera::modules
