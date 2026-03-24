#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QWidget>

#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/services/audit_service.h"
#include "core/services/clinical_data_service.h"

namespace panthera::modules {

class DataManagementPage final : public QWidget {
    Q_OBJECT

public:
    enum class Section {
        PatientInfo = 0,
        ImagingData,
        TreatmentReport,
        TreatmentData
    };

    DataManagementPage(
        panthera::core::ApplicationContext* context,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        QWidget* parent = nullptr);

    void showSection(Section section);

private slots:
    void refreshFromContext();
    void filterPatients(const QString& keyword);
    void onAddPatientClicked();
    void onBatchImportClicked();
    void onEditButtonClicked();
    void onDeleteButtonClicked();

private:
    struct ImportedPatientRow {
        QString id;
        QString name;
        int age {0};
        QString diagnosis;
    };

    enum class PatientPanelMode {
        View,
        Edit,
        Create
    };

    void populatePatientList(const QString& keyword = QString());
    void selectPatientById(const QString& patientId);
    void refreshPatientOverview(const panthera::core::PatientRecord* patient);
    void setPatientPanelMode(PatientPanelMode mode);
    void refreshPatientActionState();
    void enterCreateMode();
    void closeCreateMode();
    bool saveCurrentPatient();
    bool archiveCurrentPatient();
    bool importPatientsFromCsv();
    QVector<QString> splitCsvLine(const QString& line) const;
    bool parseImportedPatientRow(
        const QVector<QString>& columns,
        const QHash<QString, int>& headerIndexMap,
        ImportedPatientRow* row,
        QString* error) const;
    void fillImagingGallery(
        const panthera::core::PatientRecord* patient,
        const QVector<panthera::core::ImageSeriesRecord>& imageSeries);
    QWidget* createImagingThumbnailCard(const panthera::core::ImageSeriesRecord& imageSeries, int index) const;
    void fillTreatmentTable(const QVector<panthera::core::TreatmentSessionRecord>& treatmentSessions);

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::AuditService* m_auditService {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::core::ClinicalDataService m_clinicalDataService;
    QLineEdit* m_searchEdit {nullptr};
    QListWidget* m_patientList {nullptr};
    QWidget* m_sidebarActionsWidget {nullptr};
    QPushButton* m_addPatientButton {nullptr};
    QPushButton* m_batchImportButton {nullptr};
    QStackedWidget* m_sectionStack {nullptr};
    QLabel* m_panelTitleLabel {nullptr};
    QPushButton* m_editButton {nullptr};
    QPushButton* m_deleteButton {nullptr};
    QLineEdit* m_patientIdValue {nullptr};
    QLineEdit* m_patientNameEdit {nullptr};
    QSpinBox* m_patientAgeSpin {nullptr};
    QPlainTextEdit* m_patientDiagnosisEdit {nullptr};
    QLabel* m_imagingPatientNameLabel {nullptr};
    QScrollArea* m_imagingScrollArea {nullptr};
    QWidget* m_imagingGridContainer {nullptr};
    QTableWidget* m_treatmentTable {nullptr};
    QTextBrowser* m_reportPreview {nullptr};
    PatientPanelMode m_panelMode {PatientPanelMode::View};
};

}  // namespace panthera::modules
