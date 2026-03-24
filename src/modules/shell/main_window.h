#pragma once

#include <QAction>
#include <QAbstractButton>
#include <QLabel>
#include <QMainWindow>
#include <QStackedWidget>
#include <QToolButton>

#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/repositories/clinical_data_repository.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "modules/data/data_management_page.h"

namespace panthera::modules {

class DeviceMonitorPage;
class PlanningPage;
class TreatmentPage;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(
        panthera::core::ApplicationContext* context,
        panthera::core::SafetyKernel* safetyKernel,
        panthera::core::AuditService* auditService,
        panthera::core::IClinicalDataRepository* clinicalDataRepository,
        panthera::adapters::SimulationDeviceFacade* simulationDevice,
        QWidget* parent = nullptr);

private slots:
    void showDashboard();
    void showPlanning();
    void showTreatment();
    void showDataManagement();
    void updateStatusBarSummary();

private:
    void showDataManagementSection(DataManagementPage::Section section);
    void setActivePage(int index, QAbstractButton* activeButton);

    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    panthera::core::IClinicalDataRepository* m_clinicalDataRepository {nullptr};
    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};

    QStackedWidget* m_stack {nullptr};
    QToolButton* m_dashboardButton {nullptr};
    QToolButton* m_planningButton {nullptr};
    QToolButton* m_treatmentButton {nullptr};
    QToolButton* m_dataButton {nullptr};
    QLabel* m_statusLabel {nullptr};
    DataManagementPage* m_dataManagementPage {nullptr};
    QAction* m_dataPatientInfoAction {nullptr};
    QAction* m_dataImagingAction {nullptr};
    QAction* m_dataReportAction {nullptr};
    QAction* m_dataTreatmentDataAction {nullptr};
};

}  // namespace panthera::modules
