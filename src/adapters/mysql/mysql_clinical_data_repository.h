#pragma once

#include <QString>

#include "adapters/mysql/mysql_repository_facade.h"
#include "core/repositories/clinical_data_repository.h"

namespace panthera::adapters {

class MySqlClinicalDataRepository final : public panthera::core::IClinicalDataRepository {
public:
    MySqlClinicalDataRepository();
    ~MySqlClinicalDataRepository() override;

    bool open(const DatabaseConnectionSettings& settings);
    void close();
    bool isOpen() const;
    bool initializeSchemaFromFile(const QString& schemaFilePath);

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
    bool ensureTherapyPlanRow(const panthera::core::TreatmentSessionRecord& treatmentSession);
    void setLastError(const QString& error) const;

    MySqlRepositoryFacade m_facade;
    mutable QString m_lastError;
};

}  // namespace panthera::adapters
