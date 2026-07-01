#pragma once

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

#include <array>

namespace panthera::core {

// 这里定义整个原型系统共用的工作流类型与数据契约。
// 该头文件应保持与界面层解耦，方便测试、适配器和后续服务层复用。

// 工作站的高层运行模式，由导航壳和安全内核共同驱动。
enum class SystemMode {
    Startup,
    Idle,
    Planning,
    TreatmentReady,
    Treating,
    Paused,
    Alarm,
    Maintenance,
    Shutdown
};

enum class SafetyState {
    Green,
    Yellow,
    Red
};

enum class AlarmLevel {
    Info,
    Warning,
    Critical
};

enum class InterlockReason {
    None,
    NoPatientSelected,
    PlanNotApproved,
    WaterLoopFault,
    TemperatureFault,
    PowerFault,
    MotionFault,
    EmergencyStop,
    UltrasoundUnavailable
};

enum class ApprovalState {
    Draft,
    UnderReview,
    Approved,
    Locked,
    Superseded
};

enum class RoleType {
    Operator,
    Physician,
    Engineer,
    Administrator
};

enum class TreatmentPattern {
    Point,
    Line,
    Segmented
};

enum class InfusionPumpRunState {
    Unknown,
    Stopped,
    Running,
    Fault
};

enum class InfusionPumpOperatingMode {
    Speed,
    Flow
};

enum class InfusionPumpCycleMode {
    Automatic,
    SemiAutomatic,
    Manual
};

enum class InfusionPumpDirection {
    Clockwise,
    CounterClockwise
};

enum class TemperatureInputType {
    ThermocoupleS,
    ThermocoupleR,
    ThermocoupleB,
    ThermocoupleK,
    ThermocoupleN,
    ThermocoupleE,
    ThermocoupleJ,
    ThermocoupleT,
    Pt100,
    Cu50,
    Cu100,
    Millivolt,
    Resistance,
    Disabled
};

// 六轴治疗头通用位姿定义。
struct Coordinate6D {
    double x {0.0};
    double y {0.0};
    double z {0.0};
    double a {0.0};
    double b {0.0};
    double c {0.0};
};

struct PatientRecord {
    QString id;
    QString name;
    int age {0};
    QString gender;
    QString diagnosis;
    QString contact;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime deletedAt;
};

struct ImageSeriesRecord {
    QString id;
    QString patientId;
    QString type;
    QString storagePath;
    QDate acquisitionDate;
    QString notes;
    QDateTime createdAt;
};

// 规划模块输出的治疗点数据在下游应视为只读。
// 方案一旦激活，执行模块应消费其快照，而不是在原方案上直接修改点位状态。
struct TherapyPoint {
    int index {0};
    QPointF positionMm;
    double dwellSeconds {0.0};
    double powerWatts {0.0};
    int lineGroupIndex {-1};
    int lineSampleIndex {0};
    bool lineStart {false};
    bool lineEnd {false};
};

struct TherapySegment {
    QString id;
    int orderIndex {0};
    QString label;
    double plannedDurationSeconds {0.0};
    QVector<TherapyPoint> points;
    int sourceSliceIndex {-1};
    int axis7PositionSteps {-1};
    QString sourceImagePath;
};

struct TherapyPlan {
    QString id;
    QString patientId;
    QString name;
    TreatmentPattern pattern {TreatmentPattern::Point};
    ApprovalState approvalState {ApprovalState::Draft};
    QVector<TherapySegment> segments;
    double plannedPowerWatts {0.0};
    double spacingMm {0.0};
    double dwellSeconds {0.0};
    bool respiratoryTrackingEnabled {false};
    QString deliveryMode;
    double coordinateX {0.0};
    double coordinateY {0.0};
    double coordinateZ {0.0};
    double depthMm {0.0};
    QDateTime createdAt;
    QDateTime approvedAt;
    QString approvedBy;
};

struct TreatmentSessionRecord {
    QString id;
    QString patientId;
    QString planId;
    QString lesionType;
    QString pathSummary;
    QDateTime treatmentDate;
    QDateTime startedAt;
    QDateTime endedAt;
    double totalEnergyJ {0.0};
    double totalDurationSeconds {0.0};
    double dose {0.0};
    QString status;
    QDateTime createdAt;
};

struct TreatmentRecord {
    QString id;
    QString sessionId;
    int segmentIndex {0};
    int pointIndex {0};
    QDateTime executedAt;
    double deliveredEnergyJ {0.0};
    double deliveredDose {0.0};
};

struct TreatmentReportRecord {
    QString id;
    QString patientId;
    QString treatmentSessionId;
    QDateTime generatedAt;
    QString title;
    QString contentHtml;
    QString notes;
};

// DIP 1500 V2 注水蠕动泵的中立遥测模型。
// 真实硬件适配器必须先完成范围校验、通信时效校验和故障码解析，再更新该结构。
struct InfusionPumpTelemetry {
    bool connected {false};
    bool dataValid {false};
    bool dataStale {true};
    bool safetyLimitsOk {false};
    QString backendName;
    QString lastError;
    InfusionPumpRunState runState {InfusionPumpRunState::Unknown};
    InfusionPumpOperatingMode operatingMode {InfusionPumpOperatingMode::Flow};
    InfusionPumpCycleMode cycleMode {InfusionPumpCycleMode::Automatic};
    InfusionPumpDirection direction {InfusionPumpDirection::Clockwise};
    double speedRpm {0.0};
    double targetFlowMlPerMin {0.0};
    double actualFlowMlPerMin {0.0};
    double targetVolumeMl {0.0};
    double deliveredVolumeMl {0.0};
    double runTimeSeconds {0.0};
    double stopTimeSeconds {0.0};
    int modbusAddress {192};
    int baudRate {9600};
    QDateTime updatedAt;
};

// LU-926UT6Y 六路温控模块的中立遥测模型。
// 真实硬件适配器必须完成 Modbus CRC、地址/功能码校验、0.1 摄氏度换算、
// 通信时效校验和报警/故障位解析后，再更新该结构。
struct TemperatureChannelTelemetry {
    int channelIndex {1};
    QString label;
    bool enabled {true};
    bool dataValid {false};
    bool alarmActive {false};
    bool faultActive {false};
    TemperatureInputType inputType {TemperatureInputType::Pt100};
    double processTemperatureCelsius {0.0};
    double setpointTemperatureCelsius {0.0};
    double outputPercent {0.0};
    double lowerSafetyLimitCelsius {0.0};
    double upperSafetyLimitCelsius {50.0};
    QString statusMessage;
};

struct TemperatureModuleTelemetry {
    bool connected {false};
    bool dataValid {false};
    bool dataStale {true};
    bool safetyLimitsOk {false};
    QString backendName;
    QString lastError;
    int modbusAddress {1};
    int baudRate {9600};
    double samplePeriodSeconds {0.5};
    double coldJunctionTemperatureCelsius {0.0};
    unsigned int faultCode {0};
    std::array<TemperatureChannelTelemetry, 6> channels;
    QDateTime updatedAt;
};

// DeviceSnapshot 是适配层向界面层和安全逻辑输出的统一设备遥测快照。
// 后续真实硬件接入时，应把厂商协议转换为这个中立结构。
struct DeviceSnapshot {
    QDateTime capturedAt;
    Coordinate6D position;
    double inputVoltageVolts {0.0};
    double workingCurrentAmps {0.0};
    double realtimePowerWatts {0.0};
    double waterLevelPercent {0.0};
    double inletTemperatureCelsius {0.0};
    double outletTemperatureCelsius {0.0};
    double flowRateLpm {0.0};
    double pressureMpa {0.0};
    double transducerTemperatureCelsius {0.0};
    double vibrationFrequencyMhz {0.0};
    double conversionEfficiencyPercent {0.0};
    double motorLoadPercent {0.0};
    double motionAccuracyMm {0.0};
    double imageBrightness {0.0};
    double imageContrast {0.0};
    double imageClarity {0.0};
    double outputPowerWatts {0.0};
    bool waterPumpOn {true};
    bool coolerOn {true};
    bool heaterOn {true};
    bool emergencyStopEngaged {false};
    InfusionPumpTelemetry infusionPump;
    TemperatureModuleTelemetry temperatureModule;
};

struct AuditEntry {
    QDateTime occurredAt;
    QString actor;
    QString category;
    QString details;
};

struct AlarmRecord {
    QDateTime occurredAt;
    AlarmLevel level {AlarmLevel::Info};
    InterlockReason reason {InterlockReason::None};
    QString message;
    QString source;
};

// SafetySnapshot 表示安全内核综合流程前提和设备健康状态后的判定结果。
// 治疗页是否允许开始/暂停/继续，应以这里的结果为准。
struct SafetySnapshot {
    SafetyState state {SafetyState::Yellow};
    QVector<InterlockReason> activeInterlocks;
    QString message;
    bool canStartTreatment {false};
};

QString toDisplayString(SystemMode mode);
QString toDisplayString(SafetyState state);
QString toDisplayString(AlarmLevel level);
QString toDisplayString(InterlockReason reason);
QString toDisplayString(ApprovalState state);
QString toDisplayString(RoleType role);
QString toDisplayString(TreatmentPattern pattern);
QString toDisplayString(InfusionPumpRunState state);
QString toDisplayString(InfusionPumpOperatingMode mode);
QString toDisplayString(InfusionPumpCycleMode mode);
QString toDisplayString(InfusionPumpDirection direction);
QString toDisplayString(TemperatureInputType type);

}  // panthera::core 命名空间

Q_DECLARE_METATYPE(panthera::core::PatientRecord)
Q_DECLARE_METATYPE(panthera::core::TherapyPlan)
Q_DECLARE_METATYPE(panthera::core::TemperatureInputType)
Q_DECLARE_METATYPE(panthera::core::TemperatureChannelTelemetry)
Q_DECLARE_METATYPE(panthera::core::TemperatureModuleTelemetry)
Q_DECLARE_METATYPE(panthera::core::DeviceSnapshot)
Q_DECLARE_METATYPE(panthera::core::SafetySnapshot)
Q_DECLARE_METATYPE(panthera::core::AuditEntry)
