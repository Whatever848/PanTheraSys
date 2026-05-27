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

class IInfusionPumpController {
public:
    virtual ~IInfusionPumpController() = default;

    virtual InfusionPumpTelemetry latestInfusionPumpTelemetry() const = 0;
    virtual bool setInfusionPumpEnabled(bool enabled, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpOperatingMode(InfusionPumpOperatingMode mode, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpDirection(InfusionPumpDirection direction, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpSpeedRpm(double rpm, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpFlowMlPerMin(double flowMlPerMin, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpTargetVolumeMl(double volumeMl, QString* errorMessage = nullptr) = 0;
    virtual bool setInfusionPumpCycleTiming(double runSeconds, double stopSeconds, QString* errorMessage = nullptr) = 0;
    virtual bool configureInfusionPumpCommunication(int modbusAddress, int baudRate, QString* errorMessage = nullptr) = 0;
    virtual bool resetInfusionPumpFault(QString* errorMessage = nullptr) = 0;
};

class ITemperatureController {
public:
    virtual ~ITemperatureController() = default;

    virtual TemperatureModuleTelemetry latestTemperatureTelemetry() const = 0;
    virtual bool setTemperatureControlEnabled(bool enabled, QString* errorMessage = nullptr) = 0;
    virtual bool setTemperatureChannelSetpoint(int channelIndex, double celsius, QString* errorMessage = nullptr) = 0;
    virtual bool configureTemperatureInput(int channelIndex, TemperatureInputType inputType, QString* errorMessage = nullptr) = 0;
    virtual bool configureTemperatureCommunication(int modbusAddress, int baudRate, QString* errorMessage = nullptr) = 0;
    virtual bool resetTemperatureFault(QString* errorMessage = nullptr) = 0;
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
