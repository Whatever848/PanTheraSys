#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QWidget>

#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/services/audit_service.h"

namespace panthera::modules {

class DataManagementPage final : public QWidget {
    Q_OBJECT

public:
    DataManagementPage(
        panthera::core::ApplicationContext* context,
        panthera::core::AuditService* auditService,
        const panthera::core::IClinicalDataRepository* clinicalDataRepository,
        QWidget* parent = nullptr);

private slots:
    void appendAuditEntry(const panthera::core::AuditEntry& entry);
    void refreshFromContext();
    void filterPatients(const QString& keyword);

private:
    void populatePatientList(const QString& keyword = QString());
    void selectPatientById(const QString& patientId);
    void refreshPatientOverview(const panthera::core::PatientRecord* patient);
    void fillImagingTable(const QVector<panthera::core::ImageSeriesRecord>& imageSeries);
    void fillTreatmentTable(const QVector<panthera::core::TreatmentSessionRecord>& treatmentSessions);
    QString buildReportHtml(
        const panthera::core::PatientRecord& patient,
        const QVector<panthera::core::TreatmentReportRecord>& reports) const;

    panthera::core::ApplicationContext* m_context {nullptr};
    const panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    QLineEdit* m_searchEdit {nullptr};
    QListWidget* m_patientList {nullptr};
    QLabel* m_patientIdValue {nullptr};
    QLabel* m_patientNameValue {nullptr};
    QLabel* m_patientAgeValue {nullptr};
    QLabel* m_patientGenderValue {nullptr};
    QLabel* m_patientDiagnosisValue {nullptr};
    QTableWidget* m_imagingTable {nullptr};
    QTableWidget* m_treatmentTable {nullptr};
    QTextBrowser* m_reportPreview {nullptr};
    QListWidget* m_auditList {nullptr};
};

}  // namespace panthera::modules
