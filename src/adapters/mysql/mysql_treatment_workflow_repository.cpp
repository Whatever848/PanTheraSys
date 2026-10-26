#include "adapters/mysql/mysql_treatment_workflow_repository.h"

#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace panthera::adapters {

namespace {

QVariant nullable(const QString& value)
{
    return value.trimmed().isEmpty() ? QVariant() : QVariant(value);
}

bool isOpenDatabase(const QSqlDatabase& database, QString* errorMessage)
{
    if (database.isValid() && database.isOpen()) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("MySQL clinical connection is unavailable.");
    }
    return false;
}

bool execute(QSqlQuery* query, const QString& operation, QString* errorMessage)
{
    if (query->exec()) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("%1: %2").arg(operation, query->lastError().text());
    }
    return false;
}

}  // namespace

MySqlTreatmentWorkflowRepository::MySqlTreatmentWorkflowRepository(QString connectionName)
    : m_connectionName(std::move(connectionName))
{
}

bool MySqlTreatmentWorkflowRepository::saveImageAnnotation(
    const TreatmentImageAnnotationSnapshot& snapshot,
    QString* errorMessage) const
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }

    QSqlQuery capture(database);
    capture.prepare(QStringLiteral(
        "INSERT INTO treatment_image_capture (id, patient_id, plan_id, treatment_session_id, captured_by, layer_index, slice_index, storage_path, captured_at, acquisition_parameters) "
        "VALUES (:id, :patient_id, :plan_id, :session_id, :user_id, :layer_index, :slice_index, :storage_path, :captured_at, :parameters) "
        "ON DUPLICATE KEY UPDATE plan_id = VALUES(plan_id), treatment_session_id = COALESCE(VALUES(treatment_session_id), treatment_session_id), "
        "captured_by = VALUES(captured_by), storage_path = VALUES(storage_path), captured_at = VALUES(captured_at), acquisition_parameters = VALUES(acquisition_parameters)"));
    capture.bindValue(QStringLiteral(":id"), snapshot.capturedImageId);
    capture.bindValue(QStringLiteral(":patient_id"), snapshot.patientId);
    capture.bindValue(QStringLiteral(":plan_id"), snapshot.planId);
    capture.bindValue(QStringLiteral(":session_id"), nullable(snapshot.treatmentSessionId));
    capture.bindValue(QStringLiteral(":user_id"), nullable(snapshot.userId));
    capture.bindValue(QStringLiteral(":layer_index"), snapshot.layerIndex);
    capture.bindValue(QStringLiteral(":slice_index"), snapshot.sliceIndex);
    capture.bindValue(QStringLiteral(":storage_path"), snapshot.capturePath);
    capture.bindValue(QStringLiteral(":captured_at"), snapshot.capturedAt);
    capture.bindValue(QStringLiteral(":parameters"), snapshot.annotationPayload);
    if (!execute(&capture, QStringLiteral("save treatment image capture"), errorMessage)) {
        return false;
    }

    QSqlQuery annotation(database);
    annotation.prepare(QStringLiteral(
        "INSERT INTO treatment_image_annotation (id, captured_image_id, patient_id, plan_id, treatment_session_id, annotated_by, annotation_state, annotated_image_path, annotation_payload, target_area_mm2, target_volume_mm3, saved_reason, created_at, updated_at) "
        "VALUES (:id, :capture_id, :patient_id, :plan_id, :session_id, :user_id, 'saved', :image_path, :payload, :area, :volume, :reason, :now, :now) "
        "ON DUPLICATE KEY UPDATE plan_id = VALUES(plan_id), treatment_session_id = COALESCE(VALUES(treatment_session_id), treatment_session_id), "
        "annotated_by = VALUES(annotated_by), annotated_image_path = VALUES(annotated_image_path), annotation_payload = VALUES(annotation_payload), "
        "target_area_mm2 = VALUES(target_area_mm2), target_volume_mm3 = VALUES(target_volume_mm3), saved_reason = VALUES(saved_reason), updated_at = VALUES(updated_at)"));
    annotation.bindValue(QStringLiteral(":id"), snapshot.annotationId);
    annotation.bindValue(QStringLiteral(":capture_id"), snapshot.capturedImageId);
    annotation.bindValue(QStringLiteral(":patient_id"), snapshot.patientId);
    annotation.bindValue(QStringLiteral(":plan_id"), snapshot.planId);
    annotation.bindValue(QStringLiteral(":session_id"), nullable(snapshot.treatmentSessionId));
    annotation.bindValue(QStringLiteral(":user_id"), nullable(snapshot.userId));
    annotation.bindValue(QStringLiteral(":image_path"), snapshot.annotatedImagePath);
    annotation.bindValue(QStringLiteral(":payload"), snapshot.annotationPayload);
    annotation.bindValue(QStringLiteral(":area"), snapshot.targetAreaMm2);
    annotation.bindValue(QStringLiteral(":volume"), snapshot.targetVolumeMm3);
    annotation.bindValue(QStringLiteral(":reason"), snapshot.savedReason);
    annotation.bindValue(QStringLiteral(":now"), QDateTime::currentDateTime());
    return execute(&annotation, QStringLiteral("save treatment image annotation"), errorMessage);
}

QVector<TreatmentImageAnnotationSnapshot> MySqlTreatmentWorkflowRepository::loadPlanAnnotations(
    const QString& patientId,
    const QString& planId,
    QString* errorMessage) const
{
    QVector<TreatmentImageAnnotationSnapshot> result;
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return result;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT capture.id AS capture_id, annotation.id AS annotation_id, capture.patient_id, capture.plan_id, capture.treatment_session_id, "
        "capture.captured_by, capture.layer_index, capture.slice_index, capture.storage_path, annotation.annotated_image_path, "
        "annotation.annotation_payload, annotation.target_area_mm2, annotation.target_volume_mm3, capture.captured_at, annotation.saved_reason "
        "FROM treatment_image_annotation AS annotation "
        "JOIN treatment_image_capture AS capture ON capture.id = annotation.captured_image_id "
        "WHERE annotation.patient_id = :patient_id AND annotation.plan_id = :plan_id "
        "ORDER BY capture.layer_index, capture.slice_index, annotation.updated_at"));
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    query.bindValue(QStringLiteral(":plan_id"), planId);
    if (!execute(&query, QStringLiteral("load treatment image annotations"), errorMessage)) {
        return result;
    }

    while (query.next()) {
        TreatmentImageAnnotationSnapshot snapshot;
        snapshot.capturedImageId = query.value(QStringLiteral("capture_id")).toString();
        snapshot.annotationId = query.value(QStringLiteral("annotation_id")).toString();
        snapshot.patientId = query.value(QStringLiteral("patient_id")).toString();
        snapshot.planId = query.value(QStringLiteral("plan_id")).toString();
        snapshot.treatmentSessionId = query.value(QStringLiteral("treatment_session_id")).toString();
        snapshot.userId = query.value(QStringLiteral("captured_by")).toString();
        snapshot.layerIndex = query.value(QStringLiteral("layer_index")).toInt();
        snapshot.sliceIndex = query.value(QStringLiteral("slice_index")).toInt();
        snapshot.capturePath = query.value(QStringLiteral("storage_path")).toString();
        snapshot.annotatedImagePath = query.value(QStringLiteral("annotated_image_path")).toString();
        snapshot.annotationPayload = query.value(QStringLiteral("annotation_payload")).toString();
        snapshot.targetAreaMm2 = query.value(QStringLiteral("target_area_mm2")).toDouble();
        snapshot.targetVolumeMm3 = query.value(QStringLiteral("target_volume_mm3")).toDouble();
        snapshot.capturedAt = query.value(QStringLiteral("captured_at")).toDateTime();
        snapshot.savedReason = query.value(QStringLiteral("saved_reason")).toString();
        result.push_back(snapshot);
    }
    return result;
}

bool MySqlTreatmentWorkflowRepository::recordContinuationImports(
    const QString& previousSessionId,
    const QStringList& annotationIds,
    const QString& userId,
    QString* errorMessage) const
{
    if (annotationIds.isEmpty()) {
        return true;
    }
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO treatment_continue_import (previous_session_id, new_session_id, annotation_id, imported_by, imported_at, import_reason) "
        "VALUES (:previous_session_id, NULL, :annotation_id, :user_id, :imported_at, :reason)"));
    for (const QString& annotationId : annotationIds) {
        query.bindValue(QStringLiteral(":previous_session_id"), previousSessionId);
        query.bindValue(QStringLiteral(":annotation_id"), annotationId);
        query.bindValue(QStringLiteral(":user_id"), nullable(userId));
        query.bindValue(QStringLiteral(":imported_at"), QDateTime::currentDateTime());
        query.bindValue(QStringLiteral(":reason"), QStringLiteral("continue_treatment_restore"));
        if (!execute(&query, QStringLiteral("record treatment continuation import"), errorMessage)) {
            return false;
        }
    }
    return true;
}

bool MySqlTreatmentWorkflowRepository::attachPlanAnnotationsToTreatmentSession(
    const QString& planId,
    const QString& treatmentSessionId,
    QString* errorMessage) const
{
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }

    QSqlQuery capture(database);
    capture.prepare(QStringLiteral(
        "UPDATE treatment_image_capture SET treatment_session_id = :session_id "
        "WHERE plan_id = :plan_id AND treatment_session_id IS NULL"));
    capture.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
    capture.bindValue(QStringLiteral(":plan_id"), planId);
    if (!execute(&capture, QStringLiteral("attach treatment image captures to treatment session"), errorMessage)) {
        return false;
    }

    QSqlQuery annotation(database);
    annotation.prepare(QStringLiteral(
        "UPDATE treatment_image_annotation SET treatment_session_id = :session_id "
        "WHERE plan_id = :plan_id AND treatment_session_id IS NULL"));
    annotation.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
    annotation.bindValue(QStringLiteral(":plan_id"), planId);
    return execute(&annotation, QStringLiteral("attach treatment image annotations to treatment session"), errorMessage);
}

bool MySqlTreatmentWorkflowRepository::recordTreatmentPointRecords(
    const QString& treatmentSessionId,
    const QVector<int>& completedPointCountsBySegment,
    const QVector<double>& deliveredEnergyJBySegment,
    double totalDose,
    const QDateTime& startedAt,
    const QDateTime& endedAt,
    QString* errorMessage) const
{
    int totalCompletedPointCount = 0;
    for (const int completedPointCount : completedPointCountsBySegment) {
        totalCompletedPointCount += std::max(0, completedPointCount);
    }
    if (totalCompletedPointCount <= 0) {
        return true;
    }
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO treatment_record (id, session_id, segment_index, point_index, delivered_energy_j, delivered_dose, executed_at) "
        "VALUES (:id, :session_id, :segment_index, :point_index, :energy, :dose, :executed_at)"));
    double totalEnergyJ = 0.0;
    for (const double energyJ : deliveredEnergyJBySegment) {
        totalEnergyJ += std::max(0.0, energyJ);
    }
    const qint64 durationMs = startedAt.isValid() && endedAt.isValid() ? startedAt.msecsTo(endedAt) : 0;
    int executedPointNumber = 0;
    for (int segmentIndex = 0; segmentIndex < completedPointCountsBySegment.size(); ++segmentIndex) {
        const int completedPointCount = std::max(0, completedPointCountsBySegment.at(segmentIndex));
        if (completedPointCount == 0) {
            continue;
        }
        const double segmentEnergyJ = segmentIndex < deliveredEnergyJBySegment.size()
            ? std::max(0.0, deliveredEnergyJBySegment.at(segmentIndex))
            : 0.0;
        const double energyPerPoint = segmentEnergyJ / static_cast<double>(completedPointCount);
        const double dosePerPoint = totalEnergyJ > 0.0
            ? (totalDose * segmentEnergyJ / totalEnergyJ) / static_cast<double>(completedPointCount)
            : totalDose / static_cast<double>(totalCompletedPointCount);

        for (int pointIndex = 0; pointIndex < completedPointCount; ++pointIndex) {
            ++executedPointNumber;
            const QString recordId = QStringLiteral("TR-%1-L%2-P%3")
                                         .arg(treatmentSessionId.left(44))
                                         .arg(segmentIndex + 1)
                                         .arg(pointIndex + 1)
                                         .left(64);
            const QDateTime executedAt = durationMs > 0
                ? startedAt.addMSecs((durationMs * executedPointNumber) / totalCompletedPointCount)
                : QDateTime::currentDateTime();
            query.bindValue(QStringLiteral(":id"), recordId);
            query.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
            query.bindValue(QStringLiteral(":segment_index"), segmentIndex);
            query.bindValue(QStringLiteral(":point_index"), pointIndex);
            query.bindValue(QStringLiteral(":energy"), energyPerPoint);
            query.bindValue(QStringLiteral(":dose"), dosePerPoint);
            query.bindValue(QStringLiteral(":executed_at"), executedAt);
            if (!execute(&query, QStringLiteral("record treatment point"), errorMessage)) {
                return false;
            }
        }
    }
    return true;
}

bool MySqlTreatmentWorkflowRepository::recordTreatmentDeviceSnapshot(
    const panthera::core::DeviceSnapshot& snapshot,
    const QString& treatmentSessionId,
    const QString& userId,
    QString* errorMessage) const
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }

    const QDateTime capturedAt = snapshot.capturedAt.isValid() ? snapshot.capturedAt : QDateTime::currentDateTime();
    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }
    const auto rollback = [&database]() {
        database.rollback();
    };

    QSqlQuery deviceSnapshot(database);
    deviceSnapshot.prepare(QStringLiteral(
        "INSERT INTO device_snapshot (captured_at, voltage, current, power_watts, water_pressure_mpa, water_flow_lpm, transducer_temp_c, vibration_frequency_mhz, motor_load_percent, position_x, position_y, position_z, position_a, position_b, position_c) "
        "VALUES (:captured_at, :voltage, :current, :power, :pressure, :flow, :transducer_temp, :frequency, :motor_load, :x, :y, :z, :a, :b, :c)"));
    deviceSnapshot.bindValue(QStringLiteral(":captured_at"), capturedAt);
    deviceSnapshot.bindValue(QStringLiteral(":voltage"), snapshot.inputVoltageVolts);
    deviceSnapshot.bindValue(QStringLiteral(":current"), snapshot.workingCurrentAmps);
    deviceSnapshot.bindValue(QStringLiteral(":power"), snapshot.outputPowerWatts > 0.0 ? snapshot.outputPowerWatts : snapshot.realtimePowerWatts);
    deviceSnapshot.bindValue(QStringLiteral(":pressure"), snapshot.pressureMpa);
    deviceSnapshot.bindValue(QStringLiteral(":flow"), snapshot.flowRateLpm);
    deviceSnapshot.bindValue(QStringLiteral(":transducer_temp"), snapshot.transducerTemperatureCelsius);
    deviceSnapshot.bindValue(QStringLiteral(":frequency"), snapshot.vibrationFrequencyMhz);
    deviceSnapshot.bindValue(QStringLiteral(":motor_load"), snapshot.motorLoadPercent);
    deviceSnapshot.bindValue(QStringLiteral(":x"), snapshot.position.x);
    deviceSnapshot.bindValue(QStringLiteral(":y"), snapshot.position.y);
    deviceSnapshot.bindValue(QStringLiteral(":z"), snapshot.position.z);
    deviceSnapshot.bindValue(QStringLiteral(":a"), snapshot.position.a);
    deviceSnapshot.bindValue(QStringLiteral(":b"), snapshot.position.b);
    deviceSnapshot.bindValue(QStringLiteral(":c"), snapshot.position.c);
    if (!execute(&deviceSnapshot, QStringLiteral("record device snapshot"), errorMessage)) {
        rollback();
        return false;
    }

    const QString sessionId = treatmentSessionId.trimmed();
    const QString connectionState = snapshot.capturedAt.isValid() ? QStringLiteral("captured") : QStringLiteral("unavailable");
    const QString waterState = snapshot.waterPumpOn ? QStringLiteral("running") : QStringLiteral("stopped");
    const QString temperatureState = snapshot.temperatureModule.connected ? QStringLiteral("connected") : connectionState;
    const QString infusionState = snapshot.infusionPump.connected ? QStringLiteral("connected") : connectionState;
    const QString deviceRawMessage = QStringLiteral("flow_lpm=%1; pressure_mpa=%2; water_level_percent=%3; inlet_temp_c=%4; outlet_temp_c=%5; transducer_temp_c=%6; output_power_w=%7")
                                         .arg(snapshot.flowRateLpm, 0, 'f', 3)
                                         .arg(snapshot.pressureMpa, 0, 'f', 3)
                                         .arg(snapshot.waterLevelPercent, 0, 'f', 2)
                                         .arg(snapshot.inletTemperatureCelsius, 0, 'f', 2)
                                         .arg(snapshot.outletTemperatureCelsius, 0, 'f', 2)
                                         .arg(snapshot.transducerTemperatureCelsius, 0, 'f', 2)
                                         .arg(snapshot.outputPowerWatts > 0.0 ? snapshot.outputPowerWatts : snapshot.realtimePowerWatts, 0, 'f', 2);

    QSqlQuery waterPump(database);
    waterPump.prepare(QStringLiteral(
        "INSERT INTO water_pump_status (captured_at, login_user_id, treatment_session_id, pump_03_state, pump_02_state, pump_ro_state, vacuum_pump_state, loop_state, connection_state, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :pump03, :pump02, :pump_ro, :vacuum, :loop, :connection, :raw_message)"));
    waterPump.bindValue(QStringLiteral(":captured_at"), capturedAt);
    waterPump.bindValue(QStringLiteral(":user_id"), nullable(userId));
    waterPump.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    waterPump.bindValue(QStringLiteral(":pump03"), waterState);
    waterPump.bindValue(QStringLiteral(":pump02"), waterState);
    waterPump.bindValue(QStringLiteral(":pump_ro"), toDisplayString(snapshot.infusionPump.runState));
    waterPump.bindValue(QStringLiteral(":vacuum"), snapshot.coolerOn ? QStringLiteral("running") : QStringLiteral("stopped"));
    waterPump.bindValue(QStringLiteral(":loop"), waterState);
    waterPump.bindValue(QStringLiteral(":connection"), infusionState);
    waterPump.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&waterPump, QStringLiteral("record water pump status"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery temperature(database);
    temperature.prepare(QStringLiteral(
        "INSERT INTO temperature_control_status (captured_at, login_user_id, treatment_session_id, channel_name, set_temperature_c, measured_temperature_c, connection_state, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :channel, :set_temperature, :measured_temperature, :connection, :raw_message)"));
    temperature.bindValue(QStringLiteral(":captured_at"), capturedAt);
    temperature.bindValue(QStringLiteral(":user_id"), nullable(userId));
    temperature.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    temperature.bindValue(QStringLiteral(":channel"), QStringLiteral("treatment-head"));
    temperature.bindValue(QStringLiteral(":set_temperature"), snapshot.temperatureModule.channels.at(0).setpointTemperatureCelsius);
    temperature.bindValue(QStringLiteral(":measured_temperature"), snapshot.transducerTemperatureCelsius);
    temperature.bindValue(QStringLiteral(":connection"), temperatureState);
    temperature.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&temperature, QStringLiteral("record temperature control status"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery liquidLevel(database);
    liquidLevel.prepare(QStringLiteral(
        "INSERT INTO liquid_level_status (captured_at, login_user_id, treatment_session_id, current_level_value, connection_state, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :level, :connection, :raw_message)"));
    liquidLevel.bindValue(QStringLiteral(":captured_at"), capturedAt);
    liquidLevel.bindValue(QStringLiteral(":user_id"), nullable(userId));
    liquidLevel.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    liquidLevel.bindValue(QStringLiteral(":level"), snapshot.waterLevelPercent);
    liquidLevel.bindValue(QStringLiteral(":connection"), connectionState);
    liquidLevel.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&liquidLevel, QStringLiteral("record liquid level status"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery robotArm(database);
    robotArm.prepare(QStringLiteral(
        "INSERT INTO robot_arm_status (captured_at, login_user_id, treatment_session_id, connection_state, enabled_state, position_x, position_y, position_z, position_rx, position_ry, position_rz, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :connection, :enabled, :x, :y, :z, :rx, :ry, :rz, :raw_message)"));
    robotArm.bindValue(QStringLiteral(":captured_at"), capturedAt);
    robotArm.bindValue(QStringLiteral(":user_id"), nullable(userId));
    robotArm.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    robotArm.bindValue(QStringLiteral(":connection"), connectionState);
    robotArm.bindValue(QStringLiteral(":enabled"), snapshot.emergencyStopEngaged ? 0 : 1);
    robotArm.bindValue(QStringLiteral(":x"), snapshot.position.x);
    robotArm.bindValue(QStringLiteral(":y"), snapshot.position.y);
    robotArm.bindValue(QStringLiteral(":z"), snapshot.position.z);
    robotArm.bindValue(QStringLiteral(":rx"), snapshot.position.a);
    robotArm.bindValue(QStringLiteral(":ry"), snapshot.position.b);
    robotArm.bindValue(QStringLiteral(":rz"), snapshot.position.c);
    robotArm.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&robotArm, QStringLiteral("record robot arm status"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery motors(database);
    motors.prepare(QStringLiteral(
        "INSERT INTO three_motor_status (captured_at, login_user_id, treatment_session_id, connection_state, emergency_stop_state, motor_6_position_value, motor_6_state, motor_7_position_value, motor_7_state, motor_8_position_value, motor_8_state, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :connection, :emergency, :motor6, :motor6_state, :motor7, :motor7_state, :motor8, :motor8_state, :raw_message)"));
    motors.bindValue(QStringLiteral(":captured_at"), capturedAt);
    motors.bindValue(QStringLiteral(":user_id"), nullable(userId));
    motors.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    motors.bindValue(QStringLiteral(":connection"), connectionState);
    motors.bindValue(QStringLiteral(":emergency"), snapshot.emergencyStopEngaged ? QStringLiteral("engaged") : QStringLiteral("clear"));
    motors.bindValue(QStringLiteral(":motor6"), snapshot.position.a);
    motors.bindValue(QStringLiteral(":motor6_state"), snapshot.motorLoadPercent > 0.0 ? QStringLiteral("active") : QStringLiteral("idle"));
    motors.bindValue(QStringLiteral(":motor7"), snapshot.position.b);
    motors.bindValue(QStringLiteral(":motor7_state"), snapshot.motorLoadPercent > 0.0 ? QStringLiteral("active") : QStringLiteral("idle"));
    motors.bindValue(QStringLiteral(":motor8"), snapshot.position.c);
    motors.bindValue(QStringLiteral(":motor8_state"), snapshot.motorLoadPercent > 0.0 ? QStringLiteral("active") : QStringLiteral("idle"));
    motors.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&motors, QStringLiteral("record three motor status"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery powerDevice(database);
    powerDevice.prepare(QStringLiteral(
        "INSERT INTO power_device_status (captured_at, login_user_id, treatment_session_id, device_type, output_enabled, duty_cycle_percent, output_current_a, output_power_w, connection_state, raw_message) "
        "VALUES (:captured_at, :user_id, :session_id, :device_type, :enabled, :duty_cycle, :current, :power, :connection, :raw_message)"));
    powerDevice.bindValue(QStringLiteral(":captured_at"), capturedAt);
    powerDevice.bindValue(QStringLiteral(":user_id"), nullable(userId));
    powerDevice.bindValue(QStringLiteral(":session_id"), nullable(sessionId));
    powerDevice.bindValue(QStringLiteral(":device_type"), QStringLiteral("ultrasound-power-chain"));
    powerDevice.bindValue(QStringLiteral(":enabled"), snapshot.outputPowerWatts > 0.0 || snapshot.realtimePowerWatts > 0.0 ? 1 : 0);
    powerDevice.bindValue(QStringLiteral(":duty_cycle"), snapshot.conversionEfficiencyPercent);
    powerDevice.bindValue(QStringLiteral(":current"), snapshot.workingCurrentAmps);
    powerDevice.bindValue(QStringLiteral(":power"), snapshot.outputPowerWatts > 0.0 ? snapshot.outputPowerWatts : snapshot.realtimePowerWatts);
    powerDevice.bindValue(QStringLiteral(":connection"), connectionState);
    powerDevice.bindValue(QStringLiteral(":raw_message"), deviceRawMessage);
    if (!execute(&powerDevice, QStringLiteral("record power device status"), errorMessage)) {
        rollback();
        return false;
    }

    if (database.commit()) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = database.lastError().text();
    }
    rollback();
    return false;
}

bool MySqlTreatmentWorkflowRepository::recordClinicalOperationLogs(
    const QVector<ClinicalOperationSnapshot>& operations,
    const QString& patientId,
    const QString& treatmentSessionId,
    const QString& userId,
    const QString& doctorId,
    QString* errorMessage) const
{
    if (operations.isEmpty()) {
        return true;
    }
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO clinical_operation_log (login_user_id, doctor_id, patient_id, treatment_session_id, occurred_at, screen_name, module_name, function_name, operation_name, operation_payload, result_state, result_message, evidence_level, workstation_name, created_at) "
        "VALUES (:user_id, :doctor_id, :patient_id, :session_id, :occurred_at, 'clinical', :module_name, :function_name, :operation_name, :payload, 'recorded', :message, 'normal', 'PanTheraSys', :created_at)"));
    for (const ClinicalOperationSnapshot& operation : operations) {
        query.bindValue(QStringLiteral(":user_id"), nullable(userId));
        query.bindValue(QStringLiteral(":doctor_id"), nullable(doctorId));
        query.bindValue(QStringLiteral(":patient_id"), patientId);
        query.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
        query.bindValue(QStringLiteral(":occurred_at"), operation.occurredAt);
        query.bindValue(QStringLiteral(":module_name"), operation.category.left(64));
        query.bindValue(QStringLiteral(":function_name"), operation.actor.left(128));
        query.bindValue(QStringLiteral(":operation_name"), operation.category.left(128));
        query.bindValue(QStringLiteral(":payload"), operation.details);
        query.bindValue(QStringLiteral(":message"), operation.details.left(255));
        query.bindValue(QStringLiteral(":created_at"), QDateTime::currentDateTime());
        if (!execute(&query, QStringLiteral("record clinical operation snapshot"), errorMessage)) {
            return false;
        }
    }
    return true;
}

bool MySqlTreatmentWorkflowRepository::recordTerminationEvidence(
    const TreatmentTerminationEvidence& evidence,
    QString* errorMessage) const
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }
    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }
    const auto rollback = [&database]() {
        database.rollback();
    };

    QSqlQuery operation(database);
    operation.prepare(QStringLiteral(
        "INSERT INTO clinical_operation_log (login_user_id, doctor_id, patient_id, treatment_session_id, occurred_at, screen_name, module_name, function_name, operation_name, operation_payload, result_state, result_message, evidence_level, workstation_name, created_at) "
        "VALUES (:user_id, :doctor_id, :patient_id, :session_id, :occurred_at, 'treatment', 'treatment_execution', 'terminateTreatmentPlan', :operation_name, :payload, 'success', :message, 'evidence', 'PanTheraSys', :created_at)"));
    operation.bindValue(QStringLiteral(":user_id"), nullable(evidence.userId));
    operation.bindValue(QStringLiteral(":doctor_id"), nullable(evidence.doctorId));
    operation.bindValue(QStringLiteral(":patient_id"), evidence.patientId);
    operation.bindValue(QStringLiteral(":session_id"), evidence.treatmentSessionId);
    operation.bindValue(QStringLiteral(":occurred_at"), evidence.endedAt);
    operation.bindValue(QStringLiteral(":operation_name"), evidence.continueTreatmentSelected ? QStringLiteral("continue_treatment") : QStringLiteral("terminate_treatment"));
    operation.bindValue(QStringLiteral(":payload"), evidence.summaryPayload);
    operation.bindValue(QStringLiteral(":message"), evidence.reason);
    operation.bindValue(QStringLiteral(":created_at"), evidence.endedAt);
    if (!execute(&operation, QStringLiteral("record clinical operation log"), errorMessage)) {
        rollback();
        return false;
    }

    QSqlQuery termination(database);
    termination.prepare(QStringLiteral(
        "INSERT INTO treatment_termination_log (treatment_session_id, patient_id, doctor_id, login_user_id, terminated_at, termination_type, reason_text, final_status, continue_treatment_selected, summary_payload) "
        "VALUES (:session_id, :patient_id, :doctor_id, :user_id, :terminated_at, :termination_type, :reason, :status, :continue_selected, :payload)"));
    termination.bindValue(QStringLiteral(":session_id"), evidence.treatmentSessionId);
    termination.bindValue(QStringLiteral(":patient_id"), evidence.patientId);
    termination.bindValue(QStringLiteral(":doctor_id"), nullable(evidence.doctorId));
    termination.bindValue(QStringLiteral(":user_id"), nullable(evidence.userId));
    termination.bindValue(QStringLiteral(":terminated_at"), evidence.endedAt);
    termination.bindValue(QStringLiteral(":termination_type"), evidence.terminationType);
    termination.bindValue(QStringLiteral(":reason"), evidence.reason);
    termination.bindValue(QStringLiteral(":status"), evidence.finalStatus);
    termination.bindValue(QStringLiteral(":continue_selected"), evidence.continueTreatmentSelected ? 1 : 0);
    termination.bindValue(QStringLiteral(":payload"), evidence.summaryPayload);
    if (!execute(&termination, QStringLiteral("record treatment termination log"), errorMessage)) {
        rollback();
        return false;
    }

    if (evidence.documentPath.trimmed().isEmpty()) {
        if (database.commit()) {
            return true;
        }
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        rollback();
        return false;
    }

    QSqlQuery document(database);
    document.prepare(QStringLiteral(
        "INSERT INTO medical_evidence_document (treatment_session_id, patient_id, doctor_id, login_user_id, generated_at, document_type, docx_path, source_log_start_at, source_log_end_at, checksum_sha256, generation_state, generated_by, notes) "
        "VALUES (:session_id, :patient_id, :doctor_id, :user_id, :generated_at, 'treatment_operation_evidence', :path, :started_at, :ended_at, :checksum, 'generated', 'PanTheraSys', :notes)"));
    document.bindValue(QStringLiteral(":session_id"), evidence.treatmentSessionId);
    document.bindValue(QStringLiteral(":patient_id"), evidence.patientId);
    document.bindValue(QStringLiteral(":doctor_id"), nullable(evidence.doctorId));
    document.bindValue(QStringLiteral(":user_id"), nullable(evidence.userId));
    document.bindValue(QStringLiteral(":generated_at"), evidence.endedAt);
    document.bindValue(QStringLiteral(":path"), evidence.documentPath);
    document.bindValue(QStringLiteral(":started_at"), evidence.startedAt);
    document.bindValue(QStringLiteral(":ended_at"), evidence.endedAt);
    document.bindValue(QStringLiteral(":checksum"), evidence.documentChecksum);
    document.bindValue(QStringLiteral(":notes"), evidence.finalStatus);
    if (!execute(&document, QStringLiteral("record medical evidence document"), errorMessage)) {
        rollback();
        return false;
    }
    if (database.commit()) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = database.lastError().text();
    }
    rollback();
    return false;
}

bool MySqlTreatmentWorkflowRepository::registerRecoveredEvidenceDocument(
    const QString& treatmentSessionId,
    const QString& patientId,
    const QString& documentPath,
    const QDateTime& generatedAt,
    QString* errorMessage) const
{
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }
    if (treatmentSessionId.trimmed().isEmpty() || patientId.trimmed().isEmpty() || documentPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Treatment session, patient, and document path are required.");
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO medical_evidence_document (treatment_session_id, patient_id, generated_at, document_type, docx_path, generation_state, generated_by, notes) "
        "VALUES (:session_id, :patient_id, :generated_at, 'treatment_operation_evidence', :path, 'recovered', 'PanTheraSys recovery', 'Recovered from local evidence storage')"));
    query.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    query.bindValue(QStringLiteral(":generated_at"), generatedAt.isValid() ? generatedAt : QDateTime::currentDateTime());
    query.bindValue(QStringLiteral(":path"), documentPath);
    return execute(&query, QStringLiteral("register recovered medical evidence document"), errorMessage);
}

bool MySqlTreatmentWorkflowRepository::findLatestEvidenceDocumentPath(
    const QString& treatmentSessionId,
    QString* documentPath,
    QString* errorMessage) const
{
    if (documentPath != nullptr) {
        documentPath->clear();
    }
    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    if (!isOpenDatabase(database, errorMessage)) {
        return false;
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT docx_path FROM medical_evidence_document WHERE treatment_session_id = :session_id "
        "ORDER BY generated_at DESC, id DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":session_id"), treatmentSessionId);
    if (!execute(&query, QStringLiteral("find treatment evidence document"), errorMessage)) {
        return false;
    }
    if (!query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No evidence document is available for this treatment session.");
        }
        return false;
    }
    if (documentPath != nullptr) {
        *documentPath = query.value(0).toString();
    }
    return true;
}

}  // namespace panthera::adapters
