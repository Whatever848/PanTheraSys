#pragma once

#include <QDateTime>
#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include "limit_test_session.h"
#include "uim_passive_gateway.h"

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace panthera::tools::uim_limit {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct TestPanel {
        QString title;
        QSpinBox* nodeIdSpin {nullptr};
        QLabel* currentLevelLabel {nullptr};
        QLabel* lastEventLabel {nullptr};
        QLabel* lastTimeLabel {nullptr};
        QLabel* triggerTimeLabel {nullptr};
        QLabel* restoreTimeLabel {nullptr};
        QLabel* resultLabel {nullptr};
        QLabel* promptLabel {nullptr};
        QPushButton* startButton {nullptr};
        QPushButton* finishButton {nullptr};
        QTimer* timeoutTimer {nullptr};
        LimitTestSession session;
        quint64 hardwareFramesAtStart {0};
        quint64 targetFramesAtStart {0};
        quint64 targetS3EventsAtStart {0};
    };

    void buildUi();
    QWidget* createConnectionPanel();
    QWidget* createTestPanel(TestPanel* panel, const QString& title, int defaultNodeId);
    QWidget* createMonitorPanel();
    QWidget* createSimulationPanel();
    void loadSettings();
    void saveSettings() const;
    [[nodiscard]] QString settingsPath() const;

    void browseSdk();
    void refreshChannels();
    void toggleGateway();
    void setGatewayUiConnected(bool connected);
    void handleFrame(const RawCanFrame& rawFrame);
    void appendFrameRow(const ParsedCanFrame& frame);
    void updatePanelFromFrame(TestPanel* panel, const ParsedCanFrame& frame);
    void startTest(TestPanel* panel);
    void finishTest(TestPanel* panel, bool timeout);
    void refreshTestPanel(TestPanel* panel);
    void setPanelResultStyle(TestPanel* panel, const QString& state);
    void injectSimulation(quint32 nodeId, S3Edge edge);
    void saveLog();
    void appendSystemMessage(const QString& message);
    void appendDiagnostic(const QString& level, const QString& category, const QString& message);
    void checkListeningHealth();
    void appendTestFailureDiagnosis(TestPanel* panel);

    UimPassiveGateway* m_gateway {nullptr};
    QLineEdit* m_sdkPathEdit {nullptr};
    QPushButton* m_browseSdkButton {nullptr};
    QPushButton* m_refreshChannelsButton {nullptr};
    QComboBox* m_channelCombo {nullptr};
    QPushButton* m_gatewayButton {nullptr};
    QLabel* m_gatewayStatusLabel {nullptr};
    QLabel* m_osSerialPortsLabel {nullptr};
    QLabel* m_nodeSummaryLabel {nullptr};
    QSpinBox* m_timeoutSpin {nullptr};
    QTableWidget* m_frameTable {nullptr};
    QPlainTextEdit* m_diagnosticLog {nullptr};
    QLabel* m_systemMessageLabel {nullptr};
    QTimer* m_listenWatchdog {nullptr};
    TestPanel m_upperPanel;
    TestPanel m_lowerPanel;
    QVector<UimGatewayInfo> m_gateways;
    QStringList m_diagnosticEntries;
    QDateTime m_gatewayOpenedAt;
    QDateTime m_lastHardwareFrameAt;
    quint64 m_hardwareFrameCount {0};
    QHash<quint32, quint64> m_hardwareNodeFrameCounts;
    QHash<quint32, quint64> m_hardwareS3EventCounts;
    int m_lastNoFrameWarningSeconds {0};
};

}  // namespace panthera::tools::uim_limit

