#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QVector>

namespace diji::adapters::uim {

struct UimDeviceInfo {
    quint32 deviceType {0};
    quint32 deviceIndex {0};
    quint32 comIndex {0};
    quint32 baudRate {0};
    QString name;
    quint32 protocol {0};
};

struct UimNodeInfo {
    quint32 nodeId {0};
    QString modelName;
    quint32 firmwareVersion {0};
    quint32 current {0};
    bool integratedEncoder {false};
    bool encoderEnabled {false};
    bool motionSupported {false};
};

struct UimMotorSnapshot {
    bool gatewayOpen {false};
    quint32 deviceIndex {0};
    quint32 nodeId {0};
    bool enabled {false};
    bool direction {false};
    bool currentReduced {false};
    int microstep {0};
    int current {0};
    int speed {0};
    int step {0};
    bool hasPosition {false};
    int position {0};
    bool hasEncoderPosition {false};
    int encoderPosition {0};
    bool hasSensorFeedback {false};
    bool sensor1 {false};
    bool sensor2 {false};
    bool sensor3 {false};
    double analogInput {0.0};
    QString updatedAt;
};

struct UimSensorDebounceConfig {
    int sensor1Milliseconds {0};
    int sensor2Milliseconds {0};
    int sensor3Milliseconds {0};
};

struct UimSensorActionConfig {
    int sensor1RisingAction {0};
    int sensor1FallingAction {0};
    int sensor2RisingAction {0};
    int sensor2FallingAction {0};
    int sensor3RisingAction {0};
    int sensor3FallingAction {0};
    bool sensor3Available {false};
};

class UimMotorGateway final : public QObject {
    Q_OBJECT

public:
    explicit UimMotorGateway(QObject* parent = nullptr);
    ~UimMotorGateway() override;

    bool loadSdk(const QString& dllPath, QString* errorMessage = nullptr);
    void unloadSdk();
    [[nodiscard]] bool isSdkLoaded() const;
    [[nodiscard]] QString sdkPath() const;

    QVector<UimDeviceInfo> searchGateways(QString* errorMessage = nullptr);
    bool openGateway(quint32 deviceIndex, QString* errorMessage = nullptr);
    void closeGateway();
    [[nodiscard]] bool isGatewayOpen() const;
    [[nodiscard]] quint32 deviceIndex() const;
    [[nodiscard]] quint32 canBitRate() const;
    [[nodiscard]] QVector<UimNodeInfo> nodes() const;

    bool selectNode(quint32 nodeId, QString* errorMessage = nullptr);
    [[nodiscard]] quint32 selectedNodeId() const;
    [[nodiscard]] bool hasSelectedNode() const;

    bool enableMotor(QString* errorMessage = nullptr);
    bool disableMotor(QString* errorMessage = nullptr);
    bool home(QString* errorMessage = nullptr);
    bool setCurrentAmps(double currentAmps, QString* errorMessage = nullptr);
    bool setSpeed(int speed, QString* errorMessage = nullptr);
    bool setStep(int step, QString* errorMessage = nullptr);
    bool setOpenLoopPosition(int position, QString* errorMessage = nullptr);
    bool setClosedLoopEncoderPosition(int encoderPosition, QString* errorMessage = nullptr);
    bool setDigitalOutput(bool high, QString* errorMessage = nullptr);
    bool readDigitalOutput(bool* high, QString* errorMessage = nullptr);
    bool refreshSnapshot(QString* errorMessage = nullptr);
    bool refreshSensorFeedback(QString* errorMessage = nullptr);
    bool readSensorDebounceConfig(UimSensorDebounceConfig* config, QString* errorMessage = nullptr);
    bool setSensorDebounce(int sensorIndex, int milliseconds, QString* errorMessage = nullptr);
    bool readSensorActionConfig(UimSensorActionConfig* config, QString* errorMessage = nullptr);
    bool setSensorActionConfig(const UimSensorActionConfig& config, QString* errorMessage = nullptr);

    [[nodiscard]] UimMotorSnapshot latestSnapshot() const;
    [[nodiscard]] QString lastError() const;

signals:
    void gatewayOpened();
    void gatewayClosed();
    void nodesChanged();
    void snapshotChanged(const diji::adapters::uim::UimMotorSnapshot& snapshot);
    void errorOccurred(const QString& message);

private:
    struct Functions;

    bool requireSdk(QString* errorMessage) const;
    bool requireReady(QString* errorMessage) const;
    bool setError(const QString& message, QString* errorMessage) const;
    QString formatSdkError(const QString& action) const;
    bool invokeSetValue(const char* actionName, quint32 result, int returnedValue, QString* errorMessage);
    bool updateBasicFeedback(QString* errorMessage);
    static bool isFailure(quint32 result);

    std::unique_ptr<Functions> m_functions;
    QString m_sdkPath;
    QVector<UimDeviceInfo> m_devices;
    QVector<UimNodeInfo> m_nodes;
    quint32 m_deviceIndex {0};
    quint32 m_canBitRate {0};
    quint32 m_selectedNodeId {0};
    bool m_gatewayOpen {false};
    UimMotorSnapshot m_snapshot;
    mutable QString m_lastError;
};

}  // namespace diji::adapters::uim
