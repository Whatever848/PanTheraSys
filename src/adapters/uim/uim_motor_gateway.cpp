#include "adapters/uim/uim_motor_gateway.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>

namespace diji::adapters::uim {

namespace {

using UimDword = quint32;
using UimUint = unsigned int;
using UimInt = int;
using UimBool = int;
using UimFloat = float;

constexpr UimDword kUimDeviceAll = 0x01U | 0x02U | 0x04U | 0x08U;
constexpr UimDword kUimFailure = std::numeric_limits<UimDword>::max();

struct BasicAckObject {
    UimUint reserved;
    UimBool enabled;
    UimBool direction;
    UimBool currentReduced;
    UimUint microstep;
    UimUint current;
    UimUint speed;
    UimUint step;
};

struct BasicFeedbackObject {
    UimUint reserved;
    UimBool enabled;
    UimBool direction;
    UimBool currentReduced;
    UimUint microstep;
    UimUint current;
    UimInt speed;
    UimInt step;
};

struct MdlInfoObject {
    UimUint canNodeId;
    UimUint canNodeType;
    UimUint current;
    UimBool integratedEncoder;
    UimBool encoderEnabled;
    UimBool motionSupported;
    UimBool twoSensor;
    UimBool fourSensor;
    UimUint firmwareVersion;
    char modelName[20];
};

struct SensorFeedbackObject {
    UimBool sensor1;
    UimBool sensor2;
    UimBool sensor3;
    UimFloat analogInput;
};

struct StgInfoObject {
    UimUint sensorNumber;
    UimUint sensor1TriggerTime;
    UimUint sensor2TriggerTime;
    UimUint sensor3TriggerTime;
};

struct S12ConObject {
    UimUint sensor2RisingAction;
    UimUint sensor2FallingAction;
    UimUint sensor1RisingAction;
    UimUint sensor1FallingAction;
};

struct S34ConObject {
    UimBool p4LevelHigh;
    UimUint p4Event;
    UimUint sensor3RisingAction;
    UimUint sensor3FallingAction;
    UimUint stallAction;
};

struct DevInfoObject {
    UimDword deviceType;
    UimDword deviceIndex;
    UimUint comIndex;
    UimUint baudRate;
    char deviceName[64];
    UimUint protocol;
};

struct GatewaySearchPara {
    UimDword comIndex;
    UimDword bitRate;
};

using GetLastErrFunc = UimDword (*)(char* errorMessage, UimDword length);
using SearchGatewayFunc = UimDword (*)(UimDword gatewayType, GatewaySearchPara* searchPara, DevInfoObject* deviceInfo, int length);
using OpenGatewayFunc = UimDword (*)(UimDword deviceIndex, UimDword* subNodeIdList, int length, UimDword* canBitRate);
using CloseGatewayFunc = UimDword (*)(UimDword deviceIndex);
using GetMdlFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, MdlInfoObject* mdlInfo);
using UimEnaFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, UimBool ackEnabled, BasicAckObject* ack);
using UimOffFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, UimBool ackEnabled, BasicAckObject* ack);
using UimFbkFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, BasicFeedbackObject* feedback);
using UimSfbkFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, SensorFeedbackObject* feedback);
using GetStgFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, StgInfoObject* stgInfo);
using SetStgFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, StgInfoObject* stgInfoIn, UimBool ackEnabled, StgInfoObject* stgInfoOut);
using GetS12ConFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, S12ConObject* s12Con);
using SetS12ConFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, S12ConObject* s12ConIn, UimBool ackEnabled, S12ConObject* s12ConOut);
using GetS34ConFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, S34ConObject* s34Con);
using SetS34ConFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, S34ConObject* s34ConIn, UimBool ackEnabled, S34ConObject* s34ConOut);
using SetValueFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, UimInt value, UimBool ackEnabled, UimInt* returnedValue);
using GetValueFunc = UimDword (*)(UimDword deviceIndex, UimDword canNodeId, UimInt* returnedValue);

template <typename T>
void bindFunction(QLibrary& library, T& function, const char* symbolName, QStringList* missingSymbols)
{
    function = reinterpret_cast<T>(library.resolve(symbolName));
    if (function == nullptr && missingSymbols != nullptr) {
        missingSymbols->append(QString::fromLatin1(symbolName));
    }
}

QString boundedLocalString(const char* value, qsizetype maxLength)
{
    if (value == nullptr) {
        return QString();
    }

    qsizetype length = 0;
    while (length < maxLength && value[length] != '\0') {
        ++length;
    }
    return QString::fromLocal8Bit(value, static_cast<int>(length)).trimmed();
}

void applyAckToSnapshot(const BasicAckObject& ack, UimMotorSnapshot* snapshot)
{
    if (snapshot == nullptr) {
        return;
    }

    snapshot->enabled = ack.enabled != 0;
    snapshot->direction = ack.direction != 0;
    snapshot->currentReduced = ack.currentReduced != 0;
    snapshot->microstep = static_cast<int>(ack.microstep);
    snapshot->current = static_cast<int>(ack.current);
    snapshot->speed = static_cast<int>(ack.speed);
    snapshot->step = static_cast<int>(ack.step);
}

void applyFeedbackToSnapshot(const BasicFeedbackObject& feedback, UimMotorSnapshot* snapshot)
{
    if (snapshot == nullptr) {
        return;
    }

    snapshot->enabled = feedback.enabled != 0;
    snapshot->direction = feedback.direction != 0;
    snapshot->currentReduced = feedback.currentReduced != 0;
    snapshot->microstep = static_cast<int>(feedback.microstep);
    snapshot->current = static_cast<int>(feedback.current);
    snapshot->speed = feedback.speed;
    snapshot->step = feedback.step;
}

bool isSupportedSensorAction(int action)
{
    switch (action) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        return true;
    default:
        return false;
    }
}

}  // namespace

struct UimMotorGateway::Functions {
    QLibrary library;
    GetLastErrFunc getLastErr {nullptr};
    SearchGatewayFunc searchGateway {nullptr};
    OpenGatewayFunc openGateway {nullptr};
    CloseGatewayFunc closeGateway {nullptr};
    GetMdlFunc getMdl {nullptr};
    UimEnaFunc uimEna {nullptr};
    UimOffFunc uimOff {nullptr};
    UimFbkFunc uimFbk {nullptr};
    UimSfbkFunc uimSfbk {nullptr};
    GetStgFunc getStg {nullptr};
    SetStgFunc setStg {nullptr};
    GetS12ConFunc getS12Con {nullptr};
    SetS12ConFunc setS12Con {nullptr};
    GetS34ConFunc getS34Con {nullptr};
    SetS34ConFunc setS34Con {nullptr};
    SetValueFunc setCurrent {nullptr};
    SetValueFunc setSpeed {nullptr};
    SetValueFunc setStep {nullptr};
    SetValueFunc setPosition {nullptr};
    SetValueFunc setEncoderPosition {nullptr};
    SetValueFunc setOrigin {nullptr};
    SetValueFunc setDigitalOutput {nullptr};
    GetValueFunc getSpeed {nullptr};
    GetValueFunc getStep {nullptr};
    GetValueFunc getPosition {nullptr};
    GetValueFunc getEncoderPosition {nullptr};
    GetValueFunc getDigitalOutput {nullptr};
};

UimMotorGateway::UimMotorGateway(QObject* parent)
    : QObject(parent)
    , m_functions(std::make_unique<Functions>())
{
}

UimMotorGateway::~UimMotorGateway()
{
    closeGateway();
    unloadSdk();
}

bool UimMotorGateway::loadSdk(const QString& dllPath, QString* errorMessage)
{
    closeGateway();
    unloadSdk();

    const QFileInfo dllInfo(dllPath);
    if (!dllInfo.exists() || !dllInfo.isFile()) {
        return setError(QStringLiteral("SDK DLL 不存在：%1").arg(QDir::toNativeSeparators(dllPath)), errorMessage);
    }

    m_functions->library.setFileName(dllInfo.absoluteFilePath());
    if (!m_functions->library.load()) {
        return setError(QStringLiteral("加载 SDK DLL 失败：%1").arg(m_functions->library.errorString()), errorMessage);
    }

    QStringList missingSymbols;
    bindFunction(m_functions->library, m_functions->getLastErr, "GetLastErr", nullptr);
    bindFunction(m_functions->library, m_functions->searchGateway, "SearchGateway", &missingSymbols);
    bindFunction(m_functions->library, m_functions->openGateway, "OpenGateway", &missingSymbols);
    bindFunction(m_functions->library, m_functions->closeGateway, "CloseGateway", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getMdl, "GetMDL", &missingSymbols);
    bindFunction(m_functions->library, m_functions->uimEna, "UimENA", &missingSymbols);
    bindFunction(m_functions->library, m_functions->uimOff, "UimOFF", &missingSymbols);
    bindFunction(m_functions->library, m_functions->uimFbk, "UimFBK", &missingSymbols);
    bindFunction(m_functions->library, m_functions->uimSfbk, "UimSFBK", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getStg, "GetSTG", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setStg, "SetSTG", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getS12Con, "GetS12CON", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setS12Con, "SetS12CON", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getS34Con, "GetS34CON", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setS34Con, "SetS34CON", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setCurrent, "SetCUR", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setSpeed, "SetSPD", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setStep, "SetSTP", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setPosition, "SetPOS", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setEncoderPosition, "SetQEC", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setOrigin, "SetORG", &missingSymbols);
    bindFunction(m_functions->library, m_functions->setDigitalOutput, "SetDOUT", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getSpeed, "GetSPD", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getStep, "GetSTP", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getPosition, "GetPOS", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getEncoderPosition, "GetQEC", &missingSymbols);
    bindFunction(m_functions->library, m_functions->getDigitalOutput, "GetDOUT", &missingSymbols);

    if (!missingSymbols.isEmpty()) {
        const QString message = QStringLiteral("SDK DLL 缺少函数：%1").arg(missingSymbols.join(QStringLiteral(", ")));
        unloadSdk();
        return setError(message, errorMessage);
    }

    m_sdkPath = dllInfo.absoluteFilePath();
    m_lastError.clear();
    return true;
}

void UimMotorGateway::unloadSdk()
{
    if (m_functions == nullptr) {
        return;
    }

    if (m_functions->library.isLoaded()) {
        m_functions->library.unload();
    }

    m_functions = std::make_unique<Functions>();
    m_sdkPath.clear();
    m_devices.clear();
}

bool UimMotorGateway::isSdkLoaded() const
{
    return m_functions != nullptr && m_functions->library.isLoaded();
}

QString UimMotorGateway::sdkPath() const
{
    return m_sdkPath;
}

QVector<UimDeviceInfo> UimMotorGateway::searchGateways(QString* errorMessage)
{
    m_devices.clear();

    if (!requireSdk(errorMessage)) {
        return {};
    }

    std::array<DevInfoObject, 16> rawDevices {};
    const UimDword result = m_functions->searchGateway(kUimDeviceAll, nullptr, rawDevices.data(), static_cast<int>(rawDevices.size()));
    if (isFailure(result)) {
        setError(formatSdkError(QStringLiteral("SearchGateway 执行失败")), errorMessage);
        return {};
    }

    const qsizetype deviceCount = std::min(
        static_cast<qsizetype>(result),
        static_cast<qsizetype>(rawDevices.size()));
    m_devices.reserve(deviceCount);
    for (qsizetype index = 0; index < deviceCount; ++index) {
        const DevInfoObject& rawDevice = rawDevices[static_cast<size_t>(index)];
        UimDeviceInfo device;
        device.deviceType = rawDevice.deviceType;
        device.deviceIndex = rawDevice.deviceIndex;
        device.comIndex = rawDevice.comIndex;
        device.baudRate = rawDevice.baudRate;
        device.name = boundedLocalString(rawDevice.deviceName, static_cast<qsizetype>(std::size(rawDevice.deviceName)));
        device.protocol = rawDevice.protocol;
        m_devices.push_back(device);
    }

    m_lastError.clear();
    return m_devices;
}

bool UimMotorGateway::openGateway(quint32 deviceIndex, QString* errorMessage)
{
    if (!requireSdk(errorMessage)) {
        return false;
    }

    closeGateway();

    std::array<UimDword, 120> rawNodeIds {};
    UimDword canBitRate = 0;
    const UimDword result = m_functions->openGateway(deviceIndex, rawNodeIds.data(), static_cast<int>(rawNodeIds.size()), &canBitRate);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("OpenGateway 执行失败")), errorMessage);
    }

    m_deviceIndex = deviceIndex;
    m_canBitRate = canBitRate;
    m_gatewayOpen = true;
    m_selectedNodeId = 0;
    m_nodes.clear();
    m_nodes.reserve(std::min(
        static_cast<qsizetype>(result),
        static_cast<qsizetype>(rawNodeIds.size())));

    const qsizetype nodeCount = std::min(
        static_cast<qsizetype>(result),
        static_cast<qsizetype>(rawNodeIds.size()));
    for (qsizetype index = 0; index < nodeCount; ++index) {
        const UimDword nodeId = rawNodeIds[static_cast<size_t>(index)];
        MdlInfoObject rawMdl {};
        UimNodeInfo node;
        node.nodeId = nodeId;

        if (!isFailure(m_functions->getMdl(m_deviceIndex, nodeId, &rawMdl))) {
            node.modelName = boundedLocalString(rawMdl.modelName, static_cast<qsizetype>(std::size(rawMdl.modelName)));
            node.firmwareVersion = rawMdl.firmwareVersion;
            node.current = rawMdl.current;
            node.integratedEncoder = rawMdl.integratedEncoder != 0;
            node.encoderEnabled = rawMdl.encoderEnabled != 0;
            node.motionSupported = rawMdl.motionSupported != 0;
        }

        m_nodes.push_back(node);
    }

    if (!m_nodes.isEmpty()) {
        m_selectedNodeId = m_nodes.first().nodeId;
    }

    m_snapshot = UimMotorSnapshot {};
    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);

    emit gatewayOpened();
    emit nodesChanged();
    emit snapshotChanged(m_snapshot);
    m_lastError.clear();
    return true;
}

void UimMotorGateway::closeGateway()
{
    if (m_gatewayOpen && isSdkLoaded() && m_functions->closeGateway != nullptr) {
        m_functions->closeGateway(m_deviceIndex);
    }

    if (m_gatewayOpen) {
        m_gatewayOpen = false;
        m_deviceIndex = 0;
        m_canBitRate = 0;
        m_selectedNodeId = 0;
        m_nodes.clear();
        m_snapshot = UimMotorSnapshot {};
        emit gatewayClosed();
        emit nodesChanged();
        emit snapshotChanged(m_snapshot);
    }
}

bool UimMotorGateway::isGatewayOpen() const
{
    return m_gatewayOpen;
}

quint32 UimMotorGateway::deviceIndex() const
{
    return m_deviceIndex;
}

quint32 UimMotorGateway::canBitRate() const
{
    return m_canBitRate;
}

QVector<UimNodeInfo> UimMotorGateway::nodes() const
{
    return m_nodes;
}

bool UimMotorGateway::selectNode(quint32 nodeId, QString* errorMessage)
{
    if (!m_gatewayOpen) {
        return setError(QStringLiteral("网关尚未打开"), errorMessage);
    }

    const auto found = std::find_if(m_nodes.cbegin(), m_nodes.cend(), [nodeId](const UimNodeInfo& node) {
        return node.nodeId == nodeId;
    });
    if (found == m_nodes.cend()) {
        return setError(QStringLiteral("CAN 节点不存在：%1").arg(nodeId), errorMessage);
    }

    m_selectedNodeId = nodeId;
    m_snapshot.nodeId = nodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    return true;
}

quint32 UimMotorGateway::selectedNodeId() const
{
    return m_selectedNodeId;
}

bool UimMotorGateway::hasSelectedNode() const
{
    return m_gatewayOpen && m_selectedNodeId != 0;
}

bool UimMotorGateway::enableMotor(QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    BasicAckObject ack {};
    const UimDword result = m_functions->uimEna(m_deviceIndex, m_selectedNodeId, 1, &ack);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("UimENA 执行失败")), errorMessage);
    }

    applyAckToSnapshot(ack, &m_snapshot);
    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    return true;
}

bool UimMotorGateway::disableMotor(QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    BasicAckObject ack {};
    const UimDword result = m_functions->uimOff(m_deviceIndex, m_selectedNodeId, 1, &ack);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("UimOFF 执行失败")), errorMessage);
    }

    applyAckToSnapshot(ack, &m_snapshot);
    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.enabled = false;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    return true;
}

bool UimMotorGateway::home(QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setOrigin(m_deviceIndex, m_selectedNodeId, 0, 1, &returnedValue);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("SetORG 执行失败")), errorMessage);
    }

    m_snapshot.hasPosition = true;
    m_snapshot.position = returnedValue;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    return true;
}

bool UimMotorGateway::setCurrentAmps(double currentAmps, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }
    if (currentAmps < 0.0) {
        return setError(QStringLiteral("电流不能小于 0A"), errorMessage);
    }

    const int currentValue = static_cast<int>(std::lround(currentAmps * 10.0));
    UimInt returnedValue = 0;
    const UimDword result = m_functions->setCurrent(m_deviceIndex, m_selectedNodeId, currentValue, 1, &returnedValue);
    return invokeSetValue("SetCUR", result, returnedValue, errorMessage);
}

bool UimMotorGateway::setSpeed(int speed, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setSpeed(m_deviceIndex, m_selectedNodeId, speed, 1, &returnedValue);
    return invokeSetValue("SetSPD", result, returnedValue, errorMessage);
}

bool UimMotorGateway::setStep(int step, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setStep(m_deviceIndex, m_selectedNodeId, step, 1, &returnedValue);
    return invokeSetValue("SetSTP", result, returnedValue, errorMessage);
}

bool UimMotorGateway::setOpenLoopPosition(int position, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setPosition(m_deviceIndex, m_selectedNodeId, position, 1, &returnedValue);
    if (!invokeSetValue("SetPOS", result, returnedValue, errorMessage)) {
        return false;
    }

    m_snapshot.hasPosition = true;
    m_snapshot.position = returnedValue;
    emit snapshotChanged(m_snapshot);
    return true;
}

bool UimMotorGateway::setClosedLoopEncoderPosition(int encoderPosition, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setEncoderPosition(m_deviceIndex, m_selectedNodeId, encoderPosition, 1, &returnedValue);
    if (!invokeSetValue("SetQEC", result, returnedValue, errorMessage)) {
        return false;
    }

    m_snapshot.hasEncoderPosition = true;
    m_snapshot.encoderPosition = returnedValue;
    emit snapshotChanged(m_snapshot);
    return true;
}

bool UimMotorGateway::setDigitalOutput(bool high, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->setDigitalOutput(m_deviceIndex, m_selectedNodeId, high ? 1 : 0, 1, &returnedValue);
    return invokeSetValue("SetDOUT", result, returnedValue, errorMessage);
}

bool UimMotorGateway::readDigitalOutput(bool* high, QString* errorMessage)
{
    if (high == nullptr) {
        return setError(QStringLiteral("P4 输出读取缺少输出参数"), errorMessage);
    }
    if (!requireReady(errorMessage)) {
        return false;
    }

    UimInt returnedValue = 0;
    const UimDword result = m_functions->getDigitalOutput(m_deviceIndex, m_selectedNodeId, &returnedValue);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("GetDOUT 执行失败")), errorMessage);
    }

    *high = returnedValue != 0;
    m_lastError.clear();
    return true;
}

bool UimMotorGateway::refreshSnapshot(QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    bool primaryFeedbackOk = updateBasicFeedback(nullptr);
    UimInt value = 0;

    if (!isFailure(m_functions->getSpeed(m_deviceIndex, m_selectedNodeId, &value))) {
        m_snapshot.speed = value;
        primaryFeedbackOk = true;
    }

    if (!isFailure(m_functions->getStep(m_deviceIndex, m_selectedNodeId, &value))) {
        m_snapshot.step = value;
        primaryFeedbackOk = true;
    }

    if (!primaryFeedbackOk) {
        return setError(formatSdkError(QStringLiteral("GetSPD/GetSTP 执行失败")), errorMessage);
    }

    if (!isFailure(m_functions->getPosition(m_deviceIndex, m_selectedNodeId, &value))) {
        m_snapshot.hasPosition = true;
        m_snapshot.position = value;
    } else {
        m_snapshot.hasPosition = false;
    }

    if (!isFailure(m_functions->getEncoderPosition(m_deviceIndex, m_selectedNodeId, &value))) {
        m_snapshot.hasEncoderPosition = true;
        m_snapshot.encoderPosition = value;
    } else {
        m_snapshot.hasEncoderPosition = false;
    }

    SensorFeedbackObject sensorFeedback {};
    if (!isFailure(m_functions->uimSfbk(m_deviceIndex, m_selectedNodeId, &sensorFeedback))) {
        m_snapshot.hasSensorFeedback = true;
        m_snapshot.sensor1 = sensorFeedback.sensor1 != 0;
        m_snapshot.sensor2 = sensorFeedback.sensor2 != 0;
        m_snapshot.sensor3 = sensorFeedback.sensor3 != 0;
        m_snapshot.analogInput = sensorFeedback.analogInput;
    } else {
        m_snapshot.hasSensorFeedback = false;
    }

    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    m_lastError.clear();
    return true;
}

bool UimMotorGateway::refreshSensorFeedback(QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    SensorFeedbackObject sensorFeedback {};
    if (isFailure(m_functions->uimSfbk(m_deviceIndex, m_selectedNodeId, &sensorFeedback))) {
        return setError(formatSdkError(QStringLiteral("UimSFBK 执行失败")), errorMessage);
    }

    m_snapshot.hasSensorFeedback = true;
    m_snapshot.sensor1 = sensorFeedback.sensor1 != 0;
    m_snapshot.sensor2 = sensorFeedback.sensor2 != 0;
    m_snapshot.sensor3 = sensorFeedback.sensor3 != 0;
    m_snapshot.analogInput = sensorFeedback.analogInput;
    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    m_lastError.clear();
    return true;
}

bool UimMotorGateway::readSensorDebounceConfig(UimSensorDebounceConfig* config, QString* errorMessage)
{
    if (config == nullptr) {
        return setError(QStringLiteral("传感器消抖配置输出为空"), errorMessage);
    }
    if (!requireReady(errorMessage)) {
        return false;
    }

    StgInfoObject stgInfo {};
    if (isFailure(m_functions->getStg(m_deviceIndex, m_selectedNodeId, &stgInfo))) {
        return setError(formatSdkError(QStringLiteral("GetSTG 执行失败")), errorMessage);
    }

    config->sensor1Milliseconds = static_cast<int>(stgInfo.sensor1TriggerTime);
    config->sensor2Milliseconds = static_cast<int>(stgInfo.sensor2TriggerTime);
    config->sensor3Milliseconds = static_cast<int>(stgInfo.sensor3TriggerTime);
    m_lastError.clear();
    return true;
}

bool UimMotorGateway::setSensorDebounce(int sensorIndex, int milliseconds, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }
    if (sensorIndex < 1 || sensorIndex > 3) {
        return setError(QStringLiteral("传感器编号必须为 1、2 或 3"), errorMessage);
    }
    if (milliseconds < 0 || milliseconds > 60000) {
        return setError(QStringLiteral("传感器消抖时间必须在 0-60000 ms 内"), errorMessage);
    }

    StgInfoObject stgInfoIn {};
    stgInfoIn.sensorNumber = static_cast<UimUint>(sensorIndex);
    if (sensorIndex == 1) {
        stgInfoIn.sensor1TriggerTime = static_cast<UimUint>(milliseconds);
    } else if (sensorIndex == 2) {
        stgInfoIn.sensor2TriggerTime = static_cast<UimUint>(milliseconds);
    } else {
        stgInfoIn.sensor3TriggerTime = static_cast<UimUint>(milliseconds);
    }

    StgInfoObject stgInfoOut {};
    if (isFailure(m_functions->setStg(m_deviceIndex, m_selectedNodeId, &stgInfoIn, 1, &stgInfoOut))) {
        return setError(formatSdkError(QStringLiteral("SetSTG 执行失败")), errorMessage);
    }

    m_lastError.clear();
    return true;
}

bool UimMotorGateway::readSensorActionConfig(UimSensorActionConfig* config, QString* errorMessage)
{
    if (config == nullptr) {
        return setError(QStringLiteral("传感器动作配置输出为空"), errorMessage);
    }
    if (!requireReady(errorMessage)) {
        return false;
    }

    S12ConObject s12Con {};
    if (isFailure(m_functions->getS12Con(m_deviceIndex, m_selectedNodeId, &s12Con))) {
        return setError(formatSdkError(QStringLiteral("GetS12CON 执行失败")), errorMessage);
    }

    config->sensor1RisingAction = static_cast<int>(s12Con.sensor1RisingAction);
    config->sensor1FallingAction = static_cast<int>(s12Con.sensor1FallingAction);
    config->sensor2RisingAction = static_cast<int>(s12Con.sensor2RisingAction);
    config->sensor2FallingAction = static_cast<int>(s12Con.sensor2FallingAction);
    config->sensor3RisingAction = 0;
    config->sensor3FallingAction = 0;
    config->sensor3Available = false;

    S34ConObject s34Con {};
    if (!isFailure(m_functions->getS34Con(m_deviceIndex, m_selectedNodeId, &s34Con))) {
        config->sensor3RisingAction = static_cast<int>(s34Con.sensor3RisingAction);
        config->sensor3FallingAction = static_cast<int>(s34Con.sensor3FallingAction);
        config->sensor3Available = true;
    }

    m_lastError.clear();
    return true;
}

bool UimMotorGateway::setSensorActionConfig(const UimSensorActionConfig& config, QString* errorMessage)
{
    if (!requireReady(errorMessage)) {
        return false;
    }

    const std::array<int, 6> actions {
        config.sensor1RisingAction,
        config.sensor1FallingAction,
        config.sensor2RisingAction,
        config.sensor2FallingAction,
        config.sensor3RisingAction,
        config.sensor3FallingAction
    };
    for (int action : actions) {
        if (!isSupportedSensorAction(action)) {
            return setError(QStringLiteral("传感器动作代码不支持：%1").arg(action), errorMessage);
        }
    }

    S34ConObject s34ConIn {};
    const bool s34Readable = !isFailure(m_functions->getS34Con(m_deviceIndex, m_selectedNodeId, &s34ConIn));
    const bool writeS34 = s34Readable
        || config.sensor3RisingAction != 0
        || config.sensor3FallingAction != 0;
    if (writeS34 && !s34Readable) {
        return setError(formatSdkError(QStringLiteral("GetS34CON 执行失败，无法写入 S3 动作")), errorMessage);
    }

    S12ConObject s12ConIn {};
    s12ConIn.sensor2RisingAction = static_cast<UimUint>(config.sensor2RisingAction);
    s12ConIn.sensor2FallingAction = static_cast<UimUint>(config.sensor2FallingAction);
    s12ConIn.sensor1RisingAction = static_cast<UimUint>(config.sensor1RisingAction);
    s12ConIn.sensor1FallingAction = static_cast<UimUint>(config.sensor1FallingAction);

    S12ConObject s12ConOut {};
    if (isFailure(m_functions->setS12Con(m_deviceIndex, m_selectedNodeId, &s12ConIn, 1, &s12ConOut))) {
        return setError(formatSdkError(QStringLiteral("SetS12CON 执行失败")), errorMessage);
    }

    if (writeS34) {
        s34ConIn.sensor3RisingAction = static_cast<UimUint>(config.sensor3RisingAction);
        s34ConIn.sensor3FallingAction = static_cast<UimUint>(config.sensor3FallingAction);

        S34ConObject s34ConOut {};
        if (isFailure(m_functions->setS34Con(m_deviceIndex, m_selectedNodeId, &s34ConIn, 1, &s34ConOut))) {
            return setError(formatSdkError(QStringLiteral("SetS34CON 执行失败")), errorMessage);
        }
    }

    m_lastError.clear();
    return true;
}

UimMotorSnapshot UimMotorGateway::latestSnapshot() const
{
    return m_snapshot;
}

QString UimMotorGateway::lastError() const
{
    return m_lastError;
}

bool UimMotorGateway::requireSdk(QString* errorMessage) const
{
    if (!isSdkLoaded()) {
        return setError(QStringLiteral("SDK 尚未加载"), errorMessage);
    }
    return true;
}

bool UimMotorGateway::requireReady(QString* errorMessage) const
{
    if (!requireSdk(errorMessage)) {
        return false;
    }

    if (!m_gatewayOpen) {
        return setError(QStringLiteral("网关尚未打开"), errorMessage);
    }

    if (m_selectedNodeId == 0) {
        return setError(QStringLiteral("尚未选择 CAN 节点"), errorMessage);
    }

    return true;
}

bool UimMotorGateway::setError(const QString& message, QString* errorMessage) const
{
    m_lastError = message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    emit const_cast<UimMotorGateway*>(this)->errorOccurred(message);
    return false;
}

QString UimMotorGateway::formatSdkError(const QString& action) const
{
    QString sdkMessage;
    if (m_functions != nullptr && m_functions->getLastErr != nullptr) {
        QByteArray buffer(512, '\0');
        const UimDword result = m_functions->getLastErr(buffer.data(), static_cast<UimDword>(buffer.size() - 1));
        if (!isFailure(result)) {
            sdkMessage = QString::fromLocal8Bit(buffer.constData()).trimmed();
        }
    }

    return sdkMessage.isEmpty() ? action : QStringLiteral("%1：%2").arg(action, sdkMessage);
}

bool UimMotorGateway::invokeSetValue(const char* actionName, quint32 result, int returnedValue, QString* errorMessage)
{
    const QString action = QString::fromLatin1(actionName);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("%1 执行失败").arg(action)), errorMessage);
    }

    if (action == QStringLiteral("SetCUR")) {
        m_snapshot.current = returnedValue;
    } else if (action == QStringLiteral("SetSPD")) {
        m_snapshot.speed = returnedValue;
    } else if (action == QStringLiteral("SetSTP")) {
        m_snapshot.step = returnedValue;
    }

    m_snapshot.gatewayOpen = true;
    m_snapshot.deviceIndex = m_deviceIndex;
    m_snapshot.nodeId = m_selectedNodeId;
    m_snapshot.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    emit snapshotChanged(m_snapshot);
    m_lastError.clear();
    return true;
}

bool UimMotorGateway::updateBasicFeedback(QString* errorMessage)
{
    BasicFeedbackObject feedback {};
    const UimDword result = m_functions->uimFbk(m_deviceIndex, m_selectedNodeId, &feedback);
    if (isFailure(result)) {
        return setError(formatSdkError(QStringLiteral("UimFBK 执行失败")), errorMessage);
    }

    applyFeedbackToSnapshot(feedback, &m_snapshot);
    return true;
}

bool UimMotorGateway::isFailure(quint32 result)
{
    return result == kUimFailure;
}

}  // namespace diji::adapters::uim
