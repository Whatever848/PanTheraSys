#pragma once

#include <memory>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class QSerialPort;

namespace panthera::adapters::anthone {

struct Lu926TemperatureSerialSettings final {
    QString portName;
    int baudRate {9600};
    int responseTimeoutMs {2000};
};

class Lu926TemperatureModbusClient final {
public:
    Lu926TemperatureModbusClient();
    ~Lu926TemperatureModbusClient();

    bool open(const Lu926TemperatureSerialSettings& settings, QString* errorMessage = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString portName() const;
    [[nodiscard]] int baudRate() const;
    [[nodiscard]] bool requestToSend() const;
    [[nodiscard]] bool dataTerminalReady() const;
    [[nodiscard]] QStringList lastDebugLog() const;

    bool setChannel1Setpoint(
        double celsius,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);
    bool readChannel1Temperature(
        double* celsius,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);
    bool readChannel1Setpoint(
        double* celsius,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);

    bool setChannelSetpoint(
        quint8 address,
        int channelIndex,
        double celsius,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);
    bool readChannelTemperature(
        quint8 address,
        int channelIndex,
        double* celsius,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);

    static QByteArray buildWriteSet1Frame(double celsius);
    static QByteArray buildReadPv1Frame();
    static QByteArray buildReadSet1Frame();
    static bool decodeReadPv1TemperatureResponse(
        const QByteArray& response,
        double* celsius,
        QString* errorMessage = nullptr);
    static bool decodeReadSet1Response(
        const QByteArray& response,
        double* celsius,
        QString* errorMessage = nullptr);
    static QByteArray buildSetChannelSetpointFrame(quint8 address, int channelIndex, double celsius);
    static QByteArray buildReadChannelTemperatureFrame(quint8 address, int channelIndex);
    static bool decodeReadChannelTemperatureResponse(
        quint8 address,
        int channelIndex,
        const QByteArray& response,
        double* celsius,
        QString* errorMessage = nullptr);
    static quint16 modbusCrc(const QByteArray& frameWithoutCrc);
    static bool frameHasValidCrc(const QByteArray& frame);
    static QString frameToHex(const QByteArray& frame);

private:
    bool sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response);
    void clearDebugLog();
    void appendDebugLog(const QString& text);
    static bool validateAddress(quint8 address, QString* errorMessage);
    static bool validateChannel(int channelIndex, QString* errorMessage);
    static bool validateResponse(quint8 address, quint8 functionCode, const QByteArray& response, QString* errorMessage);
    static QByteArray matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes);

    std::unique_ptr<QSerialPort> m_serialPort;
    int m_responseTimeoutMs {2000};
    QStringList m_lastDebugLog;
};

}  // namespace panthera::adapters::anthone
