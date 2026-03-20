#pragma once

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>

#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"

namespace panthera::modules {

class DataManagementPage;
class DeviceMonitorPage;
class PlanningPage;
class TreatmentPage;

// MainWindow 是原型工作站的主导航壳层。
// 它负责把共享服务注入到各个主工作区，并执行基础的角色可见性控制。
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        QWidget* parent = nullptr);

private slots:
    void showDashboard();
    void showPlanning();
    void showTreatment();
    void showDataManagement();
    void updateStatusBarSummary();
    void applyRoleVisibility(panthera::core::RoleType role);

private:
    void setActivePage(int index, QPushButton* activeButton);

    panthera::core::ApplicationContext* m_context {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    QStackedWidget* m_stack {nullptr};
    QPushButton* m_dashboardButton {nullptr};
    QPushButton* m_planningButton {nullptr};
    QPushButton* m_treatmentButton {nullptr};
    QPushButton* m_dataButton {nullptr};
    QLabel* m_statusLabel {nullptr};
    QComboBox* m_roleCombo {nullptr};
};

}  // panthera::modules 命名空间
