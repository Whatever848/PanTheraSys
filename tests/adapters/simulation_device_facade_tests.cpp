#include <QtTest/QtTest>

#include "adapters/anthone/lu926_temperature_protocol.h"
#include "adapters/sim/simulation_device_facade.h"

using panthera::adapters::anthone::Lu926TemperatureProtocol;
using namespace panthera::adapters;
using namespace panthera::core;

class SimulationDeviceFacadeTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesInfusionPumpTelemetryDefaults();
    void validatesInfusionPumpManualLimits();
    void startsAndStopsInfusionPumpSafely();
    void blocksInfusionPumpWhenWaterLoopFaulted();
    void mapsLu926TemperatureRegisters();
    void exposesTemperatureTelemetryDefaults();
    void validatesTemperatureManualLimits();
    void blocksTemperatureWhenFaulted();
};

void SimulationDeviceFacadeTests::exposesInfusionPumpTelemetryDefaults()
{
    SimulationDeviceFacade facade;

    const auto telemetry = facade.latestInfusionPumpTelemetry();
    QVERIFY(telemetry.connected);
    QVERIFY(telemetry.dataValid);
    QVERIFY(!telemetry.dataStale);
    QVERIFY(telemetry.safetyLimitsOk);
    QCOMPARE(telemetry.runState, InfusionPumpRunState::Stopped);
    QCOMPARE(telemetry.operatingMode, InfusionPumpOperatingMode::Flow);
    QCOMPARE(telemetry.cycleMode, InfusionPumpCycleMode::Automatic);
    QCOMPARE(telemetry.direction, InfusionPumpDirection::Clockwise);
    QCOMPARE(telemetry.speedRpm, 180.0);
    QCOMPARE(telemetry.targetFlowMlPerMin, 720.0);
    QCOMPARE(telemetry.modbusAddress, 192);
    QCOMPARE(telemetry.baudRate, 9600);
}

void SimulationDeviceFacadeTests::validatesInfusionPumpManualLimits()
{
    SimulationDeviceFacade facade;
    QString errorMessage;

    QVERIFY(!facade.setInfusionPumpSpeedRpm(0.0, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("0.1")));
    QVERIFY(!facade.setInfusionPumpSpeedRpm(400.1, &errorMessage));

    QVERIFY(!facade.setInfusionPumpFlowMlPerMin(1500.1, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("1500.0")));

    QVERIFY(!facade.setInfusionPumpTargetVolumeMl(10000.0, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("9999.0")));

    QVERIFY(!facade.setInfusionPumpCycleTiming(1000.0, 0.0, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("999.0")));
    QVERIFY(!facade.setInfusionPumpCycleTiming(0.0, 1000.0, &errorMessage));

    QVERIFY(!facade.configureInfusionPumpCommunication(0, 9600, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("1-247")));
    QVERIFY(!facade.configureInfusionPumpCommunication(192, 12345, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("波特率")));

    QVERIFY(facade.setInfusionPumpSpeedRpm(400.0, &errorMessage));
    QVERIFY(facade.setInfusionPumpFlowMlPerMin(1500.0, &errorMessage));
    QVERIFY(facade.setInfusionPumpTargetVolumeMl(9999.0, &errorMessage));
    QVERIFY(facade.setInfusionPumpCycleTiming(999.0, 999.0, &errorMessage));
    QVERIFY(facade.configureInfusionPumpCommunication(247, 115200, &errorMessage));
}

void SimulationDeviceFacadeTests::startsAndStopsInfusionPumpSafely()
{
    SimulationDeviceFacade facade;
    QString errorMessage;

    QVERIFY(facade.setInfusionPumpEnabled(true, &errorMessage));
    auto telemetry = facade.latestInfusionPumpTelemetry();
    QCOMPARE(telemetry.runState, InfusionPumpRunState::Running);
    QVERIFY(telemetry.actualFlowMlPerMin >= 0.0);
    QVERIFY(telemetry.actualFlowMlPerMin <= 1500.0);
    QVERIFY(telemetry.safetyLimitsOk);

    QVERIFY(facade.setInfusionPumpEnabled(false, &errorMessage));
    telemetry = facade.latestInfusionPumpTelemetry();
    QCOMPARE(telemetry.runState, InfusionPumpRunState::Stopped);
    QCOMPARE(telemetry.actualFlowMlPerMin, 0.0);
    QVERIFY(telemetry.safetyLimitsOk);
}

void SimulationDeviceFacadeTests::blocksInfusionPumpWhenWaterLoopFaulted()
{
    SimulationDeviceFacade facade;
    QString errorMessage;

    facade.injectFault(InterlockReason::WaterLoopFault, true);

    QVERIFY(!facade.isWaterLoopHealthy());
    QVERIFY(!facade.setInfusionPumpEnabled(true, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("禁止启动注水泵")));

    const auto telemetry = facade.latestInfusionPumpTelemetry();
    QCOMPARE(telemetry.runState, InfusionPumpRunState::Fault);
    QVERIFY(!telemetry.connected);
    QVERIFY(!telemetry.safetyLimitsOk);
}

void SimulationDeviceFacadeTests::mapsLu926TemperatureRegisters()
{
    QCOMPARE(Lu926TemperatureProtocol::setpointRegister(1), 0x0000);
    QCOMPARE(Lu926TemperatureProtocol::setpointRegister(6), 0x0005);
    QCOMPARE(Lu926TemperatureProtocol::outputPercentRegister(1), 0x0108);
    QCOMPARE(Lu926TemperatureProtocol::processValueRegister(6), 0x0115);
    QCOMPARE(Lu926TemperatureProtocol::alarmStateRegister(6), 0x011B);
    QCOMPARE(Lu926TemperatureProtocol::processValueRegister(7), -1);
    QCOMPARE(Lu926TemperatureProtocol::decodeRegisterTemperature(365), 36.5);
    QCOMPARE(Lu926TemperatureProtocol::encodeRegisterTemperature(36.5), qint16(365));
    QCOMPARE(Lu926TemperatureProtocol::decodeOutputPercent(12800), 50.0);
    QCOMPARE(Lu926TemperatureProtocol::encodeOutputPercent(50.0), quint16(12800));
    QVERIFY(Lu926TemperatureProtocol::isSupportedBaudRate(38400));
    QVERIFY(!Lu926TemperatureProtocol::isSupportedBaudRate(115200));
}

void SimulationDeviceFacadeTests::exposesTemperatureTelemetryDefaults()
{
    SimulationDeviceFacade facade;

    const auto telemetry = facade.latestTemperatureTelemetry();
    QVERIFY(telemetry.connected);
    QVERIFY(telemetry.dataValid);
    QVERIFY(!telemetry.dataStale);
    QVERIFY(telemetry.safetyLimitsOk);
    QCOMPARE(telemetry.modbusAddress, 4);
    QCOMPARE(telemetry.baudRate, 9600);
    QCOMPARE(telemetry.samplePeriodSeconds, 0.5);
    QCOMPARE(static_cast<int>(telemetry.channels.size()), 6);
    QCOMPARE(telemetry.channels.at(0).channelIndex, 1);
    QCOMPARE(telemetry.channels.at(0).inputType, TemperatureInputType::Pt100);
    QCOMPARE(telemetry.channels.at(0).setpointTemperatureCelsius, 20.0);
    QCOMPARE(telemetry.channels.at(3).setpointTemperatureCelsius, 37.0);
}

void SimulationDeviceFacadeTests::validatesTemperatureManualLimits()
{
    SimulationDeviceFacade facade;
    QString errorMessage;

    QVERIFY(!facade.setTemperatureChannelSetpoint(0, 20.0, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("1-6")));
    QVERIFY(!facade.setTemperatureChannelSetpoint(1, -0.1, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("0.0")));
    QVERIFY(!facade.setTemperatureChannelSetpoint(1, 45.1, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("45.0")));
    QVERIFY(facade.setTemperatureChannelSetpoint(1, 45.0, &errorMessage));

    QVERIFY(facade.configureTemperatureInput(6, TemperatureInputType::ThermocoupleK, &errorMessage));
    QCOMPARE(facade.latestTemperatureTelemetry().channels.at(5).inputType, TemperatureInputType::ThermocoupleK);

    QVERIFY(!facade.configureTemperatureCommunication(0, 9600, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("1-247")));
    QVERIFY(!facade.configureTemperatureCommunication(1, 115200, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("LU-926UT6Y")));
    QVERIFY(facade.configureTemperatureCommunication(247, 38400, &errorMessage));
}

void SimulationDeviceFacadeTests::blocksTemperatureWhenFaulted()
{
    SimulationDeviceFacade facade;
    QString errorMessage;

    facade.injectFault(InterlockReason::TemperatureFault, true);

    auto telemetry = facade.latestTemperatureTelemetry();
    QVERIFY(!telemetry.connected);
    QVERIFY(!telemetry.safetyLimitsOk);
    QVERIFY(!facade.setTemperatureControlEnabled(true, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("禁止启用温度控制")));

    QVERIFY(facade.resetTemperatureFault(&errorMessage));
    telemetry = facade.latestTemperatureTelemetry();
    QVERIFY(telemetry.connected);
    QVERIFY(telemetry.safetyLimitsOk);
}

QTEST_GUILESS_MAIN(SimulationDeviceFacadeTests)

#include "simulation_device_facade_tests.moc"
