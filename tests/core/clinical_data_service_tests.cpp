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
    void saveTreatmentReportRequiresContent();
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

QTEST_GUILESS_MAIN(ClinicalDataServiceTests)

#include "clinical_data_service_tests.moc"
