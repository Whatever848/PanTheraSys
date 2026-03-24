#pragma once

#include <QString>
#include <QVector>

#include "core/repositories/clinical_data_repository.h"

namespace panthera::adapters {

class SeedClinicalDataRepository final : public panthera::core::IClinicalDataRepository {
public:
    SeedClinicalDataRepository();

    QString repositoryName() const override;
    bool supportsWriteOperations() const override;
    QString lastError() const override;

    QVector<panthera::core::PatientRecord> listPatients() const override;
    bool findPatientById(const QString& patientId, panthera::core::PatientRecord* patient) const override;
    bool createPatient(const panthera::core::PatientRecord& patient) override;
    bool updatePatient(const panthera::core::PatientRecord& patient) override;
    bool archivePatient(const QString& patientId, const QDateTime& archivedAt) override;

    QVector<panthera::core::ImageSeriesRecord> listImageSeriesForPatient(const QString& patientId) const override;
    bool findImageSeriesById(const QString& imageSeriesId, panthera::core::ImageSeriesRecord* imageSeries) const override;
    bool createImageSeries(const panthera::core::ImageSeriesRecord& imageSeries) override;
    bool updateImageSeries(const panthera::core::ImageSeriesRecord& imageSeries) override;
    bool deleteImageSeries(const QString& imageSeriesId) override;

    QVector<panthera::core::TreatmentSessionRecord> listTreatmentSessionsForPatient(const QString& patientId) const override;
    bool findTreatmentSessionById(const QString& treatmentSessionId, panthera::core::TreatmentSessionRecord* treatmentSession) const override;
    bool createTreatmentSession(const panthera::core::TreatmentSessionRecord& treatmentSession) override;
    bool updateTreatmentSession(const panthera::core::TreatmentSessionRecord& treatmentSession) override;
    bool deleteTreatmentSession(const QString& treatmentSessionId) override;

    QVector<panthera::core::TreatmentReportRecord> listTreatmentReportsForPatient(const QString& patientId) const override;
    bool findTreatmentReportById(const QString& treatmentReportId, panthera::core::TreatmentReportRecord* treatmentReport) const override;
    bool createTreatmentReport(const panthera::core::TreatmentReportRecord& treatmentReport) override;
    bool updateTreatmentReport(const panthera::core::TreatmentReportRecord& treatmentReport) override;
    bool deleteTreatmentReport(const QString& treatmentReportId) override;

private:
    struct SeedPatientBundle {
        panthera::core::PatientRecord patient;
        QVector<panthera::core::ImageSeriesRecord> imageSeries;
        QVector<panthera::core::TreatmentSessionRecord> treatmentSessions;
        QVector<panthera::core::TreatmentReportRecord> treatmentReports;
    };

    SeedPatientBundle* findBundleByPatientId(const QString& patientId);
    const SeedPatientBundle* findBundleByPatientId(const QString& patientId) const;
    void setLastError(const QString& error) const;

    SeedPatientBundle buildFirstPatientBundle() const;
    SeedPatientBundle buildSecondPatientBundle() const;

    QVector<SeedPatientBundle> m_seedBundles;
    mutable QString m_lastError;
};

}  // namespace panthera::adapters
