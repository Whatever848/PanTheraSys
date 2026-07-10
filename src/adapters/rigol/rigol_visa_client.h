#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace panthera::adapters::rigol {

class RigolVisaClient final : public QObject {
    Q_OBJECT

public:
    explicit RigolVisaClient(QObject* parent = nullptr);
    ~RigolVisaClient() override;

    QStringList searchUsbResources();
    QStringList searchRigolUsbResources(int timeoutMs = 1200);
    bool connectToDevice(const QString& resourceName);
    void disconnectDevice();
    [[nodiscard]] bool isConnected() const;

    bool writeScpi(const QString& cmd);
    QString queryScpi(const QString& cmd, int timeoutMs = 3000);

    QString getDeviceInfo();
    bool clearStatus();

    bool initUltrasoundSignal(double dutyCyclePercent);

    bool setPulseWave();

    bool setFrequencyMHz();
    QString getFrequency();

    bool setVoltageUnitVpp();
    bool setVoltageVpp();
    QString getVoltage();

    bool setDutyCycle(double dutyCyclePercent);
    QString getDutyCycle();

    bool setLoad50Ohm();
    QString getLoad();

    bool outputOn();
    bool outputOff();
    QString getOutputState();

    QString getError();

signals:
    void logMessage(const QString& message);
    void connectedChanged(bool connected);

private:
    struct VisaApi;

    bool ensureVisaLoaded();
    bool ensureResourceManager();
    void closeInstrument();
    void closeResourceManager();
    bool setVisaTimeout(int timeoutMs);
    bool validateDutyCycle(double dutyCyclePercent);

    VisaApi* visa_ {nullptr};
    QString resourceName_;
    QString deviceInfo_;
};

}  // namespace panthera::adapters::rigol
