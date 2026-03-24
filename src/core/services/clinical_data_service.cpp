#include "core/services/clinical_data_service.h"

#include <QUuid>

namespace panthera::core {

ClinicalDataService::ClinicalDataService(IClinicalDataRepository* repository)
    : m_repository(repository)
{
}

void ClinicalDataService::setRepository(IClinicalDataRepository* repository)
{
    m_repository = repository;
    setLastError(QString());
}

IClinicalDataRepository* ClinicalDataService::repository() const
{
    return m_repository;
}

QString ClinicalDataService::lastError() const
{
    return m_lastError;
}

QVector<PatientRecord> ClinicalDataService::listPatients() const
{
    return m_repository == nullptr ? QVector<PatientRecord> {} : m_repository->listPatients();
}

QVector<ImageSeriesRecord> ClinicalDataService::listImageSeriesForPatient(const QString& patientId) const
{
    return m_repository == nullptr ? QVector<ImageSeriesRecord> {} : m_repository->listImageSeriesForPatient(patientId);
}

QVector<TreatmentSessionRecord> ClinicalDataService::listTreatmentSessionsForPatient(const QString& patientId) const
{
    return m_repository == nullptr ? QVector<TreatmentSessionRecord> {} : m_repository->listTreatmentSessionsForPatient(patientId);
}

QVector<TreatmentReportRecord> ClinicalDataService::listTreatmentReportsForPatient(const QString& patientId) const
{
    return m_repository == nullptr ? QVector<TreatmentReportRecord> {} : m_repository->listTreatmentReportsForPatient(patientId);
}

bool ClinicalDataService::savePatient(PatientRecord* patient)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (patient == nullptr) {
        setLastError(QStringLiteral("Patient payload is null."));
        return false;
    }
    if (patient->name.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Patient name is required."));
        return false;
    }
    if (patient->diagnosis.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Patient diagnosis is required."));
        return false;
    }
    if (patient->age < 0 || patient->age > 150) {
        setLastError(QStringLiteral("Patient age is out of range."));
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (patient->id.trimmed().isEmpty()) {
        patient->id = makeId(QStringLiteral("P"));
    }
    if (!patient->createdAt.isValid()) {
        patient->createdAt = now;
    }
    patient->updatedAt = now;

    PatientRecord existing;
    const bool exists = m_repository->findPatientById(patient->id, &existing);
    if (exists) {
        if (!m_repository->updatePatient(*patient)) {
            setLastError(m_repository->lastError());
            return false;
        }
    } else {
        if (!m_repository->createPatient(*patient)) {
            setLastError(m_repository->lastError());
            return false;
        }
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::archivePatient(const QString& patientId)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (patientId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Patient id is required."));
        return false;
    }

    if (!m_repository->archivePatient(patientId, QDateTime::currentDateTime())) {
        setLastError(m_repository->lastError());
        return false;
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::saveImageSeries(ImageSeriesRecord* imageSeries)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (imageSeries == nullptr) {
        setLastError(QStringLiteral("Image series payload is null."));
        return false;
    }
    if (imageSeries->patientId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Image series patient id is required."));
        return false;
    }
    if (imageSeries->type.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Image series type is required."));
        return false;
    }
    if (imageSeries->storagePath.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Image series storage path is required."));
        return false;
    }

    if (imageSeries->id.trimmed().isEmpty()) {
        imageSeries->id = makeId(QStringLiteral("IMG"));
    }
    if (!imageSeries->acquisitionDate.isValid()) {
        imageSeries->acquisitionDate = QDate::currentDate();
    }
    if (!imageSeries->createdAt.isValid()) {
        imageSeries->createdAt = QDateTime::currentDateTime();
    }

    ImageSeriesRecord existing;
    const bool exists = m_repository->findImageSeriesById(imageSeries->id, &existing);
    if (exists) {
        if (!m_repository->updateImageSeries(*imageSeries)) {
            setLastError(m_repository->lastError());
            return false;
        }
    } else {
        if (!m_repository->createImageSeries(*imageSeries)) {
            setLastError(m_repository->lastError());
            return false;
        }
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::deleteImageSeries(const QString& imageSeriesId)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (imageSeriesId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Image series id is required."));
        return false;
    }

    if (!m_repository->deleteImageSeries(imageSeriesId)) {
        setLastError(m_repository->lastError());
        return false;
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::saveTreatmentSession(TreatmentSessionRecord* treatmentSession)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (treatmentSession == nullptr) {
        setLastError(QStringLiteral("Treatment session payload is null."));
        return false;
    }
    if (treatmentSession->patientId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment session patient id is required."));
        return false;
    }
    if (treatmentSession->planId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment session plan id is required."));
        return false;
    }
    if (treatmentSession->lesionType.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment session lesion type is required."));
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (treatmentSession->id.trimmed().isEmpty()) {
        treatmentSession->id = makeId(QStringLiteral("TX"));
    }
    if (!treatmentSession->treatmentDate.isValid()) {
        treatmentSession->treatmentDate = treatmentSession->startedAt.isValid() ? treatmentSession->startedAt : now;
    }
    if (!treatmentSession->startedAt.isValid()) {
        treatmentSession->startedAt = treatmentSession->treatmentDate;
    }
    if (!treatmentSession->endedAt.isValid() && treatmentSession->totalDurationSeconds > 0.0) {
        treatmentSession->endedAt = treatmentSession->startedAt.addSecs(static_cast<int>(treatmentSession->totalDurationSeconds));
    }
    if (treatmentSession->status.trimmed().isEmpty()) {
        treatmentSession->status = QStringLiteral("草稿");
    }
    if (!treatmentSession->createdAt.isValid()) {
        treatmentSession->createdAt = now;
    }

    TreatmentSessionRecord existing;
    const bool exists = m_repository->findTreatmentSessionById(treatmentSession->id, &existing);
    if (exists) {
        if (!m_repository->updateTreatmentSession(*treatmentSession)) {
            setLastError(m_repository->lastError());
            return false;
        }
    } else {
        if (!m_repository->createTreatmentSession(*treatmentSession)) {
            setLastError(m_repository->lastError());
            return false;
        }
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::deleteTreatmentSession(const QString& treatmentSessionId)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (treatmentSessionId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment session id is required."));
        return false;
    }

    if (!m_repository->deleteTreatmentSession(treatmentSessionId)) {
        setLastError(m_repository->lastError());
        return false;
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::saveTreatmentReport(TreatmentReportRecord* treatmentReport)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (treatmentReport == nullptr) {
        setLastError(QStringLiteral("Treatment report payload is null."));
        return false;
    }
    if (treatmentReport->patientId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment report patient id is required."));
        return false;
    }
    if (treatmentReport->treatmentSessionId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment report session id is required."));
        return false;
    }
    if (treatmentReport->contentHtml.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment report content is required."));
        return false;
    }

    if (treatmentReport->id.trimmed().isEmpty()) {
        treatmentReport->id = makeId(QStringLiteral("REP"));
    }
    if (!treatmentReport->generatedAt.isValid()) {
        treatmentReport->generatedAt = QDateTime::currentDateTime();
    }
    if (treatmentReport->title.trimmed().isEmpty()) {
        treatmentReport->title = QStringLiteral("治疗报告");
    }

    TreatmentReportRecord existing;
    const bool exists = m_repository->findTreatmentReportById(treatmentReport->id, &existing);
    if (exists) {
        if (!m_repository->updateTreatmentReport(*treatmentReport)) {
            setLastError(m_repository->lastError());
            return false;
        }
    } else {
        if (!m_repository->createTreatmentReport(*treatmentReport)) {
            setLastError(m_repository->lastError());
            return false;
        }
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::deleteTreatmentReport(const QString& treatmentReportId)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (treatmentReportId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Treatment report id is required."));
        return false;
    }

    if (!m_repository->deleteTreatmentReport(treatmentReportId)) {
        setLastError(m_repository->lastError());
        return false;
    }

    setLastError(QString());
    return true;
}

bool ClinicalDataService::bootstrapFrom(const IClinicalDataRepository& sourceRepository)
{
    if (!ensureWritableRepository()) {
        return false;
    }
    if (m_repository == &sourceRepository) {
        setLastError(QString());
        return true;
    }

    const QVector<PatientRecord> sourcePatients = sourceRepository.listPatients();
    for (PatientRecord patient : sourcePatients) {
        PatientRecord existingPatient;
        if (!m_repository->findPatientById(patient.id, &existingPatient)) {
            if (!m_repository->createPatient(patient)) {
                setLastError(m_repository->lastError());
                return false;
            }
        }

        const QVector<ImageSeriesRecord> imageSeries = sourceRepository.listImageSeriesForPatient(patient.id);
        for (ImageSeriesRecord image : imageSeries) {
            ImageSeriesRecord existingImage;
            if (!m_repository->findImageSeriesById(image.id, &existingImage)) {
                if (!m_repository->createImageSeries(image)) {
                    setLastError(m_repository->lastError());
                    return false;
                }
            }
        }

        const QVector<TreatmentSessionRecord> sessions = sourceRepository.listTreatmentSessionsForPatient(patient.id);
        for (TreatmentSessionRecord session : sessions) {
            TreatmentSessionRecord existingSession;
            if (!m_repository->findTreatmentSessionById(session.id, &existingSession)) {
                if (!m_repository->createTreatmentSession(session)) {
                    setLastError(m_repository->lastError());
                    return false;
                }
            }
        }

        const QVector<TreatmentReportRecord> reports = sourceRepository.listTreatmentReportsForPatient(patient.id);
        for (TreatmentReportRecord report : reports) {
            TreatmentReportRecord existingReport;
            if (!m_repository->findTreatmentReportById(report.id, &existingReport)) {
                if (!m_repository->createTreatmentReport(report)) {
                    setLastError(m_repository->lastError());
                    return false;
                }
            }
        }
    }

    setLastError(QString());
    return true;
}

QString ClinicalDataService::makeId(const QString& prefix) const
{
    return QStringLiteral("%1-%2").arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void ClinicalDataService::setLastError(const QString& error)
{
    m_lastError = error;
}

bool ClinicalDataService::ensureRepositoryAvailable()
{
    if (m_repository != nullptr) {
        return true;
    }

    setLastError(QStringLiteral("Clinical data repository is not configured."));
    return false;
}

bool ClinicalDataService::ensureWritableRepository()
{
    if (!ensureRepositoryAvailable()) {
        return false;
    }
    if (m_repository->supportsWriteOperations()) {
        return true;
    }

    setLastError(QStringLiteral("Clinical data repository is read-only."));
    return false;
}

}  // namespace panthera::core
