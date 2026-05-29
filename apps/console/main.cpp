#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QResource>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

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
#include "modules/dashboard/device_monitor_page.h"
#include "modules/planning/planning_page.h"
#include "modules/shell/main_window.h"
#include "modules/treatment/treatment_page.h"

namespace {

enum class ClinicalDisplayRole {
    Dashboard,
    Planning,
    Treatment
};

struct DisplaySettings {
    bool threeScreenMode {false};
    bool fullScreen {true};
    bool fallbackSingleWindow {true};
    int dashboardScreen {-1};
    int planningScreen {-1};
    int treatmentScreen {-1};
};

QString clinicalDisplayTitle(ClinicalDisplayRole role)
{
    switch (role) {
    case ClinicalDisplayRole::Dashboard:
        return QStringLiteral("设备监控屏");
    case ClinicalDisplayRole::Planning:
        return QStringLiteral("治疗方案屏");
    case ClinicalDisplayRole::Treatment:
        return QStringLiteral("治疗执行屏");
    }

    return QStringLiteral("临床显示屏");
}

QString clinicalDisplaySubtitle(ClinicalDisplayRole role)
{
    switch (role) {
    case ClinicalDisplayRole::Dashboard:
        return QStringLiteral("实时设备状态 / 安全联锁");
    case ClinicalDisplayRole::Planning:
        return QStringLiteral("术前影像 / 治疗方案设计");
    case ClinicalDisplayRole::Treatment:
        return QStringLiteral("治疗执行 / 焦点覆盖监视");
    }

    return QString();
}

QString displayModeText(const QString& rawMode)
{
    return rawMode.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
}

QString executableDirectoryPath(int argc, char* argv[])
{
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return QDir::currentPath();
    }

    const QString executablePath = QString::fromLocal8Bit(argv[0]);
    QFileInfo executableInfo(executablePath);
    if (executableInfo.isRelative()) {
        executableInfo = QFileInfo(QDir::current().absoluteFilePath(executablePath));
    }
    return executableInfo.absoluteDir().absolutePath();
}

void prependQtPluginRoot(const QString& pluginRoot)
{
    const QByteArray nativePluginRoot = QDir::toNativeSeparators(pluginRoot).toLocal8Bit();
    if (nativePluginRoot.isEmpty()) {
        return;
    }

    const QByteArray existingPluginPath = qgetenv("QT_PLUGIN_PATH");
    if (existingPluginPath.isEmpty()) {
        qputenv("QT_PLUGIN_PATH", nativePluginRoot);
        return;
    }

    if (!existingPluginPath.split(';').contains(nativePluginRoot)) {
        qputenv("QT_PLUGIN_PATH", nativePluginRoot + QByteArray(";") + existingPluginPath);
    }
}

void configureQtPlatformPluginPath(int argc, char* argv[])
{
    const QDir executableDir(executableDirectoryPath(argc, argv));
    const QString platformsPath = executableDir.absoluteFilePath(QStringLiteral("platforms"));
    const QDir platformsDir(platformsPath);
    const bool hasWindowsPlatformPlugin =
        QFileInfo::exists(platformsDir.absoluteFilePath(QStringLiteral("qwindowsd.dll")))
        || QFileInfo::exists(platformsDir.absoluteFilePath(QStringLiteral("qwindows.dll")));

    if (!hasWindowsPlatformPlugin) {
        return;
    }

    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QDir::toNativeSeparators(platformsPath).toLocal8Bit());
    prependQtPluginRoot(executableDir.absolutePath());
}

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
    const int connectTimeoutSeconds = std::max(1, settings.value(QStringLiteral("database/connect_timeout_seconds"), 2).toInt());
    connectionSettings.connectOptions = QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%1;MYSQL_OPT_WRITE_TIMEOUT=%1")
        .arg(connectTimeoutSeconds);
    return connectionSettings;
}

bool isDatabaseEnabled(const QString& defaultsIniPath)
{
    if (!QFileInfo::exists(defaultsIniPath)) {
        return false;
    }

    QSettings settings(defaultsIniPath, QSettings::IniFormat);
    return settings.value(QStringLiteral("database/enabled"), false).toBool();
}

int parseScreenIndex(const QSettings& settings, const QString& key)
{
    const QString rawValue = settings.value(key, QStringLiteral("auto")).toString().trimmed().toLower();
    if (rawValue.isEmpty() || rawValue == QStringLiteral("auto")) {
        return -1;
    }

    bool ok = false;
    const int screenIndex = rawValue.toInt(&ok);
    return ok ? screenIndex : -1;
}

DisplaySettings loadDisplaySettings(const QString& defaultsIniPath)
{
    DisplaySettings displaySettings;
    if (!QFileInfo::exists(defaultsIniPath)) {
        return displaySettings;
    }

    const QSettings settings(defaultsIniPath, QSettings::IniFormat);
    const QString mode = displayModeText(settings.value(QStringLiteral("display/mode"), QStringLiteral("single")).toString());
    displaySettings.threeScreenMode =
        mode == QStringLiteral("three_screen")
        || mode == QStringLiteral("triple_screen")
        || mode == QStringLiteral("clinical_three_screen")
        || settings.value(QStringLiteral("display/enabled"), false).toBool();
    displaySettings.fullScreen = settings.value(QStringLiteral("display/fullscreen"), true).toBool();
    displaySettings.fallbackSingleWindow = settings.value(QStringLiteral("display/fallback_single_window"), true).toBool();
    displaySettings.dashboardScreen = parseScreenIndex(settings, QStringLiteral("display/dashboard_screen"));
    displaySettings.planningScreen = parseScreenIndex(settings, QStringLiteral("display/planning_screen"));
    displaySettings.treatmentScreen = parseScreenIndex(settings, QStringLiteral("display/treatment_screen"));
    return displaySettings;
}

bool hasCommandLineFlag(int argc, char* argv[], const QString& flag)
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == flag) {
            return true;
        }
    }
    return false;
}

void printScreenInventory()
{
    QTextStream out(stdout);
    const QList<QScreen*> screens = QApplication::screens();
    out << "PanTheraSys detected screens: " << screens.size() << Qt::endl;
    for (int index = 0; index < screens.size(); ++index) {
        const QScreen* screen = screens.at(index);
        const QRect geometry = screen->geometry();
        const QRect availableGeometry = screen->availableGeometry();
        out << index
            << " | name=" << screen->name()
            << " | geometry=" << geometry.x() << "," << geometry.y() << " "
            << geometry.width() << "x" << geometry.height()
            << " | available=" << availableGeometry.x() << "," << availableGeometry.y() << " "
            << availableGeometry.width() << "x" << availableGeometry.height();
        if (screen == QApplication::primaryScreen()) {
            out << " | primary";
        }
        out << Qt::endl;
    }
}

QVector<int> resolveClinicalScreenIndexes(const DisplaySettings& settings)
{
    const QList<QScreen*> screens = QApplication::screens();
    QVector<int> assignments(3, -1);

    const auto isValidScreenIndex = [&screens](int screenIndex) {
        return screenIndex >= 0 && screenIndex < screens.size();
    };
    const auto isAssigned = [&assignments](int screenIndex) {
        return assignments.contains(screenIndex);
    };
    const auto assignRequestedScreen = [&](int assignmentIndex, int requestedScreen) {
        if (isValidScreenIndex(requestedScreen) && !isAssigned(requestedScreen)) {
            assignments[assignmentIndex] = requestedScreen;
        }
    };

    assignRequestedScreen(0, settings.dashboardScreen);
    assignRequestedScreen(1, settings.planningScreen);
    assignRequestedScreen(2, settings.treatmentScreen);

    const auto pickUnusedScreen = [&](const auto& isBetter) {
        int bestIndex = -1;
        for (int index = 0; index < screens.size(); ++index) {
            if (isAssigned(index)) {
                continue;
            }
            if (bestIndex < 0 || isBetter(index, bestIndex)) {
                bestIndex = index;
            }
        }
        return bestIndex;
    };

    if (assignments[2] < 0) {
        assignments[2] = pickUnusedScreen([&screens](int candidate, int best) {
            const QRect candidateGeometry = screens.at(candidate)->geometry();
            const QRect bestGeometry = screens.at(best)->geometry();
            if (candidateGeometry.width() != bestGeometry.width()) {
                return candidateGeometry.width() > bestGeometry.width();
            }
            return candidateGeometry.width() * candidateGeometry.height() > bestGeometry.width() * bestGeometry.height();
        });
    }

    if (assignments[0] < 0) {
        assignments[0] = pickUnusedScreen([&screens](int candidate, int best) {
            const QRect candidateGeometry = screens.at(candidate)->geometry();
            const QRect bestGeometry = screens.at(best)->geometry();
            if (candidateGeometry.x() != bestGeometry.x()) {
                return candidateGeometry.x() < bestGeometry.x();
            }
            return candidateGeometry.height() > bestGeometry.height();
        });
    }

    if (assignments[1] < 0) {
        assignments[1] = pickUnusedScreen([&screens](int candidate, int best) {
            const QRect candidateGeometry = screens.at(candidate)->geometry();
            const QRect bestGeometry = screens.at(best)->geometry();
            if (candidateGeometry.y() != bestGeometry.y()) {
                return candidateGeometry.y() < bestGeometry.y();
            }
            return candidateGeometry.x() < bestGeometry.x();
        });
    }

    return assignments;
}

class ClinicalDisplayWindow final : public QMainWindow {
public:
    ClinicalDisplayWindow(
        ClinicalDisplayRole role,
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        QWidget* parent = nullptr)
        : QMainWindow(parent)
        , m_context(context)
        , m_safetyKernel(safetyKernel)
    {
        auto* centralWidget = new QWidget(this);
        auto* rootLayout = new QVBoxLayout(centralWidget);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        auto* header = new QFrame();
        header->setObjectName(QStringLiteral("navBar"));
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 18, 0);
        headerLayout->setSpacing(0);

        auto* titleBlock = new QFrame();
        titleBlock->setObjectName(QStringLiteral("navTitleBlock"));
        auto* titleLayout = new QHBoxLayout(titleBlock);
        titleLayout->setContentsMargins(24, 0, 26, 0);
        titleLayout->setSpacing(12);

        auto* titleIcon = new QLabel();
        titleIcon->setObjectName(QStringLiteral("navTitleIcon"));
        titleIcon->setPixmap(style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(24, 24));

        auto* titleLabel = new QLabel(QStringLiteral("低强度超声系统 V1.0"));
        titleLabel->setObjectName(QStringLiteral("navTitleLabel"));

        titleLayout->addWidget(titleIcon);
        titleLayout->addWidget(titleLabel);
        headerLayout->addWidget(titleBlock);

        auto* roleBlock = new QFrame();
        roleBlock->setProperty("navButton", true);
        auto* roleLayout = new QVBoxLayout(roleBlock);
        roleLayout->setContentsMargins(30, 8, 30, 8);
        roleLayout->setSpacing(2);

        auto* roleTitle = new QLabel(clinicalDisplayTitle(role));
        roleTitle->setObjectName(QStringLiteral("navTitleLabel"));
        auto* roleSubtitle = new QLabel(clinicalDisplaySubtitle(role));
        roleSubtitle->setObjectName(QStringLiteral("navStatusLabel"));
        roleLayout->addWidget(roleTitle);
        roleLayout->addWidget(roleSubtitle);
        headerLayout->addWidget(roleBlock);

        headerLayout->addStretch();

        m_statusLabel = new QLabel();
        m_statusLabel->setObjectName(QStringLiteral("navStatusLabel"));
        headerLayout->addWidget(m_statusLabel);

        rootLayout->addWidget(header);
        rootLayout->addWidget(createClinicalPage(role, auditService, clinicalDataRepository, simulationDevice), 1);

        setCentralWidget(centralWidget);
        setWindowTitle(QStringLiteral("PanTheraSys Console - %1").arg(clinicalDisplayTitle(role)));
        resize(1600, 920);

        connect(m_safetyKernel, &panthera::core::SafetyKernel::safetySnapshotChanged, this, [this]() {
            updateStatusSummary();
        });
        connect(m_safetyKernel, &panthera::core::SafetyKernel::systemModeChanged, this, [this]() {
            updateStatusSummary();
        });
        updateStatusSummary();
    }

private:
    QWidget* createClinicalPage(
        ClinicalDisplayRole role,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        panthera::adapters::SimulationDeviceFacade* simulationDevice)
    {
        using panthera::modules::DeviceMonitorPage;
        using panthera::modules::PlanningPage;
        using panthera::modules::TreatmentPage;

        switch (role) {
        case ClinicalDisplayRole::Dashboard:
            return new DeviceMonitorPage(simulationDevice, m_safetyKernel);
        case ClinicalDisplayRole::Planning: {
            auto* planningPage = new PlanningPage(m_context, m_safetyKernel, auditService, clinicalDataRepository, simulationDevice);
            connect(m_context, &panthera::core::ApplicationContext::treatmentLayerVisualizationRequested, planningPage, [planningPage](const QString& planId, int layerIndex, bool treatmentActive) {
                if (treatmentActive) {
                    planningPage->showTreatmentComparisonLayer(planId, layerIndex, true);
                }
            });
            return planningPage;
        }
        case ClinicalDisplayRole::Treatment:
            return new TreatmentPage(m_context, m_safetyKernel, auditService, clinicalDataRepository, simulationDevice);
        }

        return new QWidget();
    }

    void updateStatusSummary()
    {
        const panthera::core::SafetySnapshot snapshot = m_safetyKernel->snapshot();
        m_statusLabel->setText(
            QStringLiteral("系统%1 | 模式：%2 | 联锁：%3")
                .arg(panthera::core::toDisplayString(snapshot.state), panthera::core::toDisplayString(m_safetyKernel->mode()), snapshot.message));
    }

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    QLabel* m_statusLabel {nullptr};
};

void showWindowOnScreen(QMainWindow* window, QScreen* screen, bool fullScreen)
{
    if (window == nullptr || screen == nullptr) {
        return;
    }

    const QRect targetGeometry = fullScreen ? screen->geometry() : screen->availableGeometry();
    window->setGeometry(targetGeometry);
    window->show();
    if (QWindow* handle = window->windowHandle()) {
        handle->setScreen(screen);
    }
    window->setGeometry(targetGeometry);
    if (fullScreen) {
        window->showFullScreen();
    } else {
        window->showMaximized();
    }
    window->raise();
    window->activateWindow();
}

}  // namespace

int main(int argc, char* argv[])
{
    configureQtPlatformPluginPath(argc, argv);

    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    application.setOrganizationName(QStringLiteral("PanTheraSys"));
    application.setApplicationName(QStringLiteral("PanTheraConsole"));
    Q_INIT_RESOURCE(resources);

    if (hasCommandLineFlag(argc, argv, QStringLiteral("--list-screens"))) {
        printScreenInventory();
        return 0;
    }

    qRegisterMetaType<panthera::core::PatientRecord>();
    qRegisterMetaType<panthera::core::TherapyPlan>();
    qRegisterMetaType<panthera::core::TemperatureInputType>();
    qRegisterMetaType<panthera::core::TemperatureChannelTelemetry>();
    qRegisterMetaType<panthera::core::TemperatureModuleTelemetry>();
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
        [&safetyKernel](bool waterHealthy, bool powerReady, bool motionReady, bool temperatureHealthy, bool emergencyStopReleased, bool ultrasoundAvailable) {
            safetyKernel.setWaterLoopHealthy(waterHealthy);
            safetyKernel.setPowerReady(powerReady);
            safetyKernel.setMotionReady(motionReady);
            safetyKernel.setTemperatureHealthy(temperatureHealthy);
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

    if (isDatabaseEnabled(defaultsIniPath) && mysqlClinicalDataRepository.open(loadDatabaseSettings(defaultsIniPath))) {
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
        const QString reason = isDatabaseEnabled(defaultsIniPath)
            ? mysqlClinicalDataRepository.lastError()
            : QStringLiteral("MySQL disabled; using seed data for startup");
        auditService.appendEntry(QStringLiteral("system"), QStringLiteral("database"), QStringLiteral("Fallback to seed clinical data repository: %1").arg(reason));
    }

    simulationDevice.start();

    QVector<QMainWindow*> topLevelWindows;
    const DisplaySettings displaySettings = loadDisplaySettings(defaultsIniPath);
    const QVector<int> clinicalScreenIndexes = resolveClinicalScreenIndexes(displaySettings);
    const QList<QScreen*> screens = QApplication::screens();
    const bool canOpenThreeScreenMode =
        displaySettings.threeScreenMode
        && screens.size() >= 3
        && clinicalScreenIndexes.size() == 3
        && clinicalScreenIndexes[0] >= 0
        && clinicalScreenIndexes[1] >= 0
        && clinicalScreenIndexes[2] >= 0;

    if (canOpenThreeScreenMode) {
        safetyKernel.enterPlanningMode();
        topLevelWindows.push_back(new ClinicalDisplayWindow(
            ClinicalDisplayRole::Dashboard,
            &context,
            &safetyKernel,
            &auditService,
            clinicalDataRepository,
            &simulationDevice));
        topLevelWindows.push_back(new ClinicalDisplayWindow(
            ClinicalDisplayRole::Planning,
            &context,
            &safetyKernel,
            &auditService,
            clinicalDataRepository,
            &simulationDevice));
        topLevelWindows.push_back(new ClinicalDisplayWindow(
            ClinicalDisplayRole::Treatment,
            &context,
            &safetyKernel,
            &auditService,
            clinicalDataRepository,
            &simulationDevice));

        QTimer::singleShot(0, &application, [displaySettings, clinicalScreenIndexes, screens, &application, &topLevelWindows]() {
            for (int index = 0; index < topLevelWindows.size(); ++index) {
                showWindowOnScreen(topLevelWindows.at(index), screens.at(clinicalScreenIndexes.at(index)), displaySettings.fullScreen);
            }
            QTimer::singleShot(3000, &application, [&application]() {
                application.setQuitOnLastWindowClosed(true);
            });
        });
    } else {
        if (displaySettings.threeScreenMode && !displaySettings.fallbackSingleWindow) {
            printScreenInventory();
            return 1;
        }

        auto* mainWindow = new panthera::modules::MainWindow(&context, &safetyKernel, &auditService, clinicalDataRepository, &simulationDevice);
        topLevelWindows.push_back(mainWindow);
        QTimer::singleShot(0, &application, [&application, mainWindow]() {
            mainWindow->show();
            mainWindow->raise();
            mainWindow->activateWindow();
            QTimer::singleShot(3000, &application, [&application]() {
                application.setQuitOnLastWindowClosed(true);
            });
        });
        QTimer::singleShot(250, &application, [mainWindow]() {
            if (mainWindow->isVisible()) {
                mainWindow->raise();
                mainWindow->activateWindow();
            }
        });
    }

    const int exitCode = QApplication::exec();
    for (QMainWindow* window : topLevelWindows) {
        delete window;
    }
    return exitCode;
}
