#pragma once

#include <memory>

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class QSerialPort;

namespace panthera::adapters::waterpump {

struct WaterPumpSerialSettings final {
    QString portName;
    int baudRate {9600};
    int responseTimeoutMs {800};
};

class WaterPumpModbusClient final {
public:
    static constexpr quint8 kReturnPumpAddress = 0x02;
    static constexpr quint8 kSupplyPumpAddress = 0x03;
    static constexpr int kMinimumFlowMlPerMin = 0;
    static constexpr int kMaximumFlowMlPerMin = 1500;
    static constexpr int kMaximumRunDurationSeconds = 86400;

    WaterPumpModbusClient();
    ~WaterPumpModbusClient();

    bool open(const WaterPumpSerialSettings& settings, QString* errorMessage = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString portName() const;
    [[nodiscard]] int baudRate() const;

    bool setFlowMlPerMin(quint8 address, double flowMlPerMin, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool readFlowMlPerMin(quint8 address, double* flowMlPerMin, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool setRunDurationSeconds(quint8 address, int seconds, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool readConfiguredRunDurationSeconds(quint8 address, double* seconds, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool readRealtimeRunDurationSeconds(quint8 address, double* seconds, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool startPump(quint8 address, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool stopPump(quint8 address, QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool setClockwise(quint8 address, QString* errorMessage = nullptr, QByteArray* response = nullptr);

    static QByteArray buildSetFlowFrame(quint8 address, double flowMlPerMin);
    static QByteArray buildReadFlowFrame(quint8 address);
    static QByteArray buildSetRunDurationFrame(quint8 address, int seconds);
    static QByteArray buildReadConfiguredRunDurationFrame(quint8 address);
    static QByteArray buildReadRealtimeRunDurationFrame(quint8 address);
    static QByteArray buildStartFrame(quint8 address);
    static QByteArray buildStopFrame(quint8 address);
    static QByteArray buildClockwiseFrame(quint8 address);
    static quint16 modbusCrc(const QByteArray& frameWithoutCrc);
    static bool frameHasValidCrc(const QByteArray& frame);
    static QString frameToHex(const QByteArray& frame);

private:
    bool sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response);
    static bool validateAddress(quint8 address, QString* errorMessage);
    static bool validateResponse(quint8 address, quint8 functionCode, const QByteArray& response, QString* errorMessage);
    static QByteArray matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes);
    static bool decodeFlowResponse(quint8 address, const QByteArray& response, double* flowMlPerMin, QString* errorMessage);
    static bool decodeDurationResponse(quint8 address, const QByteArray& response, double* seconds, QString* errorMessage);

    std::unique_ptr<QSerialPort> m_serialPort;
    int m_responseTimeoutMs {800};
};

}  // namespace panthera::adapters::waterpump
