#pragma once

#include <memory>

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class QSerialPort;

namespace panthera::adapters::liquidlevel {

struct LiquidLevelSerialSettings final {
    QString portName;
    int baudRate {9600};
    int responseTimeoutMs {800};
};

class LiquidLevelModbusClient final {
public:
    static constexpr quint8 kDefaultAddress = 0x01;

    LiquidLevelModbusClient();
    ~LiquidLevelModbusClient();

    bool open(const LiquidLevelSerialSettings& settings, QString* errorMessage = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString portName() const;
    [[nodiscard]] int baudRate() const;

    bool readDeviceAddress(
        quint8* address,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);
    bool readLevelMillimeters(
        quint8 address,
        double* millimeters,
        QString* errorMessage = nullptr,
        QByteArray* response = nullptr);

    static QByteArray buildReadAddressFrame();
    static bool decodeReadAddressResponse(
        const QByteArray& response,
        quint8* address,
        QString* errorMessage = nullptr);
    static QByteArray buildReadLevelFrame(quint8 address);
    static bool decodeReadLevelResponse(
        quint8 address,
        const QByteArray& response,
        double* millimeters,
        QString* errorMessage = nullptr);
    static quint16 modbusCrc(const QByteArray& frameWithoutCrc);
    static bool frameHasValidCrc(const QByteArray& frame);
    static QString frameToHex(const QByteArray& frame);

private:
    bool sendRawFrame(
        const QByteArray& request,
        int expectedMinimumResponseSize,
        QString* errorMessage,
        QByteArray* response);
    bool sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response);
    static bool validateAddress(quint8 address, QString* errorMessage);
    static bool validateResponse(quint8 address, quint8 functionCode, const QByteArray& response, QString* errorMessage);
    static QByteArray matchingReadAddressResponseFrame(const QByteArray& bytes);
    static QByteArray matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes);

    std::unique_ptr<QSerialPort> m_serialPort;
#ifdef Q_OS_WIN
    void* m_nativeHandle {nullptr};
    QString m_nativePortName;
    int m_nativeBaudRate {0};
#endif
    int m_responseTimeoutMs {800};
};

}  // namespace panthera::adapters::liquidlevel
