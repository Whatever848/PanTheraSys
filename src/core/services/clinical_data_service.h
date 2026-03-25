#pragma once

#include <QString>
#include <QVector>

#include "core/repositories/clinical_data_repository.h"

namespace panthera::core {

class ClinicalDataService final {
public:
    explicit ClinicalDataService(IClinicalDataRepository* repository = nullptr);

    void setRepository(IClinicalDataRepository* repository);
    IClinicalDataRepository* repository() const;

    QString lastError() const;

    QVector<PatientRecord> listPatients() const;
    bool findPatientById(const QString& patientId, PatientRecord* patient) const;

    QVector<ImageSeriesRecord> listImageSeriesForPatient(const QString& patientId) const;
    bool findImageSeriesById(const QString& imageSeriesId, ImageSeriesRecord* imageSeries) const;

    QVector<TreatmentSessionRecord> listTreatmentSessionsForPatient(const QString& patientId) const;
    bool findTreatmentSessionById(const QString& treatmentSessionId, TreatmentSessionRecord* treatmentSession) const;

    QVector<TreatmentReportRecord> listTreatmentReportsForPatient(const QString& patientId) const;
    bool findTreatmentReportById(const QString& treatmentReportId, TreatmentReportRecord* treatmentReport) const;

    bool savePatient(PatientRecord* patient);
    bool archivePatient(const QString& patientId);

    bool saveImageSeries(ImageSeriesRecord* imageSeries);
    bool deleteImageSeries(const QString& imageSeriesId);

    bool saveTreatmentSession(TreatmentSessionRecord* treatmentSession);
    bool deleteTreatmentSession(const QString& treatmentSessionId);

    bool saveTreatmentReport(TreatmentReportRecord* treatmentReport);
    bool deleteTreatmentReport(const QString& treatmentReportId);

    bool bootstrapFrom(const IClinicalDataRepository& sourceRepository);

private:
    QString makeId(const QString& prefix) const;
    void setLastError(const QString& error) const;
    bool ensureRepositoryAvailable() const;
    bool ensureWritableRepository() const;
    bool ensurePatientExists(const QString& patientId, PatientRecord* patient = nullptr) const;
    bool ensureTreatmentSessionExists(const QString& treatmentSessionId, TreatmentSessionRecord* treatmentSession = nullptr) const;

    IClinicalDataRepository* m_repository {nullptr};
    mutable QString m_lastError;
};

}  // namespace panthera::core
