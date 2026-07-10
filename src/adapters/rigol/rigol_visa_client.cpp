#include "adapters/rigol/rigol_visa_client.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include <QLibrary>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#define PANTHERA_RIGOL_VISA_CALL __stdcall
#else
#define PANTHERA_RIGOL_VISA_CALL
#endif

namespace panthera::adapters::rigol {
namespace {

constexpr int kConnectTimeoutMs = 3000;
constexpr int kWriteTimeoutMs = 3000;
constexpr int kQueryTimeoutMs = 3000;
constexpr double kMinimumDutyCyclePercent = 1.0;
constexpr double kMaximumDutyCyclePercent = 99.0;
constexpr int kVisaResourceBufferSize = 512;
constexpr int kVisaReadBufferSize = 8192;
constexpr quint32 kVisaNull = 0;
constexpr qint32 kVisaSuccessMaxCount = 0x3FFF0006;
constexpr qint32 kVisaErrorTimeout = static_cast<qint32>(0xBFFF0015u);
constexpr quint32 kVisaAttrTimeoutValue = 0x3FFF001A;
constexpr const char* kUltrasoundFrequencyScpiValue = "1.186MHZ";
constexpr const char* kUltrasoundVoltageScpiValue = "2VPP";
constexpr const char* kUltrasoundLoadScpiValue = "50";

// TODO: 当前环境缺少 VISA SDK，需要安装 NI-VISA、Keysight IO Libraries 或 RIGOL UltraSigma 后再编译 USB-SCPI 功能。
constexpr const char* kVisaMissingSdkMessage =
    "当前环境缺少 VISA SDK/运行库，需要安装 NI-VISA、Keysight IO Libraries 或 RIGOL UltraSigma 后再使用 USB-SCPI 功能。";

QString normalizedCommand(const QString& cmd)
{
    QString command = cmd.trimmed();
    command.append(QLatin1Char('\n'));
    return command;
}

bool visaStatusOk(qint32 status)
{
    return status >= 0;
}

QString visaStatusText(qint32 status)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0')).toUpper();
}

bool scpiErrorIsNoError(QString errorText)
{
    errorText = errorText.trimmed();
    return errorText.startsWith(QLatin1Char('0'))
        || errorText.startsWith(QStringLiteral("+0"))
        || errorText.contains(QStringLiteral("No error"), Qt::CaseInsensitive);
}

}  // namespace

struct RigolVisaClient::VisaApi {
    using VisaStatus = qint32;
    using VisaSession = quint32;
    using VisaFindList = quint32;
    using VisaUInt32 = quint32;
    using VisaChar = char;

    using ViOpenDefaultRMFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(VisaSession*);
    using ViFindRsrcFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(
        VisaSession,
        const VisaChar*,
        VisaFindList*,
        VisaUInt32*,
        VisaChar[]);
    using ViFindNextFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(VisaFindList, VisaChar[]);
    using ViOpenFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(
        VisaSession,
        const VisaChar*,
        VisaUInt32,
        VisaUInt32,
        VisaSession*);
    using ViWriteFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(
        VisaSession,
        const unsigned char*,
        VisaUInt32,
        VisaUInt32*);
    using ViReadFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(
        VisaSession,
        unsigned char*,
        VisaUInt32,
        VisaUInt32*);
    using ViCloseFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(VisaSession);
    using ViSetAttributeFunction = VisaStatus (PANTHERA_RIGOL_VISA_CALL *)(VisaSession, VisaUInt32, VisaUInt32);

    std::unique_ptr<QLibrary> library;
    VisaSession resourceManager {kVisaNull};
    VisaSession instrument {kVisaNull};
    ViOpenDefaultRMFunction viOpenDefaultRM {nullptr};
    ViFindRsrcFunction viFindRsrc {nullptr};
    ViFindNextFunction viFindNext {nullptr};
    ViOpenFunction viOpen {nullptr};
    ViWriteFunction viWrite {nullptr};
    ViReadFunction viRead {nullptr};
    ViCloseFunction viClose {nullptr};
    ViSetAttributeFunction viSetAttribute {nullptr};
};

RigolVisaClient::RigolVisaClient(QObject* parent)
    : QObject(parent)
    , visa_(new VisaApi)
{
}

RigolVisaClient::~RigolVisaClient()
{
    closeInstrument();
    closeResourceManager();
    delete visa_;
    visa_ = nullptr;
}

bool RigolVisaClient::ensureVisaLoaded()
{
    if (visa_ == nullptr) {
        return false;
    }
    if (visa_->library != nullptr && visa_->library->isLoaded()) {
        return true;
    }

#if defined(Q_OS_WIN)
    const QStringList candidates {
        QStringLiteral("visa64"),
        QStringLiteral("visa32"),
        QStringLiteral("visa")
    };
#else
    const QStringList candidates {
        QStringLiteral("visa")
    };
#endif

    QStringList errors;
    for (const QString& candidate : candidates) {
        auto library = std::make_unique<QLibrary>(candidate);
        if (!library->load()) {
            errors.push_back(QStringLiteral("%1：%2").arg(candidate, library->errorString()));
            continue;
        }

        auto resolveRequired = [&library, &candidate, &errors](const char* symbol) -> QFunctionPointer {
            QFunctionPointer function = library->resolve(symbol);
            if (function == nullptr) {
                errors.push_back(QStringLiteral("%1 缺少 %2").arg(candidate, QString::fromLatin1(symbol)));
            }
            return function;
        };

        visa_->viOpenDefaultRM = reinterpret_cast<VisaApi::ViOpenDefaultRMFunction>(resolveRequired("viOpenDefaultRM"));
        visa_->viFindRsrc = reinterpret_cast<VisaApi::ViFindRsrcFunction>(resolveRequired("viFindRsrc"));
        visa_->viFindNext = reinterpret_cast<VisaApi::ViFindNextFunction>(resolveRequired("viFindNext"));
        visa_->viOpen = reinterpret_cast<VisaApi::ViOpenFunction>(resolveRequired("viOpen"));
        visa_->viWrite = reinterpret_cast<VisaApi::ViWriteFunction>(resolveRequired("viWrite"));
        visa_->viRead = reinterpret_cast<VisaApi::ViReadFunction>(resolveRequired("viRead"));
        visa_->viClose = reinterpret_cast<VisaApi::ViCloseFunction>(resolveRequired("viClose"));
        visa_->viSetAttribute = reinterpret_cast<VisaApi::ViSetAttributeFunction>(library->resolve("viSetAttribute"));

        if (visa_->viOpenDefaultRM != nullptr
            && visa_->viFindRsrc != nullptr
            && visa_->viFindNext != nullptr
            && visa_->viOpen != nullptr
            && visa_->viWrite != nullptr
            && visa_->viRead != nullptr
            && visa_->viClose != nullptr) {
            visa_->library = std::move(library);
            return true;
        }
    }

    emit logMessage(QStringLiteral("RIGOL USB-SCPI 功能不可用：%1").arg(QString::fromUtf8(kVisaMissingSdkMessage)));
    if (!errors.isEmpty()) {
        emit logMessage(QStringLiteral("VISA加载详情：%1").arg(errors.join(QStringLiteral("；"))));
    }
    return false;
}

bool RigolVisaClient::ensureResourceManager()
{
    if (!ensureVisaLoaded()) {
        return false;
    }
    if (visa_->resourceManager != kVisaNull) {
        return true;
    }

    VisaApi::VisaSession resourceManager = kVisaNull;
    const VisaApi::VisaStatus status = visa_->viOpenDefaultRM(&resourceManager);
    if (!visaStatusOk(status) || resourceManager == kVisaNull) {
        emit logMessage(QStringLiteral("VISA资源管理器打开失败：viOpenDefaultRM status=%1").arg(visaStatusText(status)));
        return false;
    }

    visa_->resourceManager = resourceManager;
    return true;
}

QStringList RigolVisaClient::searchUsbResources()
{
    QStringList resources;
    if (!ensureResourceManager()) {
        return resources;
    }

    VisaApi::VisaFindList findList = kVisaNull;
    VisaApi::VisaUInt32 count = 0;
    std::array<VisaApi::VisaChar, kVisaResourceBufferSize> descriptor {};
    const VisaApi::VisaStatus status = visa_->viFindRsrc(
        visa_->resourceManager,
        "USB?*::INSTR",
        &findList,
        &count,
        descriptor.data());
    if (!visaStatusOk(status)) {
        emit logMessage(QStringLiteral("搜索 RIGOL USB信号源失败：viFindRsrc USB?*::INSTR status=%1")
                            .arg(visaStatusText(status)));
        if (findList != kVisaNull && visa_->viClose != nullptr) {
            visa_->viClose(findList);
        }
        return resources;
    }

    if (count > 0) {
        resources.push_back(QString::fromLatin1(descriptor.data()).trimmed());
    }
    for (VisaApi::VisaUInt32 index = 1; index < count; ++index) {
        descriptor.fill('\0');
        const VisaApi::VisaStatus nextStatus = visa_->viFindNext(findList, descriptor.data());
        if (!visaStatusOk(nextStatus)) {
            emit logMessage(QStringLiteral("读取下一个 USB-VISA 资源失败：viFindNext status=%1")
                                .arg(visaStatusText(nextStatus)));
            break;
        }
        resources.push_back(QString::fromLatin1(descriptor.data()).trimmed());
    }

    if (findList != kVisaNull) {
        visa_->viClose(findList);
    }
    resources.removeAll(QString());
    resources.removeDuplicates();
    return resources;
}

QStringList RigolVisaClient::searchRigolUsbResources(int timeoutMs)
{
    QStringList rigolResources;
    const QStringList resources = searchUsbResources();
    if (resources.isEmpty() || !ensureResourceManager()) {
        return rigolResources;
    }

    const int boundedTimeoutMs = std::clamp(timeoutMs, 100, 60000);
    for (const QString& resource : resources) {
        VisaApi::VisaSession instrument = kVisaNull;
        const QByteArray resourceBytes = resource.toLatin1();
        const VisaApi::VisaStatus openStatus = visa_->viOpen(
            visa_->resourceManager,
            resourceBytes.constData(),
            kVisaNull,
            static_cast<VisaApi::VisaUInt32>(boundedTimeoutMs),
            &instrument);
        if (!visaStatusOk(openStatus) || instrument == kVisaNull) {
            continue;
        }

        if (visa_->viSetAttribute != nullptr) {
            visa_->viSetAttribute(
                instrument,
                kVisaAttrTimeoutValue,
                static_cast<VisaApi::VisaUInt32>(boundedTimeoutMs));
        }

        const QByteArray command = normalizedCommand(QStringLiteral("*IDN?")).toLatin1();
        VisaApi::VisaUInt32 written = 0;
        const VisaApi::VisaStatus writeStatus = visa_->viWrite(
            instrument,
            reinterpret_cast<const unsigned char*>(command.constData()),
            static_cast<VisaApi::VisaUInt32>(command.size()),
            &written);

        QString idn;
        if (visaStatusOk(writeStatus) && written == static_cast<VisaApi::VisaUInt32>(command.size())) {
            std::array<unsigned char, kVisaReadBufferSize> buffer {};
            QByteArray response;
            VisaApi::VisaStatus readStatus = 0;
            do {
                VisaApi::VisaUInt32 readCount = 0;
                readStatus = visa_->viRead(
                    instrument,
                    buffer.data(),
                    static_cast<VisaApi::VisaUInt32>(buffer.size()),
                    &readCount);
                if (readCount > 0) {
                    response.append(reinterpret_cast<const char*>(buffer.data()), static_cast<int>(readCount));
                }
            } while (readStatus == kVisaSuccessMaxCount);
            idn = QString::fromLatin1(response).trimmed();
        }

        visa_->viClose(instrument);

        if (idn.contains(QStringLiteral("RIGOL"), Qt::CaseInsensitive)) {
            rigolResources.push_back(resource);
            emit logMessage(QStringLiteral("自动识别到 RIGOL USB信号源：%1，设备信息：%2").arg(resource, idn));
        }
    }

    rigolResources.removeDuplicates();
    return rigolResources;
}

bool RigolVisaClient::connectToDevice(const QString& resourceName)
{
    const QString normalizedResourceName = resourceName.trimmed();
    if (normalizedResourceName.isEmpty()) {
        emit logMessage(QStringLiteral("RIGOL USB信号源连接失败：USB资源名为空，自动识别未找到 RIGOL 设备。"));
        return false;
    }
    if (!ensureResourceManager()) {
        emit connectedChanged(false);
        return false;
    }

    disconnectDevice();

    VisaApi::VisaSession instrument = kVisaNull;
    const QByteArray resourceBytes = normalizedResourceName.toLatin1();
    const VisaApi::VisaStatus status = visa_->viOpen(
        visa_->resourceManager,
        resourceBytes.constData(),
        kVisaNull,
        kConnectTimeoutMs,
        &instrument);
    if (!visaStatusOk(status) || instrument == kVisaNull) {
        emit logMessage(QStringLiteral("RIGOL USB信号源连接失败：%1，viOpen status=%2")
                            .arg(normalizedResourceName, visaStatusText(status)));
        emit connectedChanged(false);
        return false;
    }

    visa_->instrument = instrument;
    resourceName_ = normalizedResourceName;
    setVisaTimeout(kQueryTimeoutMs);

    const QString idn = queryScpi(QStringLiteral("*IDN?"));
    if (!idn.contains(QStringLiteral("RIGOL"), Qt::CaseInsensitive)) {
        emit logMessage(QStringLiteral("RIGOL USB信号源连接失败：*IDN? 未返回 RIGOL，响应：%1")
                            .arg(idn.isEmpty() ? QStringLiteral("未回包") : idn));
        closeInstrument();
        return false;
    }

    deviceInfo_ = idn;
    emit logMessage(QStringLiteral("RIGOL USB信号源连接成功：%1").arg(resourceName_));
    emit logMessage(QStringLiteral("RIGOL设备信息：%1").arg(deviceInfo_));
    emit connectedChanged(true);
    return true;
}

void RigolVisaClient::disconnectDevice()
{
    closeInstrument();
}

void RigolVisaClient::closeInstrument()
{
    if (visa_ == nullptr || visa_->instrument == kVisaNull) {
        return;
    }

    const bool wasConnected = isConnected();
    if (visa_->viClose != nullptr) {
        visa_->viClose(visa_->instrument);
    }
    visa_->instrument = kVisaNull;
    deviceInfo_.clear();
    resourceName_.clear();
    if (wasConnected) {
        emit connectedChanged(false);
    }
}

void RigolVisaClient::closeResourceManager()
{
    if (visa_ == nullptr || visa_->resourceManager == kVisaNull) {
        return;
    }
    if (visa_->viClose != nullptr) {
        visa_->viClose(visa_->resourceManager);
    }
    visa_->resourceManager = kVisaNull;
}

bool RigolVisaClient::isConnected() const
{
    return visa_ != nullptr && visa_->instrument != kVisaNull;
}

bool RigolVisaClient::setVisaTimeout(int timeoutMs)
{
    if (!isConnected() || visa_->viSetAttribute == nullptr) {
        return true;
    }

    const int boundedTimeoutMs = std::clamp(timeoutMs, 100, 60000);
    const VisaApi::VisaStatus status = visa_->viSetAttribute(
        visa_->instrument,
        kVisaAttrTimeoutValue,
        static_cast<VisaApi::VisaUInt32>(boundedTimeoutMs));
    if (!visaStatusOk(status)) {
        emit logMessage(QStringLiteral("VISA超时时间设置失败：timeout=%1 ms，status=%2")
                            .arg(boundedTimeoutMs)
                            .arg(visaStatusText(status)));
        return false;
    }
    return true;
}

bool RigolVisaClient::writeScpi(const QString& cmd)
{
    if (!isConnected()) {
        emit logMessage(QStringLiteral("RIGOL写入失败：信号源未连接，命令 %1").arg(cmd.trimmed()));
        return false;
    }

    setVisaTimeout(kWriteTimeoutMs);

    const QByteArray bytes = normalizedCommand(cmd).toLatin1();
    VisaApi::VisaUInt32 written = 0;
    const VisaApi::VisaStatus status = visa_->viWrite(
        visa_->instrument,
        reinterpret_cast<const unsigned char*>(bytes.constData()),
        static_cast<VisaApi::VisaUInt32>(bytes.size()),
        &written);
    if (!visaStatusOk(status) || written != static_cast<VisaApi::VisaUInt32>(bytes.size())) {
        emit logMessage(QStringLiteral("RIGOL写入失败：%1，written=%2 expected=%3，VISA status=%4")
                            .arg(cmd.trimmed())
                            .arg(written)
                            .arg(bytes.size())
                            .arg(visaStatusText(status)));
        return false;
    }
    return true;
}

QString RigolVisaClient::queryScpi(const QString& cmd, int timeoutMs)
{
    if (!writeScpi(cmd)) {
        return {};
    }

    const int boundedTimeoutMs = timeoutMs > 0 ? timeoutMs : kQueryTimeoutMs;
    setVisaTimeout(boundedTimeoutMs);

    QByteArray response;
    std::array<unsigned char, kVisaReadBufferSize> buffer {};
    VisaApi::VisaStatus status = 0;
    do {
        VisaApi::VisaUInt32 readCount = 0;
        status = visa_->viRead(
            visa_->instrument,
            buffer.data(),
            static_cast<VisaApi::VisaUInt32>(buffer.size()),
            &readCount);
        if (readCount > 0) {
            response.append(reinterpret_cast<const char*>(buffer.data()), static_cast<int>(readCount));
        }
    } while (status == kVisaSuccessMaxCount);

    if (!visaStatusOk(status) && response.isEmpty()) {
        if (status == kVisaErrorTimeout) {
            emit logMessage(QStringLiteral("RIGOL查询超时：%1，timeout=%2 ms").arg(cmd.trimmed()).arg(boundedTimeoutMs));
        } else {
            emit logMessage(QStringLiteral("RIGOL查询失败：%1，VISA status=%2").arg(cmd.trimmed(), visaStatusText(status)));
        }
        return {};
    }

    const QString text = QString::fromLatin1(response).trimmed();
    if (text.isEmpty()) {
        emit logMessage(QStringLiteral("RIGOL查询无返回：%1").arg(cmd.trimmed()));
    }
    return text;
}

QString RigolVisaClient::getDeviceInfo()
{
    const QString info = queryScpi(QStringLiteral("*IDN?"));
    if (!info.isEmpty()) {
        deviceInfo_ = info;
    }
    return info.isEmpty() ? deviceInfo_ : info;
}

bool RigolVisaClient::clearStatus()
{
    return writeScpi(QStringLiteral("*CLS"));
}

bool RigolVisaClient::initUltrasoundSignal(double dutyCyclePercent)
{
    if (!validateDutyCycle(dutyCyclePercent)) {
        return false;
    }

    if (!outputOff()
        || !clearStatus()
        || !setPulseWave()
        || !setFrequencyMHz()
        || !setVoltageUnitVpp()
        || !setVoltageVpp()
        || !setDutyCycle(dutyCyclePercent)
        || !setLoad50Ohm()) {
        return false;
    }

    const QString errorText = getError();
    if (errorText.isEmpty()) {
        return false;
    }
    emit logMessage(QStringLiteral("RIGOL错误查询：%1").arg(errorText));
    return scpiErrorIsNoError(errorText);
}

bool RigolVisaClient::setPulseWave()
{
    return writeScpi(QStringLiteral(":SOURce1:FUNCtion PULSe"));
}

bool RigolVisaClient::setFrequencyMHz()
{
    return writeScpi(QStringLiteral(":SOURce1:FREQuency %1")
                         .arg(QString::fromLatin1(kUltrasoundFrequencyScpiValue)));
}

QString RigolVisaClient::getFrequency()
{
    return queryScpi(QStringLiteral(":SOURce1:FREQuency?"));
}

bool RigolVisaClient::setVoltageUnitVpp()
{
    return writeScpi(QStringLiteral(":SOURce1:VOLTage:UNIT VPP"));
}

bool RigolVisaClient::setVoltageVpp()
{
    return writeScpi(QStringLiteral(":SOURce1:VOLTage %1")
                         .arg(QString::fromLatin1(kUltrasoundVoltageScpiValue)));
}

QString RigolVisaClient::getVoltage()
{
    return queryScpi(QStringLiteral(":SOURce1:VOLTage?"));
}

bool RigolVisaClient::setDutyCycle(double dutyCyclePercent)
{
    if (!validateDutyCycle(dutyCyclePercent)) {
        return false;
    }
    return writeScpi(QStringLiteral(":SOURce1:FUNCtion:PULSe:DCYCle %1")
                         .arg(dutyCyclePercent, 0, 'f', 1));
}

QString RigolVisaClient::getDutyCycle()
{
    return queryScpi(QStringLiteral(":SOURce1:FUNCtion:PULSe:DCYCle?"));
}

bool RigolVisaClient::setLoad50Ohm()
{
    return writeScpi(QStringLiteral(":OUTPut1:LOAD %1")
                         .arg(QString::fromLatin1(kUltrasoundLoadScpiValue)));
}

QString RigolVisaClient::getLoad()
{
    return queryScpi(QStringLiteral(":OUTPut1:LOAD?"));
}

bool RigolVisaClient::outputOn()
{
    return writeScpi(QStringLiteral(":OUTPut1:STATe ON"));
}

bool RigolVisaClient::outputOff()
{
    return writeScpi(QStringLiteral(":OUTPut1:STATe OFF"));
}

QString RigolVisaClient::getOutputState()
{
    return queryScpi(QStringLiteral(":OUTPut1:STATe?"));
}

QString RigolVisaClient::getError()
{
    return queryScpi(QStringLiteral(":SYSTem:ERRor?"));
}

bool RigolVisaClient::validateDutyCycle(double dutyCyclePercent)
{
    if (!std::isfinite(dutyCyclePercent)
        || dutyCyclePercent < kMinimumDutyCyclePercent
        || dutyCyclePercent > kMaximumDutyCyclePercent) {
        emit logMessage(QStringLiteral("RIGOL占空比无效：%1，范围必须为 1.0-99.0")
                            .arg(dutyCyclePercent, 0, 'f', 1));
        return false;
    }
    return true;
}

}  // namespace panthera::adapters::rigol
