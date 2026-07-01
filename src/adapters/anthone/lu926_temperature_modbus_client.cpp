#include "adapters/anthone/lu926_temperature_modbus_client.h"

#include "adapters/anthone/lu926_temperature_protocol.h"

#include <algorithm>
#include <cmath>

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>

namespace panthera::adapters::anthone {
namespace {

constexpr quint8 kReadHoldingRegisters = 0x03;
constexpr quint8 kWriteSingleRegister = 0x06;
constexpr quint16 kSet1Register = 0x0000;
constexpr quint16 kPv1Register = 0x0110;

void setError(QString* errorMessage, const QString& text)
{
    if (errorMessage != nullptr) {
        *errorMessage = text;
    }
}

quint8 byteAt(const QByteArray& bytes, qsizetype index)
{
    return static_cast<quint8>(bytes.at(index));
}

void appendByte(QByteArray* bytes, quint8 value)
{
    bytes->append(static_cast<char>(value));
}

void appendBigEndianWord(QByteArray* bytes, quint16 value)
{
    appendByte(bytes, static_cast<quint8>((value >> 8) & 0xFF));
    appendByte(bytes, static_cast<quint8>(value & 0xFF));
}

quint16 littleEndianCrcAtEnd(const QByteArray& bytes)
{
    if (bytes.size() < 2) {
        return 0;
    }
    return static_cast<quint16>(byteAt(bytes, bytes.size() - 2)
        | (byteAt(bytes, bytes.size() - 1) << 8));
}

void appendCrc(QByteArray* bytes)
{
    const quint16 crc = Lu926TemperatureModbusClient::modbusCrc(*bytes);
    appendByte(bytes, static_cast<quint8>(crc & 0xFF));
    appendByte(bytes, static_cast<quint8>((crc >> 8) & 0xFF));
}

QByteArray buildReadRegisterFrame(quint8 address, quint16 startRegister)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kReadHoldingRegisters);
    appendBigEndianWord(&frame, startRegister);
    appendBigEndianWord(&frame, 0x0001);
    appendCrc(&frame);
    return frame;
}

QByteArray buildWriteRegisterFrame(quint8 address, quint16 registerAddress, qint16 value)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kWriteSingleRegister);
    appendBigEndianWord(&frame, registerAddress);
    appendBigEndianWord(&frame, static_cast<quint16>(value));
    appendCrc(&frame);
    return frame;
}

int expectedResponseSizeForFunction(quint8 functionCode)
{
    return functionCode == kReadHoldingRegisters ? 7 : 8;
}

}  // namespace

Lu926TemperatureModbusClient::Lu926TemperatureModbusClient()
    : m_serialPort(std::make_unique<QSerialPort>())
{
    QObject::connect(
        m_serialPort.get(),
        &QSerialPort::errorOccurred,
        [this](QSerialPort::SerialPortError error) {
            if (error != QSerialPort::NoError) {
                appendDebugLog(QStringLiteral("串口错误：%1").arg(m_serialPort->errorString()));
            }
        });
}

Lu926TemperatureModbusClient::~Lu926TemperatureModbusClient() = default;

bool Lu926TemperatureModbusClient::open(const Lu926TemperatureSerialSettings& settings, QString* errorMessage)
{
    const QString port = settings.portName.trimmed();
    if (port.isEmpty()) {
        setError(errorMessage, QStringLiteral("请选择温控 485 串口"));
        return false;
    }
    if (!Lu926TemperatureProtocol::isSupportedBaudRate(settings.baudRate)) {
        setError(errorMessage, QStringLiteral("温控波特率仅支持 1200/2400/4800/9600/19200/38400"));
        return false;
    }

    close();
    m_serialPort->setPortName(port);
    m_serialPort->setBaudRate(settings.baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);
    m_responseTimeoutMs = settings.responseTimeoutMs > 0 ? settings.responseTimeoutMs : 2000;

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        setError(errorMessage, QStringLiteral("打开温控串口失败：%1").arg(m_serialPort->errorString()));
        return false;
    }
    m_serialPort->setDataTerminalReady(false);
    m_serialPort->setRequestToSend(false);
    m_serialPort->readAll();
    m_serialPort->clear(QSerialPort::Input);
    return true;
}

void Lu926TemperatureModbusClient::close()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool Lu926TemperatureModbusClient::isOpen() const
{
    return m_serialPort->isOpen();
}

QString Lu926TemperatureModbusClient::portName() const
{
    return m_serialPort->portName();
}

int Lu926TemperatureModbusClient::baudRate() const
{
    return static_cast<int>(m_serialPort->baudRate());
}

bool Lu926TemperatureModbusClient::requestToSend() const
{
    return m_serialPort->isRequestToSend();
}

bool Lu926TemperatureModbusClient::dataTerminalReady() const
{
    return m_serialPort->isDataTerminalReady();
}

QStringList Lu926TemperatureModbusClient::lastDebugLog() const
{
    return m_lastDebugLog;
}

bool Lu926TemperatureModbusClient::setChannelSetpoint(
    quint8 address,
    int channelIndex,
    double celsius,
    QString* errorMessage,
    QByteArray* response)
{
    if (!validateAddress(address, errorMessage) || !validateChannel(channelIndex, errorMessage)) {
        return false;
    }
    if (!std::isfinite(celsius)) {
        setError(errorMessage, QStringLiteral("温度设定值无效"));
        return false;
    }
    return sendFrame(buildSetChannelSetpointFrame(address, channelIndex, celsius), errorMessage, response);
}

bool Lu926TemperatureModbusClient::setChannel1Setpoint(
    double celsius,
    QString* errorMessage,
    QByteArray* response)
{
    if (!std::isfinite(celsius)) {
        setError(errorMessage, QStringLiteral("温度输入无效"));
        return false;
    }

    const QByteArray request = buildWriteSet1Frame(celsius);
    QByteArray localResponse;
    if (!sendFrame(request, errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    if (localResponse != request) {
        setError(errorMessage, QStringLiteral("设备未原样返回"));
        return false;
    }
    return true;
}

bool Lu926TemperatureModbusClient::readChannelTemperature(
    quint8 address,
    int channelIndex,
    double* celsius,
    QString* errorMessage,
    QByteArray* response)
{
    if (!validateAddress(address, errorMessage) || !validateChannel(channelIndex, errorMessage)) {
        return false;
    }
    QByteArray localResponse;
    if (!sendFrame(buildReadChannelTemperatureFrame(address, channelIndex), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeReadChannelTemperatureResponse(address, channelIndex, localResponse, celsius, errorMessage);
}

bool Lu926TemperatureModbusClient::readChannel1Temperature(
    double* celsius,
    QString* errorMessage,
    QByteArray* response)
{
    QByteArray localResponse;
    if (!sendFrame(buildReadPv1Frame(), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeReadPv1TemperatureResponse(localResponse, celsius, errorMessage);
}

bool Lu926TemperatureModbusClient::readChannel1Setpoint(
    double* celsius,
    QString* errorMessage,
    QByteArray* response)
{
    QByteArray localResponse;
    if (!sendFrame(buildReadSet1Frame(), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeReadSet1Response(localResponse, celsius, errorMessage);
}

QByteArray Lu926TemperatureModbusClient::buildWriteSet1Frame(double celsius)
{
    const qint16 rawValue = Lu926TemperatureProtocol::encodeRegisterTemperature(celsius);
    return buildWriteRegisterFrame(
        static_cast<quint8>(Lu926TemperatureProtocol::kDefaultAddress),
        kSet1Register,
        rawValue);
}

QByteArray Lu926TemperatureModbusClient::buildReadPv1Frame()
{
    return buildReadRegisterFrame(static_cast<quint8>(Lu926TemperatureProtocol::kDefaultAddress), kPv1Register);
}

QByteArray Lu926TemperatureModbusClient::buildReadSet1Frame()
{
    return buildReadRegisterFrame(static_cast<quint8>(Lu926TemperatureProtocol::kDefaultAddress), kSet1Register);
}

bool Lu926TemperatureModbusClient::decodeReadPv1TemperatureResponse(
    const QByteArray& response,
    double* celsius,
    QString* errorMessage)
{
    if (celsius == nullptr) {
        setError(errorMessage, QStringLiteral("读取温度缺少输出参数"));
        return false;
    }
    if (!validateResponse(
            static_cast<quint8>(Lu926TemperatureProtocol::kDefaultAddress),
            kReadHoldingRegisters,
            response,
            errorMessage)) {
        return false;
    }

    const quint16 rawUnsigned = static_cast<quint16>((byteAt(response, 3) << 8) | byteAt(response, 4));
    const qint16 rawSigned = static_cast<qint16>(rawUnsigned);
    *celsius = Lu926TemperatureProtocol::decodeRegisterTemperature(rawSigned);
    return true;
}

bool Lu926TemperatureModbusClient::decodeReadSet1Response(
    const QByteArray& response,
    double* celsius,
    QString* errorMessage)
{
    if (celsius == nullptr) {
        setError(errorMessage, QStringLiteral("读取设定温度缺少输出参数"));
        return false;
    }
    if (!validateResponse(
            static_cast<quint8>(Lu926TemperatureProtocol::kDefaultAddress),
            kReadHoldingRegisters,
            response,
            errorMessage)) {
        return false;
    }

    const quint16 rawUnsigned = static_cast<quint16>((byteAt(response, 3) << 8) | byteAt(response, 4));
    const qint16 rawSigned = static_cast<qint16>(rawUnsigned);
    *celsius = Lu926TemperatureProtocol::decodeRegisterTemperature(rawSigned);
    return true;
}

QByteArray Lu926TemperatureModbusClient::buildSetChannelSetpointFrame(quint8 address, int channelIndex, double celsius)
{
    const int registerAddress = Lu926TemperatureProtocol::setpointRegister(channelIndex);
    const qint16 rawValue = Lu926TemperatureProtocol::encodeRegisterTemperature(celsius);
    return buildWriteRegisterFrame(address, static_cast<quint16>(registerAddress), rawValue);
}

QByteArray Lu926TemperatureModbusClient::buildReadChannelTemperatureFrame(quint8 address, int channelIndex)
{
    const int registerAddress = Lu926TemperatureProtocol::processValueRegister(channelIndex);
    return buildReadRegisterFrame(address, static_cast<quint16>(registerAddress));
}

bool Lu926TemperatureModbusClient::decodeReadChannelTemperatureResponse(
    quint8 address,
    int channelIndex,
    const QByteArray& response,
    double* celsius,
    QString* errorMessage)
{
    if (celsius == nullptr) {
        setError(errorMessage, QStringLiteral("读取温度缺少输出参数"));
        return false;
    }
    if (!validateAddress(address, errorMessage) || !validateChannel(channelIndex, errorMessage)) {
        return false;
    }
    if (!validateResponse(address, kReadHoldingRegisters, response, errorMessage)) {
        return false;
    }

    const quint16 rawUnsigned = static_cast<quint16>((byteAt(response, 3) << 8) | byteAt(response, 4));
    const qint16 rawSigned = static_cast<qint16>(rawUnsigned);
    *celsius = Lu926TemperatureProtocol::decodeRegisterTemperature(rawSigned);
    return true;
}

quint16 Lu926TemperatureModbusClient::modbusCrc(const QByteArray& frameWithoutCrc)
{
    quint16 crc = 0xFFFF;
    for (const char value : frameWithoutCrc) {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit) {
            const bool carry = (crc & 0x0001) != 0;
            crc >>= 1;
            if (carry) {
                crc ^= 0xA001;
            }
        }
    }
    return crc;
}

bool Lu926TemperatureModbusClient::frameHasValidCrc(const QByteArray& frame)
{
    if (frame.size() < 3) {
        return false;
    }
    const QByteArray payload = frame.left(frame.size() - 2);
    return modbusCrc(payload) == littleEndianCrcAtEnd(frame);
}

QString Lu926TemperatureModbusClient::frameToHex(const QByteArray& frame)
{
    return QString::fromLatin1(frame.toHex(' ').toUpper());
}

bool Lu926TemperatureModbusClient::sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response)
{
    clearDebugLog();
    appendDebugLog(QStringLiteral("TX len=%1").arg(request.size()));
    appendDebugLog(QStringLiteral("TX: %1").arg(frameToHex(request)));

    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("温控串口未连接"));
        return false;
    }
    if (request.size() < 4) {
        setError(errorMessage, QStringLiteral("温控指令为空或不完整"));
        return false;
    }

    const quint8 address = byteAt(request, 0);
    const quint8 functionCode = byteAt(request, 1);
    const int expectedMinimumResponseSize = expectedResponseSizeForFunction(functionCode);

    m_serialPort->readAll();
    m_serialPort->clear(QSerialPort::Input);
    const qint64 written = m_serialPort->write(request);
    if (written != request.size()) {
        setError(errorMessage,
            QStringLiteral("发送失败：写入字节数不完整，written=%1 expected=%2，%3")
                .arg(written)
                .arg(request.size())
                .arg(m_serialPort->errorString()));
        return false;
    }
    if (!m_serialPort->waitForBytesWritten(1000)) {
        setError(errorMessage, QStringLiteral("发送失败：waitForBytesWritten 超时：%1").arg(m_serialPort->errorString()));
        return false;
    }

    QByteArray localResponse;
    QElapsedTimer timer;
    timer.start();
    qsizetype lastAvailable = -1;
    while (timer.elapsed() < m_responseTimeoutMs) {
        const qsizetype available = m_serialPort->bytesAvailable();
        if (available != lastAvailable) {
            appendDebugLog(QStringLiteral("RX wait bytesAvailable=%1").arg(available));
            lastAvailable = available;
        }
        if (available > 0) {
            const QByteArray part = m_serialPort->readAll();
            if (!part.isEmpty()) {
                localResponse.append(part);
                appendDebugLog(QStringLiteral("RX part len=%1, hex=%2")
                                   .arg(part.size())
                                   .arg(frameToHex(part)));
            }
            if (localResponse.size() >= expectedMinimumResponseSize) {
                break;
            }
        } else {
            m_serialPort->waitForReadyRead(10);
        }
    }
    while (m_serialPort->waitForReadyRead(20)) {
        const QByteArray part = m_serialPort->readAll();
        if (!part.isEmpty()) {
            localResponse.append(part);
            appendDebugLog(QStringLiteral("RX part len=%1, hex=%2")
                               .arg(part.size())
                               .arg(frameToHex(part)));
        }
    }

    appendDebugLog(QStringLiteral("RX total len=%1").arg(localResponse.size()));
    appendDebugLog(QStringLiteral("RX total: %1").arg(frameToHex(localResponse)));
    if (response != nullptr) {
        *response = localResponse;
    }

    if (localResponse.size() < expectedMinimumResponseSize) {
        appendDebugLog(QStringLiteral("RX diagnostics: RTS=%1, DTR=%2")
                           .arg(requestToSend() ? QStringLiteral("true") : QStringLiteral("false"),
                                dataTerminalReady() ? QStringLiteral("true") : QStringLiteral("false")));
        appendDebugLog(QStringLiteral("RX diagnostics: 温控串口未连接 readyRead 读数据槽，当前为同步 readAll"));
        appendDebugLog(QStringLiteral("RX diagnostics: 仅发送前 clear(Input)，发送后未 clear(Input/AllDirections)"));
        setError(errorMessage,
            QStringLiteral("超时，RX len=%1，RX=%2")
                .arg(localResponse.size())
                .arg(frameToHex(localResponse)));
        return false;
    }

    const QByteArray parsedResponse = matchingResponseFrame(address, functionCode, localResponse);
    if (response != nullptr) {
        *response = parsedResponse;
    }
    return validateResponse(address, functionCode, parsedResponse, errorMessage);
}

void Lu926TemperatureModbusClient::clearDebugLog()
{
    m_lastDebugLog.clear();
}

void Lu926TemperatureModbusClient::appendDebugLog(const QString& text)
{
    m_lastDebugLog.push_back(text);
}

bool Lu926TemperatureModbusClient::validateAddress(quint8 address, QString* errorMessage)
{
    if (address == 0 || address > 247) {
        setError(errorMessage, QStringLiteral("温控 485 地址必须在 1-247 之间"));
        return false;
    }
    return true;
}

bool Lu926TemperatureModbusClient::validateChannel(int channelIndex, QString* errorMessage)
{
    if (!Lu926TemperatureProtocol::isValidChannelIndex(channelIndex)) {
        setError(errorMessage, QStringLiteral("温控通道必须在 1-6 之间"));
        return false;
    }
    return true;
}

bool Lu926TemperatureModbusClient::validateResponse(
    quint8 address,
    quint8 functionCode,
    const QByteArray& response,
    QString* errorMessage)
{
    if (response.size() < 5) {
        setError(errorMessage, QStringLiteral("温控响应长度不足：%1").arg(frameToHex(response)));
        return false;
    }
    if (!frameHasValidCrc(response)) {
        setError(errorMessage, QStringLiteral("温控响应 CRC 校验失败：%1").arg(frameToHex(response)));
        return false;
    }
    if (byteAt(response, 0) != address) {
        setError(errorMessage, QStringLiteral("温控响应地址不匹配：期望 %1，收到 %2")
                                   .arg(address, 2, 16, QLatin1Char('0'))
                                   .arg(byteAt(response, 0), 2, 16, QLatin1Char('0')));
        return false;
    }

    const quint8 responseFunction = byteAt(response, 1);
    if (responseFunction == (functionCode | 0x80)) {
        const quint8 exceptionCode = response.size() >= 3 ? byteAt(response, 2) : 0;
        setError(errorMessage, QStringLiteral("温控返回 Modbus 异常码：0x%1")
                                   .arg(exceptionCode, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    if (responseFunction != functionCode) {
        setError(errorMessage, QStringLiteral("温控响应功能码不匹配：期望 0x%1，收到 0x%2")
                                   .arg(functionCode, 2, 16, QLatin1Char('0'))
                                   .arg(responseFunction, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }

    const int expectedResponseSize = expectedResponseSizeForFunction(functionCode);
    if (response.size() != expectedResponseSize) {
        setError(errorMessage, QStringLiteral("温控响应长度异常：期望 %1 字节，收到 %2 字节，帧 %3")
                                   .arg(expectedResponseSize)
                                   .arg(response.size())
                                   .arg(frameToHex(response)));
        return false;
    }
    if (functionCode == kReadHoldingRegisters && byteAt(response, 2) != 0x02) {
        setError(errorMessage, QStringLiteral("温控读取响应格式不正确：%1").arg(frameToHex(response)));
        return false;
    }
    return true;
}

QByteArray Lu926TemperatureModbusClient::matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes)
{
    const int normalResponseSize = expectedResponseSizeForFunction(functionCode);
    for (qsizetype offset = 0; offset < bytes.size(); ++offset) {
        if (byteAt(bytes, offset) != address || offset + 5 > bytes.size()) {
            continue;
        }

        const quint8 responseFunction = byteAt(bytes, offset + 1);
        const int frameSize = responseFunction == (functionCode | 0x80)
            ? 5
            : (responseFunction == functionCode ? normalResponseSize : 0);
        if (frameSize == 0 || offset + frameSize > bytes.size()) {
            continue;
        }

        const QByteArray candidate = bytes.mid(offset, frameSize);
        if (frameHasValidCrc(candidate)) {
            return candidate;
        }
    }
    return bytes;
}

}  // namespace panthera::adapters::anthone
