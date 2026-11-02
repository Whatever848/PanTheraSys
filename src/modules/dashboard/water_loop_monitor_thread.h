#pragma once

#include <functional>

#include <QMutex>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QWaitCondition>

namespace panthera::modules {

inline constexpr double kWaterLoopLowerLevelCentimeters = 13.0;
inline constexpr double kWaterLoopUpperLevelCentimeters = 15.0;
inline constexpr int kWaterLoopMonitorIntervalMs = 15000;

enum class WaterLoopPumpMode {
    Stopped,
    SupplyOnly,
    Both,
    ReturnOnly
};

[[nodiscard]] WaterLoopPumpMode waterLoopPumpModeForLevel(double centimeters);
[[nodiscard]] QString waterLoopPumpModeText(WaterLoopPumpMode mode);

// The worker owns only the periodic wait. The callback is queued to the
// receiver's thread so all COM3 access remains serialized with the existing
// Modbus clients.
class WaterLoopMonitorThread final : public QThread {
public:
    WaterLoopMonitorThread(
        QObject* dispatchTarget,
        std::function<void()> pollCallback,
        int intervalMs = kWaterLoopMonitorIntervalMs);
    ~WaterLoopMonitorThread() override;

    void startMonitoring();
    void stopMonitoring();
    [[nodiscard]] bool isMonitoring() const;

protected:
    void run() override;

private:
    QPointer<QObject> m_dispatchTarget;
    std::function<void()> m_pollCallback;
    int m_intervalMs {kWaterLoopMonitorIntervalMs};
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_stopRequested {true};
};

}  // namespace panthera::modules
