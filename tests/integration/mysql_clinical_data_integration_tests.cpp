#include <algorithm>

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "adapters/mysql/mysql_clinical_data_repository.h"
#include "core/services/clinical_data_service.h"

using namespace panthera::adapters;
using namespace panthera::core;

namespace {

QString resolveRuntimePath(const QString& relativePath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QDir::current().absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../%1").arg(relativePath)),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../%1").arg(relativePath))
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath(relativePath));
}

QString envOrSetting(QSettings& settings, const char* envName, const QString& settingKey, const QString& fallback = QString())
{
    const QString envValue = qEnvironmentVariable(envName);
    if (!envValue.isEmpty()) {
        return envValue;
    }

    return settings.value(settingKey, fallback).toString();
}

int envOrSettingInt(QSettings& settings, const char* envName, const QString& settingKey, int fallback)
{
    bool ok = false;
    const QString envValue = qEnvironmentVariable(envName);
    if (!envValue.isEmpty()) {
        const int value = envValue.toInt(&ok);
        if (ok) {
            return value;
        }
    }

    return settings.value(settingKey, fallback).toInt();
}

DatabaseConnectionSettings loadTestSettings()
{
    const QString defaultsPath = resolveRuntimePath(QStringLiteral("config/defaults.ini"));
    QSettings settings(defaultsPath, QSettings::IniFormat);

    DatabaseConnectionSettings connectionSettings;
    connectionSettings.connectionName = QStringLiteral("PanTheraClinicalDataIntegration");
    connectionSettings.host = envOrSetting(settings, "PANTHERA_MYSQL_HOST", QStringLiteral("database/host"), QStringLiteral("127.0.0.1"));
    connectionSettings.port = envOrSettingInt(settings, "PANTHERA_MYSQL_PORT", QStringLiteral("database/port"), 3306);
    connectionSettings.username = envOrSetting(settings, "PANTHERA_MYSQL_USERNAME", QStringLiteral("database/username"), QStringLiteral("root"));
    connectionSettings.password = envOrSetting(settings, "PANTHERA_MYSQL_PASSWORD", QStringLiteral("database/password"), QStringLiteral("root"));

    const QString defaultSchema = envOrSetting(settings, "PANTHERA_MYSQL_SCHEMA", QStringLiteral("database/schema"), QStringLiteral("panthera_sys"));
    connectionSettings.schema = qEnvironmentVariable("PANTHERA_MYSQL_TEST_SCHEMA");
    if (connectionSettings.schema.isEmpty()) {
        connectionSettings.schema = QStringLiteral("%1_integration").arg(defaultSchema);
    }

    return connectionSettings;
}

bool executeStatement(QSqlDatabase* database, const QString& statement, QString* error)
{
    QSqlQuery query(*database);
    if (query.exec(statement)) {
        return true;
    }

    if (error != nullptr) {
        *error = query.lastError().text();
    }
    return false;
}

QString quotedSchemaName(const QString& schema)
{
    QString escaped = schema;
    escaped.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QStringLiteral("`%1`").arg(escaped);
}

}  // namespace

class MySqlClinicalDataIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void saveUpdateAndArchivePatientAgainstMySql();
    void saveUpdateAndDeleteImageSeriesAgainstMySql();
    void saveUpdateTreatmentSessionAgainstMySql();
    void saveUpdateAndDeleteTreatmentReportAgainstMySql();
    void deleteTreatmentSessionCascadesReportsInMySql();

private:
    bool recreateSchema(QString* error);
    bool openRepository(QString* error);
    bool reopenRepository(QString* error);
    PatientRecord makePatient(const QString& suffix) const;
    bool preparePatient(PatientRecord* patient, QString* error);
    bool prepareSession(const QString& patientId, TreatmentSessionRecord* treatmentSession, QString* error);

    DatabaseConnectionSettings m_settings;
    QString m_schemaFilePath;
    bool m_mysqlReady {false};
    MySqlClinicalDataRepository m_repository;
    ClinicalDataService m_service;
};

void MySqlClinicalDataIntegrationTests::initTestCase()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        QSKIP("QMYSQL driver is unavailable in the current Qt runtime.");
    }

    m_settings = loadTestSettings();
    m_schemaFilePath = resolveRuntimePath(QStringLiteral("db/schema/mysql_5_7_init.sql"));
    if (!QFileInfo::exists(m_schemaFilePath)) {
        QSKIP(qPrintable(QStringLiteral("MySQL schema file not found: %1").arg(m_schemaFilePath)));
    }

    QString error;
    if (!recreateSchema(&error)) {
        QSKIP(qPrintable(QStringLiteral("Unable to prepare MySQL integration schema '%1': %2")
                             .arg(m_settings.schema, error)));
    }

    m_mysqlReady = true;
}

void MySqlClinicalDataIntegrationTests::init()
{
    if (!m_mysqlReady) {
        QSKIP("MySQL integration environment is unavailable.");
    }

    QString error;
    QVERIFY2(recreateSchema(&error), qPrintable(error));
    QVERIFY2(openRepository(&error), qPrintable(error));
}

void MySqlClinicalDataIntegrationTests::cleanup()
{
    m_service.setRepository(nullptr);
    m_repository.close();
}

void MySqlClinicalDataIntegrationTests::cleanupTestCase()
{
    if (!m_mysqlReady) {
        return;
    }

    QString error;
    recreateSchema(&error);
}

void MySqlClinicalDataIntegrationTests::saveUpdateAndArchivePatientAgainstMySql()
{
    PatientRecord patient = makePatient(QStringLiteral("patient-crud"));
    QVERIFY2(m_service.savePatient(&patient), qPrintable(m_service.lastError()));

    QString error;
    QVERIFY2(reopenRepository(&error), qPrintable(error));

    PatientRecord persistedPatient;
    QVERIFY(m_repository.findPatientById(patient.id, &persistedPatient));
    QCOMPARE(persistedPatient.name, patient.name);
    QCOMPARE(persistedPatient.diagnosis, patient.diagnosis);

    persistedPatient.contact = QStringLiteral("400-800-1000");
    persistedPatient.diagnosis = QStringLiteral("Updated diagnosis");
    QVERIFY2(m_service.savePatient(&persistedPatient), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    PatientRecord updatedPatient;
    QVERIFY(m_repository.findPatientById(patient.id, &updatedPatient));
    QCOMPARE(updatedPatient.contact, persistedPatient.contact);
    QCOMPARE(updatedPatient.diagnosis, persistedPatient.diagnosis);

    QVERIFY2(m_service.archivePatient(patient.id), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    PatientRecord archivedPatient;
    QVERIFY(!m_repository.findPatientById(patient.id, &archivedPatient));
    QVERIFY(m_repository.lastError().contains(QStringLiteral("not found"), Qt::CaseInsensitive));

    const QVector<PatientRecord> activePatients = m_repository.listPatients();
    const bool stillVisible = std::any_of(activePatients.cbegin(), activePatients.cend(), [&patient](const PatientRecord& current) {
        return current.id == patient.id;
    });
    QVERIFY(!stillVisible);
}

void MySqlClinicalDataIntegrationTests::saveUpdateAndDeleteImageSeriesAgainstMySql()
{
    PatientRecord patient;
    QString error;
    QVERIFY2(preparePatient(&patient, &error), qPrintable(error));

    ImageSeriesRecord imageSeries;
    imageSeries.patientId = patient.id;
    imageSeries.type = QStringLiteral("Ultrasound");
    imageSeries.storagePath = QStringLiteral("D:/PanTheraSys/runtime/images/integration-image-001.png");
    imageSeries.notes = QStringLiteral("Baseline study");

    QVERIFY2(m_service.saveImageSeries(&imageSeries), qPrintable(m_service.lastError()));
    QVERIFY(!imageSeries.id.isEmpty());

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    ImageSeriesRecord persistedImageSeries;
    QVERIFY(m_repository.findImageSeriesById(imageSeries.id, &persistedImageSeries));
    QCOMPARE(persistedImageSeries.patientId, patient.id);
    QCOMPARE(persistedImageSeries.storagePath, imageSeries.storagePath);

    persistedImageSeries.storagePath = QStringLiteral("D:/PanTheraSys/runtime/images/integration-image-002.png");
    persistedImageSeries.notes = QStringLiteral("Updated study");
    QVERIFY2(m_service.saveImageSeries(&persistedImageSeries), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    ImageSeriesRecord updatedImageSeries;
    QVERIFY(m_repository.findImageSeriesById(imageSeries.id, &updatedImageSeries));
    QCOMPARE(updatedImageSeries.storagePath, persistedImageSeries.storagePath);
    QCOMPARE(updatedImageSeries.notes, persistedImageSeries.notes);

    QVERIFY2(m_service.deleteImageSeries(imageSeries.id), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    ImageSeriesRecord deletedImageSeries;
    QVERIFY(!m_repository.findImageSeriesById(imageSeries.id, &deletedImageSeries));
    QVERIFY(m_repository.lastError().contains(QStringLiteral("not found"), Qt::CaseInsensitive));
}

void MySqlClinicalDataIntegrationTests::saveUpdateTreatmentSessionAgainstMySql()
{
    PatientRecord patient;
    QString error;
    QVERIFY2(preparePatient(&patient, &error), qPrintable(error));

    TreatmentSessionRecord treatmentSession;
    QVERIFY2(prepareSession(patient.id, &treatmentSession, &error), qPrintable(error));
    QVERIFY2(m_service.saveTreatmentSession(&treatmentSession), qPrintable(m_service.lastError()));
    QVERIFY(!treatmentSession.id.isEmpty());

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentSessionRecord persistedSession;
    QVERIFY(m_repository.findTreatmentSessionById(treatmentSession.id, &persistedSession));
    QCOMPARE(persistedSession.patientId, patient.id);
    QCOMPARE(persistedSession.planId, treatmentSession.planId);
    QCOMPARE(persistedSession.pathSummary, treatmentSession.pathSummary);

    persistedSession.totalEnergyJ = 6800.0;
    persistedSession.status = QStringLiteral("Completed");
    persistedSession.pathSummary = QStringLiteral("Updated ablation grid");
    QVERIFY2(m_service.saveTreatmentSession(&persistedSession), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentSessionRecord updatedSession;
    QVERIFY(m_repository.findTreatmentSessionById(treatmentSession.id, &updatedSession));
    QCOMPARE(updatedSession.totalEnergyJ, persistedSession.totalEnergyJ);
    QCOMPARE(updatedSession.status, persistedSession.status);
    QCOMPARE(updatedSession.pathSummary, persistedSession.pathSummary);
}

void MySqlClinicalDataIntegrationTests::saveUpdateAndDeleteTreatmentReportAgainstMySql()
{
    PatientRecord patient;
    QString error;
    QVERIFY2(preparePatient(&patient, &error), qPrintable(error));

    TreatmentSessionRecord treatmentSession;
    QVERIFY2(prepareSession(patient.id, &treatmentSession, &error), qPrintable(error));
    QVERIFY2(m_service.saveTreatmentSession(&treatmentSession), qPrintable(m_service.lastError()));

    TreatmentReportRecord treatmentReport;
    treatmentReport.patientId = patient.id;
    treatmentReport.treatmentSessionId = treatmentSession.id;
    treatmentReport.title = QStringLiteral("Integration treatment report");
    treatmentReport.notes = QStringLiteral("Initial notes");
    treatmentReport.contentHtml = QStringLiteral("<p>Initial report content</p>");

    QVERIFY2(m_service.saveTreatmentReport(&treatmentReport), qPrintable(m_service.lastError()));
    QVERIFY(!treatmentReport.id.isEmpty());

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentReportRecord persistedReport;
    QVERIFY(m_repository.findTreatmentReportById(treatmentReport.id, &persistedReport));
    QCOMPARE(persistedReport.patientId, patient.id);
    QCOMPARE(persistedReport.treatmentSessionId, treatmentSession.id);
    QCOMPARE(persistedReport.title, treatmentReport.title);

    persistedReport.title = QStringLiteral("Updated treatment report");
    persistedReport.notes = QStringLiteral("Updated notes");
    persistedReport.contentHtml = QStringLiteral("<p>Updated report content</p>");
    QVERIFY2(m_service.saveTreatmentReport(&persistedReport), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentReportRecord updatedReport;
    QVERIFY(m_repository.findTreatmentReportById(treatmentReport.id, &updatedReport));
    QCOMPARE(updatedReport.title, persistedReport.title);
    QCOMPARE(updatedReport.notes, persistedReport.notes);
    QCOMPARE(updatedReport.contentHtml, persistedReport.contentHtml);

    QVERIFY2(m_service.deleteTreatmentReport(treatmentReport.id), qPrintable(m_service.lastError()));

    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentReportRecord deletedReport;
    QVERIFY(!m_repository.findTreatmentReportById(treatmentReport.id, &deletedReport));
    QVERIFY(m_repository.lastError().contains(QStringLiteral("not found"), Qt::CaseInsensitive));
}

void MySqlClinicalDataIntegrationTests::deleteTreatmentSessionCascadesReportsInMySql()
{
    PatientRecord patient;
    QString error;
    QVERIFY2(preparePatient(&patient, &error), qPrintable(error));

    TreatmentSessionRecord treatmentSession;
    QVERIFY2(prepareSession(patient.id, &treatmentSession, &error), qPrintable(error));
    QVERIFY2(m_service.saveTreatmentSession(&treatmentSession), qPrintable(m_service.lastError()));

    TreatmentReportRecord treatmentReport;
    treatmentReport.patientId = patient.id;
    treatmentReport.treatmentSessionId = treatmentSession.id;
    treatmentReport.title = QStringLiteral("Cascade target report");
    treatmentReport.contentHtml = QStringLiteral("<p>Cascade verification</p>");
    QVERIFY2(m_service.saveTreatmentReport(&treatmentReport), qPrintable(m_service.lastError()));

    QVERIFY2(m_service.deleteTreatmentSession(treatmentSession.id), qPrintable(m_service.lastError()));
    QVERIFY2(reopenRepository(&error), qPrintable(error));

    TreatmentSessionRecord deletedSession;
    QVERIFY(!m_repository.findTreatmentSessionById(treatmentSession.id, &deletedSession));

    const QVector<TreatmentReportRecord> reports = m_repository.listTreatmentReportsForPatient(patient.id);
    const bool danglingReportExists = std::any_of(reports.cbegin(), reports.cend(), [&treatmentSession](const TreatmentReportRecord& report) {
        return report.treatmentSessionId == treatmentSession.id;
    });
    QVERIFY(!danglingReportExists);
}

bool MySqlClinicalDataIntegrationTests::recreateSchema(QString* error)
{
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        database.setHostName(m_settings.host);
        database.setPort(m_settings.port);
        database.setUserName(m_settings.username);
        database.setPassword(m_settings.password);

        if (!database.open()) {
            if (error != nullptr) {
                *error = database.lastError().text();
            }
            database.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        const QString dropDatabase = QStringLiteral("DROP DATABASE IF EXISTS %1").arg(quotedSchemaName(m_settings.schema));
        const QString createDatabase =
            QStringLiteral("CREATE DATABASE %1 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci")
                .arg(quotedSchemaName(m_settings.schema));

        if (!executeStatement(&database, dropDatabase, error) || !executeStatement(&database, createDatabase, error)) {
            database.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        database.close();
    }

    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

bool MySqlClinicalDataIntegrationTests::openRepository(QString* error)
{
    m_service.setRepository(nullptr);
    if (!m_repository.open(m_settings)) {
        if (error != nullptr) {
            *error = m_repository.lastError();
        }
        return false;
    }

    if (!m_repository.initializeSchemaFromFile(m_schemaFilePath)) {
        if (error != nullptr) {
            *error = m_repository.lastError();
        }
        m_repository.close();
        return false;
    }

    m_service.setRepository(&m_repository);
    return true;
}

bool MySqlClinicalDataIntegrationTests::reopenRepository(QString* error)
{
    m_service.setRepository(nullptr);
    m_repository.close();
    return openRepository(error);
}

PatientRecord MySqlClinicalDataIntegrationTests::makePatient(const QString& suffix) const
{
    PatientRecord patient;
    patient.name = QStringLiteral("Integration-%1").arg(suffix);
    patient.age = 46;
    patient.gender = QStringLiteral("F");
    patient.diagnosis = QStringLiteral("Breast ultrasound therapy integration test");
    patient.contact = QStringLiteral("13900000000");
    return patient;
}

bool MySqlClinicalDataIntegrationTests::preparePatient(PatientRecord* patient, QString* error)
{
    if (patient == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Patient payload is null.");
        }
        return false;
    }

    *patient = makePatient(QStringLiteral("seed"));
    if (m_service.savePatient(patient)) {
        return true;
    }

    if (error != nullptr) {
        *error = m_service.lastError();
    }
    return false;
}

bool MySqlClinicalDataIntegrationTests::prepareSession(const QString& patientId, TreatmentSessionRecord* treatmentSession, QString* error)
{
    if (treatmentSession == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Treatment session payload is null.");
        }
        return false;
    }

    treatmentSession->patientId = patientId;
    treatmentSession->planId = QStringLiteral("PLAN-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    treatmentSession->lesionType = QStringLiteral("Breast lesion");
    treatmentSession->pathSummary = QStringLiteral("Ablation path summary");
    treatmentSession->totalEnergyJ = 5600.0;
    treatmentSession->totalDurationSeconds = 420.0;
    treatmentSession->dose = 62.5;
    treatmentSession->status = QStringLiteral("Draft");
    treatmentSession->treatmentDate = QDateTime::currentDateTime().addSecs(-120);
    treatmentSession->startedAt = treatmentSession->treatmentDate;
    treatmentSession->endedAt = treatmentSession->startedAt.addSecs(static_cast<int>(treatmentSession->totalDurationSeconds));
    Q_UNUSED(error)
    return true;
}

QTEST_GUILESS_MAIN(MySqlClinicalDataIntegrationTests)

#include "mysql_clinical_data_integration_tests.moc"
