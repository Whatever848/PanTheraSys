#include <QtTest/QtTest>

#include "adapters/aigtek/aigtek_power_amplifier_client.h"

using panthera::adapters::aigtek::AigtekPowerAmplifierClient;

class AigtekPowerAmplifierClientTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsManualOutputCommands();
    void encodesManualGainExamples();
    void rejectsOutOfRangeGain();
};

void AigtekPowerAmplifierClientTests::buildsManualOutputCommands()
{
    QCOMPARE(AigtekPowerAmplifierClient::buildOutputOnCommand(), QByteArray("ATA,SET,AON,0000;"));
    QCOMPARE(AigtekPowerAmplifierClient::buildOutputOffCommand(), QByteArray("ATA,SET,AOF,0000;"));
}

void AigtekPowerAmplifierClientTests::encodesManualGainExamples()
{
    QCOMPARE(AigtekPowerAmplifierClient::buildSetGainCommand(30.0), QByteArray("ATA,SET,AMP,0300;"));
    QCOMPARE(AigtekPowerAmplifierClient::buildSetGainCommand(21.5), QByteArray("ATA,SET,AMP,0215;"));
    QCOMPARE(AigtekPowerAmplifierClient::buildSetGainCommand(1.2), QByteArray("ATA,SET,AMP,0012;"));
    QCOMPARE(AigtekPowerAmplifierClient::buildSetGainCommand(0.3), QByteArray("ATA,SET,AMP,0003;"));
}

void AigtekPowerAmplifierClientTests::rejectsOutOfRangeGain()
{
    QString code;
    QString errorMessage;
    QVERIFY(!AigtekPowerAmplifierClient::encodeGainCode(30.1, &code, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(AigtekPowerAmplifierClient::buildSetGainCommand(30.1).isEmpty());
    QVERIFY(AigtekPowerAmplifierClient::buildSetGainCommand(40.0).isEmpty());
}

QTEST_GUILESS_MAIN(AigtekPowerAmplifierClientTests)

#include "aigtek_power_amplifier_client_tests.moc"
