#include <algorithm>

#include <QtTest/QtTest>

#include "adapters/seed/seed_clinical_data_repository.h"
#include "core/services/clinical_data_service.h"

using namespace panthera::core;

class ClinicalDataServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void savePatientGeneratesIdAndPersistsRecord();
    void archivePatientRemovesItFromActiveListing();
    void saveImageSeriesRequiresExistingPatient();
    void saveImageSeriesGeneratesIdAndPersistsRecord();
    void saveTreatmentSessionRequiresExistingPatient();
    void saveTreatmentSessionGeneratesIdAndPersistsRecord();
    void saveTreatmentReportRequiresContent();
    void saveTreatmentReportRequiresExistingSession();
    void saveTreatmentReportRejectsPatientSessionMismatch();
    void deleteTreatmentSessionRemovesAssociatedReports();
};

void ClinicalDataServiceTests::savePatientGeneratesIdAndPersistsRecord()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    PatientRecord patient;
    patient.name = QStringLiteral("Alice");
    patient.age = 35;
    patient.gender = QStringLiteral("F");
    patient.diagnosis = QStringLiteral("Diagnosis-A");
    patient.contact = QStringLiteral("10086");

    QVERIFY(service.savePatient(&patient));
    QVERIFY(!patient.id.isEmpty());
    QVERIFY(patient.createdAt.isValid());
    QVERIFY(patient.updatedAt.isValid());

    PatientRecord persisted;
    QVERIFY(repository.findPatientById(patient.id, &persisted));
    QCOMPARE(persisted.name, patient.name);
    QCOMPARE(persisted.diagnosis, patient.diagnosis);
}

void ClinicalDataServiceTests::archivePatientRemovesItFromActiveListing()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    PatientRecord patient;
    patient.name = QStringLiteral("Bob");
    patient.age = 40;
    patient.gender = QStringLiteral("M");
    patient.diagnosis = QStringLiteral("Diagnosis-B");

    QVERIFY(service.savePatient(&patient));
    QVERIFY(service.archivePatient(patient.id));

    const QVector<PatientRecord> patients = repository.listPatients();
    const bool stillVisible = std::any_of(patients.cbegin(), patients.cend(), [&patient](const PatientRecord& current) {
        return current.id == patient.id;
    });
    QVERIFY(!stillVisible);
}

void ClinicalDataServiceTests::saveImageSeriesRequiresExistingPatient()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    ImageSeriesRecord imageSeries;
    imageSeries.patientId = QStringLiteral("P-MISSING");
    imageSeries.type = QStringLiteral("Ultrasound");
    imageSeries.storagePath = QStringLiteral("D:/PanTheraSys/runtime/images/missing-study.dcm");

    QVERIFY(!service.saveImageSeries(&imageSeries));
    QVERIFY(service.lastError().contains(QStringLiteral("Patient not found"), Qt::CaseInsensitive));
}

void ClinicalDataServiceTests::saveImageSeriesGeneratesIdAndPersistsRecord()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    const QVector<PatientRecord> patients = repository.listPatients();
    QVERIFY(!patients.isEmpty());

    ImageSeriesRecord imageSeries;
    imageSeries.patientId = patients.first().id;
    imageSeries.type = QStringLiteral("Ultrasound");
    imageSeries.storagePath = QStringLiteral("D:/PanTheraSys/runtime/images/new-study-001.dcm");
    imageSeries.notes = QStringLiteral("Synthetic persistence test");

    QVERIFY(service.saveImageSeries(&imageSeries));
    QVERIFY(!imageSeries.id.isEmpty());
    QVERIFY(imageSeries.acquisitionDate.isValid());
    QVERIFY(imageSeries.createdAt.isValid());

    ImageSeriesRecord persisted;
    QVERIFY(repository.findImageSeriesById(imageSeries.id, &persisted));
    QCOMPARE(persisted.patientId, imageSeries.patientId);
    QCOMPARE(persisted.storagePath, imageSeries.storagePath);
}

void ClinicalDataServiceTests::saveTreatmentSessionRequiresExistingPatient()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    TreatmentSessionRecord session;
    session.patientId = QStringLiteral("P-MISSING");
    session.planId = QStringLiteral("PLAN-001");
    session.lesionType = QStringLiteral("Invasive ductal carcinoma");
    session.pathSummary = QStringLiteral("Path-A");
    session.totalEnergyJ = 1200.0;
    session.totalDurationSeconds = 180.0;
    session.dose = 35.0;

    QVERIFY(!service.saveTreatmentSession(&session));
    QVERIFY(service.lastError().contains(QStringLiteral("Patient not found"), Qt::CaseInsensitive));
}

void ClinicalDataServiceTests::saveTreatmentSessionGeneratesIdAndPersistsRecord()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    const QVector<PatientRecord> patients = repository.listPatients();
    QVERIFY(!patients.isEmpty());

    TreatmentSessionRecord session;
    session.patientId = patients.first().id;
    session.planId = QStringLiteral("PLAN-NEW-001");
    session.lesionType = QStringLiteral("Breast lesion");
    session.pathSummary = QStringLiteral("Grid-Path-01");
    session.totalEnergyJ = 3200.0;
    session.totalDurationSeconds = 420.0;
    session.dose = 56.5;

    QVERIFY(service.saveTreatmentSession(&session));
    QVERIFY(!session.id.isEmpty());
    QVERIFY(session.treatmentDate.isValid());
    QVERIFY(session.startedAt.isValid());
    QVERIFY(session.endedAt.isValid());
    QVERIFY(session.createdAt.isValid());
    QCOMPARE(session.status, QStringLiteral("草稿"));

    TreatmentSessionRecord persisted;
    QVERIFY(repository.findTreatmentSessionById(session.id, &persisted));
    QCOMPARE(persisted.patientId, session.patientId);
    QCOMPARE(persisted.planId, session.planId);
    QCOMPARE(persisted.pathSummary, session.pathSummary);
}

void ClinicalDataServiceTests::saveTreatmentReportRequiresContent()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    TreatmentReportRecord report;
    report.patientId = QStringLiteral("P2026001");
    report.treatmentSessionId = QStringLiteral("TX-800");

    QVERIFY(!service.saveTreatmentReport(&report));
    QVERIFY(!service.lastError().isEmpty());
}

void ClinicalDataServiceTests::saveTreatmentReportRequiresExistingSession()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    const QVector<PatientRecord> patients = repository.listPatients();
    QVERIFY(!patients.isEmpty());

    TreatmentReportRecord report;
    report.patientId = patients.first().id;
    report.treatmentSessionId = QStringLiteral("TX-MISSING");
    report.contentHtml = QStringLiteral("<p>Validation only</p>");

    QVERIFY(!service.saveTreatmentReport(&report));
    QVERIFY(service.lastError().contains(QStringLiteral("Treatment session not found"), Qt::CaseInsensitive));
}

void ClinicalDataServiceTests::saveTreatmentReportRejectsPatientSessionMismatch()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    const QVector<PatientRecord> patients = repository.listPatients();
    QVERIFY(patients.size() >= 2);

    const QVector<TreatmentSessionRecord> firstPatientSessions = repository.listTreatmentSessionsForPatient(patients.first().id);
    QVERIFY(!firstPatientSessions.isEmpty());

    TreatmentReportRecord report;
    report.patientId = patients.last().id;
    report.treatmentSessionId = firstPatientSessions.first().id;
    report.contentHtml = QStringLiteral("<p>Cross-patient report should fail</p>");

    QVERIFY(!service.saveTreatmentReport(&report));
    QVERIFY(service.lastError().contains(QStringLiteral("does not match"), Qt::CaseInsensitive));
}

void ClinicalDataServiceTests::deleteTreatmentSessionRemovesAssociatedReports()
{
    panthera::adapters::SeedClinicalDataRepository repository;
    ClinicalDataService service(&repository);

    const QVector<PatientRecord> patients = repository.listPatients();
    QVERIFY(!patients.isEmpty());

    const QString patientId = patients.first().id;
    const QVector<TreatmentSessionRecord> sessions = repository.listTreatmentSessionsForPatient(patientId);
    QVERIFY(!sessions.isEmpty());

    const QString treatmentSessionId = sessions.first().id;
    const QVector<TreatmentReportRecord> reportsBefore = repository.listTreatmentReportsForPatient(patientId);
    const bool reportExists = std::any_of(reportsBefore.cbegin(), reportsBefore.cend(), [&treatmentSessionId](const TreatmentReportRecord& report) {
        return report.treatmentSessionId == treatmentSessionId;
    });
    QVERIFY(reportExists);

    QVERIFY(service.deleteTreatmentSession(treatmentSessionId));

    TreatmentSessionRecord deletedSession;
    QVERIFY(!repository.findTreatmentSessionById(treatmentSessionId, &deletedSession));

    const QVector<TreatmentReportRecord> reportsAfter = repository.listTreatmentReportsForPatient(patientId);
    const bool danglingReportExists = std::any_of(reportsAfter.cbegin(), reportsAfter.cend(), [&treatmentSessionId](const TreatmentReportRecord& report) {
        return report.treatmentSessionId == treatmentSessionId;
    });
    QVERIFY(!danglingReportExists);
}

QTEST_GUILESS_MAIN(ClinicalDataServiceTests)

#include "clinical_data_service_tests.moc"
