#pragma once

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

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
};

struct TherapySegment {
    QString id;
    int orderIndex {0};
    QString label;
    double plannedDurationSeconds {0.0};
    QVector<TherapyPoint> points;
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
    bool respiratoryTrackingEnabled {false};
    QDateTime createdAt;
    QDateTime approvedAt;
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

}  // panthera::core 命名空间

Q_DECLARE_METATYPE(panthera::core::PatientRecord)
Q_DECLARE_METATYPE(panthera::core::TherapyPlan)
Q_DECLARE_METATYPE(panthera::core::DeviceSnapshot)
Q_DECLARE_METATYPE(panthera::core::SafetySnapshot)
Q_DECLARE_METATYPE(panthera::core::AuditEntry)
