#pragma once

#include <memory>

#include <QLibrary>
#include <QObject>
#include <QString>
#include <QVector>

#include "uim_can_protocol.h"

namespace panthera::tools::uim_limit {

struct UimGatewayInfo {
    quint32 deviceType {0};
    quint32 deviceIndex {0};
    quint32 comIndex {0};
    quint32 baudRate {0};
    QString name;
    quint32 protocol {0};
};

class UimPassiveGateway final : public QObject {
    Q_OBJECT

public:
    // Public only so the C ABI callback type can be declared exactly; callers
    // receive the converted RawCanFrame signal instead of this SDK structure.
    struct CanMessageObject;

    explicit UimPassiveGateway(QObject* parent = nullptr);
    ~UimPassiveGateway() override;

    bool loadSdk(const QString& dllPath, QString* errorMessage = nullptr);
    void unloadSdk();
    [[nodiscard]] bool isSdkLoaded() const;
    [[nodiscard]] QString sdkPath() const;

    [[nodiscard]] QVector<UimGatewayInfo> searchGateways(QString* errorMessage = nullptr);
    bool openGateway(quint32 deviceIndex, QString* errorMessage = nullptr);
    void closeGateway();

    [[nodiscard]] bool isGatewayOpen() const;
    [[nodiscard]] quint32 deviceIndex() const;
    [[nodiscard]] quint32 canBitRate() const;
    [[nodiscard]] quint32 callbackRegistrationResult() const;
    [[nodiscard]] QVector<quint32> nodeIds() const;
    [[nodiscard]] QString lastError() const;

signals:
    void frameReceived(const panthera::tools::uim_limit::RawCanFrame& frame);
    void gatewayClosed();
    void diagnosticMessage(const QString& category, const QString& message);
    void errorOccurred(const QString& message);

private:
    struct Functions;

    static void sdkSensorCallback(quint32 deviceIndex, CanMessageObject* message, quint32 messageLength);
    bool setError(const QString& message, QString* errorMessage);
    [[nodiscard]] QString formatSdkError(const QString& action) const;
    [[nodiscard]] static bool isFailure(quint32 result);

    std::unique_ptr<Functions> m_functions;
    QString m_sdkPath;
    QVector<UimGatewayInfo> m_gateways;
    QVector<quint32> m_nodeIds;
    quint32 m_deviceIndex {0};
    quint32 m_canBitRate {0};
    quint32 m_callbackRegistrationResult {0xFFFFFFFFU};
    bool m_gatewayOpen {false};
    bool m_callbackRegistered {false};
    QString m_lastError;
};

}  // namespace panthera::tools::uim_limit

