#include "uim_can_protocol.h"

#include <algorithm>

namespace panthera::tools::uim_limit {

bool ParsedCanFrame::isS3Event() const
{
    return s3Edge != S3Edge::None;
}

ParsedCanFrame UimCanProtocol::parse(const RawCanFrame& frame)
{
    ParsedCanFrame parsed;
    parsed.raw = frame;
    parsed.nodeId = decodeNodeId(frame.canId);
    parsed.nodeSourceKnown = frame.canId <= 0x1FFFFFFFU && parsed.nodeId >= 1U && parsed.nodeId <= 127U;
    parsed.canCommand = static_cast<quint8>(frame.canId & 0xFFU);

    switch (parsed.canCommand) {
    case kCanS3FallingCommand:
        parsed.manualEventCode = kManualS3FallingEvent;
        parsed.s3Edge = S3Edge::Falling;
        parsed.levelText = QStringLiteral("S3 低电平");
        parsed.resultText = QStringLiteral("S3 下降沿");
        break;
    case kCanS3RisingCommand:
        parsed.manualEventCode = kManualS3RisingEvent;
        parsed.s3Edge = S3Edge::Rising;
        parsed.levelText = QStringLiteral("S3 高电平");
        parsed.resultText = QStringLiteral("S3 上升沿");
        break;
    default:
        parsed.levelText = QStringLiteral("未知 / 未收到 S3 事件");
        parsed.resultText = QStringLiteral("非 S3 状态变化事件，待确认");
        break;
    }

    return parsed;
}

quint32 UimCanProtocol::decodeNodeId(quint32 canId)
{
    // UIROBOT USB-CAN SDK sample RevRtcnNotify() reconstructs a UIM node
    // from these two bit fields in the callback CAN ID.
    return ((canId >> 3U) & 0x060U) + ((canId >> 19U) & 0x01FU);
}

quint32 UimCanProtocol::makeSimulationCanId(quint32 nodeId, S3Edge edge)
{
    const quint8 command = edge == S3Edge::Rising
        ? kCanS3RisingCommand
        : kCanS3FallingCommand;
    return ((nodeId & 0x01FU) << 19U)
        | ((nodeId & 0x060U) << 3U)
        | static_cast<quint32>(command);
}

QString UimCanProtocol::canIdText(quint32 canId)
{
    const QString digits = QString::number(canId, 16).toUpper().rightJustified(8, QLatin1Char('0'));
    return QStringLiteral("0x%1").arg(digits);
}

QString UimCanProtocol::dataText(const QByteArray& data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (const char value : data) {
        bytes.append(QStringLiteral("%1")
                         .arg(static_cast<quint8>(value), 2, 16, QLatin1Char('0'))
                         .toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

QString UimCanProtocol::frameTypeText(const RawCanFrame& frame)
{
    const QString idType = frame.ide == 0 ? QStringLiteral("标准帧") : QStringLiteral("扩展帧");
    const QString payloadType = frame.rtr == 0 ? QStringLiteral("数据帧") : QStringLiteral("远程帧");
    return QStringLiteral("%1 / %2 / SendType=%3")
        .arg(idType, payloadType)
        .arg(frame.sendType);
}

QString UimCanProtocol::edgeText(S3Edge edge)
{
    switch (edge) {
    case S3Edge::Falling:
        return QStringLiteral("S3 下降沿");
    case S3Edge::Rising:
        return QStringLiteral("S3 上升沿");
    case S3Edge::None:
        break;
    }
    return QStringLiteral("未知");
}

QString UimCanProtocol::eventCodeText(const ParsedCanFrame& frame)
{
    if (!frame.isS3Event()) {
        const QString command = QString::number(frame.canCommand, 16).toUpper().rightJustified(2, QLatin1Char('0'));
        return QStringLiteral("待确认（CAN RCW=0x%1）").arg(command);
    }

    const QString event = QString::number(frame.manualEventCode, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    const QString command = QString::number(frame.canCommand, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    return QStringLiteral("0x%1（CAN RCW=0x%2）").arg(event, command);
}

}  // namespace panthera::tools::uim_limit

