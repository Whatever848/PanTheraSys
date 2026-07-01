#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QResource>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
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
#include "modules/data/data_management_page.h"
#include "modules/dashboard/device_monitor_page.h"
#include "modules/planning/planning_page.h"
#include "modules/shared/usb_camera_frame_source.h"
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
    const QList<QDir> baseDirectories {
        QDir::current(),
        QDir(appDir)
    };
    const QStringList relativeCandidates {
        relativePath,
        QStringLiteral("../%1").arg(relativePath),
        QStringLiteral("../../%1").arg(relativePath),
        QStringLiteral("../../../%1").arg(relativePath),
        QStringLiteral("../../../../%1").arg(relativePath),
        QStringLiteral("../../../../../%1").arg(relativePath)
    };

    for (const QDir& baseDirectory : baseDirectories) {
        for (const QString& relativeCandidate : relativeCandidates) {
            const QString candidate = baseDirectory.absoluteFilePath(relativeCandidate);
            if (QFileInfo::exists(candidate)) {
                return QDir::cleanPath(candidate);
            }
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

    if (screens.size() >= 3 && assignments[0] < 0 && assignments[1] < 0 && assignments[2] < 0) {
        int leftScreenIndex = 0;
        for (int index = 1; index < screens.size(); ++index) {
            const QRect candidateGeometry = screens.at(index)->geometry();
            const QRect bestGeometry = screens.at(leftScreenIndex)->geometry();
            if (candidateGeometry.x() < bestGeometry.x()) {
                leftScreenIndex = index;
            }
        }

        assignments[0] = leftScreenIndex;

        int upperRightScreenIndex = -1;
        int lowerRightScreenIndex = -1;
        for (int index = 0; index < screens.size(); ++index) {
            if (index == leftScreenIndex) {
                continue;
            }

            if (upperRightScreenIndex < 0) {
                upperRightScreenIndex = index;
                lowerRightScreenIndex = index;
                continue;
            }

            const QRect candidateGeometry = screens.at(index)->geometry();
            const QRect upperGeometry = screens.at(upperRightScreenIndex)->geometry();
            const QRect lowerGeometry = screens.at(lowerRightScreenIndex)->geometry();
            if (candidateGeometry.y() < upperGeometry.y()) {
                upperRightScreenIndex = index;
            }
            if (candidateGeometry.y() > lowerGeometry.y()) {
                lowerRightScreenIndex = index;
            }
        }

        assignments[1] = lowerRightScreenIndex;
        assignments[2] = upperRightScreenIndex;
        return assignments;
    }

    for (int assignmentIndex = 0; assignmentIndex < assignments.size(); ++assignmentIndex) {
        if (assignments.at(assignmentIndex) >= 0) {
            continue;
        }

        for (int screenIndex = 0; screenIndex < screens.size(); ++screenIndex) {
            if (!isAssigned(screenIndex)) {
                assignments[assignmentIndex] = screenIndex;
                break;
            }
        }
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

        if (role == ClinicalDisplayRole::Dashboard) {
            m_dashboardMonitorButton = createHeaderNavButton(QStringLiteral("\u8bbe\u5907\u76d1\u63a7"), QStyle::SP_ComputerIcon, true);
            m_dashboardDataButton = createHeaderNavButton(QStringLiteral("\u6570\u636e\u7ba1\u7406"), QStyle::SP_DirIcon, true);
            configureDashboardDataMenu();
            headerLayout->addSpacing(18);
            headerLayout->addWidget(m_dashboardMonitorButton);
            headerLayout->addWidget(m_dashboardDataButton);
        } else {
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
        }

        headerLayout->addStretch();

        m_statusLabel = new QLabel();
        m_statusLabel->setObjectName(QStringLiteral("navStatusLabel"));
        headerLayout->addWidget(m_statusLabel);
        headerLayout->addSpacing(8);

        if (role == ClinicalDisplayRole::Planning) {
            m_planningHabitButton = createUtilityButton(QStyle::SP_FileDialogDetailedView, QStringLiteral("\u533b\u751f\u4e60\u60ef\u8bbe\u7f6e"));
            m_planningHabitButton->setText(QStringLiteral("\u2699"));
            m_planningHabitButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
            m_planningHabitButton->setPopupMode(QToolButton::InstantPopup);
            m_planningHabitMenu = new QMenu(m_planningHabitButton);
            m_planningHabitMenu->setObjectName(QStringLiteral("navDropMenu"));
            m_planningHabitMenu->setMinimumWidth(190);
            m_planningHabitActionGroup = new QActionGroup(m_planningHabitMenu);
            m_planningHabitActionGroup->setExclusive(true);
            m_collapseAllAction = m_planningHabitMenu->addAction(QStringLiteral("\u5168\u6298\u53e0"));
            m_collapseAllAction->setCheckable(true);
            m_planningHabitActionGroup->addAction(m_collapseAllAction);
            m_expandAllAction = m_planningHabitMenu->addAction(QStringLiteral("\u5168\u5c55\u5f00"));
            m_expandAllAction->setCheckable(true);
            m_planningHabitActionGroup->addAction(m_expandAllAction);
            m_saveHabitSeparator = m_planningHabitMenu->addSeparator();
            m_saveHabitAction = m_planningHabitMenu->addAction(QStringLiteral("\u4fdd\u5b58\u533b\u751f\u4e60\u60ef"));
            m_planningHabitButton->setMenu(m_planningHabitMenu);
            headerLayout->addWidget(m_planningHabitButton, 0, Qt::AlignVCenter);
        }

        m_exitButton = createUtilityButton(QStyle::SP_DialogCloseButton, QStringLiteral("\u9000\u51fa\u7cfb\u7edf"));
        headerLayout->addWidget(m_exitButton, 0, Qt::AlignVCenter);

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
        if (m_dashboardMonitorButton != nullptr) {
            connect(m_dashboardMonitorButton, &QToolButton::clicked, this, [this]() {
                showDashboardMonitor();
            });
        }
        if (m_dashboardDataButton != nullptr) {
            connect(m_dashboardDataButton, &QToolButton::clicked, this, [this]() {
                showDashboardData();
            });
        }
        if (m_expandAllAction != nullptr) {
            connect(m_expandAllAction, &QAction::triggered, this, [this]() {
                applyPlanningPersonalizationProfile(QStringLiteral("\u5168\u5c55\u5f00"));
            });
        }
        if (m_collapseAllAction != nullptr) {
            connect(m_collapseAllAction, &QAction::triggered, this, [this]() {
                applyPlanningPersonalizationProfile(QStringLiteral("\u5168\u6298\u53e0"));
            });
        }
        if (m_saveHabitAction != nullptr) {
            connect(m_saveHabitAction, &QAction::triggered, this, [this]() {
                savePlanningPersonalizationProfile();
            });
        }
        if (m_planningHabitMenu != nullptr) {
            connect(m_planningHabitMenu, &QMenu::aboutToShow, this, [this]() {
                refreshPlanningHabitMenu();
            });
        }
        connect(m_exitButton, &QToolButton::clicked, qApp, []() {
            qApp->quit();
        });
        updateStatusSummary();
    }

private:
    QToolButton* createHeaderNavButton(const QString& text, QStyle::StandardPixmap iconType, bool checkable)
    {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setIcon(style()->standardIcon(iconType));
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setProperty("navButton", true);
        return button;
    }

    QToolButton* createUtilityButton(QStyle::StandardPixmap iconType, const QString& tooltip)
    {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("navUtilityButton"));
        button->setIcon(style()->standardIcon(iconType));
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        return button;
    }

    void configureDashboardDataMenu()
    {
        if (m_dashboardDataButton == nullptr) {
            return;
        }

        m_dashboardDataButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_dashboardDataMenu = new QMenu(m_dashboardDataButton);
        m_dashboardDataMenu->setObjectName(QStringLiteral("navDropMenu"));
        m_dashboardDataMenu->setMinimumWidth(170);
        m_dashboardDataActionGroup = new QActionGroup(m_dashboardDataMenu);
        m_dashboardDataActionGroup->setExclusive(true);

        const auto addSectionAction = [this](const QString& text, panthera::modules::DataManagementPage::Section section) {
            QAction* action = m_dashboardDataMenu->addAction(text);
            action->setCheckable(true);
            action->setData(static_cast<int>(section));
            m_dashboardDataActionGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, section]() {
                showDashboardDataSection(section);
            });
            return action;
        };

        m_patientInfoAction = addSectionAction(QStringLiteral("\u60a3\u8005\u4fe1\u606f"), panthera::modules::DataManagementPage::Section::PatientInfo);
        m_imagingDataAction = addSectionAction(QStringLiteral("\u5f71\u50cf\u6570\u636e"), panthera::modules::DataManagementPage::Section::ImagingData);
        m_treatmentReportAction = addSectionAction(QStringLiteral("\u6cbb\u7597\u62a5\u544a"), panthera::modules::DataManagementPage::Section::TreatmentReport);
        m_treatmentDataAction = addSectionAction(QStringLiteral("\u6cbb\u7597\u6570\u636e"), panthera::modules::DataManagementPage::Section::TreatmentData);
        m_patientInfoAction->setChecked(true);
        m_dashboardDataButton->setMenu(m_dashboardDataMenu);
    }

    QWidget* createClinicalPage(
        ClinicalDisplayRole role,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        panthera::adapters::SimulationDeviceFacade* simulationDevice)
    {
        using panthera::modules::DeviceMonitorPage;
        using panthera::modules::DataManagementPage;
        using panthera::modules::PlanningPage;
        using panthera::modules::TreatmentPage;

        switch (role) {
        case ClinicalDisplayRole::Dashboard: {
            m_dashboardStack = new QStackedWidget();
            m_dashboardStack->addWidget(new DeviceMonitorPage(simulationDevice, m_safetyKernel));
            m_dataManagementPage = new DataManagementPage(m_context, auditService, clinicalDataRepository);
            m_dashboardStack->addWidget(m_dataManagementPage);
            showDashboardMonitor();
            return m_dashboardStack;
        }
        case ClinicalDisplayRole::Planning: {
            m_planningPage = new PlanningPage(m_context, m_safetyKernel, auditService, clinicalDataRepository, simulationDevice);
            connect(m_context, &panthera::core::ApplicationContext::treatmentLayerVisualizationRequested, m_planningPage, [this](const QString& planId, int layerIndex, bool treatmentActive) {
                if (treatmentActive) {
                    m_planningPage->showTreatmentComparisonLayer(planId, layerIndex, true);
                }
            });
            return m_planningPage;
        }
        case ClinicalDisplayRole::Treatment:
            return new TreatmentPage(m_context, m_safetyKernel, auditService, clinicalDataRepository, simulationDevice);
        }

        return new QWidget();
    }

    void showDashboardMonitor()
    {
        if (m_dashboardStack != nullptr) {
            m_dashboardStack->setCurrentIndex(0);
        }
        if (m_dashboardMonitorButton != nullptr) {
            m_dashboardMonitorButton->setChecked(true);
        }
        if (m_dashboardDataButton != nullptr) {
            m_dashboardDataButton->setChecked(false);
        }
    }

    void showDashboardData()
    {
        showDashboardDataSection(panthera::modules::DataManagementPage::Section::PatientInfo);
    }

    void showDashboardDataSection(panthera::modules::DataManagementPage::Section section)
    {
        if (m_dashboardStack != nullptr) {
            m_dashboardStack->setCurrentIndex(1);
        }
        if (m_dataManagementPage != nullptr) {
            m_dataManagementPage->showSection(section);
        }
        if (m_dashboardDataActionGroup != nullptr) {
            for (QAction* action : m_dashboardDataActionGroup->actions()) {
                action->setChecked(action->data().toInt() == static_cast<int>(section));
            }
        }
        if (m_dashboardMonitorButton != nullptr) {
            m_dashboardMonitorButton->setChecked(false);
        }
        if (m_dashboardDataButton != nullptr) {
            m_dashboardDataButton->setChecked(true);
        }
    }

    void applyPlanningPersonalizationProfile(const QString& profileName)
    {
        if (m_planningPage == nullptr) {
            return;
        }

        m_planningPage->applyPersonalizationProfile(profileName);
        refreshPlanningHabitMenu();
    }

    void clearSavedPlanningHabitActions()
    {
        for (QAction* action : m_savedHabitActions) {
            if (action == nullptr) {
                continue;
            }
            if (m_planningHabitActionGroup != nullptr) {
                m_planningHabitActionGroup->removeAction(action);
            }
            if (m_planningHabitMenu != nullptr) {
                m_planningHabitMenu->removeAction(action);
            }
            delete action;
        }
        m_savedHabitActions.clear();
    }

    void rebuildSavedPlanningHabitActions(const QString& activeProfileName)
    {
        clearSavedPlanningHabitActions();
        if (m_planningPage == nullptr || m_planningHabitMenu == nullptr || m_saveHabitSeparator == nullptr) {
            return;
        }

        const QStringList profileNames = m_planningPage->personalizationProfileNames();
        for (const QString& profileName : profileNames) {
            auto* action = new QWidgetAction(m_planningHabitMenu);
            auto* row = new QWidget(m_planningHabitMenu);
            row->setMinimumWidth(184);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(6, 2, 6, 2);
            rowLayout->setSpacing(4);

            auto* profileButton = new QToolButton(row);
            profileButton->setText(profileName);
            profileButton->setToolTip(profileName);
            profileButton->setAutoRaise(true);
            profileButton->setCheckable(true);
            profileButton->setChecked(profileName == activeProfileName);
            profileButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
            profileButton->setStyleSheet(QStringLiteral(
                "QToolButton { border: none; background: transparent; color: #e9f6ff; padding: 6px 8px; text-align: left; }"
                "QToolButton:hover { background: rgba(69, 137, 203, 0.18); }"
                "QToolButton:checked { color: #24d7ff; font-weight: 600; }"));
            rowLayout->addWidget(profileButton, 1);

            auto* deleteButton = new QToolButton(row);
            deleteButton->setText(QStringLiteral("×"));
            deleteButton->setToolTip(QStringLiteral("删除该医生习惯"));
            deleteButton->setAutoRaise(true);
            deleteButton->setFixedWidth(24);
            deleteButton->setStyleSheet(QStringLiteral(
                "QToolButton { border: none; background: transparent; color: rgba(215, 230, 242, 110); padding: 0; font-size: 16px; }"
                "QToolButton:hover { color: #ff7a7a; background: rgba(255, 96, 96, 0.12); }"));
            rowLayout->addWidget(deleteButton, 0);

            action->setDefaultWidget(row);
            action->setData(profileName);
            connect(profileButton, &QToolButton::clicked, this, [this, profileName]() {
                if (m_planningHabitMenu != nullptr) {
                    m_planningHabitMenu->hide();
                }
                QTimer::singleShot(0, this, [this, profileName]() {
                    applyPlanningPersonalizationProfile(profileName);
                });
            });
            connect(deleteButton, &QToolButton::clicked, this, [this, profileName]() {
                if (m_planningHabitMenu != nullptr) {
                    m_planningHabitMenu->hide();
                }
                QTimer::singleShot(0, this, [this, profileName]() {
                    deletePlanningPersonalizationProfile(profileName);
                });
            });
            m_planningHabitMenu->insertAction(m_saveHabitSeparator, action);
            m_savedHabitActions.push_back(action);
        }
    }

    void refreshPlanningHabitMenu()
    {
        if (m_planningPage == nullptr) {
            return;
        }

        const QString activeProfileName = m_planningPage->activePersonalizationProfileName();
        rebuildSavedPlanningHabitActions(activeProfileName);
        if (m_planningHabitActionGroup != nullptr) {
            m_planningHabitActionGroup->setExclusive(false);
        }
        if (m_expandAllAction != nullptr) {
            m_expandAllAction->setChecked(activeProfileName == QStringLiteral("\u5168\u5c55\u5f00"));
        }
        if (m_collapseAllAction != nullptr) {
            m_collapseAllAction->setChecked(activeProfileName == QStringLiteral("\u5168\u6298\u53e0"));
        }
        if (m_planningHabitActionGroup != nullptr) {
            m_planningHabitActionGroup->setExclusive(true);
        }
    }

    void savePlanningPersonalizationProfile()
    {
        if (m_planningPage == nullptr) {
            return;
        }

        const QString profileName = QInputDialog::getText(
            this,
            QStringLiteral("\u4fdd\u5b58\u533b\u751f\u4e60\u60ef"),
            QStringLiteral("\u8bf7\u8f93\u5165\u4e60\u60ef\u540d\u79f0"));
        if (profileName.trimmed().isEmpty()) {
            return;
        }

        if (m_planningPage->saveCurrentPersonalizationProfile(profileName)) {
            refreshPlanningHabitMenu();
        }
    }

    void deletePlanningPersonalizationProfile(const QString& profileName)
    {
        if (m_planningPage == nullptr) {
            return;
        }

        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            QStringLiteral("删除医生习惯"),
            QStringLiteral("确定删除医生习惯“%1”吗？").arg(profileName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            return;
        }

        if (!m_planningPage->deletePersonalizationProfile(profileName)) {
            QMessageBox::warning(
                this,
                QStringLiteral("删除医生习惯"),
                QStringLiteral("未能删除医生习惯“%1”。").arg(profileName));
            return;
        }
        refreshPlanningHabitMenu();
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
    QStackedWidget* m_dashboardStack {nullptr};
    panthera::modules::DataManagementPage* m_dataManagementPage {nullptr};
    panthera::modules::PlanningPage* m_planningPage {nullptr};
    QToolButton* m_dashboardMonitorButton {nullptr};
    QToolButton* m_dashboardDataButton {nullptr};
    QMenu* m_dashboardDataMenu {nullptr};
    QActionGroup* m_dashboardDataActionGroup {nullptr};
    QAction* m_patientInfoAction {nullptr};
    QAction* m_imagingDataAction {nullptr};
    QAction* m_treatmentReportAction {nullptr};
    QAction* m_treatmentDataAction {nullptr};
    QToolButton* m_planningHabitButton {nullptr};
    QMenu* m_planningHabitMenu {nullptr};
    QActionGroup* m_planningHabitActionGroup {nullptr};
    QAction* m_expandAllAction {nullptr};
    QAction* m_collapseAllAction {nullptr};
    QAction* m_saveHabitSeparator {nullptr};
    QAction* m_saveHabitAction {nullptr};
    QList<QAction*> m_savedHabitActions;
    QToolButton* m_exitButton {nullptr};
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
        window->show();
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

    panthera::modules::UsbCameraFrameSource treatmentCameraFrameSource(&application);
    QElapsedTimer treatmentCameraStartTimer;
    QElapsedTimer latestTreatmentCameraFrameTimer;
    bool hasTreatmentCameraFrame = false;
    QObject::connect(
        &treatmentCameraFrameSource,
        &panthera::modules::UsbCameraFrameSource::frameAvailable,
        &context,
        [&context, &treatmentCameraFrameSource, &latestTreatmentCameraFrameTimer, &hasTreatmentCameraFrame](const QImage& image) {
            hasTreatmentCameraFrame = true;
            latestTreatmentCameraFrameTimer.restart();
            context.updateTreatmentCameraFrame(image, treatmentCameraFrameSource.activeCameraDescription());
        });
    QObject::connect(
        &treatmentCameraFrameSource,
        &panthera::modules::UsbCameraFrameSource::statusChanged,
        &auditService,
        [&auditService](const QString& status) {
            auditService.appendEntry(QStringLiteral("system"), QStringLiteral("camera"), status);
        });
    QObject::connect(
        &treatmentCameraFrameSource,
        &panthera::modules::UsbCameraFrameSource::errorOccurred,
        &auditService,
        [&auditService](const QString& error) {
            auditService.appendEntry(QStringLiteral("system"), QStringLiteral("camera"), error);
        });
    const auto startTreatmentCamera = [&treatmentCameraFrameSource, &treatmentCameraStartTimer, &hasTreatmentCameraFrame]() {
        hasTreatmentCameraFrame = false;
        treatmentCameraStartTimer.restart();
        treatmentCameraFrameSource.start(QStringLiteral("USB3 PLUS Video"));
    };
    QTimer treatmentCameraWatchdog(&application);
    treatmentCameraWatchdog.setInterval(5000);
    QObject::connect(&treatmentCameraWatchdog, &QTimer::timeout, &application, [&]() {
        const bool activeWithoutFirstFrame =
            treatmentCameraFrameSource.isActive()
            && !hasTreatmentCameraFrame
            && treatmentCameraStartTimer.isValid()
            && treatmentCameraStartTimer.elapsed() > 8000;
        const bool activeWithStaleFrame =
            treatmentCameraFrameSource.isActive()
            && hasTreatmentCameraFrame
            && latestTreatmentCameraFrameTimer.isValid()
            && latestTreatmentCameraFrameTimer.elapsed() > 8000;
        if (!treatmentCameraFrameSource.isActive() || activeWithoutFirstFrame || activeWithStaleFrame) {
            startTreatmentCamera();
        }
    });
    treatmentCameraWatchdog.start();
    QTimer::singleShot(0, &application, startTreatmentCamera);

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
