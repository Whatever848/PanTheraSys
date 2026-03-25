#pragma once

#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
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
#include <QTextEdit>
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
    void onAddImageSeriesClicked();
    void onSaveImageSeriesClicked();
    void onDeleteImageSeriesClicked();
    void onAddTreatmentSessionClicked();
    void onSaveTreatmentSessionClicked();
    void onDeleteTreatmentSessionClicked();
    void onTreatmentSelectionChanged();
    void onAddTreatmentReportClicked();
    void onSaveTreatmentReportClicked();
    void onDeleteTreatmentReportClicked();
    void onReportSelectionChanged();
    void syncReportPreview();

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
    void refreshDataActionState();
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
    QWidget* createImagingThumbnailCard(const panthera::core::ImageSeriesRecord& imageSeries, int index);
    void populateImageSeriesEditor(const panthera::core::ImageSeriesRecord* imageSeries);
    const panthera::core::ImageSeriesRecord* findSelectedImageSeries() const;
    void setSelectedImageSeries(const QString& imageSeriesId);
    bool saveCurrentImageSeries();
    bool deleteCurrentImageSeries();

    void fillTreatmentTable(const QVector<panthera::core::TreatmentSessionRecord>& treatmentSessions);
    void populateTreatmentSessionEditor(const panthera::core::TreatmentSessionRecord* treatmentSession);
    const panthera::core::TreatmentSessionRecord* findSelectedTreatmentSession() const;
    void setSelectedTreatmentSession(const QString& treatmentSessionId);
    bool saveCurrentTreatmentSession();
    bool deleteCurrentTreatmentSession();

    void fillReportList(const QVector<panthera::core::TreatmentReportRecord>& treatmentReports);
    void populateTreatmentReportEditor(const panthera::core::TreatmentReportRecord* treatmentReport);
    const panthera::core::TreatmentReportRecord* findSelectedTreatmentReport() const;
    void setSelectedTreatmentReport(const QString& treatmentReportId);
    void refreshReportSessionOptions(const QString& preferredTreatmentSessionId = QString());
    bool saveCurrentTreatmentReport();
    bool deleteCurrentTreatmentReport();

    void refreshImagePreview(const QString& storagePath);
    QPixmap loadPreviewPixmap(const QString& storagePath, const QSize& targetSize) const;
    void clearReportPreview(const QString& message);
    bool hasWritableRepository() const;

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
    QPushButton* m_addImageSeriesButton {nullptr};
    QPushButton* m_saveImageSeriesButton {nullptr};
    QPushButton* m_deleteImageSeriesButton {nullptr};
    QScrollArea* m_imagingScrollArea {nullptr};
    QWidget* m_imagingGridContainer {nullptr};
    QLineEdit* m_imageSeriesIdValue {nullptr};
    QLineEdit* m_imageSeriesTypeEdit {nullptr};
    QLineEdit* m_imageSeriesStoragePathEdit {nullptr};
    QDateEdit* m_imageSeriesDateEdit {nullptr};
    QPlainTextEdit* m_imageSeriesNotesEdit {nullptr};
    QLabel* m_imageSeriesPreviewLabel {nullptr};

    QPushButton* m_addReportButton {nullptr};
    QPushButton* m_saveReportButton {nullptr};
    QPushButton* m_deleteReportButton {nullptr};
    QListWidget* m_reportListWidget {nullptr};
    QLineEdit* m_reportIdValue {nullptr};
    QComboBox* m_reportSessionCombo {nullptr};
    QLineEdit* m_reportTitleEdit {nullptr};
    QDateTimeEdit* m_reportGeneratedAtEdit {nullptr};
    QPlainTextEdit* m_reportNotesEdit {nullptr};
    QTextEdit* m_reportContentEdit {nullptr};
    QTextBrowser* m_reportPreview {nullptr};

    QPushButton* m_addTreatmentButton {nullptr};
    QPushButton* m_saveTreatmentButton {nullptr};
    QPushButton* m_deleteTreatmentButton {nullptr};
    QTableWidget* m_treatmentTable {nullptr};
    QLineEdit* m_treatmentIdValue {nullptr};
    QLineEdit* m_treatmentPlanIdEdit {nullptr};
    QLineEdit* m_treatmentLesionTypeEdit {nullptr};
    QDateTimeEdit* m_treatmentDateEdit {nullptr};
    QLineEdit* m_treatmentStatusEdit {nullptr};
    QDoubleSpinBox* m_treatmentEnergySpin {nullptr};
    QDoubleSpinBox* m_treatmentDurationSpin {nullptr};
    QDoubleSpinBox* m_treatmentDoseSpin {nullptr};
    QPlainTextEdit* m_treatmentPathSummaryEdit {nullptr};

    QVector<panthera::core::ImageSeriesRecord> m_currentImageSeries;
    QVector<panthera::core::TreatmentSessionRecord> m_currentTreatmentSessions;
    QVector<panthera::core::TreatmentReportRecord> m_currentTreatmentReports;
    QString m_selectedImageSeriesId;
    QString m_selectedTreatmentSessionId;
    QString m_selectedTreatmentReportId;
    PatientPanelMode m_panelMode {PatientPanelMode::View};
};

}  // namespace panthera::modules
