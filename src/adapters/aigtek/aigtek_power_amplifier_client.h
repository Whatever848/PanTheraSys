#pragma once

#include <memory>

#include <QByteArray>
#include <QString>

class QSerialPort;

namespace panthera::adapters::aigtek {

struct AigtekPowerAmplifierSerialSettings final {
    QString portName;
    int baudRate {9600};
    int responseTimeoutMs {3000};
};

class AigtekPowerAmplifierClient final {
public:
    static constexpr int kDefaultBaudRate = 9600;

    AigtekPowerAmplifierClient();
    ~AigtekPowerAmplifierClient();

    bool open(const AigtekPowerAmplifierSerialSettings& settings, QString* errorMessage = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString portName() const;
    [[nodiscard]] int baudRate() const;
    [[nodiscard]] bool requestToSend() const;
    [[nodiscard]] bool dataTerminalReady() const;

    bool outputOn(QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool outputOff(QString* errorMessage = nullptr, QByteArray* response = nullptr);
    bool setGain(double gain, QString* errorMessage = nullptr, QByteArray* response = nullptr);

    static bool encodeGainCode(double gain, QString* code, QString* errorMessage = nullptr);
    static QByteArray buildOutputOnCommand();
    static QByteArray buildOutputOffCommand();
    static QByteArray buildSetGainCommand(double gain);
    static QString bytesToText(const QByteArray& bytes);

private:
    bool sendCommand(const QByteArray& command, QString* errorMessage, QByteArray* response);
    bool validateResponse(const QByteArray& response, QString* errorMessage) const;
    static QByteArray buildCommand(const char* action, const QString& valueCode);

    std::unique_ptr<QSerialPort> m_serialPort;
    int m_responseTimeoutMs {3000};
};

}  // namespace panthera::adapters::aigtek
