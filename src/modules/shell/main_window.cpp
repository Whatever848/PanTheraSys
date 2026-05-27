#include "modules/shell/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QVBoxLayout>

#include "modules/dashboard/device_monitor_page.h"
#include "modules/data/data_management_page.h"
#include "modules/planning/planning_page.h"
#include "modules/treatment/treatment_page.h"

namespace panthera::modules {

using namespace panthera::core;

MainWindow::MainWindow(
    ApplicationContext* context,
    SafetyKernel* safetyKernel,
    AuditService* auditService,
    IClinicalDataRepository* clinicalDataRepository,
    adapters::SimulationDeviceFacade* simulationDevice,
    QWidget* parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_auditService(auditService)
    , m_clinicalDataRepository(clinicalDataRepository)
    , m_simulationDevice(simulationDevice)
{
    auto* centralWidget = new QWidget();
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* navBar = new QFrame();
    navBar->setObjectName(QStringLiteral("navBar"));
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(0, 0, 18, 0);
    navLayout->setSpacing(0);

    auto* titleBlock = new QFrame();
    titleBlock->setObjectName(QStringLiteral("navTitleBlock"));
    auto* titleLayout = new QHBoxLayout(titleBlock);
    titleLayout->setContentsMargins(24, 0, 26, 0);
    titleLayout->setSpacing(12);

    auto* titleIcon = new QLabel();
    titleIcon->setObjectName(QStringLiteral("navTitleIcon"));
    titleIcon->setPixmap(style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(24, 24));

    auto* titleLabel = new QLabel(QStringLiteral("\u4f4e\u5f3a\u5ea6\u8d85\u58f0\u7cfb\u7edf V1.0"));
    titleLabel->setObjectName(QStringLiteral("navTitleLabel"));

    titleLayout->addWidget(titleIcon);
    titleLayout->addWidget(titleLabel);
    navLayout->addWidget(titleBlock);

    auto createNavButton = [this](const QString& text, QStyle::StandardPixmap iconType) {
        auto* button = new QToolButton();
        button->setText(text);
        button->setIcon(style()->standardIcon(iconType));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setProperty("navButton", true);
        return button;
    };

    m_dashboardButton = createNavButton(QStringLiteral("设备监控"), QStyle::SP_ComputerIcon);
    m_planningButton = createNavButton(QStringLiteral("治疗方案"), QStyle::SP_FileDialogDetailedView);
    m_planningPersonalizationButton = new QToolButton();
    m_planningPersonalizationButton->setObjectName(QStringLiteral("navUtilityButton"));
    m_planningPersonalizationButton->setText(QStringLiteral("⚙"));
    m_planningPersonalizationButton->setToolTip(QStringLiteral("治疗方案个性化设置"));
    m_planningPersonalizationButton->setPopupMode(QToolButton::InstantPopup);
    m_planningPersonalizationButton->setAutoRaise(true);
    m_treatmentButton = createNavButton(QStringLiteral("治疗"), QStyle::SP_MediaPlay);
    m_dataButton = createNavButton(QStringLiteral("数据管理"), QStyle::SP_DirIcon);
    m_dataButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_dataButton->setProperty("navMenuButton", true);

    m_planningPersonalizationMenu = new QMenu(m_planningPersonalizationButton);
    m_planningPersonalizationMenu->setObjectName(QStringLiteral("navDropMenu"));
    m_planningPersonalizationButton->setMenu(m_planningPersonalizationMenu);

    auto* dataMenu = new QMenu(m_dataButton);
    dataMenu->setObjectName(QStringLiteral("navDropMenu"));
    auto* dataActionGroup = new QActionGroup(dataMenu);
    dataActionGroup->setExclusive(true);

    m_dataPatientInfoAction = dataMenu->addAction(QStringLiteral("患者信息"));
    m_dataPatientInfoAction->setCheckable(true);
    m_dataImagingAction = dataMenu->addAction(QStringLiteral("影像数据"));
    m_dataImagingAction->setCheckable(true);
    m_dataReportAction = dataMenu->addAction(QStringLiteral("治疗报告"));
    m_dataReportAction->setCheckable(true);
    m_dataTreatmentDataAction = dataMenu->addAction(QStringLiteral("治疗数据"));
    m_dataTreatmentDataAction->setCheckable(true);

    dataActionGroup->addAction(m_dataPatientInfoAction);
    dataActionGroup->addAction(m_dataImagingAction);
    dataActionGroup->addAction(m_dataReportAction);
    dataActionGroup->addAction(m_dataTreatmentDataAction);
    m_dataPatientInfoAction->setChecked(true);
    m_dataButton->setMenu(dataMenu);

    navLayout->addSpacing(18);
    navLayout->addWidget(m_dashboardButton);
    navLayout->addWidget(m_planningButton);
    navLayout->addWidget(m_treatmentButton);
    navLayout->addWidget(m_dataButton);
    navLayout->addStretch();

    m_statusLabel = new QLabel();
    m_statusLabel->setObjectName(QStringLiteral("navStatusLabel"));
    navLayout->addWidget(m_statusLabel);
    navLayout->addSpacing(8);
    navLayout->addWidget(m_planningPersonalizationButton, 0, Qt::AlignVCenter);

    rootLayout->addWidget(navBar);

    m_stack = new QStackedWidget();
    m_stack->addWidget(new DeviceMonitorPage(simulationDevice, safetyKernel));
    m_stack->addWidget(new QWidget());
    m_stack->addWidget(new QWidget());
    m_stack->addWidget(new QWidget());
    rootLayout->addWidget(m_stack, 1);

    setCentralWidget(centralWidget);
    resize(1600, 920);
    setMinimumSize(1540, 860);
    setWindowTitle(QStringLiteral("PanTheraSys Console"));

    connect(m_dashboardButton, &QToolButton::clicked, this, &MainWindow::showDashboard);
    connect(m_planningButton, &QToolButton::clicked, this, &MainWindow::showPlanning);
    connect(m_planningPersonalizationMenu, &QMenu::aboutToShow, this, &MainWindow::refreshPlanningPersonalizationMenu);
    connect(m_treatmentButton, &QToolButton::clicked, this, &MainWindow::showTreatment);
    connect(m_dataButton, &QToolButton::clicked, this, &MainWindow::showDataManagement);
    connect(m_dataPatientInfoAction, &QAction::triggered, this, [this]() {
        showDataManagementSection(DataManagementPage::Section::PatientInfo);
    });
    connect(m_dataImagingAction, &QAction::triggered, this, [this]() {
        showDataManagementSection(DataManagementPage::Section::ImagingData);
    });
    connect(m_dataReportAction, &QAction::triggered, this, [this]() {
        showDataManagementSection(DataManagementPage::Section::TreatmentReport);
    });
    connect(m_dataTreatmentDataAction, &QAction::triggered, this, [this]() {
        showDataManagementSection(DataManagementPage::Section::TreatmentData);
    });
    connect(m_context, &ApplicationContext::treatmentLayerVisualizationRequested, this, [this](const QString& planId, int layerIndex, bool treatmentActive) {
        if (!treatmentActive) {
            return;
        }

        const bool planningPageAlreadyCreated = m_planningPage != nullptr;
        ensurePlanningPage();
        if (!planningPageAlreadyCreated && m_planningPage != nullptr) {
            m_planningPage->showTreatmentComparisonLayer(planId, layerIndex, true);
        }
        setActivePage(1, m_planningButton);
    });
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &MainWindow::updateStatusBarSummary);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, &MainWindow::updateStatusBarSummary);

    updateStatusBarSummary();
    showDashboard();
}

void MainWindow::showDashboard()
{
    m_safetyKernel->resetToIdle();
    setActivePage(0, m_dashboardButton);
}

void MainWindow::showPlanning()
{
    m_safetyKernel->enterPlanningMode();
    ensurePlanningPage();
    setActivePage(1, m_planningButton);
}

void MainWindow::showTreatment()
{
    ensureTreatmentPage();
    setActivePage(2, m_treatmentButton);
}

void MainWindow::showDataManagement()
{
    showDataManagementSection(DataManagementPage::Section::PatientInfo);
}

void MainWindow::showDataManagementSection(DataManagementPage::Section section)
{
    ensureDataManagementPage();
    if (m_dataPatientInfoAction != nullptr) {
        m_dataPatientInfoAction->setChecked(section == DataManagementPage::Section::PatientInfo);
    }
    if (m_dataImagingAction != nullptr) {
        m_dataImagingAction->setChecked(section == DataManagementPage::Section::ImagingData);
    }
    if (m_dataReportAction != nullptr) {
        m_dataReportAction->setChecked(section == DataManagementPage::Section::TreatmentReport);
    }
    if (m_dataTreatmentDataAction != nullptr) {
        m_dataTreatmentDataAction->setChecked(section == DataManagementPage::Section::TreatmentData);
    }
    if (m_dataManagementPage != nullptr) {
        m_dataManagementPage->showSection(section);
    }
    setActivePage(3, m_dataButton);
}

void MainWindow::ensurePlanningPage()
{
    if (m_planningPage != nullptr) {
        return;
    }

    m_planningPage = new PlanningPage(m_context, m_safetyKernel, m_auditService, m_clinicalDataRepository, m_simulationDevice);
    replacePlaceholderPage(1, m_planningPage);
}

void MainWindow::refreshPlanningPersonalizationMenu()
{
    if (m_planningPersonalizationMenu == nullptr) {
        return;
    }

    ensurePlanningPage();
    m_planningPersonalizationMenu->clear();

    const QString activeProfileName = m_planningPage != nullptr ? m_planningPage->activePersonalizationProfileName() : QString();
    const QStringList builtInProfiles {
        QStringLiteral("全折叠"),
        QStringLiteral("全展开")
    };
    for (const QString& profileName : builtInProfiles) {
        QAction* action = m_planningPersonalizationMenu->addAction(profileName);
        action->setCheckable(true);
        action->setChecked(activeProfileName == profileName);
        connect(action, &QAction::triggered, this, [this, profileName]() {
            applyPlanningPersonalizationProfile(profileName);
        });
    }

    m_planningPersonalizationMenu->addSeparator();
    QAction* saveAction = m_planningPersonalizationMenu->addAction(QStringLiteral("保存当前方案"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::savePlanningPersonalizationProfile);

    if (m_planningPage == nullptr) {
        return;
    }

    const QStringList profileNames = m_planningPage->personalizationProfileNames();
    if (profileNames.isEmpty()) {
        return;
    }

    QMenu* deleteMenu = m_planningPersonalizationMenu->addMenu(QStringLiteral("\u5220\u9664\u5df2\u4fdd\u5b58\u65b9\u6848"));
    deleteMenu->setObjectName(QStringLiteral("navDropMenu"));
    for (const QString& profileName : profileNames) {
        QAction* deleteAction = deleteMenu->addAction(profileName);
        connect(deleteAction, &QAction::triggered, this, [this, profileName]() {
            deletePlanningPersonalizationProfile(profileName);
        });
    }

    m_planningPersonalizationMenu->addSeparator();
    for (const QString& profileName : profileNames) {
        QAction* action = m_planningPersonalizationMenu->addAction(profileName);
        action->setCheckable(true);
        action->setChecked(activeProfileName == profileName);
        connect(action, &QAction::triggered, this, [this, profileName]() {
            applyPlanningPersonalizationProfile(profileName);
        });
    }
}

void MainWindow::savePlanningPersonalizationProfile()
{
    showPlanning();
    if (m_planningPage == nullptr) {
        return;
    }

    const QString profileName = QInputDialog::getText(
        this,
        QStringLiteral("保存个性化方案"),
        QStringLiteral("请输入方案名称"));
    if (profileName.trimmed().isEmpty()) {
        return;
    }

    m_planningPage->saveCurrentPersonalizationProfile(profileName);
}

void MainWindow::ensureTreatmentPage()
{
    if (m_treatmentPage != nullptr) {
        return;
    }

    m_treatmentPage = new TreatmentPage(m_context, m_safetyKernel, m_auditService, m_clinicalDataRepository, m_simulationDevice);
    replacePlaceholderPage(2, m_treatmentPage);
}

void MainWindow::ensureDataManagementPage()
{
    if (m_dataManagementPage != nullptr) {
        return;
    }

    m_dataManagementPage = new DataManagementPage(m_context, m_auditService, m_clinicalDataRepository);
    replacePlaceholderPage(3, m_dataManagementPage);
}

void MainWindow::replacePlaceholderPage(int index, QWidget* page)
{
    if (m_stack == nullptr || page == nullptr) {
        return;
    }

    QWidget* placeholder = m_stack->widget(index);
    if (placeholder != nullptr && placeholder != page) {
        m_stack->removeWidget(placeholder);
        placeholder->setParent(nullptr);
        placeholder->deleteLater();
    }
    m_stack->insertWidget(index, page);
}

void MainWindow::applyPlanningPersonalizationProfile(const QString& profileName)
{
    showPlanning();
    if (m_planningPage == nullptr) {
        return;
    }

    m_planningPage->applyPersonalizationProfile(profileName);
}

void MainWindow::deletePlanningPersonalizationProfile(const QString& profileName)
{
    showPlanning();
    if (m_planningPage == nullptr) {
        return;
    }

    const QString normalizedProfileName = profileName.trimmed();
    if (normalizedProfileName.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("\u5220\u9664\u5df2\u4fdd\u5b58\u65b9\u6848"),
        QStringLiteral("\u786e\u5b9a\u5220\u9664\u201c%1\u201d\u5417\uff1f").arg(normalizedProfileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (m_planningPage->deletePersonalizationProfile(normalizedProfileName)) {
        refreshPlanningPersonalizationMenu();
    }
}

void MainWindow::updateStatusBarSummary()
{
    const SafetySnapshot snapshot = m_safetyKernel->snapshot();
    m_statusLabel->setText(
        QStringLiteral("系统%1 | 模式：%2 | 联锁：%3")
            .arg(toDisplayString(snapshot.state), toDisplayString(m_safetyKernel->mode()), snapshot.message));
}

void MainWindow::setActivePage(int index, QAbstractButton* activeButton)
{
    m_stack->setCurrentIndex(index);
    const QList<QAbstractButton*> buttons {m_dashboardButton, m_planningButton, m_treatmentButton, m_dataButton};
    for (QAbstractButton* button : buttons) {
        button->setChecked(button == activeButton);
    }
}

}  // namespace panthera::modules
