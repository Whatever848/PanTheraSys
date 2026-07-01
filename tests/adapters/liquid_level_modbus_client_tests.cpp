#include <QtTest/QtTest>

#include "adapters/liquidlevel/liquid_level_modbus_client.h"

using panthera::adapters::liquidlevel::LiquidLevelModbusClient;

class LiquidLevelModbusClientTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsReadAddressFrameFromManual();
    void decodesManualAddressResponse();
    void buildsReadLevelFrameForDefaultAddress();
    void decodesManualExampleResponse();
};

void LiquidLevelModbusClientTests::buildsReadAddressFrameFromManual()
{
    QCOMPARE(
        LiquidLevelModbusClient::buildReadAddressFrame(),
        QByteArray::fromHex("EE07000400012294"));
}

void LiquidLevelModbusClientTests::decodesManualAddressResponse()
{
    const QByteArray response = QByteArray::fromHex("EE07000400012294");
    quint8 address = 0;

    QVERIFY(LiquidLevelModbusClient::frameHasValidCrc(response));
    QVERIFY(LiquidLevelModbusClient::decodeReadAddressResponse(response, &address));
    QCOMPARE(address, quint8(0x01));
}

void LiquidLevelModbusClientTests::buildsReadLevelFrameForDefaultAddress()
{
    QCOMPARE(
        LiquidLevelModbusClient::buildReadLevelFrame(LiquidLevelModbusClient::kDefaultAddress),
        QByteArray::fromHex("010300000001840A"));
}

void LiquidLevelModbusClientTests::decodesManualExampleResponse()
{
    const QByteArray response = QByteArray::fromHex("0103020027F85E");
    double millimeters = 0.0;

    QVERIFY(LiquidLevelModbusClient::frameHasValidCrc(response));
    QVERIFY(LiquidLevelModbusClient::decodeReadLevelResponse(
        LiquidLevelModbusClient::kDefaultAddress,
        response,
        &millimeters));
    QCOMPARE(millimeters, 3.9);
}

QTEST_GUILESS_MAIN(LiquidLevelModbusClientTests)

#include "liquid_level_modbus_client_tests.moc"
