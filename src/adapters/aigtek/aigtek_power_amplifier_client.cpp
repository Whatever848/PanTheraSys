#include "adapters/aigtek/aigtek_power_amplifier_client.h"

#include <cmath>

#include <QElapsedTimer>
#include <QIODevice>
#include <QSerialPort>

namespace panthera::adapters::aigtek {
namespace {

constexpr int kWriteTimeoutMs = 1000;
constexpr int kDefaultResponseTimeoutMs = 3000;
constexpr int kMinimumBaudRate = 1200;
constexpr int kMaximumBaudRate = 115200;
constexpr double kMaximumGain = 30.0;
const QByteArray kLineTerminator("\r\n");

void setError(QString* errorMessage, const QString& text)
{
    if (errorMessage != nullptr) {
        *errorMessage = text;
    }
}

}  // namespace

AigtekPowerAmplifierClient::AigtekPowerAmplifierClient()
    : m_serialPort(std::make_unique<QSerialPort>())
{
}

AigtekPowerAmplifierClient::~AigtekPowerAmplifierClient() = default;

bool AigtekPowerAmplifierClient::open(
    const AigtekPowerAmplifierSerialSettings& settings,
    QString* errorMessage)
{
    const QString port = settings.portName.trimmed();
    if (port.isEmpty()) {
        setError(errorMessage, QStringLiteral("请选择功率放大器串口"));
        return false;
    }
    if (settings.baudRate < kMinimumBaudRate || settings.baudRate > kMaximumBaudRate) {
        setError(errorMessage, QStringLiteral("功率放大器波特率无效：%1").arg(settings.baudRate));
        return false;
    }

    close();
    m_serialPort->setPortName(port);
    m_serialPort->setBaudRate(settings.baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);
    m_responseTimeoutMs = settings.responseTimeoutMs > 0 ? settings.responseTimeoutMs : kDefaultResponseTimeoutMs;

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        setError(errorMessage, QStringLiteral("打开功率放大器串口失败：%1").arg(m_serialPort->errorString()));
        return false;
    }

    m_serialPort->setDataTerminalReady(true);
    m_serialPort->setRequestToSend(true);
    m_serialPort->readAll();
    m_serialPort->clear(QSerialPort::Input);
    return true;
}

void AigtekPowerAmplifierClient::close()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool AigtekPowerAmplifierClient::isOpen() const
{
    return m_serialPort->isOpen();
}

QString AigtekPowerAmplifierClient::portName() const
{
    return m_serialPort->portName();
}

int AigtekPowerAmplifierClient::baudRate() const
{
    return static_cast<int>(m_serialPort->baudRate());
}

bool AigtekPowerAmplifierClient::requestToSend() const
{
    return m_serialPort->isRequestToSend();
}

bool AigtekPowerAmplifierClient::dataTerminalReady() const
{
    return m_serialPort->isDataTerminalReady();
}

bool AigtekPowerAmplifierClient::outputOn(QString* errorMessage, QByteArray* response)
{
    return sendCommand(buildOutputOnCommand(), errorMessage, response);
}

bool AigtekPowerAmplifierClient::outputOff(QString* errorMessage, QByteArray* response)
{
    return sendCommand(buildOutputOffCommand(), errorMessage, response);
}

bool AigtekPowerAmplifierClient::setGain(double gain, QString* errorMessage, QByteArray* response)
{
    QString code;
    if (!encodeGainCode(gain, &code, errorMessage)) {
        return false;
    }
    return sendCommand(buildCommand("AMP", code), errorMessage, response);
}

bool AigtekPowerAmplifierClient::encodeGainCode(double gain, QString* code, QString* errorMessage)
{
    if (code == nullptr) {
        setError(errorMessage, QStringLiteral("缺少放大倍数编码输出参数"));
        return false;
    }
    if (!std::isfinite(gain)) {
        setError(errorMessage, QStringLiteral("放大倍数无效"));
        return false;
    }

    const int encoded = static_cast<int>(std::round(gain * 10.0));
    if (encoded < 0 || encoded > static_cast<int>(std::round(kMaximumGain * 10.0))) {
        setError(errorMessage, QStringLiteral("放大倍数必须在 0.0-%1 倍之间").arg(kMaximumGain, 0, 'f', 1));
        return false;
    }

    *code = QStringLiteral("%1").arg(encoded, 4, 10, QLatin1Char('0'));
    return true;
}

QByteArray AigtekPowerAmplifierClient::buildOutputOnCommand()
{
    return buildCommand("AON", QStringLiteral("0000"));
}

QByteArray AigtekPowerAmplifierClient::buildOutputOffCommand()
{
    return buildCommand("AOF", QStringLiteral("0000"));
}

QByteArray AigtekPowerAmplifierClient::buildSetGainCommand(double gain)
{
    QString code;
    if (!encodeGainCode(gain, &code)) {
        return {};
    }
    return buildCommand("AMP", code);
}

QString AigtekPowerAmplifierClient::bytesToText(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes).trimmed();
}

bool AigtekPowerAmplifierClient::sendCommand(
    const QByteArray& command,
    QString* errorMessage,
    QByteArray* response)
{
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("功率放大器串口未连接"));
        return false;
    }
    if (command.isEmpty()) {
        setError(errorMessage, QStringLiteral("功率放大器指令为空"));
        return false;
    }

    QByteArray wireCommand = command;
    wireCommand.append(kLineTerminator);

    m_serialPort->readAll();
    m_serialPort->clear(QSerialPort::Input);
    const qint64 written = m_serialPort->write(wireCommand);
    if (written != wireCommand.size()) {
        setError(errorMessage,
            QStringLiteral("功率放大器指令写入失败：written=%1 expected=%2，%3")
                .arg(written)
                .arg(wireCommand.size())
                .arg(m_serialPort->errorString()));
        return false;
    }
    if (!m_serialPort->waitForBytesWritten(kWriteTimeoutMs)) {
        setError(errorMessage, QStringLiteral("功率放大器指令发送超时：%1").arg(m_serialPort->errorString()));
        return false;
    }

    QByteArray localResponse;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_responseTimeoutMs) {
        if (m_serialPort->waitForReadyRead(20)) {
            localResponse.append(m_serialPort->readAll());
            if (localResponse.contains('!')) {
                break;
            }
        }
    }
    while (m_serialPort->waitForReadyRead(20)) {
        localResponse.append(m_serialPort->readAll());
    }

    if (response != nullptr) {
        *response = localResponse;
    }
    return validateResponse(localResponse, errorMessage);
}

bool AigtekPowerAmplifierClient::validateResponse(const QByteArray& response, QString* errorMessage) const
{
    const QString text = bytesToText(response);
    if (text.isEmpty()) {
        setError(errorMessage, QStringLiteral("功率放大器未返回响应：timeout=%1 ms，RTS=%2，DTR=%3")
                                   .arg(m_responseTimeoutMs)
                                   .arg(requestToSend() ? QStringLiteral("ON") : QStringLiteral("OFF"),
                                        dataTerminalReady() ? QStringLiteral("ON") : QStringLiteral("OFF")));
        return false;
    }
    if (text.contains(QStringLiteral("SET DONE!"), Qt::CaseInsensitive)) {
        return true;
    }
    if (text.contains(QStringLiteral("OVER RANGE ERROR!"), Qt::CaseInsensitive)) {
        setError(errorMessage, QStringLiteral("功率放大器返回超范围：%1").arg(text));
        return false;
    }
    if (text.contains(QStringLiteral("SET ERROR!"), Qt::CaseInsensitive)) {
        setError(errorMessage, QStringLiteral("功率放大器返回设置错误：%1").arg(text));
        return false;
    }

    setError(errorMessage, QStringLiteral("功率放大器返回未知响应：%1").arg(text));
    return false;
}

QByteArray AigtekPowerAmplifierClient::buildCommand(const char* action, const QString& valueCode)
{
    QByteArray command("ATA,SET,");
    command.append(action);
    command.append(',');
    command.append(valueCode.toLatin1());
    command.append(';');
    return command;
}

}  // namespace panthera::adapters::aigtek
