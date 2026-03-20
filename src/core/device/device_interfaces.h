#pragma once

#include <QString>

#include "core/domain/system_types.h"

namespace panthera::core {

class IMotionController {
public:
    virtual ~IMotionController() = default;

    virtual Coordinate6D currentPosition() const = 0;
    virtual bool moveTo(const Coordinate6D& target, QString* errorMessage = nullptr) = 0;
    virtual bool home(QString* errorMessage = nullptr) = 0;
};

class IUltrasoundSource {
public:
    virtual ~IUltrasoundSource() = default;

    virtual bool isAvailable() const = 0;
    virtual QString backendName() const = 0;
};

class IPowerController {
public:
    virtual ~IPowerController() = default;

    virtual bool isPowerReady() const = 0;
    virtual bool setTreatmentOutputEnabled(bool enabled, QString* errorMessage = nullptr) = 0;
    virtual double outputPowerWatts() const = 0;
};

class IWaterLoopMonitor {
public:
    virtual ~IWaterLoopMonitor() = default;

    virtual bool isWaterLoopHealthy() const = 0;
    virtual double pressureMpa() const = 0;
    virtual double flowRateLpm() const = 0;
};

class IDeviceHealthService {
public:
    virtual ~IDeviceHealthService() = default;

    virtual DeviceSnapshot latestSnapshot() const = 0;
};

class IEmergencyStopChannel {
public:
    virtual ~IEmergencyStopChannel() = default;

    virtual bool isEmergencyStopReleased() const = 0;
};

}  // panthera::core 命名空间
