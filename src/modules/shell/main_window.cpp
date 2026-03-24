#include "modules/shell/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
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
    , m_safetyKernel(safetyKernel)
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

    auto* titleLabel = new QLabel(QStringLiteral("乳腺超声消融医疗系统 V1.0"));
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
    m_treatmentButton = createNavButton(QStringLiteral("治疗"), QStyle::SP_MediaPlay);
    m_dataButton = createNavButton(QStringLiteral("数据管理"), QStyle::SP_DirIcon);
    m_dataButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_dataButton->setProperty("navMenuButton", true);

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

    rootLayout->addWidget(navBar);

    m_stack = new QStackedWidget();
    m_stack->addWidget(new DeviceMonitorPage(simulationDevice, safetyKernel));
    m_stack->addWidget(new PlanningPage(context, safetyKernel, auditService, m_clinicalDataRepository));
    m_stack->addWidget(new TreatmentPage(context, safetyKernel, auditService, simulationDevice));
    m_dataManagementPage = new DataManagementPage(context, auditService, m_clinicalDataRepository);
    m_stack->addWidget(m_dataManagementPage);
    rootLayout->addWidget(m_stack, 1);

    setCentralWidget(centralWidget);
    resize(1480, 920);
    setWindowTitle(QStringLiteral("PanTheraSys Console"));

    connect(m_dashboardButton, &QToolButton::clicked, this, &MainWindow::showDashboard);
    connect(m_planningButton, &QToolButton::clicked, this, &MainWindow::showPlanning);
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
    setActivePage(1, m_planningButton);
}

void MainWindow::showTreatment()
{
    setActivePage(2, m_treatmentButton);
}

void MainWindow::showDataManagement()
{
    showDataManagementSection(DataManagementPage::Section::PatientInfo);
}

void MainWindow::showDataManagementSection(DataManagementPage::Section section)
{
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
