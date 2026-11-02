#include "modules/dashboard/water_loop_monitor_thread.h"

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QMutexLocker>

namespace panthera::modules {

WaterLoopPumpMode waterLoopPumpModeForLevel(double centimeters)
{
    if (centimeters < kWaterLoopLowerLevelCentimeters) {
        return WaterLoopPumpMode::SupplyOnly;
    }
    if (centimeters > kWaterLoopUpperLevelCentimeters) {
        return WaterLoopPumpMode::ReturnOnly;
    }
    return WaterLoopPumpMode::Both;
}

QString waterLoopPumpModeText(WaterLoopPumpMode mode)
{
    switch (mode) {
    case WaterLoopPumpMode::SupplyOnly:
        return QStringLiteral("仅启动抽入水泵（03）");
    case WaterLoopPumpMode::Both:
        return QStringLiteral("同时启动抽入水泵（03）和抽回水泵（02）");
    case WaterLoopPumpMode::ReturnOnly:
        return QStringLiteral("仅启动抽回水泵（02）");
    case WaterLoopPumpMode::Stopped:
        break;
    }
    return QStringLiteral("两台水泵已停止");
}

WaterLoopMonitorThread::WaterLoopMonitorThread(
    QObject* dispatchTarget,
    std::function<void()> pollCallback,
    int intervalMs)
    : m_dispatchTarget(dispatchTarget)
    , m_pollCallback(std::move(pollCallback))
    , m_intervalMs(std::max(1, intervalMs))
{
}

WaterLoopMonitorThread::~WaterLoopMonitorThread()
{
    stopMonitoring();
}

void WaterLoopMonitorThread::startMonitoring()
{
    QMutexLocker locker(&m_mutex);
    if (isRunning()) {
        return;
    }
    m_stopRequested = false;
    start();
}

void WaterLoopMonitorThread::stopMonitoring()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;
        m_waitCondition.wakeAll();
    }
    if (isRunning() && QThread::currentThread() != this) {
        wait();
    }
}

bool WaterLoopMonitorThread::isMonitoring() const
{
    QMutexLocker locker(&m_mutex);
    return isRunning() && !m_stopRequested;
}

void WaterLoopMonitorThread::run()
{
    QMutexLocker locker(&m_mutex);
    while (!m_stopRequested) {
        m_waitCondition.wait(&m_mutex, static_cast<unsigned long>(m_intervalMs));
        if (m_stopRequested) {
            break;
        }

        const QPointer<QObject> dispatchTarget = m_dispatchTarget;
        const std::function<void()> pollCallback = m_pollCallback;
        locker.unlock();
        if (!dispatchTarget.isNull() && pollCallback) {
            QMetaObject::invokeMethod(dispatchTarget.data(), pollCallback, Qt::QueuedConnection);
        }
        locker.relock();
    }
}

}  // namespace panthera::modules
