#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace panthera::tools::uim_limit {

enum class S3Edge {
    None,
    Falling,
    Rising
};

struct RawCanFrame {
    QDateTime receivedAt;
    quint32 deviceIndex {0};
    quint32 canId {0};
    quint8 sendType {0};
    quint8 ide {0};
    quint8 rtr {0};
    quint8 dlc {0};
    QByteArray data;
    quint32 callbackMessageLength {0};
    bool simulated {false};
};

struct ParsedCanFrame {
    RawCanFrame raw;
    quint32 nodeId {0};
    bool nodeSourceKnown {false};
    quint8 canCommand {0};
    int manualEventCode {-1};
    S3Edge s3Edge {S3Edge::None};
    QString levelText;
    QString resultText;

    [[nodiscard]] bool isS3Event() const;
};

class UimCanProtocol final {
public:
    static constexpr quint8 kCanS3FallingCommand = 0x73;
    static constexpr quint8 kCanS3RisingCommand = 0xF3;
    static constexpr int kManualS3FallingEvent = 0xA4;
    static constexpr int kManualS3RisingEvent = 0xA5;

    [[nodiscard]] static ParsedCanFrame parse(const RawCanFrame& frame);
    [[nodiscard]] static quint32 decodeNodeId(quint32 canId);
    [[nodiscard]] static quint32 makeSimulationCanId(quint32 nodeId, S3Edge edge);
    [[nodiscard]] static QString canIdText(quint32 canId);
    [[nodiscard]] static QString dataText(const QByteArray& data);
    [[nodiscard]] static QString frameTypeText(const RawCanFrame& frame);
    [[nodiscard]] static QString edgeText(S3Edge edge);
    [[nodiscard]] static QString eventCodeText(const ParsedCanFrame& frame);
};

}  // namespace panthera::tools::uim_limit

