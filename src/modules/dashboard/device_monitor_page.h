#pragma once

#include <array>
#include <functional>

#include <QCheckBox>
#include <QHash>
#include <QLabel>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "adapters/anthone/lu926_temperature_modbus_client.h"
#include "adapters/dobot/DobotZAxisAligner.h"
#include "adapters/dobot/DobotPayloadEnableService.h"
#include "adapters/dobot/dobot_trajectory_sftp_client.h"
#include "adapters/dobot/dobot_tcp_client.h"
#include "adapters/liquidlevel/liquid_level_modbus_client.h"
#include "adapters/sim/simulation_device_facade.h"
#include "adapters/uim/uim_motor_gateway.h"
#include "adapters/waterpump/water_pump_modbus_client.h"
#include "core/safety/safety_kernel.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace panthera::modules {

class DeviceMonitorPage final : public QWidget {
    Q_OBJECT

public:
    DeviceMonitorPage(
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::adapters::dobot::DobotControllerClient* robotArmClient = nullptr,
        QWidget* parent = nullptr);
    void releaseThreeAxisGatewayForSharedUse();

private slots:
    void updateSnapshot(const panthera::core::DeviceSnapshot& snapshot);
    void updateSafety(const panthera::core::SafetySnapshot& snapshot);
    void resetFaults();
    void connectRobotArm();
    void disconnectRobotArm();
    void enableRobotArm();
    void disableRobotArm();
    void toggleRobotArmConnection(bool checked);
    void toggleRobotArmEnable(bool checked);
    void toggleRobotArmDrag(bool checked);
    void toggleRobotArmPhysicalDragButton(bool checked);
    void pollRobotArmPhysicalDragButton();
    void toggleRobotPhysicalPowerButton(bool checked);
    void moveRobotToSafeOrigin();
    void alignRobotZAxis();
    void refreshRobotTrajectoryFiles();
    void replaySelectedPresetTrajectory();
    void runSelectedRobotTrajectory();
    void pauseRobotTrajectory();
    void loadThreeAxisSdk();
    void searchThreeAxisGateways();
    void toggleThreeAxisGateway();
    void enableAllThreeAxisMotors();
    void disableAllThreeAxisMotors();
    void emergencyStopThreeAxisMotors();
    void releaseThreeAxisEmergencyStop();
    void zeroAllThreeAxisMotors();
    void refreshThreeAxisMotors();
    void pollRobotArmSafetyWall();
    void refreshWaterPumpSerialPorts();
    void toggleWaterPumpConnection();
    void refreshTemperatureSerialPorts();
    void toggleTemperatureConnection();
    void refreshLiquidLevelSerialPorts();
    void toggleLiquidLevelConnection();
    void readLiquidLevelValue();

private:
    QLabel* createValueLabel();
    QWidget* createMetricCard(const QString& title, const QVector<QPair<QString, QLabel*>>& metrics);
    QWidget* createWaterLoopControlCard();
    QWidget* createTemperatureControlCard();
    QWidget* createLiquidLevelSensorCard();
    QWidget* createRobotArmControlCard();
    QWidget* createThreeAxisMotorControlCard();
    void bindFaultToggle(QCheckBox* checkBox, panthera::core::InterlockReason reason);
    void loadRobotArmSettings();
    void applyRobotArmSettingsToClient();
    bool prepareRobotArmForSafeMotion(QString* errorMessage);
    bool applyRobotArmSafeSpeed(QString* errorMessage);
    bool requestRobotArmControl(QString* errorMessage, bool logCommand = true);
    void noteRobotArmSafetyWallAlarm(int robotMode, const QString& alarmPayload);
    bool confirmAndClearRobotArmAlarm(int robotMode, const QString& alarmPayload, int commandErrorId, bool forcePrompt);
    QString robotSafeWallLabel() const;
    bool moveRobotToPose(
        const panthera::adapters::dobot::DobotPose& pose,
        int userIndex,
        int toolIndex,
        QString* errorMessage);
    bool moveRobotToTrajectoryStart(const QString& traceName, int pathType, QString* errorMessage);
    QString currentPresetTrajectoryName() const;
    void updateRobotTrajectoryCombo(const QStringList& traceNames);
    void setRobotArmStatus(const QString& message);
    void appendRobotArmLog(const QString& message);
    void logRobotArmCommand(const QString& action, const panthera::adapters::dobot::DobotCommandResult& result, const QString& commandError);
    void refreshRobotArmUi();
    void syncRobotArmSwitches();
    bool setRobotArmDragMode(bool checked, QString* errorMessage, bool logDiagnostics = true);
    void resetRobotArmPhysicalDragButtonState();
    bool prepareRobotPhysicalPowerMotorGateway(QString* errorMessage);
    bool setRobotPhysicalPowerPins(bool node6High, bool node7High, const QString& action, QString* errorMessage);
    quint8 waterPumpAddress(int pumpIndex) const;
    QString waterPumpName(int pumpIndex) const;
    QString waterPumpAddressText(int pumpIndex) const;
    bool sharedRs485Connected() const;
    void closeSharedRs485Clients();
    void refreshSharedRs485Ui();
    void setSharedRs485ConnectedStatus(const QString& source);
    void setSharedRs485DisconnectedStatus(const QString& source);
    bool openSharedRs485ForWaterPump(QString* errorMessage);
    bool openSharedRs485ForTemperature(QString* errorMessage);
    bool openSharedRs485ForLiquidLevel(QString* errorMessage);
    bool ensureWaterPumpConnection();
    void setWaterPumpStatus(const QString& message, bool ok);
    void refreshWaterPumpUi();
    bool applySharedWaterPumpFlow(double flow, QStringList* responses, QStringList* failures);
    bool setWaterPumpFlowValue(int pumpIndex, double flow, QString* responseText, QString* errorMessage);
    void setWaterPumpFlow(int pumpIndex);
    void readWaterPumpFlow(int pumpIndex);
    void setWaterPumpRunDuration(int pumpIndex);
    void readWaterPumpConfiguredRunDuration(int pumpIndex);
    void readWaterPumpRealtimeRunDuration(int pumpIndex);
    void startWaterPump(int pumpIndex);
    void stopWaterPump(int pumpIndex);
    void setWaterPumpClockwise(int pumpIndex);
    void toggleTank2Fill();
    void pollTank2FillLevel();
    void stopTank2Fill(bool reachedTarget, const QString& reason);
    void startWaterLoop();
    void stopWaterLoop();
    bool ensureRobotPumpControl(QString* errorMessage);
    bool sendRobotPumpDo(int index, bool on, const QString& action, QString* errorMessage);
    bool setRobotPumpMode(bool do13On, bool do14On, const QString& action, QString* errorMessage);
    void startRobotPumpForward();
    void startRobotPumpReverse();
    void stopRobotPump();
    bool ensureTemperatureConnection();
    void setTemperatureStatus(const QString& message, bool ok);
    void refreshTemperatureUi();
    void setTemperatureSetpoint();
    void readTemperatureValue();
    bool readCurrentPv1Temperature(double& temperature, QString& errorMessage, bool verboseLog);
    void updateRealtimeTemperature();
    bool ensureLiquidLevelConnection();
    void setLiquidLevelStatus(const QString& message, bool ok);
    void refreshLiquidLevelUi();
    void handleWaterTankLevelSensors(const diji::adapters::uim::UimMotorSnapshot& snapshot);
    void updateWaterTankLimitStatus();
    void triggerWaterTankLowLevelAlarm();
    QString setTemperatureSetpointsToZeroForWaterTankAlarm();
    void moveThreeAxisMotor(int axisIndex, int direction);
    void startThreeAxisContinuousMove(int axisIndex, int direction);
    void stopThreeAxisContinuousMove(int axisIndex);
    void moveThreeAxisMotorToAbsolute(int axisIndex);
    void enableThreeAxisMotor(int axisIndex);
    void disableThreeAxisMotor(int axisIndex);
    bool configureThreeAxisMotorParameters();
    bool cancelThreeAxisMotorMotion(int axisIndex, QString* errorMessage);
    bool refreshThreeAxisMotorSnapshot(int axisIndex, diji::adapters::uim::UimMotorSnapshot* snapshot, QString* errorMessage);
    bool stopThreeAxisMotorAndRestoreSpeed(int axisIndex, QString* errorMessage);
    bool zeroThreeAxisLinearMotorToS2(int axisIndex, QString* errorMessage);
    bool zeroThreeAxisSwingMotor(QString* errorMessage);
    bool runThreeAxisCommand(int axisIndex, const QString& action, const std::function<bool(QString*)>& command);
    bool selectThreeAxisNode(int axisIndex, QString* errorMessage);
    bool threeAxisNodeAvailable(int axisIndex) const;
    int threeAxisMinimumSteps(int axisIndex) const;
    int threeAxisMaximumSteps(int axisIndex) const;
    int threeAxisMoveSteps(int axisIndex, double amount) const;
    double threeAxisStepsToDisplayUnits(int axisIndex, int steps) const;
    int threeAxisDisplayUnitsToSteps(int axisIndex, double value) const;
    double threeAxisMinimumDisplayUnits(int axisIndex) const;
    double threeAxisMaximumDisplayUnits(int axisIndex) const;
    int threeAxisDisplayDecimals(int axisIndex) const;
    int threeAxisSpeedForAxis(int axisIndex) const;
    double threeAxisJogAmountForAxis(int axisIndex) const;
    double threeAxisContinuousAmountForAxis(int axisIndex) const;
    QString threeAxisJogActionTitle(int axisIndex, int direction) const;
    bool threeAxisAbsoluteTargetAllowed(int axisIndex, int targetSteps, QString* errorMessage) const;
    bool threeAxisCachedSensorLimitAllowsMove(int axisIndex, int deltaSteps) const;
    bool threeAxisSensorLimitAllowsMove(int axisIndex, int deltaSteps, QString* errorMessage);
    QString threeAxisDisplayUnitText(int axisIndex) const;
    QString threeAxisPositionRangeText(int axisIndex) const;
    QString threeAxisPositionText(int axisIndex, int positionSteps) const;
    QString threeAxisAxisTitle(int axisIndex) const;
    void updateThreeAxisSnapshot(const diji::adapters::uim::UimMotorSnapshot& snapshot);
    void updateThreeAxisNodeStatus();
    void setThreeAxisStatus(const QString& message);
    void appendThreeAxisLog(const QString& message);
    void refreshThreeAxisUi();
    static QString defaultThreeAxisSdkPath();

    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::adapters::dobot::DobotControllerClient m_ownedRobotArmClient;
    panthera::adapters::dobot::DobotControllerClient& m_robotArmClient;
    panthera::adapters::dobot::DobotZAxisAligner m_robotZAxisAligner;
    panthera::adapters::dobot::DobotConnectionSettings m_robotArmSettings;
    panthera::adapters::dobot::DobotPose m_robotSafeOriginPose;
    int m_robotSafeOriginUserIndex {0};
    int m_robotSafeOriginToolIndex {0};
    QStringList m_robotTrajectoryFiles;
    QStringList m_robotConfiguredTrajectoryFiles;
    panthera::adapters::dobot::DobotTrajectorySftpSettings m_robotTrajectorySftpSettings;
    int m_robotTrajectoryPathType {1};
    panthera::adapters::dobot::DobotStartPathOptions m_robotStartPathOptions;
    int m_robotZAxisAlignUserIndex {0};
    int m_robotZAxisAlignToolIndex {0};
    double m_robotZAxisAlignRx {180.0};
    double m_robotZAxisAlignRy {0.0};
    double m_robotZAxisAlignRz {0.0};
    bool m_robotZAxisAlignKeepCurrentRz {true};
    double m_robotZAxisAlignMaxJointDeltaDeg {180.0};
    panthera::adapters::dobot::DobotPose m_robotZAxisAlignTargetPose;
    QVector<double> m_robotZAxisAlignTargetJoint;
    bool m_hasRobotZAxisAlignTarget {false};
    int m_robotSafeSpeedPercent {1};
    int m_robotSafeAccelerationPercent {1};
    QString m_robotSafeWallName {QStringLiteral("testSafe")};
    int m_robotSafeWallIndex {1};
    bool m_robotSafeWallMonitorEnabled {true};
    int m_robotSafeWallPollIntervalMs {1000};
    bool m_robotSafeWallRecoveryMode {false};
    bool m_robotSafeWallAlarmLatched {false};
    bool m_robotAlarmClearDialogActive {false};
    bool m_robotAlarmClearPromptDismissed {false};
    QString m_robotLastSafeWallAlarmPayload;
    int m_robotLastSafeWallMode {-1};
    bool m_robotPayloadApplyBeforeEnable {false};
    panthera::adapters::dobot::DobotPayloadPresetOptions m_robotPayloadOptions;
    bool m_robotEnabled {false};
    bool m_robotArmDragging {false};
    bool m_robotPhysicalDragMonitorEnabled {false};
    bool m_robotPhysicalDragButtonPressed {false};
    bool m_robotPhysicalDragButtonStateKnown {false};
    bool m_robotPhysicalPowerSwitchOn {false};
    bool m_waterTankHighLevelKnown {false};
    bool m_waterTankHighLevelActive {false};
    bool m_waterTankLowLevelKnown {false};
    bool m_waterTankLowLevelActive {false};
    bool m_waterTankLowLevelAlarmLatched {false};
    bool m_updatingRobotArmSwitches {false};
    bool m_robotTrajectoryRefreshRunning {false};
    panthera::adapters::anthone::Lu926TemperatureModbusClient m_temperatureClient;
    panthera::adapters::liquidlevel::LiquidLevelModbusClient m_liquidLevelClient;
    quint8 m_liquidLevelAddress {panthera::adapters::liquidlevel::LiquidLevelModbusClient::kDefaultAddress};
    panthera::adapters::waterpump::WaterPumpModbusClient m_waterPumpClient;
    diji::adapters::uim::UimMotorGateway m_threeAxisGateway;
    QVector<diji::adapters::uim::UimDeviceInfo> m_threeAxisDevices;
    QVector<diji::adapters::uim::UimNodeInfo> m_threeAxisNodes;
    std::array<int, 3> m_threeAxisSoftPositionSteps {0, 0, 0};
    std::array<bool, 3> m_threeAxisPositionKnown {false, false, false};
    std::array<bool, 3> m_threeAxisSensorFeedbackKnown {false, false, false};
    std::array<bool, 3> m_threeAxisSensor1High {true, true, true};
    std::array<bool, 3> m_threeAxisSensor2High {true, true, true};
    std::array<int, 3> m_threeAxisPreEmergencySpeeds {0, 0, 0};
    bool m_threeAxisEmergencyStopActive {false};
    bool m_threeAxisZeroingActive {false};
    QHash<QString, QLabel*> m_valueLabels;
    QLabel* m_safetyStateLabel {nullptr};
    QLabel* m_interlockLabel {nullptr};
    QLabel* m_robotArmStatusLabel {nullptr};
    QPlainTextEdit* m_robotArmLogEdit {nullptr};
    QLineEdit* m_robotArmHostEdit {nullptr};
    QCheckBox* m_robotArmConnectionSwitch {nullptr};
    QCheckBox* m_robotArmEnableSwitch {nullptr};
    QCheckBox* m_robotArmDragSwitch {nullptr};
    QCheckBox* m_robotArmPhysicalDragButtonSwitch {nullptr};
    QCheckBox* m_robotPhysicalPowerButtonSwitch {nullptr};
    QPushButton* m_robotArmSafeOriginButton {nullptr};
    QPushButton* m_robotArmZAxisAlignButton {nullptr};
    QPushButton* m_robotArmRefreshTrajectoriesButton {nullptr};
    QPushButton* m_robotArmReplayPresetButton {nullptr};
    QPushButton* m_robotArmRunTrajectoryButton {nullptr};
    QPushButton* m_robotArmPauseButton {nullptr};
    QComboBox* m_robotArmTrajectoryCombo {nullptr};
    QPlainTextEdit* m_waterPumpLogEdit {nullptr};
    QLineEdit* m_waterPumpPortCombo {nullptr};
    QLineEdit* m_waterPumpBaudCombo {nullptr};
    QPushButton* m_waterPumpRefreshPortsButton {nullptr};
    QPushButton* m_waterPumpConnectionButton {nullptr};
    QDoubleSpinBox* m_tank2FillTargetLevelSpin {nullptr};
    QPushButton* m_tank2FillButton {nullptr};
    std::array<QDoubleSpinBox*, 2> m_waterPumpFlowSpins {nullptr, nullptr};
    std::array<QSpinBox*, 2> m_waterPumpRunDurationSpins {nullptr, nullptr};
    QVector<QWidget*> m_waterPumpCommandWidgets;
    QString m_lastWaterPumpAlertMessage;
    qint64 m_lastWaterPumpAlertTimestampMs {0};
    QLabel* m_temperatureResultLabel {nullptr};
    QLineEdit* m_temperaturePortCombo {nullptr};
    QLineEdit* m_temperatureBaudCombo {nullptr};
    QPushButton* m_temperatureRefreshPortsButton {nullptr};
    QPushButton* m_temperatureConnectionButton {nullptr};
    QComboBox* m_temperatureChannelCombo {nullptr};
    QDoubleSpinBox* m_temperatureSetpointSpin {nullptr};
    QLineEdit* m_temperatureCurrentDisplay {nullptr};
    QPushButton* m_temperatureSetButton {nullptr};
    QPushButton* m_temperatureReadButton {nullptr};
    QLabel* m_liquidLevelResultLabel {nullptr};
    QLineEdit* m_liquidLevelPortCombo {nullptr};
    QLineEdit* m_liquidLevelBaudCombo {nullptr};
    QPushButton* m_liquidLevelRefreshPortsButton {nullptr};
    QPushButton* m_liquidLevelConnectionButton {nullptr};
    QLineEdit* m_liquidLevelAddressEdit {nullptr};
    QPushButton* m_liquidLevelReadButton {nullptr};
    QLabel* m_waterTankLimitStatusLabel {nullptr};
    QPlainTextEdit* m_threeAxisLogEdit {nullptr};
    QTimer m_robotArmSafetyWallTimer;
    QTimer m_robotPhysicalDragPollTimer;
    QTimer m_tank2FillTimer;
    QTimer m_temperatureRealtimeTimer;
    bool m_temperatureRequestBusy {false};
    bool m_tank2FillActive {false};
    double m_tank2FillTargetLevelCentimeters {0.0};
    QLineEdit* m_threeAxisSdkPathEdit {nullptr};
    QComboBox* m_threeAxisDeviceCombo {nullptr};
    QPushButton* m_threeAxisLoadSdkButton {nullptr};
    QPushButton* m_threeAxisSearchButton {nullptr};
    QPushButton* m_threeAxisGatewayButton {nullptr};
    QPushButton* m_threeAxisEnableAllButton {nullptr};
    QPushButton* m_threeAxisDisableAllButton {nullptr};
    QPushButton* m_threeAxisEmergencyStopButton {nullptr};
    QPushButton* m_threeAxisReleaseEmergencyStopButton {nullptr};
    QPushButton* m_threeAxisZeroAllButton {nullptr};
    std::array<QLabel*, 3> m_threeAxisNodeLabels {nullptr, nullptr, nullptr};
    std::array<QLabel*, 3> m_threeAxisSoftPositionLabels {nullptr, nullptr, nullptr};
    std::array<QDoubleSpinBox*, 3> m_threeAxisTargetPositionSpins {nullptr, nullptr, nullptr};
    std::array<QDoubleSpinBox*, 3> m_threeAxisJogDistanceSpins {nullptr, nullptr, nullptr};
    std::array<QDoubleSpinBox*, 3> m_threeAxisContinuousDistanceSpins {nullptr, nullptr, nullptr};
    std::array<QPushButton*, 3> m_threeAxisMoveToButtons {nullptr, nullptr, nullptr};
    std::array<QPushButton*, 3> m_threeAxisNegativeButtons {nullptr, nullptr, nullptr};
    std::array<QPushButton*, 3> m_threeAxisPositiveButtons {nullptr, nullptr, nullptr};
    std::array<QPushButton*, 3> m_threeAxisContinuousNegativeButtons {nullptr, nullptr, nullptr};
    std::array<QPushButton*, 3> m_threeAxisContinuousPositiveButtons {nullptr, nullptr, nullptr};
    std::array<bool, 3> m_threeAxisContinuousMoving {false, false, false};
    QTimer m_threeAxisRefreshTimer;
    QVector<QCheckBox*> m_faultToggles;
};

}  // panthera::modules 命名空间
