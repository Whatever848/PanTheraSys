#include <QtTest/QtTest>

#include "adapters/waterpump/water_pump_modbus_client.h"

using panthera::adapters::waterpump::WaterPumpModbusClient;

class WaterPumpModbusClientTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsFramesForVerifiedAddress02Examples();
    void recalculatesCrcForSupplyPumpAddress03();
};

void WaterPumpModbusClientTests::buildsFramesForVerifiedAddress02Examples()
{
    QCOMPARE(
        WaterPumpModbusClient::buildSetFlowFrame(0x02, 600.0),
        QByteArray::fromHex("0210400B000204000044163E55"));
    QCOMPARE(
        WaterPumpModbusClient::buildReadFlowFrame(0x02),
        QByteArray::fromHex("0203400B0002A03A"));
    QCOMPARE(
        WaterPumpModbusClient::buildSetRunDurationFrame(0x02, 0),
        QByteArray::fromHex("02104005000204000000000D17"));
    QCOMPARE(
        WaterPumpModbusClient::buildSetRunDurationFrame(0x02, 60),
        QByteArray::fromHex("02104005000204EA6000003911"));
    QCOMPARE(
        WaterPumpModbusClient::buildReadConfiguredRunDurationFrame(0x02),
        QByteArray::fromHex("020340050002C1F9"));
    QCOMPARE(
        WaterPumpModbusClient::buildReadRealtimeRunDurationFrame(0x02),
        QByteArray::fromHex("0203400700026039"));
    QCOMPARE(
        WaterPumpModbusClient::buildStartFrame(0x02),
        QByteArray::fromHex("02050001FF00DDC9"));
    QCOMPARE(
        WaterPumpModbusClient::buildStopFrame(0x02),
        QByteArray::fromHex("0205000100009C39"));
    QCOMPARE(
        WaterPumpModbusClient::buildClockwiseFrame(0x02),
        QByteArray::fromHex("02050002FF002DC9"));
}

void WaterPumpModbusClientTests::recalculatesCrcForSupplyPumpAddress03()
{
    const QByteArray returnPumpFrame = WaterPumpModbusClient::buildSetFlowFrame(0x02, 600.0);
    const QByteArray supplyPumpFrame = WaterPumpModbusClient::buildSetFlowFrame(0x03, 600.0);

    QCOMPARE(supplyPumpFrame.left(supplyPumpFrame.size() - 2), QByteArray::fromHex("0310400B00020400004416"));
    QVERIFY(WaterPumpModbusClient::frameHasValidCrc(supplyPumpFrame));
    QVERIFY(supplyPumpFrame.right(2) != returnPumpFrame.right(2));
}

QTEST_GUILESS_MAIN(WaterPumpModbusClientTests)

#include "water_pump_modbus_client_tests.moc"
