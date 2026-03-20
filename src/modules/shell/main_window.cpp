#include "modules/shell/main_window.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QStatusBar>
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
    adapters::SimulationDeviceFacade* simulationDevice,
    QWidget* parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_simulationDevice(simulationDevice)
{
    auto* centralWidget = new QWidget();
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* navBar = new QFrame();
    navBar->setObjectName(QStringLiteral("navBar"));
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(18, 12, 18, 12);
    navLayout->setSpacing(10);

    auto* titleLabel = new QLabel(QStringLiteral("乳腺超声消融医疗系统 V1.0"));
    titleLabel->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 700; color: #18d4ff;"));

    m_dashboardButton = new QPushButton(QStringLiteral("设备监控"));
    m_dashboardButton->setCheckable(true);
    m_planningButton = new QPushButton(QStringLiteral("治疗方案"));
    m_planningButton->setCheckable(true);
    m_treatmentButton = new QPushButton(QStringLiteral("治疗"));
    m_treatmentButton->setCheckable(true);
    m_dataButton = new QPushButton(QStringLiteral("数据管理"));
    m_dataButton->setCheckable(true);

    m_statusLabel = new QLabel();
    m_roleCombo = new QComboBox();
    m_roleCombo->addItem(toDisplayString(RoleType::Physician), static_cast<int>(RoleType::Physician));
    m_roleCombo->addItem(toDisplayString(RoleType::Engineer), static_cast<int>(RoleType::Engineer));
    m_roleCombo->addItem(toDisplayString(RoleType::Administrator), static_cast<int>(RoleType::Administrator));

    navLayout->addWidget(titleLabel);
    navLayout->addSpacing(24);
    navLayout->addWidget(m_dashboardButton);
    navLayout->addWidget(m_planningButton);
    navLayout->addWidget(m_treatmentButton);
    navLayout->addWidget(m_dataButton);
    navLayout->addStretch();
    navLayout->addWidget(new QLabel(QStringLiteral("当前角色")));
    navLayout->addWidget(m_roleCombo);
    navLayout->addSpacing(16);
    navLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(navBar);

    // 主窗口统一持有顶层页面实例，并在构造时把核心服务传入各页面。
    // 页面之间不直接调用，而是通过 ApplicationContext 和 SafetyKernel 协同。
    m_stack = new QStackedWidget();
    m_stack->addWidget(new DeviceMonitorPage(simulationDevice, safetyKernel));
    m_stack->addWidget(new PlanningPage(context, safetyKernel, auditService));
    m_stack->addWidget(new TreatmentPage(context, safetyKernel, auditService, simulationDevice));
    m_stack->addWidget(new DataManagementPage(context, auditService));
    rootLayout->addWidget(m_stack, 1);

    setCentralWidget(centralWidget);
    resize(1480, 920);
    setWindowTitle(QStringLiteral("PanTheraSys Console"));

    connect(m_dashboardButton, &QPushButton::clicked, this, &MainWindow::showDashboard);
    connect(m_planningButton, &QPushButton::clicked, this, &MainWindow::showPlanning);
    connect(m_treatmentButton, &QPushButton::clicked, this, &MainWindow::showTreatment);
    connect(m_dataButton, &QPushButton::clicked, this, &MainWindow::showDataManagement);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &MainWindow::updateStatusBarSummary);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, &MainWindow::updateStatusBarSummary);
    connect(m_roleCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto role = static_cast<RoleType>(m_roleCombo->itemData(index).toInt());
        m_context->setCurrentRole(role);
    });
    connect(m_context, &ApplicationContext::currentRoleChanged, this, &MainWindow::applyRoleVisibility);

    applyRoleVisibility(m_context->currentRole());
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
    if (!m_dataButton->isVisible()) {
        return;
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

void MainWindow::applyRoleVisibility(RoleType role)
{
    // 当前原型阶段按需求将“数据管理”默认隐藏，只对工程师和管理员角色开放。
    const bool canSeeDataManagement = role == RoleType::Engineer || role == RoleType::Administrator;
    m_dataButton->setVisible(canSeeDataManagement);
    if (!canSeeDataManagement && m_stack->currentIndex() == 3) {
        showDashboard();
    }
}

void MainWindow::setActivePage(int index, QPushButton* activeButton)
{
    // 导航按钮选中状态和页面切换统一在这里维护，保证重定向时界面表现一致。
    m_stack->setCurrentIndex(index);
    const QList<QPushButton*> buttons {m_dashboardButton, m_planningButton, m_treatmentButton, m_dataButton};
    for (QPushButton* button : buttons) {
        button->setChecked(button == activeButton);
    }
}

}  // panthera::modules 命名空间
