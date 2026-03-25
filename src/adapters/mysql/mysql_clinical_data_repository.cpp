#include "adapters/mysql/mysql_clinical_data_repository.h"

#include <QMetaType>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace panthera::adapters {

using namespace panthera::core;

namespace {

QVariant nullableString(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(value);
}

bool executeQuery(QSqlQuery* query, QString* lastError, const QString& context)
{
    if (query->exec()) {
        return true;
    }

    *lastError = QStringLiteral("%1: %2").arg(context, query->lastError().text());
    return false;
}

bool ensureAffected(QSqlQuery* query, QString* lastError, const QString& context)
{
    if (query->numRowsAffected() > 0) {
        return true;
    }

    *lastError = QStringLiteral("%1: no rows affected").arg(context);
    return false;
}

PatientRecord mapPatient(const QSqlQuery& query)
{
    PatientRecord patient;
    patient.id = query.value(QStringLiteral("id")).toString();
    patient.name = query.value(QStringLiteral("name")).toString();
    patient.age = query.value(QStringLiteral("age")).toInt();
    patient.gender = query.value(QStringLiteral("gender")).toString();
    patient.diagnosis = query.value(QStringLiteral("diagnosis")).toString();
    patient.contact = query.value(QStringLiteral("contact")).toString();
    patient.createdAt = query.value(QStringLiteral("created_at")).toDateTime();
    patient.updatedAt = query.value(QStringLiteral("updated_at")).toDateTime();
    patient.deletedAt = query.value(QStringLiteral("deleted_at")).toDateTime();
    return patient;
}

ImageSeriesRecord mapImageSeries(const QSqlQuery& query)
{
    ImageSeriesRecord imageSeries;
    imageSeries.id = query.value(QStringLiteral("id")).toString();
    imageSeries.patientId = query.value(QStringLiteral("patient_id")).toString();
    imageSeries.type = query.value(QStringLiteral("type")).toString();
    imageSeries.storagePath = query.value(QStringLiteral("storage_path")).toString();
    imageSeries.acquisitionDate = query.value(QStringLiteral("acquisition_date")).toDate();
    imageSeries.notes = query.value(QStringLiteral("notes")).toString();
    imageSeries.createdAt = query.value(QStringLiteral("created_at")).toDateTime();
    return imageSeries;
}

QString approvalStateKey(ApprovalState state)
{
    switch (state) {
    case ApprovalState::Draft:
        return QStringLiteral("Draft");
    case ApprovalState::UnderReview:
        return QStringLiteral("UnderReview");
    case ApprovalState::Approved:
        return QStringLiteral("Approved");
    case ApprovalState::Locked:
        return QStringLiteral("Locked");
    case ApprovalState::Superseded:
        return QStringLiteral("Superseded");
    }
    return QStringLiteral("Draft");
}

ApprovalState approvalStateFromKey(const QString& key)
{
    if (key.compare(QStringLiteral("UnderReview"), Qt::CaseInsensitive) == 0) {
        return ApprovalState::UnderReview;
    }
    if (key.compare(QStringLiteral("Approved"), Qt::CaseInsensitive) == 0) {
        return ApprovalState::Approved;
    }
    if (key.compare(QStringLiteral("Locked"), Qt::CaseInsensitive) == 0) {
        return ApprovalState::Locked;
    }
    if (key.compare(QStringLiteral("Superseded"), Qt::CaseInsensitive) == 0) {
        return ApprovalState::Superseded;
    }
    return ApprovalState::Draft;
}

QString treatmentPatternKey(TreatmentPattern pattern)
{
    switch (pattern) {
    case TreatmentPattern::Point:
        return QStringLiteral("Point");
    case TreatmentPattern::Line:
        return QStringLiteral("Line");
    case TreatmentPattern::Segmented:
        return QStringLiteral("Segmented");
    }
    return QStringLiteral("Point");
}

TreatmentPattern treatmentPatternFromKey(const QString& key)
{
    if (key.compare(QStringLiteral("Line"), Qt::CaseInsensitive) == 0) {
        return TreatmentPattern::Line;
    }
    if (key.compare(QStringLiteral("Segmented"), Qt::CaseInsensitive) == 0) {
        return TreatmentPattern::Segmented;
    }
    return TreatmentPattern::Point;
}

QJsonObject therapyPointToJson(const TherapyPoint& point)
{
    QJsonObject json;
    json.insert(QStringLiteral("index"), point.index);
    json.insert(QStringLiteral("x"), point.positionMm.x());
    json.insert(QStringLiteral("y"), point.positionMm.y());
    json.insert(QStringLiteral("dwell_seconds"), point.dwellSeconds);
    json.insert(QStringLiteral("power_watts"), point.powerWatts);
    return json;
}

TherapyPoint therapyPointFromJson(const QJsonObject& json)
{
    TherapyPoint point;
    point.index = json.value(QStringLiteral("index")).toInt();
    point.positionMm = QPointF(json.value(QStringLiteral("x")).toDouble(), json.value(QStringLiteral("y")).toDouble());
    point.dwellSeconds = json.value(QStringLiteral("dwell_seconds")).toDouble();
    point.powerWatts = json.value(QStringLiteral("power_watts")).toDouble();
    return point;
}

QString serializePlanPayload(const TherapyPlan& therapyPlan)
{
    QJsonObject root;
    root.insert(QStringLiteral("delivery_mode"), therapyPlan.deliveryMode);
    root.insert(QStringLiteral("coordinate_x"), therapyPlan.coordinateX);
    root.insert(QStringLiteral("coordinate_y"), therapyPlan.coordinateY);
    root.insert(QStringLiteral("coordinate_z"), therapyPlan.coordinateZ);
    root.insert(QStringLiteral("depth_mm"), therapyPlan.depthMm);
    root.insert(QStringLiteral("dwell_seconds"), therapyPlan.dwellSeconds);
    root.insert(QStringLiteral("approved_by"), therapyPlan.approvedBy);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void hydratePlanPayload(const QString& serializedPayload, TherapyPlan* therapyPlan)
{
    if (therapyPlan == nullptr || serializedPayload.trimmed().isEmpty()) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(serializedPayload.toUtf8());
    if (!document.isObject()) {
        return;
    }

    const QJsonObject root = document.object();
    therapyPlan->deliveryMode = root.value(QStringLiteral("delivery_mode")).toString(therapyPlan->deliveryMode);
    therapyPlan->coordinateX = root.value(QStringLiteral("coordinate_x")).toDouble(therapyPlan->coordinateX);
    therapyPlan->coordinateY = root.value(QStringLiteral("coordinate_y")).toDouble(therapyPlan->coordinateY);
    therapyPlan->coordinateZ = root.value(QStringLiteral("coordinate_z")).toDouble(therapyPlan->coordinateZ);
    therapyPlan->depthMm = root.value(QStringLiteral("depth_mm")).toDouble(therapyPlan->depthMm);
    therapyPlan->dwellSeconds = root.value(QStringLiteral("dwell_seconds")).toDouble(therapyPlan->dwellSeconds);
    if (therapyPlan->approvedBy.isEmpty()) {
        therapyPlan->approvedBy = root.value(QStringLiteral("approved_by")).toString();
    }
}

QString serializeSegmentPayload(const TherapySegment& segment)
{
    QJsonObject root;
    QJsonArray points;
    for (const TherapyPoint& point : segment.points) {
        points.push_back(therapyPointToJson(point));
    }
    root.insert(QStringLiteral("points"), points);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

TherapySegment mapTherapySegment(const QSqlQuery& query)
{
    TherapySegment segment;
    segment.id = query.value(QStringLiteral("id")).toString();
    segment.orderIndex = query.value(QStringLiteral("order_index")).toInt();
    segment.label = query.value(QStringLiteral("label_name")).toString();
    segment.plannedDurationSeconds = query.value(QStringLiteral("planned_duration_seconds")).toDouble();

    const QJsonDocument document = QJsonDocument::fromJson(query.value(QStringLiteral("serialized_payload")).toString().toUtf8());
    if (document.isObject()) {
        const QJsonArray points = document.object().value(QStringLiteral("points")).toArray();
        for (const QJsonValue& pointValue : points) {
            if (pointValue.isObject()) {
                segment.points.push_back(therapyPointFromJson(pointValue.toObject()));
            }
        }
    }

    return segment;
}

bool loadTherapySegments(QSqlDatabase database, const QString& planId, QVector<TherapySegment>* segments, QString* lastError)
{
    if (segments == nullptr) {
        return true;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, plan_id, order_index, label_name, planned_duration_seconds, serialized_payload "
        "FROM therapy_segment WHERE plan_id = :plan_id ORDER BY order_index ASC"));
    query.bindValue(QStringLiteral(":plan_id"), planId);
    if (!executeQuery(&query, lastError, QStringLiteral("loadTherapySegments"))) {
        return false;
    }

    segments->clear();
    while (query.next()) {
        segments->push_back(mapTherapySegment(query));
    }
    return true;
}

bool replaceTherapySegments(QSqlDatabase database, const TherapyPlan& therapyPlan, QString* lastError)
{
    QSqlQuery deleteQuery(database);
    deleteQuery.prepare(QStringLiteral("DELETE FROM therapy_segment WHERE plan_id = :plan_id"));
    deleteQuery.bindValue(QStringLiteral(":plan_id"), therapyPlan.id);
    if (!executeQuery(&deleteQuery, lastError, QStringLiteral("replaceTherapySegments.deleteExisting"))) {
        return false;
    }

    for (int index = 0; index < therapyPlan.segments.size(); ++index) {
        TherapySegment segment = therapyPlan.segments.at(index);
        if (segment.id.trimmed().isEmpty()) {
            segment.id = QStringLiteral("%1-S%2").arg(therapyPlan.id).arg(index + 1);
        }

        QSqlQuery insertQuery(database);
        insertQuery.prepare(QStringLiteral(
            "INSERT INTO therapy_segment(id, plan_id, order_index, label_name, planned_duration_seconds, serialized_payload) "
            "VALUES(:id, :plan_id, :order_index, :label_name, :planned_duration_seconds, :serialized_payload)"));
        insertQuery.bindValue(QStringLiteral(":id"), segment.id);
        insertQuery.bindValue(QStringLiteral(":plan_id"), therapyPlan.id);
        insertQuery.bindValue(QStringLiteral(":order_index"), segment.orderIndex);
        insertQuery.bindValue(QStringLiteral(":label_name"), segment.label);
        insertQuery.bindValue(QStringLiteral(":planned_duration_seconds"), segment.plannedDurationSeconds);
        insertQuery.bindValue(QStringLiteral(":serialized_payload"), serializeSegmentPayload(segment));
        if (!executeQuery(&insertQuery, lastError, QStringLiteral("replaceTherapySegments.insertSegment"))) {
            return false;
        }
    }

    return true;
}

TherapyPlan mapTherapyPlan(const QSqlQuery& query)
{
    TherapyPlan therapyPlan;
    therapyPlan.id = query.value(QStringLiteral("id")).toString();
    therapyPlan.patientId = query.value(QStringLiteral("patient_id")).toString();
    therapyPlan.name = query.value(QStringLiteral("name")).toString();
    therapyPlan.pattern = treatmentPatternFromKey(query.value(QStringLiteral("pattern")).toString());
    therapyPlan.approvalState = approvalStateFromKey(query.value(QStringLiteral("approval_state")).toString());
    therapyPlan.plannedPowerWatts = query.value(QStringLiteral("planned_power_watts")).toDouble();
    therapyPlan.spacingMm = query.value(QStringLiteral("spacing_mm")).toDouble();
    therapyPlan.respiratoryTrackingEnabled = query.value(QStringLiteral("respiratory_tracking_enabled")).toBool();
    therapyPlan.createdAt = query.value(QStringLiteral("created_at")).toDateTime();
    therapyPlan.approvedAt = query.value(QStringLiteral("approved_at")).toDateTime();
    therapyPlan.approvedBy = query.value(QStringLiteral("approved_by")).toString();
    hydratePlanPayload(query.value(QStringLiteral("serialized_payload")).toString(), &therapyPlan);
    return therapyPlan;
}

TreatmentSessionRecord mapTreatmentSession(const QSqlQuery& query)
{
    TreatmentSessionRecord treatmentSession;
    treatmentSession.id = query.value(QStringLiteral("id")).toString();
    treatmentSession.patientId = query.value(QStringLiteral("patient_id")).toString();
    treatmentSession.planId = query.value(QStringLiteral("plan_id")).toString();
    treatmentSession.lesionType = query.value(QStringLiteral("lesion_type")).toString();
    treatmentSession.pathSummary = query.value(QStringLiteral("path_summary")).toString();
    treatmentSession.treatmentDate = query.value(QStringLiteral("treatment_date")).toDateTime();
    treatmentSession.startedAt = treatmentSession.treatmentDate;
    treatmentSession.totalEnergyJ = query.value(QStringLiteral("total_energy_j")).toDouble();
    treatmentSession.totalDurationSeconds = query.value(QStringLiteral("total_duration_seconds")).toDouble();
    treatmentSession.endedAt = treatmentSession.startedAt.isValid()
        ? treatmentSession.startedAt.addSecs(static_cast<int>(treatmentSession.totalDurationSeconds))
        : QDateTime {};
    treatmentSession.dose = query.value(QStringLiteral("dose")).toDouble();
    treatmentSession.status = query.value(QStringLiteral("status")).toString();
    treatmentSession.createdAt = query.value(QStringLiteral("created_at")).toDateTime();
    return treatmentSession;
}

TreatmentReportRecord mapTreatmentReport(const QSqlQuery& query)
{
    TreatmentReportRecord treatmentReport;
    treatmentReport.id = query.value(QStringLiteral("id")).toString();
    treatmentReport.patientId = query.value(QStringLiteral("patient_id")).toString();
    treatmentReport.treatmentSessionId = query.value(QStringLiteral("treatment_session_id")).toString();
    treatmentReport.generatedAt = query.value(QStringLiteral("generated_at")).toDateTime();
    treatmentReport.title = query.value(QStringLiteral("title")).toString();
    treatmentReport.contentHtml = query.value(QStringLiteral("content_html")).toString();
    treatmentReport.notes = query.value(QStringLiteral("notes")).toString();
    return treatmentReport;
}

}  // namespace

MySqlClinicalDataRepository::MySqlClinicalDataRepository() = default;

MySqlClinicalDataRepository::~MySqlClinicalDataRepository()
{
    close();
}

bool MySqlClinicalDataRepository::open(const DatabaseConnectionSettings& settings)
{
    const bool opened = m_facade.open(settings);
    setLastError(opened ? QString() : m_facade.lastError());
    return opened;
}

void MySqlClinicalDataRepository::close()
{
    m_facade.close();
    setLastError(QString());
}

bool MySqlClinicalDataRepository::isOpen() const
{
    return m_facade.isOpen();
}

bool MySqlClinicalDataRepository::initializeSchemaFromFile(const QString& schemaFilePath)
{
    const bool initialized = m_facade.initializeSchemaFromFile(schemaFilePath);
    setLastError(initialized ? QString() : m_facade.lastError());
    return initialized;
}

QString MySqlClinicalDataRepository::repositoryName() const
{
    return QStringLiteral("mysql");
}

bool MySqlClinicalDataRepository::supportsWriteOperations() const
{
    return true;
}

QString MySqlClinicalDataRepository::lastError() const
{
    return m_lastError;
}

QVector<PatientRecord> MySqlClinicalDataRepository::listPatients() const
{
    QVector<PatientRecord> patients;
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return patients;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, name, age, gender, diagnosis, contact, created_at, updated_at, deleted_at "
        "FROM patient WHERE deleted_at IS NULL ORDER BY updated_at DESC, created_at DESC"));
    if (!executeQuery(&query, &m_lastError, QStringLiteral("listPatients"))) {
        return patients;
    }

    while (query.next()) {
        patients.push_back(mapPatient(query));
    }

    setLastError(QString());
    return patients;
}

bool MySqlClinicalDataRepository::findPatientById(const QString& patientId, PatientRecord* patient) const
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, name, age, gender, diagnosis, contact, created_at, updated_at, deleted_at "
        "FROM patient WHERE id = :id AND deleted_at IS NULL"));
    query.bindValue(QStringLiteral(":id"), patientId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("findPatientById"))) {
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("Patient not found: %1").arg(patientId));
        return false;
    }

    if (patient != nullptr) {
        *patient = mapPatient(query);
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::createPatient(const PatientRecord& patient)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "INSERT INTO patient(id, name, age, gender, diagnosis, contact, deleted_at, created_at, updated_at) "
        "VALUES(:id, :name, :age, :gender, :diagnosis, :contact, NULL, :created_at, :updated_at)"));
    query.bindValue(QStringLiteral(":id"), patient.id);
    query.bindValue(QStringLiteral(":name"), patient.name);
    query.bindValue(QStringLiteral(":age"), patient.age);
    query.bindValue(QStringLiteral(":gender"), patient.gender);
    query.bindValue(QStringLiteral(":diagnosis"), patient.diagnosis);
    query.bindValue(QStringLiteral(":contact"), nullableString(patient.contact));
    query.bindValue(QStringLiteral(":created_at"), patient.createdAt);
    query.bindValue(QStringLiteral(":updated_at"), patient.updatedAt);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("createPatient"))) {
        return false;
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::updatePatient(const PatientRecord& patient)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "UPDATE patient SET name = :name, age = :age, gender = :gender, diagnosis = :diagnosis, "
        "contact = :contact, updated_at = :updated_at WHERE id = :id AND deleted_at IS NULL"));
    query.bindValue(QStringLiteral(":id"), patient.id);
    query.bindValue(QStringLiteral(":name"), patient.name);
    query.bindValue(QStringLiteral(":age"), patient.age);
    query.bindValue(QStringLiteral(":gender"), patient.gender);
    query.bindValue(QStringLiteral(":diagnosis"), patient.diagnosis);
    query.bindValue(QStringLiteral(":contact"), nullableString(patient.contact));
    query.bindValue(QStringLiteral(":updated_at"), patient.updatedAt);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("updatePatient"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("updatePatient"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::archivePatient(const QString& patientId, const QDateTime& archivedAt)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "UPDATE patient SET deleted_at = :deleted_at, updated_at = :updated_at "
        "WHERE id = :id AND deleted_at IS NULL"));
    query.bindValue(QStringLiteral(":id"), patientId);
    query.bindValue(QStringLiteral(":deleted_at"), archivedAt);
    query.bindValue(QStringLiteral(":updated_at"), archivedAt);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("archivePatient"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("archivePatient"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

QVector<ImageSeriesRecord> MySqlClinicalDataRepository::listImageSeriesForPatient(const QString& patientId) const
{
    QVector<ImageSeriesRecord> imageSeries;
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return imageSeries;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, type, storage_path, acquisition_date, notes, created_at "
        "FROM image_series WHERE patient_id = :patient_id ORDER BY acquisition_date DESC, created_at DESC"));
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("listImageSeriesForPatient"))) {
        return imageSeries;
    }

    while (query.next()) {
        imageSeries.push_back(mapImageSeries(query));
    }

    setLastError(QString());
    return imageSeries;
}

bool MySqlClinicalDataRepository::findImageSeriesById(const QString& imageSeriesId, ImageSeriesRecord* imageSeries) const
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, type, storage_path, acquisition_date, notes, created_at "
        "FROM image_series WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), imageSeriesId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("findImageSeriesById"))) {
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("Image series not found: %1").arg(imageSeriesId));
        return false;
    }

    if (imageSeries != nullptr) {
        *imageSeries = mapImageSeries(query);
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::createImageSeries(const ImageSeriesRecord& imageSeries)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "INSERT INTO image_series(id, patient_id, type, storage_path, acquisition_date, notes, created_at) "
        "VALUES(:id, :patient_id, :type, :storage_path, :acquisition_date, :notes, :created_at)"));
    query.bindValue(QStringLiteral(":id"), imageSeries.id);
    query.bindValue(QStringLiteral(":patient_id"), imageSeries.patientId);
    query.bindValue(QStringLiteral(":type"), imageSeries.type);
    query.bindValue(QStringLiteral(":storage_path"), imageSeries.storagePath);
    query.bindValue(QStringLiteral(":acquisition_date"), imageSeries.acquisitionDate);
    query.bindValue(QStringLiteral(":notes"), nullableString(imageSeries.notes));
    query.bindValue(QStringLiteral(":created_at"), imageSeries.createdAt);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("createImageSeries"))) {
        return false;
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::updateImageSeries(const ImageSeriesRecord& imageSeries)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "UPDATE image_series SET patient_id = :patient_id, type = :type, storage_path = :storage_path, "
        "acquisition_date = :acquisition_date, notes = :notes WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), imageSeries.id);
    query.bindValue(QStringLiteral(":patient_id"), imageSeries.patientId);
    query.bindValue(QStringLiteral(":type"), imageSeries.type);
    query.bindValue(QStringLiteral(":storage_path"), imageSeries.storagePath);
    query.bindValue(QStringLiteral(":acquisition_date"), imageSeries.acquisitionDate);
    query.bindValue(QStringLiteral(":notes"), nullableString(imageSeries.notes));

    if (!executeQuery(&query, &m_lastError, QStringLiteral("updateImageSeries"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("updateImageSeries"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::deleteImageSeries(const QString& imageSeriesId)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral("DELETE FROM image_series WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), imageSeriesId);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("deleteImageSeries"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("deleteImageSeries"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

QVector<TherapyPlan> MySqlClinicalDataRepository::listTherapyPlansForPatient(const QString& patientId) const
{
    QVector<TherapyPlan> therapyPlans;
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return therapyPlans;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, name, pattern, approval_state, planned_power_watts, spacing_mm, "
        "respiratory_tracking_enabled, serialized_payload, created_at, approved_at, approved_by "
        "FROM therapy_plan WHERE patient_id = :patient_id ORDER BY created_at DESC, id DESC"));
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("listTherapyPlansForPatient"))) {
        return therapyPlans;
    }

    while (query.next()) {
        TherapyPlan therapyPlan = mapTherapyPlan(query);
        if (!loadTherapySegments(m_facade.database(), therapyPlan.id, &therapyPlan.segments, &m_lastError)) {
            return {};
        }
        therapyPlans.push_back(therapyPlan);
    }

    setLastError(QString());
    return therapyPlans;
}

bool MySqlClinicalDataRepository::findTherapyPlanById(const QString& therapyPlanId, TherapyPlan* therapyPlan) const
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, name, pattern, approval_state, planned_power_watts, spacing_mm, "
        "respiratory_tracking_enabled, serialized_payload, created_at, approved_at, approved_by "
        "FROM therapy_plan WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), therapyPlanId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("findTherapyPlanById"))) {
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("Therapy plan not found: %1").arg(therapyPlanId));
        return false;
    }

    if (therapyPlan != nullptr) {
        *therapyPlan = mapTherapyPlan(query);
        if (!loadTherapySegments(m_facade.database(), therapyPlan->id, &therapyPlan->segments, &m_lastError)) {
            return false;
        }
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::createTherapyPlan(const TherapyPlan& therapyPlan)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlDatabase database = m_facade.database();
    if (!database.transaction()) {
        setLastError(database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO therapy_plan(id, patient_id, name, pattern, approval_state, planned_power_watts, spacing_mm, "
        "respiratory_tracking_enabled, serialized_payload, created_at, approved_at, approved_by) "
        "VALUES(:id, :patient_id, :name, :pattern, :approval_state, :planned_power_watts, :spacing_mm, "
        ":respiratory_tracking_enabled, :serialized_payload, :created_at, :approved_at, :approved_by)"));
    query.bindValue(QStringLiteral(":id"), therapyPlan.id);
    query.bindValue(QStringLiteral(":patient_id"), therapyPlan.patientId);
    query.bindValue(QStringLiteral(":name"), therapyPlan.name);
    query.bindValue(QStringLiteral(":pattern"), treatmentPatternKey(therapyPlan.pattern));
    query.bindValue(QStringLiteral(":approval_state"), approvalStateKey(therapyPlan.approvalState));
    query.bindValue(QStringLiteral(":planned_power_watts"), therapyPlan.plannedPowerWatts);
    query.bindValue(QStringLiteral(":spacing_mm"), therapyPlan.spacingMm);
    query.bindValue(QStringLiteral(":respiratory_tracking_enabled"), therapyPlan.respiratoryTrackingEnabled);
    query.bindValue(QStringLiteral(":serialized_payload"), serializePlanPayload(therapyPlan));
    query.bindValue(QStringLiteral(":created_at"), therapyPlan.createdAt);
    query.bindValue(QStringLiteral(":approved_at"), therapyPlan.approvedAt.isValid()
            ? QVariant(therapyPlan.approvedAt)
            : QVariant(QMetaType::fromType<QDateTime>()));
    query.bindValue(QStringLiteral(":approved_by"), QVariant(QMetaType::fromType<QString>()));

    if (!executeQuery(&query, &m_lastError, QStringLiteral("createTherapyPlan"))
        || !replaceTherapySegments(database, therapyPlan, &m_lastError)) {
        database.rollback();
        return false;
    }

    if (!database.commit()) {
        setLastError(database.lastError().text());
        database.rollback();
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::updateTherapyPlan(const TherapyPlan& therapyPlan)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlDatabase database = m_facade.database();
    if (!database.transaction()) {
        setLastError(database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE therapy_plan SET patient_id = :patient_id, name = :name, pattern = :pattern, "
        "approval_state = :approval_state, planned_power_watts = :planned_power_watts, spacing_mm = :spacing_mm, "
        "respiratory_tracking_enabled = :respiratory_tracking_enabled, serialized_payload = :serialized_payload, "
        "created_at = :created_at, approved_at = :approved_at, approved_by = :approved_by WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), therapyPlan.id);
    query.bindValue(QStringLiteral(":patient_id"), therapyPlan.patientId);
    query.bindValue(QStringLiteral(":name"), therapyPlan.name);
    query.bindValue(QStringLiteral(":pattern"), treatmentPatternKey(therapyPlan.pattern));
    query.bindValue(QStringLiteral(":approval_state"), approvalStateKey(therapyPlan.approvalState));
    query.bindValue(QStringLiteral(":planned_power_watts"), therapyPlan.plannedPowerWatts);
    query.bindValue(QStringLiteral(":spacing_mm"), therapyPlan.spacingMm);
    query.bindValue(QStringLiteral(":respiratory_tracking_enabled"), therapyPlan.respiratoryTrackingEnabled);
    query.bindValue(QStringLiteral(":serialized_payload"), serializePlanPayload(therapyPlan));
    query.bindValue(QStringLiteral(":created_at"), therapyPlan.createdAt);
    query.bindValue(QStringLiteral(":approved_at"), therapyPlan.approvedAt.isValid()
            ? QVariant(therapyPlan.approvedAt)
            : QVariant(QMetaType::fromType<QDateTime>()));
    query.bindValue(QStringLiteral(":approved_by"), QVariant(QMetaType::fromType<QString>()));

    if (!executeQuery(&query, &m_lastError, QStringLiteral("updateTherapyPlan"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("updateTherapyPlan"))
        || !replaceTherapySegments(database, therapyPlan, &m_lastError)) {
        database.rollback();
        return false;
    }

    if (!database.commit()) {
        setLastError(database.lastError().text());
        database.rollback();
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::deleteTherapyPlan(const QString& therapyPlanId)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlDatabase database = m_facade.database();
    if (!database.transaction()) {
        setLastError(database.lastError().text());
        return false;
    }

    QSqlQuery deleteSegments(database);
    deleteSegments.prepare(QStringLiteral("DELETE FROM therapy_segment WHERE plan_id = :plan_id"));
    deleteSegments.bindValue(QStringLiteral(":plan_id"), therapyPlanId);
    if (!executeQuery(&deleteSegments, &m_lastError, QStringLiteral("deleteTherapyPlan.deleteSegments"))) {
        database.rollback();
        return false;
    }

    QSqlQuery deletePlan(database);
    deletePlan.prepare(QStringLiteral("DELETE FROM therapy_plan WHERE id = :id"));
    deletePlan.bindValue(QStringLiteral(":id"), therapyPlanId);
    if (!executeQuery(&deletePlan, &m_lastError, QStringLiteral("deleteTherapyPlan.deletePlan"))
        || !ensureAffected(&deletePlan, &m_lastError, QStringLiteral("deleteTherapyPlan.deletePlan"))) {
        database.rollback();
        return false;
    }

    if (!database.commit()) {
        setLastError(database.lastError().text());
        database.rollback();
        return false;
    }

    setLastError(QString());
    return true;
}

QVector<TreatmentSessionRecord> MySqlClinicalDataRepository::listTreatmentSessionsForPatient(const QString& patientId) const
{
    QVector<TreatmentSessionRecord> treatmentSessions;
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return treatmentSessions;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, plan_id, treatment_date, lesion_type, total_energy_j, "
        "total_duration_seconds, path_summary, dose, status, created_at "
        "FROM treatment_session WHERE patient_id = :patient_id ORDER BY treatment_date DESC, created_at DESC"));
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("listTreatmentSessionsForPatient"))) {
        return treatmentSessions;
    }

    while (query.next()) {
        treatmentSessions.push_back(mapTreatmentSession(query));
    }

    setLastError(QString());
    return treatmentSessions;
}

bool MySqlClinicalDataRepository::findTreatmentSessionById(const QString& treatmentSessionId, TreatmentSessionRecord* treatmentSession) const
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, plan_id, treatment_date, lesion_type, total_energy_j, "
        "total_duration_seconds, path_summary, dose, status, created_at "
        "FROM treatment_session WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), treatmentSessionId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("findTreatmentSessionById"))) {
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("Treatment session not found: %1").arg(treatmentSessionId));
        return false;
    }

    if (treatmentSession != nullptr) {
        *treatmentSession = mapTreatmentSession(query);
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::createTreatmentSession(const TreatmentSessionRecord& treatmentSession)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }
    if (!ensureTherapyPlanRow(treatmentSession)) {
        return false;
    }

    const QDateTime treatmentDate = treatmentSession.treatmentDate.isValid() ? treatmentSession.treatmentDate : treatmentSession.startedAt;

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "INSERT INTO treatment_session(id, patient_id, plan_id, treatment_date, lesion_type, total_energy_j, "
        "total_duration_seconds, path_summary, dose, status, created_at) "
        "VALUES(:id, :patient_id, :plan_id, :treatment_date, :lesion_type, :total_energy_j, "
        ":total_duration_seconds, :path_summary, :dose, :status, :created_at)"));
    query.bindValue(QStringLiteral(":id"), treatmentSession.id);
    query.bindValue(QStringLiteral(":patient_id"), treatmentSession.patientId);
    query.bindValue(QStringLiteral(":plan_id"), treatmentSession.planId);
    query.bindValue(QStringLiteral(":treatment_date"), treatmentDate);
    query.bindValue(QStringLiteral(":lesion_type"), treatmentSession.lesionType);
    query.bindValue(QStringLiteral(":total_energy_j"), treatmentSession.totalEnergyJ);
    query.bindValue(QStringLiteral(":total_duration_seconds"), treatmentSession.totalDurationSeconds);
    query.bindValue(QStringLiteral(":path_summary"), treatmentSession.pathSummary);
    query.bindValue(QStringLiteral(":dose"), treatmentSession.dose);
    query.bindValue(QStringLiteral(":status"), treatmentSession.status);
    query.bindValue(QStringLiteral(":created_at"), treatmentSession.createdAt);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("createTreatmentSession"))) {
        return false;
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::updateTreatmentSession(const TreatmentSessionRecord& treatmentSession)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }
    if (!ensureTherapyPlanRow(treatmentSession)) {
        return false;
    }

    const QDateTime treatmentDate = treatmentSession.treatmentDate.isValid() ? treatmentSession.treatmentDate : treatmentSession.startedAt;

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "UPDATE treatment_session SET patient_id = :patient_id, plan_id = :plan_id, treatment_date = :treatment_date, "
        "lesion_type = :lesion_type, total_energy_j = :total_energy_j, total_duration_seconds = :total_duration_seconds, "
        "path_summary = :path_summary, dose = :dose, status = :status WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), treatmentSession.id);
    query.bindValue(QStringLiteral(":patient_id"), treatmentSession.patientId);
    query.bindValue(QStringLiteral(":plan_id"), treatmentSession.planId);
    query.bindValue(QStringLiteral(":treatment_date"), treatmentDate);
    query.bindValue(QStringLiteral(":lesion_type"), treatmentSession.lesionType);
    query.bindValue(QStringLiteral(":total_energy_j"), treatmentSession.totalEnergyJ);
    query.bindValue(QStringLiteral(":total_duration_seconds"), treatmentSession.totalDurationSeconds);
    query.bindValue(QStringLiteral(":path_summary"), treatmentSession.pathSummary);
    query.bindValue(QStringLiteral(":dose"), treatmentSession.dose);
    query.bindValue(QStringLiteral(":status"), treatmentSession.status);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("updateTreatmentSession"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("updateTreatmentSession"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::deleteTreatmentSession(const QString& treatmentSessionId)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlDatabase database = m_facade.database();
    if (!database.transaction()) {
        setLastError(database.lastError().text());
        return false;
    }

    QSqlQuery deleteReports(database);
    deleteReports.prepare(QStringLiteral("DELETE FROM treatment_report WHERE treatment_session_id = :id"));
    deleteReports.bindValue(QStringLiteral(":id"), treatmentSessionId);
    if (!executeQuery(&deleteReports, &m_lastError, QStringLiteral("deleteTreatmentSession.deleteReports"))) {
        database.rollback();
        return false;
    }

    QSqlQuery deleteRecords(database);
    deleteRecords.prepare(QStringLiteral("DELETE FROM treatment_record WHERE session_id = :id"));
    deleteRecords.bindValue(QStringLiteral(":id"), treatmentSessionId);
    if (!executeQuery(&deleteRecords, &m_lastError, QStringLiteral("deleteTreatmentSession.deleteRecords"))) {
        database.rollback();
        return false;
    }

    QSqlQuery deleteSession(database);
    deleteSession.prepare(QStringLiteral("DELETE FROM treatment_session WHERE id = :id"));
    deleteSession.bindValue(QStringLiteral(":id"), treatmentSessionId);
    if (!executeQuery(&deleteSession, &m_lastError, QStringLiteral("deleteTreatmentSession.deleteSession"))
        || !ensureAffected(&deleteSession, &m_lastError, QStringLiteral("deleteTreatmentSession.deleteSession"))) {
        database.rollback();
        return false;
    }

    if (!database.commit()) {
        setLastError(database.lastError().text());
        database.rollback();
        return false;
    }

    setLastError(QString());
    return true;
}

QVector<TreatmentReportRecord> MySqlClinicalDataRepository::listTreatmentReportsForPatient(const QString& patientId) const
{
    QVector<TreatmentReportRecord> treatmentReports;
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return treatmentReports;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, treatment_session_id, generated_at, title, content_html, notes "
        "FROM treatment_report WHERE patient_id = :patient_id ORDER BY generated_at DESC"));
    query.bindValue(QStringLiteral(":patient_id"), patientId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("listTreatmentReportsForPatient"))) {
        return treatmentReports;
    }

    while (query.next()) {
        treatmentReports.push_back(mapTreatmentReport(query));
    }

    setLastError(QString());
    return treatmentReports;
}

bool MySqlClinicalDataRepository::findTreatmentReportById(const QString& treatmentReportId, TreatmentReportRecord* treatmentReport) const
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "SELECT id, patient_id, treatment_session_id, generated_at, title, content_html, notes "
        "FROM treatment_report WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), treatmentReportId);
    if (!executeQuery(&query, &m_lastError, QStringLiteral("findTreatmentReportById"))) {
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("Treatment report not found: %1").arg(treatmentReportId));
        return false;
    }

    if (treatmentReport != nullptr) {
        *treatmentReport = mapTreatmentReport(query);
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::createTreatmentReport(const TreatmentReportRecord& treatmentReport)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "INSERT INTO treatment_report(id, patient_id, treatment_session_id, generated_at, title, content_html, notes) "
        "VALUES(:id, :patient_id, :treatment_session_id, :generated_at, :title, :content_html, :notes)"));
    query.bindValue(QStringLiteral(":id"), treatmentReport.id);
    query.bindValue(QStringLiteral(":patient_id"), treatmentReport.patientId);
    query.bindValue(QStringLiteral(":treatment_session_id"), treatmentReport.treatmentSessionId);
    query.bindValue(QStringLiteral(":generated_at"), treatmentReport.generatedAt);
    query.bindValue(QStringLiteral(":title"), treatmentReport.title);
    query.bindValue(QStringLiteral(":content_html"), treatmentReport.contentHtml);
    query.bindValue(QStringLiteral(":notes"), nullableString(treatmentReport.notes));

    if (!executeQuery(&query, &m_lastError, QStringLiteral("createTreatmentReport"))) {
        return false;
    }
    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::updateTreatmentReport(const TreatmentReportRecord& treatmentReport)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral(
        "UPDATE treatment_report SET patient_id = :patient_id, treatment_session_id = :treatment_session_id, "
        "generated_at = :generated_at, title = :title, content_html = :content_html, notes = :notes WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), treatmentReport.id);
    query.bindValue(QStringLiteral(":patient_id"), treatmentReport.patientId);
    query.bindValue(QStringLiteral(":treatment_session_id"), treatmentReport.treatmentSessionId);
    query.bindValue(QStringLiteral(":generated_at"), treatmentReport.generatedAt);
    query.bindValue(QStringLiteral(":title"), treatmentReport.title);
    query.bindValue(QStringLiteral(":content_html"), treatmentReport.contentHtml);
    query.bindValue(QStringLiteral(":notes"), nullableString(treatmentReport.notes));

    if (!executeQuery(&query, &m_lastError, QStringLiteral("updateTreatmentReport"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("updateTreatmentReport"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::deleteTreatmentReport(const QString& treatmentReportId)
{
    if (!m_facade.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(m_facade.database());
    query.prepare(QStringLiteral("DELETE FROM treatment_report WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), treatmentReportId);

    if (!executeQuery(&query, &m_lastError, QStringLiteral("deleteTreatmentReport"))
        || !ensureAffected(&query, &m_lastError, QStringLiteral("deleteTreatmentReport"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

bool MySqlClinicalDataRepository::ensureTherapyPlanRow(const TreatmentSessionRecord& treatmentSession)
{
    if (treatmentSession.planId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment session plan id is required."));
        return false;
    }

    QSqlQuery existsQuery(m_facade.database());
    existsQuery.prepare(QStringLiteral("SELECT id FROM therapy_plan WHERE id = :id"));
    existsQuery.bindValue(QStringLiteral(":id"), treatmentSession.planId);
    if (!executeQuery(&existsQuery, &m_lastError, QStringLiteral("ensureTherapyPlanRow.exists"))) {
        return false;
    }
    if (existsQuery.next()) {
        return true;
    }

    const QDateTime createdAt = treatmentSession.createdAt.isValid() ? treatmentSession.createdAt : QDateTime::currentDateTime();
    const QDateTime approvedAt = treatmentSession.treatmentDate.isValid() ? treatmentSession.treatmentDate : createdAt;

    QSqlQuery insertQuery(m_facade.database());
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO therapy_plan(id, patient_id, name, pattern, approval_state, planned_power_watts, spacing_mm, "
        "respiratory_tracking_enabled, serialized_payload, created_at, approved_at, approved_by) "
        "VALUES(:id, :patient_id, :name, :pattern, :approval_state, :planned_power_watts, :spacing_mm, "
        ":respiratory_tracking_enabled, :serialized_payload, :created_at, :approved_at, NULL)"));
    insertQuery.bindValue(QStringLiteral(":id"), treatmentSession.planId);
    insertQuery.bindValue(QStringLiteral(":patient_id"), treatmentSession.patientId);
    insertQuery.bindValue(QStringLiteral(":name"), QStringLiteral("Imported treatment plan"));
    insertQuery.bindValue(QStringLiteral(":pattern"), QStringLiteral("Point"));
    insertQuery.bindValue(QStringLiteral(":approval_state"), QStringLiteral("Locked"));
    insertQuery.bindValue(QStringLiteral(":planned_power_watts"), 0.0);
    insertQuery.bindValue(QStringLiteral(":spacing_mm"), 0.0);
    insertQuery.bindValue(QStringLiteral(":respiratory_tracking_enabled"), false);
    insertQuery.bindValue(QStringLiteral(":serialized_payload"), QStringLiteral("{}"));
    insertQuery.bindValue(QStringLiteral(":created_at"), createdAt);
    insertQuery.bindValue(QStringLiteral(":approved_at"), approvedAt);

    if (!executeQuery(&insertQuery, &m_lastError, QStringLiteral("ensureTherapyPlanRow.insert"))) {
        return false;
    }

    setLastError(QString());
    return true;
}

void MySqlClinicalDataRepository::setLastError(const QString& error) const
{
    m_lastError = error;
}

}  // namespace panthera::adapters
