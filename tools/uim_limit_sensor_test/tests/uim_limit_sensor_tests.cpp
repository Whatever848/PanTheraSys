#include <QtTest>

#include "limit_test_session.h"
#include "uim_can_protocol.h"

using namespace panthera::tools::uim_limit;

namespace {

RawCanFrame makeFrame(quint32 nodeId, S3Edge edge, int millisecondOffset = 0)
{
    RawCanFrame frame;
    frame.receivedAt = QDateTime::fromString(QStringLiteral("2026-10-18T12:00:00.000"), Qt::ISODateWithMs)
                           .addMSecs(millisecondOffset);
    frame.canId = UimCanProtocol::makeSimulationCanId(nodeId, edge);
    frame.ide = 1;
    frame.dlc = 0;
    return frame;
}

}  // namespace

class UimLimitSensorTests final : public QObject {
    Q_OBJECT

private slots:
    void decodesS3FallingEdge();
    void decodesS3RisingEdge();
    void nodeIdRoundTrip();
    void rejectsUnrelatedCanCommand();
    void marksMissingNodeSourceAsUnknown();
    void complementaryEdgesPass();
    void reverseEdgeOrderAlsoPasses();
    void repeatedEdgeDoesNotPass();
    void otherNodeCannotPassTargetTest();
};

void UimLimitSensorTests::decodesS3FallingEdge()
{
    const ParsedCanFrame parsed = UimCanProtocol::parse(makeFrame(6, S3Edge::Falling));
    QCOMPARE(parsed.nodeId, 6U);
    QCOMPARE(parsed.canCommand, UimCanProtocol::kCanS3FallingCommand);
    QCOMPARE(parsed.manualEventCode, UimCanProtocol::kManualS3FallingEvent);
    QCOMPARE(parsed.s3Edge, S3Edge::Falling);
    QCOMPARE(parsed.levelText, QStringLiteral("S3 低电平"));
}

void UimLimitSensorTests::decodesS3RisingEdge()
{
    const ParsedCanFrame parsed = UimCanProtocol::parse(makeFrame(7, S3Edge::Rising));
    QCOMPARE(parsed.nodeId, 7U);
    QCOMPARE(parsed.canCommand, UimCanProtocol::kCanS3RisingCommand);
    QCOMPARE(parsed.manualEventCode, UimCanProtocol::kManualS3RisingEvent);
    QCOMPARE(parsed.s3Edge, S3Edge::Rising);
    QCOMPARE(parsed.levelText, QStringLiteral("S3 高电平"));
}

void UimLimitSensorTests::nodeIdRoundTrip()
{
    const QVector<quint32> nodeIds {1, 6, 7, 31, 32, 63, 64, 96, 127};
    for (const quint32 nodeId : nodeIds) {
        QCOMPARE(
            UimCanProtocol::decodeNodeId(UimCanProtocol::makeSimulationCanId(nodeId, S3Edge::Falling)),
            nodeId);
    }
}

void UimLimitSensorTests::rejectsUnrelatedCanCommand()
{
    RawCanFrame frame = makeFrame(6, S3Edge::Falling);
    frame.canId = (frame.canId & ~0xFFU) | 0x72U;
    const ParsedCanFrame parsed = UimCanProtocol::parse(frame);
    QCOMPARE(parsed.nodeId, 6U);
    QVERIFY(!parsed.isS3Event());
    QCOMPARE(parsed.manualEventCode, -1);
}

void UimLimitSensorTests::marksMissingNodeSourceAsUnknown()
{
    RawCanFrame frame = makeFrame(6, S3Edge::Falling);
    frame.canId = UimCanProtocol::kCanS3FallingCommand;
    const ParsedCanFrame parsed = UimCanProtocol::parse(frame);
    QVERIFY(parsed.isS3Event());
    QVERIFY(!parsed.nodeSourceKnown);

    LimitTestSession session;
    session.start(6, frame.receivedAt);
    QCOMPARE(session.consume(parsed), LimitTestUpdate::Ignored);
    QCOMPARE(session.phase(), LimitTestPhase::WaitingForTrigger);
}

void UimLimitSensorTests::complementaryEdgesPass()
{
    LimitTestSession session;
    session.start(6, makeFrame(6, S3Edge::Falling).receivedAt);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(6, S3Edge::Falling, 100))), LimitTestUpdate::TriggerDetected);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(6, S3Edge::Rising, 200))), LimitTestUpdate::Passed);
    QCOMPARE(session.phase(), LimitTestPhase::Passed);
    QVERIFY(session.resultText().contains(QStringLiteral("测试通过")));
}

void UimLimitSensorTests::reverseEdgeOrderAlsoPasses()
{
    LimitTestSession session;
    session.start(7, makeFrame(7, S3Edge::Rising).receivedAt);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Rising, 100))), LimitTestUpdate::TriggerDetected);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Falling, 200))), LimitTestUpdate::Passed);
    QCOMPARE(session.phase(), LimitTestPhase::Passed);
}

void UimLimitSensorTests::repeatedEdgeDoesNotPass()
{
    LimitTestSession session;
    session.start(7, makeFrame(7, S3Edge::Rising).receivedAt);
    QCOMPARE(
        session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Rising, 100))),
        LimitTestUpdate::TriggerDetected);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Rising, 200))), LimitTestUpdate::RepeatedSameEdge);
    QCOMPARE(session.phase(), LimitTestPhase::WaitingForRestore);
}

void UimLimitSensorTests::otherNodeCannotPassTargetTest()
{
    LimitTestSession session;
    session.start(6, makeFrame(6, S3Edge::Falling).receivedAt);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Falling, 100))), LimitTestUpdate::Ignored);
    QCOMPARE(session.consume(UimCanProtocol::parse(makeFrame(7, S3Edge::Rising, 200))), LimitTestUpdate::Ignored);
    QCOMPARE(session.phase(), LimitTestPhase::WaitingForTrigger);
}

QTEST_GUILESS_MAIN(UimLimitSensorTests)

#include "uim_limit_sensor_tests.moc"

