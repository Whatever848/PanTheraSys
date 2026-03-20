#pragma once

#include <QCheckBox>
#include <QHash>
#include <QLabel>
#include <QWidget>

#include "adapters/sim/simulation_device_facade.h"
#include "core/safety/safety_kernel.h"

namespace panthera::modules {

class DeviceMonitorPage final : public QWidget {
    Q_OBJECT

public:
    DeviceMonitorPage(panthera::adapters::SimulationDeviceFacade* simulationDevice, panthera::core::SafetyKernel* safetyKernel, QWidget* parent = nullptr);

private slots:
    void updateSnapshot(const panthera::core::DeviceSnapshot& snapshot);
    void updateSafety(const panthera::core::SafetySnapshot& snapshot);
    void resetFaults();

private:
    QLabel* createValueLabel();
    QWidget* createMetricCard(const QString& title, const QVector<QPair<QString, QLabel*>>& metrics);
    void bindFaultToggle(QCheckBox* checkBox, panthera::core::InterlockReason reason);

    panthera::adapters::SimulationDeviceFacade* m_simulationDevice {nullptr};
    panthera::core::SafetyKernel* m_safetyKernel {nullptr};
    QHash<QString, QLabel*> m_valueLabels;
    QLabel* m_safetyStateLabel {nullptr};
    QLabel* m_interlockLabel {nullptr};
    QVector<QCheckBox*> m_faultToggles;
};

}  // panthera::modules 命名空间
