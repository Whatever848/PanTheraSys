#include <QApplication>
#include <QFile>

#include "adapters/config/local_settings_store.h"
#include "adapters/sim/simulation_device_facade.h"
#include "core/application/application_context.h"
#include "core/application/event_bus.h"
#include "core/domain/system_types.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "modules/shell/main_window.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("PanTheraSys"));
    application.setApplicationName(QStringLiteral("PanTheraConsole"));

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
    panthera::adapters::SimulationDeviceFacade simulationDevice;

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

    simulationDevice.start();

    panthera::modules::MainWindow mainWindow(&context, &safetyKernel, &auditService, &simulationDevice);
    mainWindow.show();

    return QApplication::exec();
}
