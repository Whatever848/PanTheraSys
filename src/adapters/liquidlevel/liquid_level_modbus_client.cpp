#include "adapters/liquidlevel/liquid_level_modbus_client.h"

#include <algorithm>

#include <QElapsedTimer>
#include <QSerialPort>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace panthera::adapters::liquidlevel {
namespace {

constexpr quint16 kLevelRegister = 0x0000;
constexpr quint16 kLevelRegisterCount = 0x0001;
constexpr quint8 kReadHoldingRegisters = 0x03;
constexpr quint8 kReadAddressProbeAddress = 0xEE;
constexpr quint8 kReadAddressFunction = 0x07;
constexpr quint16 kDeviceAddressRegister = 0x0004;
constexpr quint16 kDeviceAddressRegisterCount = 0x0001;
constexpr int kReadAddressResponseSize = 8;
constexpr int kReadLevelResponseSize = 7;

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
    const quint16 crc = LiquidLevelModbusClient::modbusCrc(*bytes);
    appendByte(bytes, static_cast<quint8>(crc & 0xFF));
    appendByte(bytes, static_cast<quint8>((crc >> 8) & 0xFF));
}

QByteArray buildReadRegistersFrame(quint8 address, quint16 startRegister, quint16 registerCount)
{
    QByteArray frame;
    appendByte(&frame, address);
    appendByte(&frame, kReadHoldingRegisters);
    appendBigEndianWord(&frame, startRegister);
    appendBigEndianWord(&frame, registerCount);
    appendCrc(&frame);
    return frame;
}

int expectedResponseSizeForFunction(quint8 functionCode)
{
    return functionCode == kReadHoldingRegisters ? kReadLevelResponseSize : 5;
}

#ifdef Q_OS_WIN
bool nativeHandleIsOpen(void* handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

HANDLE toNativeHandle(void* handle)
{
    return static_cast<HANDLE>(handle);
}

QString windowsErrorText(DWORD errorCode)
{
    wchar_t* messageBuffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);
    QString message = size > 0 && messageBuffer != nullptr
        ? QString::fromWCharArray(messageBuffer, static_cast<int>(size)).trimmed()
        : QStringLiteral("未知错误");
    if (messageBuffer != nullptr) {
        LocalFree(messageBuffer);
    }
    return QStringLiteral("%1（Win32 %2）").arg(message).arg(errorCode);
}

QString nativeSerialDevicePath(const QString& portName)
{
    const QString trimmed = portName.trimmed();
    return trimmed.startsWith(QStringLiteral("\\\\.\\"))
        ? trimmed
        : QStringLiteral("\\\\.\\%1").arg(trimmed);
}
#endif

}  // namespace

LiquidLevelModbusClient::LiquidLevelModbusClient()
    : m_serialPort(std::make_unique<QSerialPort>())
{
}

LiquidLevelModbusClient::~LiquidLevelModbusClient() = default;

bool LiquidLevelModbusClient::open(const LiquidLevelSerialSettings& settings, QString* errorMessage)
{
    const QString port = settings.portName.trimmed();
    if (port.isEmpty()) {
        setError(errorMessage, QStringLiteral("请选择液位传感器 485 串口"));
        return false;
    }

    close();
    m_responseTimeoutMs = settings.responseTimeoutMs > 0 ? settings.responseTimeoutMs : 800;

#ifdef Q_OS_WIN
    m_nativePortName = port;
    m_nativeBaudRate = settings.baudRate;

    const QString devicePath = nativeSerialDevicePath(port);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(devicePath.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(errorMessage, QStringLiteral("打开液位传感器串口失败：%1").arg(windowsErrorText(GetLastError())));
        m_nativeHandle = nullptr;
        return false;
    }

    SetupComm(handle, 4096, 4096);

    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        const DWORD errorCode = GetLastError();
        CloseHandle(handle);
        m_nativeHandle = nullptr;
        setError(errorMessage, QStringLiteral("读取液位传感器串口参数失败：%1").arg(windowsErrorText(errorCode)));
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(settings.baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(handle, &dcb)) {
        const DWORD errorCode = GetLastError();
        CloseHandle(handle);
        m_nativeHandle = nullptr;
        setError(errorMessage, QStringLiteral("设置液位传感器串口参数失败：%1").arg(windowsErrorText(errorCode)));
        return false;
    }

    COMMTIMEOUTS timeouts;
    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(m_responseTimeoutMs);
    if (!SetCommTimeouts(handle, &timeouts)) {
        const DWORD errorCode = GetLastError();
        CloseHandle(handle);
        m_nativeHandle = nullptr;
        setError(errorMessage, QStringLiteral("设置液位传感器串口超时失败：%1").arg(windowsErrorText(errorCode)));
        return false;
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    m_nativeHandle = handle;
    return true;
#else
    m_serialPort->setPortName(port);
    m_serialPort->setBaudRate(settings.baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        setError(errorMessage, QStringLiteral("打开液位传感器串口失败：%1").arg(m_serialPort->errorString()));
        return false;
    }
    m_serialPort->clear(QSerialPort::AllDirections);
    return true;
#endif
}

void LiquidLevelModbusClient::close()
{
#ifdef Q_OS_WIN
    if (nativeHandleIsOpen(m_nativeHandle)) {
        CloseHandle(toNativeHandle(m_nativeHandle));
        m_nativeHandle = nullptr;
    }
#endif
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool LiquidLevelModbusClient::isOpen() const
{
#ifdef Q_OS_WIN
    if (nativeHandleIsOpen(m_nativeHandle)) {
        return true;
    }
#endif
    return m_serialPort->isOpen();
}

QString LiquidLevelModbusClient::portName() const
{
#ifdef Q_OS_WIN
    if (!m_nativePortName.isEmpty()) {
        return m_nativePortName;
    }
#endif
    return m_serialPort->portName();
}

int LiquidLevelModbusClient::baudRate() const
{
#ifdef Q_OS_WIN
    if (m_nativeBaudRate > 0) {
        return m_nativeBaudRate;
    }
#endif
    return static_cast<int>(m_serialPort->baudRate());
}

bool LiquidLevelModbusClient::readDeviceAddress(
    quint8* address,
    QString* errorMessage,
    QByteArray* response)
{
    if (address == nullptr) {
        setError(errorMessage, QStringLiteral("读取液位传感器地址缺少输出参数"));
        return false;
    }

    QByteArray localResponse;
    if (!sendRawFrame(buildReadAddressFrame(), kReadAddressResponseSize, errorMessage, &localResponse)) {
        return false;
    }

    const QByteArray parsedResponse = matchingReadAddressResponseFrame(localResponse);
    if (response != nullptr) {
        *response = parsedResponse;
    }
    return decodeReadAddressResponse(parsedResponse, address, errorMessage);
}

bool LiquidLevelModbusClient::readLevelMillimeters(
    quint8 address,
    double* millimeters,
    QString* errorMessage,
    QByteArray* response)
{
    if (!validateAddress(address, errorMessage)) {
        return false;
    }

    QByteArray localResponse;
    if (!sendFrame(buildReadLevelFrame(address), errorMessage, &localResponse)) {
        return false;
    }
    if (response != nullptr) {
        *response = localResponse;
    }
    return decodeReadLevelResponse(address, localResponse, millimeters, errorMessage);
}

QByteArray LiquidLevelModbusClient::buildReadAddressFrame()
{
    QByteArray frame;
    appendByte(&frame, kReadAddressProbeAddress);
    appendByte(&frame, kReadAddressFunction);
    appendBigEndianWord(&frame, kDeviceAddressRegister);
    appendBigEndianWord(&frame, kDeviceAddressRegisterCount);
    appendCrc(&frame);
    return frame;
}

bool LiquidLevelModbusClient::decodeReadAddressResponse(
    const QByteArray& response,
    quint8* address,
    QString* errorMessage)
{
    if (address == nullptr) {
        setError(errorMessage, QStringLiteral("读取液位传感器地址缺少输出参数"));
        return false;
    }
    if (response.size() != kReadAddressResponseSize) {
        setError(errorMessage, QStringLiteral("液位传感器地址响应长度异常：期望 %1 字节，收到 %2 字节，帧 %3")
                                   .arg(kReadAddressResponseSize)
                                   .arg(response.size())
                                   .arg(frameToHex(response)));
        return false;
    }
    if (!frameHasValidCrc(response)) {
        const QByteArray payload = response.left(response.size() - 2);
        const quint16 expectedCrc = modbusCrc(payload);
        const quint16 actualCrc = littleEndianCrcAtEnd(response);
        setError(errorMessage, QStringLiteral("液位传感器地址响应 CRC 校验失败：期望 %1，收到 %2，帧 %3")
                                   .arg(crcHex(expectedCrc), crcHex(actualCrc), frameToHex(response)));
        return false;
    }
    if (byteAt(response, 0) != kReadAddressProbeAddress || byteAt(response, 1) != kReadAddressFunction
        || byteAt(response, 2) != 0x00 || byteAt(response, 3) != kDeviceAddressRegister) {
        setError(errorMessage, QStringLiteral("液位传感器地址响应格式不正确：%1").arg(frameToHex(response)));
        return false;
    }

    const quint16 rawAddress = static_cast<quint16>((byteAt(response, 4) << 8) | byteAt(response, 5));
    if (rawAddress == 0 || rawAddress > 247) {
        setError(errorMessage, QStringLiteral("液位传感器返回的 485 地址超出 1-247：%1").arg(rawAddress));
        return false;
    }

    *address = static_cast<quint8>(rawAddress);
    return true;
}

QByteArray LiquidLevelModbusClient::buildReadLevelFrame(quint8 address)
{
    return buildReadRegistersFrame(address, kLevelRegister, kLevelRegisterCount);
}

bool LiquidLevelModbusClient::decodeReadLevelResponse(
    quint8 address,
    const QByteArray& response,
    double* millimeters,
    QString* errorMessage)
{
    if (millimeters == nullptr) { 
        setError(errorMessage, QStringLiteral("读取液位缺少输出参数"));
        return false;
    }
    if (!validateResponse(address, kReadHoldingRegisters, response, errorMessage)) {
        return false;
    }

    const quint16 rawValue = static_cast<quint16>((byteAt(response, 3) << 8) | byteAt(response, 4));
    *millimeters = rawValue / 10.0;
    return true;
}

quint16 LiquidLevelModbusClient::modbusCrc(const QByteArray& frameWithoutCrc)
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

bool LiquidLevelModbusClient::frameHasValidCrc(const QByteArray& frame)
{
    if (frame.size() < 3) {
        return false;
    }
    const QByteArray payload = frame.left(frame.size() - 2);
    const quint16 expectedCrc = modbusCrc(payload);
    const quint16 actualCrc = littleEndianCrcAtEnd(frame);
    return expectedCrc == actualCrc;
}

QString LiquidLevelModbusClient::frameToHex(const QByteArray& frame)
{
    return QString::fromLatin1(frame.toHex(' ').toUpper());
}

QByteArray LiquidLevelModbusClient::matchingReadAddressResponseFrame(const QByteArray& bytes)
{
    for (qsizetype offset = 0; offset < bytes.size(); ++offset) {
        if (byteAt(bytes, offset) != kReadAddressProbeAddress || offset + kReadAddressResponseSize > bytes.size()) {
            continue;
        }

        const QByteArray candidate = bytes.mid(offset, kReadAddressResponseSize);
        if (byteAt(candidate, 1) == kReadAddressFunction && frameHasValidCrc(candidate)) {
            return candidate;
        }
    }
    return bytes;
}

QByteArray LiquidLevelModbusClient::matchingResponseFrame(quint8 address, quint8 functionCode, const QByteArray& bytes)
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

bool LiquidLevelModbusClient::sendRawFrame(
    const QByteArray& request,
    int expectedMinimumResponseSize,
    QString* errorMessage,
    QByteArray* response)
{
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("液位传感器串口未连接"));
        return false;
    }
    if (request.isEmpty()) {
        setError(errorMessage, QStringLiteral("液位传感器指令为空"));
        return false;
    }

#ifdef Q_OS_WIN
    if (nativeHandleIsOpen(m_nativeHandle)) {
        HANDLE handle = toNativeHandle(m_nativeHandle);
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

        DWORD written = 0;
        if (!WriteFile(handle, request.constData(), static_cast<DWORD>(request.size()), &written, nullptr)
            || written != static_cast<DWORD>(request.size())) {
            setError(errorMessage, QStringLiteral("液位传感器指令写入失败：%1").arg(windowsErrorText(GetLastError())));
            return false;
        }
        if (!FlushFileBuffers(handle)) {
            setError(errorMessage, QStringLiteral("液位传感器指令发送失败：%1").arg(windowsErrorText(GetLastError())));
            return false;
        }

        QByteArray localResponse;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < m_responseTimeoutMs && localResponse.size() < expectedMinimumResponseSize) {
            DWORD errors = 0;
            COMSTAT status;
            ZeroMemory(&status, sizeof(status));
            if (!ClearCommError(handle, &errors, &status)) {
                setError(errorMessage, QStringLiteral("读取液位传感器串口状态失败：%1").arg(windowsErrorText(GetLastError())));
                return false;
            }
            if (status.cbInQue == 0) {
                Sleep(10);
                continue;
            }

            const DWORD bytesToRead = std::min<DWORD>(status.cbInQue, 256);
            QByteArray chunk(static_cast<int>(bytesToRead), Qt::Uninitialized);
            DWORD bytesRead = 0;
            if (!ReadFile(handle, chunk.data(), bytesToRead, &bytesRead, nullptr)) {
                setError(errorMessage, QStringLiteral("读取液位传感器响应失败：%1").arg(windowsErrorText(GetLastError())));
                return false;
            }
            chunk.resize(static_cast<int>(bytesRead));
            localResponse.append(chunk);
        }

        QElapsedTimer drainTimer;
        drainTimer.start();
        while (drainTimer.elapsed() < 20) {
            DWORD errors = 0;
            COMSTAT status;
            ZeroMemory(&status, sizeof(status));
            if (!ClearCommError(handle, &errors, &status) || status.cbInQue == 0) {
                break;
            }
            const DWORD bytesToRead = std::min<DWORD>(status.cbInQue, 256);
            QByteArray chunk(static_cast<int>(bytesToRead), Qt::Uninitialized);
            DWORD bytesRead = 0;
            if (!ReadFile(handle, chunk.data(), bytesToRead, &bytesRead, nullptr)) {
                break;
            }
            chunk.resize(static_cast<int>(bytesRead));
            localResponse.append(chunk);
        }

        if (localResponse.isEmpty()) {
            setError(errorMessage, QStringLiteral("液位传感器未返回响应（原生串口已发送 %1 字节：%2）")
                                       .arg(request.size())
                                       .arg(frameToHex(request)));
            return false;
        }

        if (response != nullptr) {
            *response = localResponse;
        }
        return true;
    }
#endif

    m_serialPort->clear(QSerialPort::AllDirections);
    const qint64 written = m_serialPort->write(request);
    if (written != request.size()) {
        setError(errorMessage, QStringLiteral("液位传感器指令写入失败：%1").arg(m_serialPort->errorString()));
        return false;
    }
    if (!m_serialPort->waitForBytesWritten(m_responseTimeoutMs)) {
        setError(errorMessage, QStringLiteral("液位传感器指令发送超时：%1").arg(m_serialPort->errorString()));
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
        setError(errorMessage, QStringLiteral("液位传感器未返回响应（已发送 %1 字节：%2）")
                                   .arg(request.size())
                                   .arg(frameToHex(request)));
        return false;
    }

    if (response != nullptr) {
        *response = localResponse;
    }
    return true;
}

bool LiquidLevelModbusClient::sendFrame(const QByteArray& request, QString* errorMessage, QByteArray* response)
{
    if (request.size() < 4) {
        setError(errorMessage, QStringLiteral("液位传感器指令为空或不完整"));
        return false;
    }

    const quint8 address = byteAt(request, 0);
    const quint8 functionCode = byteAt(request, 1);
    QByteArray localResponse;
    if (!sendRawFrame(request, expectedResponseSizeForFunction(functionCode), errorMessage, &localResponse)) {
        return false;
    }

    const QByteArray parsedResponse = matchingResponseFrame(address, functionCode, localResponse);
    if (response != nullptr) {
        *response = parsedResponse;
    }
    return validateResponse(address, functionCode, parsedResponse, errorMessage);
}

bool LiquidLevelModbusClient::validateAddress(quint8 address, QString* errorMessage)
{
    if (address == 0 || address > 247) {
        setError(errorMessage, QStringLiteral("液位传感器 485 地址必须在 1-247 之间"));
        return false;
    }
    return true;
}

bool LiquidLevelModbusClient::validateResponse(
    quint8 address,
    quint8 functionCode,
    const QByteArray& response,
    QString* errorMessage)
{
    if (response.size() < 5) {
        setError(errorMessage, QStringLiteral("液位传感器响应长度不足，数据不完整：%1").arg(frameToHex(response)));
        return false;
    }
    if (!frameHasValidCrc(response)) {
        const QByteArray payload = response.left(response.size() - 2);
        const quint16 expectedCrc = modbusCrc(payload);
        const quint16 actualCrc = littleEndianCrcAtEnd(response);
        setError(errorMessage, QStringLiteral("液位传感器响应 CRC 校验失败：期望 %1，收到 %2，帧 %3")
                                   .arg(crcHex(expectedCrc), crcHex(actualCrc), frameToHex(response)));
        return false;
    }
    if (byteAt(response, 0) != address) {
        setError(errorMessage, QStringLiteral("液位传感器响应地址不匹配：期望 %1，收到 %2")
                                   .arg(address, 2, 16, QLatin1Char('0'))
                                   .arg(byteAt(response, 0), 2, 16, QLatin1Char('0')));
        return false;
    }

    const quint8 responseFunction = byteAt(response, 1);
    if (responseFunction == (functionCode | 0x80)) {
        const quint8 exceptionCode = response.size() >= 3 ? byteAt(response, 2) : 0;
        setError(errorMessage, QStringLiteral("液位传感器返回 Modbus 异常码：0x%1")
                                   .arg(exceptionCode, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    if (responseFunction != functionCode) {
        setError(errorMessage, QStringLiteral("液位传感器响应功能码不匹配：期望 0x%1，收到 0x%2")
                                   .arg(functionCode, 2, 16, QLatin1Char('0'))
                                   .arg(responseFunction, 2, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    if (response.size() != kReadLevelResponseSize) {
        setError(errorMessage, QStringLiteral("液位传感器响应长度异常：期望 %1 字节，收到 %2 字节，帧 %3")
                                   .arg(kReadLevelResponseSize)
                                   .arg(response.size())
                                   .arg(frameToHex(response)));
        return false;
    }
    if (byteAt(response, 2) != 0x02) {
        setError(errorMessage, QStringLiteral("液位传感器读取响应格式不正确：%1").arg(frameToHex(response)));
        return false;
    }
    return true;
}

}  // namespace panthera::adapters::liquidlevel
