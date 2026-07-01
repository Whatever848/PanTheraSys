#include "adapters/waterpump/water_pump_modbus_client.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QElapsedTimer>
#include <QSerialPort>

namespace panthera::adapters::waterpump {
namespace {

constexpr quint16 kFlowRegister = 0x400B;
constexpr quint16 kRunDurationRegister = 0x4005;
constexpr quint16 kRealtimeRunDurationRegister = 0x4007;
constexpr quint16 kStartStopCoil = 0x0001;
constexpr quint16 kDirectionCoil = 0x0002;
constexpr quint8 kReadHoldingRegisters = 0x03;
constexpr quint8 kWriteSingleCoil = 0x05;
constexpr quint8 kWriteMultipleRegisters = 0x10;

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

quint16 littleEndianCrcAtEnd(const QByteArray& bytes)
{
    if (bytes.size() < 2) {
        return 0;
    }
    return static_cast<quint16>(byteAt(bytes, bytes.size() - 2)
        | (byteAt(bytes, bytes.size() - 1) << 8));
}

QString crcHex(quint16 crc)
{
    return QStringLiteral("0x%1").arg(crc, 4, 16, QLatin1Char('0')).toUpper();
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

void appendCrc(QByteArray* bytes)
{
    const quint16 crc = WaterPumpModbusClient::modbusCrc(*bytes);
    appendByte(bytes, static_cast<quint8>(crc & 0xFF));
    appendByte(bytes, static_cast<quint8>((crc >> 8) & 0xFF));
}

QByteArray encodeWordSwappedFloat(double value)
{
    const float singlePrecisionValue = static_cast<float>(value);
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(singlePrecisionValue));
    std::memcpy(&bits, &singlePrecisionValue, sizeof(bits));

    QByteArray payload;
    appendBigEndianWord(&payload, static_cast<quint16>(bits & 0xFFFF));
    appendBigEndianWord(&payload, static_cast<quint16>((bits >> 16) & 0xFFFF));
    return payload;
}

QByteArray encodeWordSwappedUInt32(quint32 value)
{
    QByteArray payload;
    appendBigEndianWord(&payload, static_cast<quint16>(value & 0xFFFF));
    appendBigEndianWord(&payload, static_cast<quint16>((value >> 16) & 0xFFFF));
    return payload;
}

quint32 decodeWordSwappedUInt32(const QByteArray& response, qsizetype offset)
{
    const quint16 lowWord = static_cast<quint16>((byteAt(response, offset) << 8) | byteAt(response, offset + 1));
    const quint16 highWord = static_cast<quint16>((byteAt(response, offset + 2) << 8) | byteAt(response, offset + 3));
    return (static_cast<quint32>(highWord) << 16) | lowWord;
}

QByteArray buildReadRegistersFrame(quint8 address, quint16 startRegister)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kReadHoldingRegisters);
    appendBigEndianWord(&frame, startRegister);
    appendBigEndianWord(&frame, 0x0002);
    appendCrc(&frame);
    return frame;
}

QByteArray buildWriteRegistersFrame(quint8 address, quint16 startRegister, const QByteArray& payload)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kWriteMultipleRegisters);
    appendBigEndianWord(&frame, startRegister);
    appendBigEndianWord(&frame, 0x0002);
    appendByte(&frame, 0x04);
    frame.append(payload);
    appendCrc(&frame);
    return frame;
}

QByteArray buildWriteCoilFrame(quint8 address, quint16 coilAddress, bool enabled)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kWriteSingleCoil);
    appendBigEndianWord(&frame, coilAddress);
    appendBigEndianWord(&frame, enabled ? 0xFF00 : 0x0000);
    appendCrc(&frame);
    return frame;
}

int expectedResponseSizeForFunction(quint8 functionCode)
{
    return functionCode == kReadHoldingRegisters ? 9 : 8;
}

}  // namespace

WaterPumpModbusClient::WaterPumpModbusClient()
    : m_serialPort(std::make_unique<QSerialPort>())
{
}

WaterPumpModbusClient::~WaterPumpModbusClient() = default;

bool WaterPumpModbusClient::open(const WaterPumpSerialSettings& settings, QString* errorMessage)
{
    const QString port = settings.portName.trimmed();
    if (port.isEmpty()) {
        setError(errorMessage, QStringLiteral("请选择水泵 485 串口"));
        return false;
    }

    close();
    m_serialPort->setPortName(port);
    m_serialPort->setBaudRate(settings.baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);
    m_responseTimeoutMs = settings.responseTimeoutMs > 0 ? settings.responseTimeoutMs : 800;

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        setError(errorMessage, QStringLiteral("打开水泵串口失败：%1").arg(m_serialPort->errorString()));
        return false;
    }
    m_serialPort->clear(QSerialPort::AllDirections);
    return true;
}

void WaterPumpModbusClient::close()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool WaterPumpModbusClient::isOpen() const
{
    return m_serialPort->isOpen();
}

QString WaterPumpModbusClient::portName() const
{
    return m_serialPort->portName();
}

int WaterPumpModbusClient::baudRate() const
{
    return static_cast<int>(m_serialPort->baudRate());
}

bool WaterPumpModbusClient::setFlowMlPerMin(quint8 address, double flowMlPerMin, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    if (!std::isfinite(flowMlPerMin) || flowMlPerMin < kMinimumFlowMlPerMin || flowMlPerMin > kMaximumFlowMlPerMin) {
        setError(errorMessage, QStringLiteral("水泵流速必须在 %1-%2 mL/min 之间")
                                   .arg(kMinimumFlowMlPerMin)
                                   .arg(kMaximumFlowMlPerMin));
        return false;
    }
    return sendFrame(buildSetFlowFrame(address, flowMlPerMin), errorMessage, response);
}

bool WaterPumpModbusClient::readFlowMlPerMin(quint8 address, double* flowMlPerMin, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    QByteArray localResponse;
    if (!sendFrame(buildReadFlowFrame(address), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeFlowResponse(address, localResponse, flowMlPerMin, errorMessage);
}

bool WaterPumpModbusClient::setRunDurationSeconds(quint8 address, int seconds, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    if (seconds < 0 || seconds > kMaximumRunDurationSeconds) {
        setError(errorMessage, QStringLiteral("水泵运行时长必须在 0-%1 s 之间，0 表示一直运行")
                                   .arg(kMaximumRunDurationSeconds));
        return false;
    }
    return sendFrame(buildSetRunDurationFrame(address, seconds), errorMessage, response);
}

bool WaterPumpModbusClient::readConfiguredRunDurationSeconds(quint8 address, double* seconds, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    QByteArray localResponse;
    if (!sendFrame(buildReadConfiguredRunDurationFrame(address), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeDurationResponse(address, localResponse, seconds, errorMessage);
}

bool WaterPumpModbusClient::readRealtimeRunDurationSeconds(quint8 address, double* seconds, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    QByteArray localResponse;
    if (!sendFrame(buildReadRealtimeRunDurationFrame(address), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeDurationResponse(address, localResponse, seconds, errorMessage);
}

bool WaterPumpModbusClient::startPump(quint8 address, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    return sendFrame(buildStartFrame(address), errorMessage, response);
}

bool WaterPumpModbusClient::stopPump(quint8 address, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    return sendFrame(buildStopFrame(address), errorMessage, response);
}

bool WaterPumpModbusClient::setClockwise(quint8 address, QString* errorMessage, QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }
    return sendFrame(buildClockwiseFrame(address), errorMessage, response);
}

QByteArray WaterPumpModbusClient::buildSetFlowFrame(quint8 address, double flowMlPerMin)
{
    return buildWriteRegistersFrame(address, kFlowRegister, encodeWordSwappedFloat(flowMlPerMin));
}

QByteArray WaterPumpModbusClient::buildReadFlowFrame(quint8 address)
{
    return buildReadRegistersFrame(address, kFlowRegister);
}

QByteArray WaterPumpModbusClient::buildSetRunDurationFrame(quint8 address, int seconds)
{
    const quint64 milliseconds = seconds <= 0 ? 0 : static_cast<quint64>(seconds) * 1000;
    const quint32 boundedMilliseconds = milliseconds > std::numeric_limits<quint32>::max()
        ? std::numeric_limits<quint32>::max()
        : static_cast<quint32>(milliseconds);
    return buildWriteRegistersFrame(address, kRunDurationRegister, encodeWordSwappedUInt32(boundedMilliseconds));
}

QByteArray WaterPumpModbusClient::buildReadConfiguredRunDurationFrame(quint8 address)
{
    return buildReadRegistersFrame(address, kRunDurationRegister);
}

QByteArray WaterPumpModbusClient::buildReadRealtimeRunDurationFrame(quint8 address)
{
    return buildReadRegistersFrame(address, kRealtimeRunDurationRegister);
}

QByteArray WaterPumpModbusClient::buildStartFrame(quint8 address)
{
    return buildWriteCoilFrame(address, kStartStopCoil, true);
}

QByteArray WaterPumpModbusClient::buildStopFrame(quint8 address)
{
    return buildWriteCoilFrame(address, kStartStopCoil, false);
}

QByteArray WaterPumpModbusClient::buildClockwiseFrame(quint8 address)
{
    return buildWriteCoilFrame(address, kDirectionCoil, true);
}

quint16 WaterPumpModbusClient::modbusCrc(const QByteArray& frameWithoutCrc)
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

bool WaterPumpModbusClient::frameHasValidCrc(const QByteArray& frame)
{
    if (frame.size() < 3) {
        return false;
    }
    const QByteArray payload = frame.left(frame.size() - 2);
    const quint16 expectedCrc = modbusCrc(payload);
    const quint16 actualCrc = littleEndianCrcAtEnd(frame);
    return expectedCrc == actualCrc;
}

QString WaterPumpModbusClient::frameToHex(const QByteArray& frame)
{
    return QString::fromLatin1(frame.toHex(' ').toUpper());
}

QByteArray WaterPumpModbusClient::matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes)
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

bool WaterPumpModbusClient::sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response)
{
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("水泵串口未连接"));
        return false;
    }
    if (request.size() < 4) {
        setError(errorMessage, QStringLiteral("水泵指令为空或不完整"));
        return false;
    }

    const quint8 address = byteAt(request, 0);
    const quint8 functionCode = byteAt(request, 1);
    const int expectedMinimumResponseSize = expectedResponseSizeForFunction(functionCode);

    m_serialPort->clear(QSerialPort::AllDirections);
    const qint64 written = m_serialPort->write(request);
    if (written != request.size()) {
        setError(errorMessage, QStringLiteral("水泵指令写入失败：%1").arg(m_serialPort->errorString()));
        return false;
    }
    if (!m_serialPort->waitForBytesWritten(m_responseTimeoutMs)) {
        setError(errorMessage, QStringLiteral("水泵指令发送超时：%1").arg(m_serialPort->errorString()));
        return false;
    }

    QByteArray localResponse;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_responseTimeoutMs && localResponse.size() < expectedMinimumResponseSize) {
        const int remainingMs = std::max(1, m_responseTimeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_serialPort->waitForReadyRead(remainingMs)) {
            break;
        }
        localResponse.append(m_serialPort->readAll());
    }
    while (m_serialPort->waitForReadyRead(20)) {
        localResponse.append(m_serialPort->readAll());
    }

    if (localResponse.isEmpty()) {
        if (functionCode == kWriteSingleCoil) {
            if (response != nullptr) {
                response->clear();
            }
            setError(errorMessage, QString());
            return true;
        }
        setError(errorMessage, QStringLiteral("水泵未返回响应"));
        return false;
    }

    const QByteArray parsedResponse = matchingResponseFrame(address, functionCode, localResponse);
    if (response != nullptr) {
        *response = parsedResponse;
    }
    return validateResponse(address, functionCode, parsedResponse, errorMessage);
}

bool WaterPumpModbusClient::validateAddress(quint8 address, QString* errorMessage)
{
    if (address == 0 || address > 247) {
        setError(errorMessage, QStringLiteral("水泵 485 地址必须在 1-247 之间"));
        return false;
    }
    return true;
}

bool WaterPumpModbusClient::validateResponse(quint8 address, quint8 functionCode, const QByteArray& response, QString* errorMessage)
{
    if (response.size() < 5) {
        setError(errorMessage, QStringLiteral("水泵响应长度不足，数据不完整：%1").arg(frameToHex(response)));
        return false;
    }
    if (!frameHasValidCrc(response)) {
        const QByteArray payload = response.left(response.size() - 2);
        const quint16 expectedCrc = modbusCrc(payload);
        const quint16 actualCrc = littleEndianCrcAtEnd(response);
        setError(errorMessage, QStringLiteral("水泵响应 CRC 校验失败，数据可能不完整或被干扰：期望 %1，收到 %2，帧 %3")
                                   .arg(crcHex(expectedCrc), crcHex(actualCrc), frameToHex(response)));
        return false;
    }
    if (byteAt(response, 0) != address) {
        setError(errorMessage, QStringLiteral("水泵响应地址不匹配：期望 %1，收到 %2")
                                   .arg(address, 2, 16, QLatin1Char('0'))
                                   .arg(byteAt(response, 0), 2, 16, QLatin1Char('0')));
        return false;
    }

    const quint8 responseFunction = byteAt(response, 1);
    if (responseFunction == (functionCode | 0x80)) {
        if (response.size() != 5) {
            setError(errorMessage, QStringLiteral("水泵异常响应长度不正确：期望 5 字节，收到 %1 字节，帧 %2")
                                       .arg(response.size())
                                       .arg(frameToHex(response)));
            return false;
        }
        const quint8 exceptionCode = response.size() >= 3 ? byteAt(response, 2) : 0;
        setError(errorMessage, QStringLiteral("水泵返回 Modbus 异常码：0x%1")
                                   .arg(exceptionCode, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    if (responseFunction != functionCode) {
        setError(errorMessage, QStringLiteral("水泵响应功能码不匹配：期望 0x%1，收到 0x%2")
                                   .arg(functionCode, 2, 16, QLatin1Char('0'))
                                   .arg(responseFunction, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    const int expectedResponseSize = expectedResponseSizeForFunction(functionCode);
    if (response.size() != expectedResponseSize) {
        setError(errorMessage, QStringLiteral("水泵响应长度异常，数据可能不完整或粘包：期望 %1 字节，收到 %2 字节，帧 %3")
                                   .arg(expectedResponseSize)
                                   .arg(response.size())
                                   .arg(frameToHex(response)));
        return false;
    }
    if (functionCode == kReadHoldingRegisters && byteAt(response, 2) != 0x04) {
        setError(errorMessage, QStringLiteral("水泵读取响应格式不正确：%1").arg(frameToHex(response)));
        return false;
    }
    return true;
}

bool WaterPumpModbusClient::decodeFlowResponse(quint8 address, const QByteArray& response, double* flowMlPerMin, QString* errorMessage)
{
    if (flowMlPerMin == nullptr) {
        setError(errorMessage, QStringLiteral("读取流速缺少输出参数"));
        return false;
    }
    if (!validateResponse(address, kReadHoldingRegisters, response, errorMessage)) {
        return false;
    }

    const quint32 bits = decodeWordSwappedUInt32(response, 3);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    *flowMlPerMin = static_cast<double>(value);
    return true;
}

bool WaterPumpModbusClient::decodeDurationResponse(quint8 address, const QByteArray& response, double* seconds, QString* errorMessage)
{
    if (seconds == nullptr) {
        setError(errorMessage, QStringLiteral("读取运行时长缺少输出参数"));
        return false;
    }
    if (!validateResponse(address, kReadHoldingRegisters, response, errorMessage)) {
        return false;
    }

    const quint32 milliseconds = decodeWordSwappedUInt32(response, 3);
    *seconds = milliseconds / 1000.0;
    return true;
}

}  // namespace panthera::adapters::waterpump
