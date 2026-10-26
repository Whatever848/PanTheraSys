#include "uim_passive_gateway.h"

#include <algorithm>
#include <array>
#include <limits>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

namespace panthera::tools::uim_limit {

namespace {

using UimDword = quint32;
using UimUint = quint32;
using UimByte = quint8;

constexpr UimDword kUimDeviceAll = 0x01U | 0x02U | 0x04U | 0x08U;
constexpr UimDword kUimFailure = std::numeric_limits<UimDword>::max();

struct DeviceInfoObject {
    UimDword deviceType;
    UimDword deviceIndex;
    UimUint comIndex;
    UimUint baudRate;
    char deviceName[64];
    UimUint protocol;
};

struct GatewaySearchParameters {
    UimDword comIndex;
    UimDword bitRate;
};

QString boundedLocalString(const char* value, qsizetype maxLength)
{
    qsizetype length = 0;
    while (length < maxLength && value[length] != '\0') {
        ++length;
    }
    return QString::fromLocal8Bit(value, static_cast<int>(length)).trimmed();
}

QString sdkResultText(UimDword result)
{
    return QStringLiteral("%1 (0x%2)")
        .arg(result)
        .arg(QString::number(result, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

QMutex g_callbackMutex;
QHash<quint32, QPointer<UimPassiveGateway>> g_callbackTargets;

}  // namespace

struct UimPassiveGateway::CanMessageObject {
    UimDword id;
    UimDword reserved0;
    UimByte reserved1;
    UimByte sendType;
    UimByte ide;
    UimByte rtr;
    UimByte dataLength;
    UimByte data[8];
    UimByte reserved[3];
};

static_assert(sizeof(UimPassiveGateway::CanMessageObject) == 24, "Unexpected UIROBOT CAN_MSG_OBJ ABI");

using SensorNotifyCallback = void (*)(UimDword, UimPassiveGateway::CanMessageObject*, UimDword);
using GetLastErrorFunc = UimDword (*)(char*, UimDword);
using SearchGatewayFunc = UimDword (*)(UimDword, GatewaySearchParameters*, DeviceInfoObject*, int);
using OpenGatewayFunc = UimDword (*)(UimDword, UimDword*, int, UimDword*);
using CloseGatewayFunc = UimDword (*)(UimDword);
using RegisterCallbackFunc = UimDword (*)(UimDword, UimDword, SensorNotifyCallback);

struct UimPassiveGateway::Functions {
    QLibrary library;
    GetLastErrorFunc getLastError {nullptr};
    SearchGatewayFunc searchGateway {nullptr};
    OpenGatewayFunc openGateway {nullptr};
    CloseGatewayFunc closeGateway {nullptr};
    RegisterCallbackFunc registerCallback {nullptr};
};

UimPassiveGateway::UimPassiveGateway(QObject* parent)
    : QObject(parent)
    , m_functions(std::make_unique<Functions>())
{
}

UimPassiveGateway::~UimPassiveGateway()
{
    closeGateway();
    unloadSdk();
}

bool UimPassiveGateway::loadSdk(const QString& dllPath, QString* errorMessage)
{
    closeGateway();
    unloadSdk();

    const QFileInfo dllInfo(dllPath);
    emit diagnosticMessage(
        QStringLiteral("SDK"),
        QStringLiteral("准备加载 DLL：%1；进程位数=%2；CAN_MSG_OBJ=%3 字节")
            .arg(QDir::toNativeSeparators(dllInfo.absoluteFilePath()))
            .arg(sizeof(void*) * 8)
            .arg(sizeof(CanMessageObject)));
    if (!dllInfo.exists() || !dllInfo.isFile()) {
        return setError(
            QStringLiteral("SDK DLL 不存在：%1").arg(QDir::toNativeSeparators(dllPath)),
            errorMessage);
    }

    m_functions->library.setFileName(dllInfo.absoluteFilePath());
    if (!m_functions->library.load()) {
        return setError(
            QStringLiteral("加载 SDK DLL 失败：%1").arg(m_functions->library.errorString()),
            errorMessage);
    }

    QFile dllFile(dllInfo.absoluteFilePath());
    QString sha256 = QStringLiteral("读取失败");
    if (dllFile.open(QIODevice::ReadOnly)) {
        sha256 = QString::fromLatin1(QCryptographicHash::hash(dllFile.readAll(), QCryptographicHash::Sha256).toHex());
    }
    emit diagnosticMessage(
        QStringLiteral("SDK"),
        QStringLiteral("DLL 已加载：大小=%1 字节；修改时间=%2；SHA-256=%3")
            .arg(dllInfo.size())
            .arg(dllInfo.lastModified().toString(Qt::ISODateWithMs))
            .arg(sha256));

    m_functions->getLastError = reinterpret_cast<GetLastErrorFunc>(m_functions->library.resolve("GetLastErr"));
    m_functions->searchGateway = reinterpret_cast<SearchGatewayFunc>(m_functions->library.resolve("SearchGateway"));
    m_functions->openGateway = reinterpret_cast<OpenGatewayFunc>(m_functions->library.resolve("OpenGateway"));
    m_functions->closeGateway = reinterpret_cast<CloseGatewayFunc>(m_functions->library.resolve("CloseGateway"));
    m_functions->registerCallback = reinterpret_cast<RegisterCallbackFunc>(m_functions->library.resolve("UIMRegRtcnCallBack"));

    QStringList missingSymbols;
    if (m_functions->searchGateway == nullptr) {
        missingSymbols.append(QStringLiteral("SearchGateway"));
    }
    if (m_functions->openGateway == nullptr) {
        missingSymbols.append(QStringLiteral("OpenGateway"));
    }
    if (m_functions->closeGateway == nullptr) {
        missingSymbols.append(QStringLiteral("CloseGateway"));
    }
    if (m_functions->registerCallback == nullptr) {
        missingSymbols.append(QStringLiteral("UIMRegRtcnCallBack"));
    }

    if (!missingSymbols.isEmpty()) {
        const QString message = QStringLiteral("SDK DLL 缺少只读监听所需函数：%1")
                                    .arg(missingSymbols.join(QStringLiteral(", ")));
        unloadSdk();
        return setError(message, errorMessage);
    }


    emit diagnosticMessage(
        QStringLiteral("SDK"),
        QStringLiteral("只读监听函数解析完成：SearchGateway/OpenGateway/CloseGateway/UIMRegRtcnCallBack/GetLastErr"));

    m_sdkPath = dllInfo.absoluteFilePath();
    m_lastError.clear();
    return true;
}

void UimPassiveGateway::unloadSdk()
{
    closeGateway();
    if (m_functions != nullptr && m_functions->library.isLoaded()) {
        m_functions->library.unload();
    }
    m_functions = std::make_unique<Functions>();
    m_sdkPath.clear();
    m_gateways.clear();
}

bool UimPassiveGateway::isSdkLoaded() const
{
    return m_functions != nullptr && m_functions->library.isLoaded();
}

QString UimPassiveGateway::sdkPath() const
{
    return m_sdkPath;
}

QVector<UimGatewayInfo> UimPassiveGateway::searchGateways(QString* errorMessage)
{
    m_gateways.clear();
    if (!isSdkLoaded()) {
        setError(QStringLiteral("SDK 尚未加载"), errorMessage);
        return {};
    }

    std::array<DeviceInfoObject, 16> rawDevices {};
    emit diagnosticMessage(QStringLiteral("通道"), QStringLiteral("调用 SearchGateway，设备类型掩码=0x0F"));
    const UimDword result = m_functions->searchGateway(
        kUimDeviceAll,
        nullptr,
        rawDevices.data(),
        static_cast<int>(rawDevices.size()));
    if (isFailure(result)) {
        setError(formatSdkError(QStringLiteral("搜索 USB-CAN 通道失败")), errorMessage);
        return {};
    }

    emit diagnosticMessage(
        QStringLiteral("通道"),
        QStringLiteral("SearchGateway 返回 %1；按设备数量解释").arg(sdkResultText(result)));

    const qsizetype count = std::min(
        static_cast<qsizetype>(result),
        static_cast<qsizetype>(rawDevices.size()));
    m_gateways.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        const DeviceInfoObject& raw = rawDevices[static_cast<size_t>(index)];
        UimGatewayInfo gateway;
        gateway.deviceType = raw.deviceType;
        gateway.deviceIndex = raw.deviceIndex;
        gateway.comIndex = raw.comIndex;
        gateway.baudRate = raw.baudRate;
        gateway.name = boundedLocalString(raw.deviceName, static_cast<qsizetype>(std::size(raw.deviceName)));
        gateway.protocol = raw.protocol;
        m_gateways.append(gateway);
        emit diagnosticMessage(
            QStringLiteral("通道"),
            QStringLiteral("发现通道[%1]：名称=%2，DeviceType=0x%3，DeviceIndex=%4，COM=%5，Baud=%6，Protocol=%7")
                .arg(index)
                .arg(gateway.name.isEmpty() ? QStringLiteral("<空>") : gateway.name)
                .arg(QString::number(gateway.deviceType, 16).toUpper().rightJustified(8, QLatin1Char('0')))
                .arg(gateway.deviceIndex)
                .arg(gateway.comIndex)
                .arg(gateway.baudRate)
                .arg(gateway.protocol));
    }

    m_lastError.clear();
    return m_gateways;
}

bool UimPassiveGateway::openGateway(quint32 deviceIndex, QString* errorMessage)
{
    if (!isSdkLoaded()) {
        return setError(QStringLiteral("SDK 尚未加载"), errorMessage);
    }

    closeGateway();

    std::array<UimDword, 120> rawNodeIds {};
    UimDword bitRate = 0;
    emit diagnosticMessage(
        QStringLiteral("网关"),
        QStringLiteral("调用 OpenGateway：DeviceIndex=%1，节点缓冲区容量=%2")
            .arg(deviceIndex)
            .arg(rawNodeIds.size()));
    const UimDword result = m_functions->openGateway(
        deviceIndex,
        rawNodeIds.data(),
        static_cast<int>(rawNodeIds.size()),
        &bitRate);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("打开 USB-CAN 通道失败")), errorMessage);
    }

    emit diagnosticMessage(
        QStringLiteral("网关"),
        QStringLiteral("OpenGateway 返回 %1；按节点数量解释；CAN 位速率返回值=%2")
            .arg(sdkResultText(result))
            .arg(bitRate));

    m_deviceIndex = deviceIndex;
    quint32 enumeratedDeviceType = 0;
    const auto selectedGateway = std::find_if(
        m_gateways.cbegin(),
        m_gateways.cend(),
        [deviceIndex](const UimGatewayInfo& gateway) { return gateway.deviceIndex == deviceIndex; });
    if (selectedGateway != m_gateways.cend()) {
        enumeratedDeviceType = selectedGateway->deviceType;
    }
    m_canBitRate = bitRate;
    m_gatewayOpen = true;
    const qsizetype nodeCount = std::min(
        static_cast<qsizetype>(result),
        static_cast<qsizetype>(rawNodeIds.size()));
    m_nodeIds.clear();
    m_nodeIds.reserve(nodeCount);
    for (qsizetype index = 0; index < nodeCount; ++index) {
        m_nodeIds.append(rawNodeIds[static_cast<size_t>(index)]);
    }
    QStringList nodeTexts;
    for (const quint32 nodeId : m_nodeIds) {
        nodeTexts.append(QString::number(nodeId));
    }
    emit diagnosticMessage(
        QStringLiteral("网关"),
        QStringLiteral("SDK 返回节点：%1")
            .arg(nodeTexts.isEmpty() ? QStringLiteral("<空>") : nodeTexts.join(QStringLiteral(", "))));

    {
        QMutexLocker locker(&g_callbackMutex);
        g_callbackTargets.insert(deviceIndex, this);
    }

    // The VC sample shipped with this 64-bit DLL always passes devType=0 to
    // UIMRegRtcnCallBack, even when SearchGateway reports UIDEV_USBCAN (0x04).
    // Passing the enumerated gateway type makes this DLL reject registration.
    constexpr UimDword kCallbackDeviceType = 0;
    const UimDword callbackResult = m_functions->registerCallback(
        kCallbackDeviceType,
        deviceIndex,
        &UimPassiveGateway::sdkSensorCallback);
    m_callbackRegistrationResult = callbackResult;
    emit diagnosticMessage(
        QStringLiteral("回调"),
        QStringLiteral("UIMRegRtcnCallBack(devType=0, deviceIndex=%1) 返回 %2；枚举设备类型=0x%3；devType=0 与配套 VC 示例一致")
            .arg(deviceIndex)
            .arg(sdkResultText(callbackResult))
            .arg(QString::number(enumeratedDeviceType, 16).toUpper().rightJustified(8, QLatin1Char('0'))));
    // This 64-bit DLL returns 0 after the same registration call that its VC
    // sample makes without checking a result. Only UINT_MAX is documented and
    // observed as an unambiguous failure; a real callback frame is the final
    // proof that registration works.
    if (isFailure(callbackResult)) {
        {
            QMutexLocker locker(&g_callbackMutex);
            g_callbackTargets.remove(deviceIndex);
        }
        m_functions->closeGateway(deviceIndex);
        m_gatewayOpen = false;
        m_deviceIndex = 0;
        m_canBitRate = 0;
        m_callbackRegistrationResult = kUimFailure;
        m_nodeIds.clear();
        return setError(
            formatSdkError(
                QStringLiteral("注册 RTCN 被动监听回调失败；SDK 返回 %1")
                    .arg(sdkResultText(callbackResult))),
            errorMessage);
    }

    m_callbackRegistered = true;
    if (callbackResult != 1U) {
        emit diagnosticMessage(
            QStringLiteral("回调"),
            QStringLiteral("SDK 返回未文档化值 %1；不据此宣称已收到事件，继续等待首个真实硬件回调验证")
                .arg(sdkResultText(callbackResult)));
    }
    m_lastError.clear();
    return true;
}

void UimPassiveGateway::closeGateway()
{
    if (!m_gatewayOpen) {
        return;
    }

    if (m_callbackRegistered && m_functions != nullptr && m_functions->registerCallback != nullptr) {
        const UimDword unregisterResult = m_functions->registerCallback(0, m_deviceIndex, nullptr);
        emit diagnosticMessage(
            QStringLiteral("回调"),
            QStringLiteral("注销 RTCN 回调返回 %1").arg(sdkResultText(unregisterResult)));
    }

    {
        QMutexLocker locker(&g_callbackMutex);
        g_callbackTargets.remove(m_deviceIndex);
    }

    if (m_functions != nullptr && m_functions->closeGateway != nullptr) {
        const UimDword closeResult = m_functions->closeGateway(m_deviceIndex);
        emit diagnosticMessage(
            QStringLiteral("网关"),
            QStringLiteral("CloseGateway(DeviceIndex=%1) 返回 %2")
                .arg(m_deviceIndex)
                .arg(sdkResultText(closeResult)));
    }

    m_callbackRegistered = false;
    m_gatewayOpen = false;
    m_deviceIndex = 0;
    m_canBitRate = 0;
    m_callbackRegistrationResult = kUimFailure;
    m_nodeIds.clear();
    emit gatewayClosed();
}

bool UimPassiveGateway::isGatewayOpen() const
{
    return m_gatewayOpen;
}

quint32 UimPassiveGateway::deviceIndex() const
{
    return m_deviceIndex;
}

quint32 UimPassiveGateway::canBitRate() const
{
    return m_canBitRate;
}

quint32 UimPassiveGateway::callbackRegistrationResult() const
{
    return m_callbackRegistrationResult;
}

QVector<quint32> UimPassiveGateway::nodeIds() const
{
    return m_nodeIds;
}

QString UimPassiveGateway::lastError() const
{
    return m_lastError;
}

void UimPassiveGateway::sdkSensorCallback(
    quint32 deviceIndex,
    CanMessageObject* message,
    quint32 messageLength)
{
    if (message == nullptr) {
        return;
    }

    QPointer<UimPassiveGateway> target;
    {
        QMutexLocker locker(&g_callbackMutex);
        target = g_callbackTargets.value(deviceIndex);
    }
    if (target.isNull()) {
        return;
    }

    RawCanFrame frame;
    frame.receivedAt = QDateTime::currentDateTime();
    frame.deviceIndex = deviceIndex;
    frame.canId = message->id;
    frame.sendType = message->sendType;
    frame.ide = message->ide;
    frame.rtr = message->rtr;
    frame.dlc = std::min<quint8>(message->dataLength, 8);
    frame.data = QByteArray(reinterpret_cast<const char*>(message->data), frame.dlc);
    frame.callbackMessageLength = messageLength;

    QMetaObject::invokeMethod(
        target,
        [target, frame]() {
            if (!target.isNull() && target->isGatewayOpen()) {
                emit target->frameReceived(frame);
            }
        },
        Qt::QueuedConnection);
}

bool UimPassiveGateway::setError(const QString& message, QString* errorMessage)
{
    m_lastError = message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    emit errorOccurred(message);
    return false;
}

QString UimPassiveGateway::formatSdkError(const QString& action) const
{
    QString sdkMessage;
    if (m_functions != nullptr && m_functions->getLastError != nullptr) {
        QByteArray buffer(512, '\0');
        const UimDword result = m_functions->getLastError(
            buffer.data(),
            static_cast<UimDword>(buffer.size() - 1));
        if (!isFailure(result)) {
            sdkMessage = QString::fromLocal8Bit(buffer.constData()).trimmed();
        }
    }
    return sdkMessage.isEmpty() ? action : QStringLiteral("%1：%2").arg(action, sdkMessage);
}

bool UimPassiveGateway::isFailure(quint32 result)
{
    return result == kUimFailure;
}

}  // namespace panthera::tools::uim_limit

