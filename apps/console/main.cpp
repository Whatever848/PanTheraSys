#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QSettings>

#include "adapters/config/local_settings_store.h"
#include "adapters/mysql/mysql_clinical_data_repository.h"
#include "adapters/seed/seed_clinical_data_repository.h"
#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/application/event_bus.h"
#include "core/domain/system_types.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "core/services/clinical_data_service.h"
#include "modules/shell/main_window.h"

namespace {

QString resolveRuntimePath(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
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

panthera::adapters::DatabaseConnectionSettings loadDatabaseSettings(const QString& defaultsIniPath)
{
    QSettings settings(defaultsIniPath, QSettings::IniFormat);

    panthera::adapters::DatabaseConnectionSettings connectionSettings;
    connectionSettings.connectionName = QStringLiteral("PanTheraClinicalData");
    connectionSettings.host = settings.value(QStringLiteral("database/host"), QStringLiteral("127.0.0.1")).toString();
    connectionSettings.schema = settings.value(QStringLiteral("database/schema"), QStringLiteral("panthera_sys")).toString();
    connectionSettings.username = settings.value(QStringLiteral("database/username"), QStringLiteral("panthera_app")).toString();
    connectionSettings.password = settings.value(QStringLiteral("database/password")).toString();
    connectionSettings.port = settings.value(QStringLiteral("database/port"), 3306).toInt();
    return connectionSettings;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("PanTheraSys"));
    application.setApplicationName(QStringLiteral("PanTheraConsole"));
    Q_INIT_RESOURCE(resources);

    qRegisterMetaType<panthera::core::PatientRecord>();
    qRegisterMetaType<panthera::core::TherapyPlan>();
    qRegisterMetaType<panthera::core::DeviceSnapshot>();
    qRegisterMetaType<panthera::core::SafetySnapshot>();
    qRegisterMetaType<panthera::core::AuditEntry>();

    QFile styleFile(QStringLiteral(":/styles/panthera.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    panthera::core::EventBus eventBus;
    panthera::core::AuditService auditService;
    panthera::core::ApplicationContext context(&eventBus, &auditService);
    panthera::core::SafetyKernel safetyKernel;
    panthera::adapters::LocalSettingsStore settingsStore;
    panthera::adapters::SeedClinicalDataRepository seedClinicalDataRepository;
    panthera::adapters::MySqlClinicalDataRepository mysqlClinicalDataRepository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;
    panthera::core::IClinicalDataRepository* clinicalDataRepository = &seedClinicalDataRepository;

    Q_UNUSED(settingsStore)

    QObject::connect(
        &simulationDevice,
        &panthera::adapters::SimulationDeviceFacade::healthSignalsChanged,
        &safetyKernel,
        [&safetyKernel](bool waterHealthy, bool powerReady, bool motionReady, bool emergencyStopReleased, bool ultrasoundAvailable) {
            safetyKernel.setWaterLoopHealthy(waterHealthy);
            safetyKernel.setPowerReady(powerReady);
            safetyKernel.setMotionReady(motionReady);
            safetyKernel.setEmergencyStopReleased(emergencyStopReleased);
            safetyKernel.setUltrasoundAvailable(ultrasoundAvailable);
        });
    QObject::connect(&context, &panthera::core::ApplicationContext::selectedPatientChanged, &safetyKernel, [&safetyKernel](const panthera::core::PatientRecord&) {
        safetyKernel.setPatientSelected(true);
    });
    QObject::connect(&context, &panthera::core::ApplicationContext::selectedPatientCleared, &safetyKernel, [&safetyKernel]() {
        safetyKernel.setPatientSelected(false);
    });
    QObject::connect(&context, &panthera::core::ApplicationContext::activePlanChanged, &safetyKernel, [&safetyKernel](const panthera::core::TherapyPlan& plan) {
        safetyKernel.setPlanApprovalState(plan.approvalState);
    });
    QObject::connect(&context, &panthera::core::ApplicationContext::activePlanCleared, &safetyKernel, [&safetyKernel]() {
        safetyKernel.setPlanApprovalState(panthera::core::ApprovalState::Draft);
    });

    const QString defaultsIniPath = resolveRuntimePath(QStringLiteral("config/defaults.ini"));
    const QString schemaFilePath = resolveRuntimePath(QStringLiteral("db/schema/mysql_5_7_init.sql"));

    if (QFileInfo::exists(defaultsIniPath) && mysqlClinicalDataRepository.open(loadDatabaseSettings(defaultsIniPath))) {
        bool mysqlReady = true;
        if (QFileInfo::exists(schemaFilePath) && !mysqlClinicalDataRepository.initializeSchemaFromFile(schemaFilePath)) {
            auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("MySQL schema init failed: %1").arg(mysqlClinicalDataRepository.lastError()));
            mysqlReady = false;
        }

        if (mysqlReady) {
            panthera::core::ClinicalDataService clinicalDataService(&mysqlClinicalDataRepository);
            if (mysqlClinicalDataRepository.listPatients().isEmpty()) {
                if (!clinicalDataService.bootstrapFrom(seedClinicalDataRepository)) {
                    auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("MySQL bootstrap failed: %1").arg(clinicalDataService.lastError()));
                    mysqlReady = false;
                } else {
                    auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("MySQL repository bootstrapped from seed data"));
                }
            }
        }

        if (mysqlReady) {
            clinicalDataRepository = &mysqlClinicalDataRepository;
            auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("Using MySQL clinical data repository"));
        } else {
            mysqlClinicalDataRepository.close();
        }
    } else {
        const QString reason = QFileInfo::exists(defaultsIniPath)
            ? mysqlClinicalDataRepository.lastError()
            : QStringLiteral("defaults.ini not found");
        auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("Fallback to seed clinical data repository: %1").arg(reason));
    }

    simulationDevice.start();

    panthera::modules::MainWindow mainWindow(&context, &safetyKernel, &auditService, clinicalDataRepository, &simulationDevice);
    mainWindow.show();

    return QApplication::exec();
}
