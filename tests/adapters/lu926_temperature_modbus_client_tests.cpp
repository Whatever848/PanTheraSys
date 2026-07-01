#include <QtTest/QtTest>

#include "adapters/anthone/lu926_temperature_modbus_client.h"

using panthera::adapters::anthone::Lu926TemperatureModbusClient;

class Lu926TemperatureModbusClientTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsReadProcessValueFrameForAddress04();
    void buildsReadSetpointFrameForAddress04();
    void buildsSetpointFrameForAddress04();
    void decodesVerifiedReadProcessValueResponse();
    void decodesVerifiedReadSetpointResponse();
};

void Lu926TemperatureModbusClientTests::buildsReadProcessValueFrameForAddress04()
{
    QCOMPARE(
        Lu926TemperatureModbusClient::buildReadPv1Frame(),
        QByteArray::fromHex("0403011000018466"));
}

void Lu926TemperatureModbusClientTests::buildsReadSetpointFrameForAddress04()
{
    QCOMPARE(
        Lu926TemperatureModbusClient::buildReadSet1Frame(),
        QByteArray::fromHex("040300000001845F"));
}

void Lu926TemperatureModbusClientTests::buildsSetpointFrameForAddress04()
{
    const QByteArray frame20 = Lu926TemperatureModbusClient::buildWriteSet1Frame(20.0);
    const QByteArray frame30 = Lu926TemperatureModbusClient::buildWriteSet1Frame(30.0);

    QCOMPARE(frame20, QByteArray::fromHex("0406000000C88809"));
    QCOMPARE(frame30, QByteArray::fromHex("04060000012C89D2"));
    QVERIFY(Lu926TemperatureModbusClient::frameHasValidCrc(frame20));
    QVERIFY(Lu926TemperatureModbusClient::frameHasValidCrc(frame30));
}

void Lu926TemperatureModbusClientTests::decodesVerifiedReadProcessValueResponse()
{
    const QByteArray response = QByteArray::fromHex("0403020139B5C6");
    double celsius = 0.0;

    QVERIFY(Lu926TemperatureModbusClient::frameHasValidCrc(response));
    QVERIFY(Lu926TemperatureModbusClient::decodeReadPv1TemperatureResponse(response, &celsius));
    QCOMPARE(celsius, 31.3);
}

void Lu926TemperatureModbusClientTests::decodesVerifiedReadSetpointResponse()
{
    const QByteArray response = QByteArray::fromHex("040302012C7409");
    double celsius = 0.0;

    QVERIFY(Lu926TemperatureModbusClient::frameHasValidCrc(response));
    QVERIFY(Lu926TemperatureModbusClient::decodeReadSet1Response(response, &celsius));
    QCOMPARE(celsius, 30.0);
}

QTEST_GUILESS_MAIN(Lu926TemperatureModbusClientTests)

#include "lu926_temperature_modbus_client_tests.moc"
