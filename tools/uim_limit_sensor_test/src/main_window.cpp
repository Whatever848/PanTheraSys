#include "main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringConverter>
#include <QStyle>
#include <QSplitter>
#include <QSysInfo>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace panthera::tools::uim_limit {

namespace {

constexpr int kMaximumLogRows = 5000;

QString sdkReturnText(quint32 result)
{
    return QStringLiteral("%1 (0x%2)")
        .arg(result)
        .arg(QString::number(result, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

QString displayTime(const QDateTime& time)
{
    return time.isValid()
        ? time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        : QStringLiteral("-");
}

QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString nodeListText(const QVector<quint32>& nodeIds)
{
    if (nodeIds.isEmpty()) {
        return QStringLiteral("未返回 CAN 节点");
    }
    QStringList values;
    values.reserve(nodeIds.size());
    for (const quint32 nodeId : nodeIds) {
        values.append(QString::number(nodeId));
    }
    return QStringLiteral("CAN 节点：%1").arg(values.join(QStringLiteral(", ")));
}

QString gatewayTypeText(quint32 deviceType)
{
    switch (deviceType) {
    case 0x01U:
        return QStringLiteral("RS232-CAN");
    case 0x02U:
        return QStringLiteral("WiFi-RS232-CAN");
    case 0x04U:
        return QStringLiteral("USB-CAN");
    case 0x08U:
        return QStringLiteral("PCI-CAN");
    default:
        return QStringLiteral("未知类型 0x%1")
            .arg(QString::number(deviceType, 16).toUpper().rightJustified(2, QLatin1Char('0')));
    }
}

QString sdkPortText(const UimGatewayInfo& gateway)
{
    if (gateway.deviceType == 0x04U && gateway.comIndex == 0U) {
        return QStringLiteral("USB直连（SDK COM字段=0，不用于打开网关）");
    }
    return QStringLiteral("SDK COM字段=%1").arg(gateway.comIndex);
}

QStringList windowsSerialPorts()
{
    QStringList ports;
    for (const QSerialPortInfo& serialPort : QSerialPortInfo::availablePorts()) {
        const QString port = serialPort.portName().trimmed();
        if (!port.isEmpty() && !ports.contains(port, Qt::CaseInsensitive)) {
            ports.append(port);
        }
    }
    ports.sort(Qt::CaseInsensitive);
    return ports;
}

quint64 countSince(quint64 current, quint64 baseline)
{
    // Reopening a gateway resets the counters. Avoid an unsigned underflow if
    // a test was already active when that happened.
    return current >= baseline ? current - baseline : current;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_gateway(new UimPassiveGateway(this))
{
    buildUi();
    loadSettings();

    connect(m_gateway, &UimPassiveGateway::frameReceived, this, &MainWindow::handleFrame);
    connect(m_gateway, &UimPassiveGateway::diagnosticMessage, this, [this](const QString& category, const QString& message) {
        appendDiagnostic(QStringLiteral("INFO"), category, message);
    });
    connect(m_gateway, &UimPassiveGateway::errorOccurred, this, [this](const QString& message) {
        if (m_systemMessageLabel != nullptr) {
            m_systemMessageLabel->setText(QStringLiteral("%1  %2")
                                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
        }
        statusBar()->showMessage(message, 8000);
        appendDiagnostic(QStringLiteral("ERROR"), QStringLiteral("SDK"), message);
    });

    m_listenWatchdog = new QTimer(this);
    m_listenWatchdog->setInterval(5000);
    connect(m_listenWatchdog, &QTimer::timeout, this, &MainWindow::checkListeningHealth);

    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("启动"),
        QStringLiteral("程序=%1；Qt=%2；CPU=%3；进程位数=%4")
            .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()))
            .arg(QString::fromLatin1(qVersion()))
            .arg(QSysInfo::currentCpuArchitecture())
            .arg(sizeof(void*) * 8));
    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("安全"),
        QStringLiteral("当前为只读 RTCN 模式；未绑定电机运动、参数写入或 EEPROM API"));

    setWindowTitle(QStringLiteral("PanTheraSys · 液位上下限位传感器测试"));
    resize(1480, 940);
    setMinimumSize(1180, 760);
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    m_listenWatchdog->stop();
    m_gateway->closeGateway();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("limitTestPage"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(20, 18, 20, 18);
    rootLayout->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("液位上下限位传感器测试"), central);
    title->setObjectName(QStringLiteral("pageTitle"));
    rootLayout->addWidget(title);

    auto* safetyNotice = new QLabel(
        QStringLiteral("只读安全模式：本工具仅打开 UIROBOT 网关并注册 RTCN 接收回调，不发送运动命令，"
                       "不修改 MCFG/SCFG/S34CON、驱动器 ID 或 EEPROM。请勿与 PanTheraConsole 同时占用同一 USB-CAN 网关。"),
        central);
    safetyNotice->setObjectName(QStringLiteral("safetyNotice"));
    safetyNotice->setWordWrap(true);
    rootLayout->addWidget(safetyNotice);

    rootLayout->addWidget(createConnectionPanel());

    auto* testsRow = new QWidget(central);
    auto* testsLayout = new QHBoxLayout(testsRow);
    testsLayout->setContentsMargins(0, 0, 0, 0);
    testsLayout->setSpacing(14);
    testsLayout->addWidget(createTestPanel(&m_upperPanel, QStringLiteral("上限位测试"), 6), 1);
    testsLayout->addWidget(createTestPanel(&m_lowerPanel, QStringLiteral("下限位测试"), 7), 1);
    rootLayout->addWidget(testsRow);

    rootLayout->addWidget(createMonitorPanel(), 1);
    rootLayout->addWidget(createSimulationPanel());

    m_systemMessageLabel = new QLabel(QStringLiteral("就绪：尚未连接 USB-CAN；可先使用模拟数据验证解析与测试流程。"), central);
    m_systemMessageLabel->setObjectName(QStringLiteral("systemMessage"));
    m_systemMessageLabel->setWordWrap(true);
    rootLayout->addWidget(m_systemMessageLabel);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("mainScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(central);
    setCentralWidget(scrollArea);
    statusBar()->showMessage(QStringLiteral("被动监听模式"));
}

QWidget* MainWindow::createConnectionPanel()
{
    auto* group = new QGroupBox(QStringLiteral("USB-CAN 连接"), this);
    group->setMinimumHeight(180);
    auto* layout = new QGridLayout(group);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(9);

    m_sdkPathEdit = new QLineEdit(group);
    m_browseSdkButton = new QPushButton(QStringLiteral("选择 DLL"), group);
    m_refreshChannelsButton = new QPushButton(QStringLiteral("刷新 USB-CAN 通道"), group);
    m_channelCombo = new QComboBox(group);
    m_channelCombo->addItem(QStringLiteral("请先刷新通道"));
    m_gatewayButton = new QPushButton(QStringLiteral("打开并监听"), group);
    m_gatewayButton->setObjectName(QStringLiteral("primaryButton"));
    m_gatewayStatusLabel = new QLabel(QStringLiteral("未连接"), group);
    m_gatewayStatusLabel->setObjectName(QStringLiteral("connectionState"));
    m_osSerialPortsLabel = new QLabel(QStringLiteral("刷新通道时读取 Windows 串口"), group);
    m_osSerialPortsLabel->setWordWrap(true);
    m_nodeSummaryLabel = new QLabel(QStringLiteral("节点信息将在打开网关后显示"), group);
    m_timeoutSpin = new QSpinBox(group);
    m_timeoutSpin->setRange(10, 600);
    m_timeoutSpin->setSuffix(QStringLiteral(" 秒"));
    m_timeoutSpin->setValue(60);

    layout->addWidget(new QLabel(QStringLiteral("SDK DLL"), group), 0, 0);
    layout->addWidget(m_sdkPathEdit, 0, 1, 1, 4);
    layout->addWidget(m_browseSdkButton, 0, 5);
    layout->addWidget(new QLabel(QStringLiteral("CAN 通道"), group), 1, 0);
    layout->addWidget(m_channelCombo, 1, 1, 1, 2);
    layout->addWidget(m_refreshChannelsButton, 1, 3);
    layout->addWidget(m_gatewayButton, 1, 4);
    layout->addWidget(m_gatewayStatusLabel, 1, 5);
    layout->addWidget(new QLabel(QStringLiteral("Windows 串口"), group), 2, 0);
    layout->addWidget(m_osSerialPortsLabel, 2, 1, 1, 5);
    layout->addWidget(new QLabel(QStringLiteral("单次测试超时"), group), 3, 0);
    layout->addWidget(m_timeoutSpin, 3, 1);
    layout->addWidget(m_nodeSummaryLabel, 3, 2, 1, 4);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);

    connect(m_browseSdkButton, &QPushButton::clicked, this, &MainWindow::browseSdk);
    connect(m_refreshChannelsButton, &QPushButton::clicked, this, &MainWindow::refreshChannels);
    connect(m_gatewayButton, &QPushButton::clicked, this, &MainWindow::toggleGateway);
    return group;
}

QWidget* MainWindow::createTestPanel(TestPanel* panel, const QString& title, int defaultNodeId)
{
    panel->title = title;
    auto* group = new QGroupBox(title, this);
    group->setMinimumHeight(230);
    auto* layout = new QGridLayout(group);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(7);

    panel->nodeIdSpin = new QSpinBox(group);
    panel->nodeIdSpin->setRange(1, 127);
    panel->nodeIdSpin->setValue(defaultNodeId);
    panel->currentLevelLabel = new QLabel(QStringLiteral("未知 / 未收到事件"), group);
    panel->lastEventLabel = new QLabel(QStringLiteral("-"), group);
    panel->lastTimeLabel = new QLabel(QStringLiteral("-"), group);
    panel->triggerTimeLabel = new QLabel(QStringLiteral("-"), group);
    panel->restoreTimeLabel = new QLabel(QStringLiteral("-"), group);
    panel->resultLabel = new QLabel(QStringLiteral("未开始"), group);
    panel->resultLabel->setObjectName(QStringLiteral("testResult"));
    panel->resultLabel->setProperty("testState", QStringLiteral("idle"));
    panel->resultLabel->setWordWrap(true);
    panel->promptLabel = new QLabel(QStringLiteral("点击开始后，先手动触发，再恢复开关。"), group);
    panel->promptLabel->setObjectName(QStringLiteral("testPrompt"));
    panel->promptLabel->setWordWrap(true);
    panel->startButton = new QPushButton(QStringLiteral("开始%1").arg(title), group);
    panel->startButton->setObjectName(QStringLiteral("primaryButton"));
    panel->finishButton = new QPushButton(QStringLiteral("结束测试"), group);
    panel->finishButton->setEnabled(false);
    panel->timeoutTimer = new QTimer(group);
    panel->timeoutTimer->setSingleShot(true);

    layout->addWidget(new QLabel(QStringLiteral("驱动器 ID"), group), 0, 0);
    layout->addWidget(panel->nodeIdSpin, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("当前状态"), group), 1, 0);
    layout->addWidget(panel->currentLevelLabel, 1, 1);
    layout->addWidget(new QLabel(QStringLiteral("最后事件码"), group), 2, 0);
    layout->addWidget(panel->lastEventLabel, 2, 1);
    layout->addWidget(new QLabel(QStringLiteral("最后变化时间"), group), 3, 0);
    layout->addWidget(panel->lastTimeLabel, 3, 1);
    layout->addWidget(new QLabel(QStringLiteral("触发时间"), group), 4, 0);
    layout->addWidget(panel->triggerTimeLabel, 4, 1);
    layout->addWidget(new QLabel(QStringLiteral("恢复时间"), group), 5, 0);
    layout->addWidget(panel->restoreTimeLabel, 5, 1);
    layout->addWidget(new QLabel(QStringLiteral("测试结果"), group), 6, 0);
    layout->addWidget(panel->resultLabel, 6, 1);
    layout->addWidget(panel->promptLabel, 7, 0, 1, 2);

    auto* buttonRow = new QWidget(group);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(panel->startButton);
    buttonLayout->addWidget(panel->finishButton);
    layout->addWidget(buttonRow, 8, 0, 1, 2);
    layout->setColumnStretch(1, 1);

    connect(panel->startButton, &QPushButton::clicked, this, [this, panel]() { startTest(panel); });
    connect(panel->finishButton, &QPushButton::clicked, this, [this, panel]() { finishTest(panel, false); });
    connect(panel->timeoutTimer, &QTimer::timeout, this, [this, panel]() { finishTest(panel, true); });
    return group;
}

QWidget* MainWindow::createMonitorPanel()
{
    auto* group = new QGroupBox(QStringLiteral("原始 CAN 报文与解析结果"), this);
    group->setMinimumHeight(300);
    auto* layout = new QVBoxLayout(group);

    auto* splitter = new QSplitter(Qt::Vertical, group);
    m_frameTable = new QTableWidget(0, 12, splitter);
    m_frameTable->setHorizontalHeaderLabels({
        QStringLiteral("时间戳"),
        QStringLiteral("来源"),
        QStringLiteral("DeviceIndex"),
        QStringLiteral("CAN ID"),
        QStringLiteral("帧类型"),
        QStringLiteral("DLC"),
        QStringLiteral("原始数据"),
        QStringLiteral("回调长度"),
        QStringLiteral("驱动器 ID"),
        QStringLiteral("CAN RCW"),
        QStringLiteral("S3 事件码"),
        QStringLiteral("解析结果")
    });
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setAlternatingRowColors(true);
    m_frameTable->verticalHeader()->setVisible(false);
    m_frameTable->horizontalHeader()->setStretchLastSection(true);
    m_frameTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(11, QHeaderView::Stretch);

    auto* diagnosticPanel = new QWidget(splitter);
    auto* diagnosticLayout = new QVBoxLayout(diagnosticPanel);
    diagnosticLayout->setContentsMargins(0, 4, 0, 0);
    auto* diagnosticTitle = new QLabel(QStringLiteral("监听诊断日志（SDK → 回调 → 解析 → 节点路由 → 测试状态）"), diagnosticPanel);
    diagnosticTitle->setObjectName(QStringLiteral("diagnosticTitle"));
    m_diagnosticLog = new QPlainTextEdit(diagnosticPanel);
    m_diagnosticLog->setObjectName(QStringLiteral("diagnosticLog"));
    m_diagnosticLog->setReadOnly(true);
    m_diagnosticLog->setMaximumBlockCount(kMaximumLogRows);
    diagnosticLayout->addWidget(diagnosticTitle);
    diagnosticLayout->addWidget(m_diagnosticLog, 1);

    splitter->addWidget(m_frameTable);
    splitter->addWidget(diagnosticPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 210});
    layout->addWidget(splitter, 1);

    auto* controls = new QWidget(group);
    auto* controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto* clearButton = new QPushButton(QStringLiteral("清空报文"), controls);
    auto* clearDiagnosticButton = new QPushButton(QStringLiteral("清空诊断日志"), controls);
    auto* saveButton = new QPushButton(QStringLiteral("保存测试日志"), controls);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(clearButton);
    controlsLayout->addWidget(clearDiagnosticButton);
    controlsLayout->addWidget(saveButton);
    layout->addWidget(controls);

    connect(clearButton, &QPushButton::clicked, m_frameTable, [this]() { m_frameTable->setRowCount(0); });
    connect(clearDiagnosticButton, &QPushButton::clicked, this, [this]() {
        m_diagnosticEntries.clear();
        m_diagnosticLog->clear();
        appendDiagnostic(QStringLiteral("INFO"), QStringLiteral("界面"), QStringLiteral("诊断日志已由用户清空"));
    });
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveLog);
    return group;
}

QWidget* MainWindow::createSimulationPanel()
{
    auto* group = new QGroupBox(QStringLiteral("模拟数据验证（不连接硬件）"), this);
    group->setMinimumHeight(90);
    auto* layout = new QHBoxLayout(group);
    auto* upperFall = new QPushButton(QStringLiteral("上限位 S3 下降沿"), group);
    auto* upperRise = new QPushButton(QStringLiteral("上限位 S3 上升沿"), group);
    auto* lowerFall = new QPushButton(QStringLiteral("下限位 S3 下降沿"), group);
    auto* lowerRise = new QPushButton(QStringLiteral("下限位 S3 上升沿"), group);
    layout->addWidget(upperFall);
    layout->addWidget(upperRise);
    layout->addWidget(lowerFall);
    layout->addWidget(lowerRise);
    layout->addStretch(1);

    connect(upperFall, &QPushButton::clicked, this, [this]() {
        injectSimulation(static_cast<quint32>(m_upperPanel.nodeIdSpin->value()), S3Edge::Falling);
    });
    connect(upperRise, &QPushButton::clicked, this, [this]() {
        injectSimulation(static_cast<quint32>(m_upperPanel.nodeIdSpin->value()), S3Edge::Rising);
    });
    connect(lowerFall, &QPushButton::clicked, this, [this]() {
        injectSimulation(static_cast<quint32>(m_lowerPanel.nodeIdSpin->value()), S3Edge::Falling);
    });
    connect(lowerRise, &QPushButton::clicked, this, [this]() {
        injectSimulation(static_cast<quint32>(m_lowerPanel.nodeIdSpin->value()), S3Edge::Rising);
    });
    return group;
}

void MainWindow::loadSettings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const QString bundledDll = QCoreApplication::applicationDirPath() + QStringLiteral("/UISimCanFunc.dll");
    const QString defaultDll = QFileInfo::exists(bundledDll)
        ? bundledDll
        : QStringLiteral("D:/PanSoftware/UIMDemo/UISimCanFunc.dll");
    m_sdkPathEdit->setText(settings.value(QStringLiteral("uim/sdk_dll"), defaultDll).toString());
    m_upperPanel.nodeIdSpin->setValue(settings.value(QStringLiteral("uim/upper_node_id"), 6).toInt());
    m_lowerPanel.nodeIdSpin->setValue(settings.value(QStringLiteral("uim/lower_node_id"), 7).toInt());
    m_timeoutSpin->setValue(settings.value(QStringLiteral("uim/test_timeout_seconds"), 60).toInt());
}

void MainWindow::saveSettings() const
{
    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("uim/sdk_dll"), m_sdkPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("uim/upper_node_id"), m_upperPanel.nodeIdSpin->value());
    settings.setValue(QStringLiteral("uim/lower_node_id"), m_lowerPanel.nodeIdSpin->value());
    settings.setValue(QStringLiteral("uim/test_timeout_seconds"), m_timeoutSpin->value());
}

QString MainWindow::settingsPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/config/limit_sensor_test.ini");
}

void MainWindow::browseSdk()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 UISimCanFunc.dll"),
        QFileInfo(m_sdkPathEdit->text()).absolutePath(),
        QStringLiteral("UIROBOT SDK (UISimCanFunc.dll);;动态链接库 (*.dll)"));
    if (!path.isEmpty()) {
        m_sdkPathEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::refreshChannels()
{
    const QStringList serialPorts = windowsSerialPorts();
    const QString serialPortText = serialPorts.isEmpty()
        ? QStringLiteral("未读取到 Windows COM 端口")
        : serialPorts.join(QStringLiteral(", "));
    m_osSerialPortsLabel->setText(
        QStringLiteral("%1（仅供核对；USB-CAN 由 SDK 使用 DeviceIndex 打开）").arg(serialPortText));
    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("串口"),
        QStringLiteral("Windows 当前注册串口：%1；此列表不作为 OpenGateway 参数")
            .arg(serialPortText));

    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("操作"),
        QStringLiteral("用户请求刷新 USB-CAN 通道；DLL=%1")
            .arg(QDir::toNativeSeparators(m_sdkPathEdit->text().trimmed())));
    QString errorMessage;
    if (!m_gateway->loadSdk(m_sdkPathEdit->text().trimmed(), &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("SDK 加载失败"), errorMessage);
        return;
    }

    m_gateways = m_gateway->searchGateways(&errorMessage);
    m_channelCombo->clear();
    for (const UimGatewayInfo& gateway : m_gateways) {
        const QString name = gateway.name.isEmpty() ? QStringLiteral("USB-CAN") : gateway.name;
        m_channelCombo->addItem(
            QStringLiteral("%1 · %2 · DeviceIndex %3 · %4")
                .arg(name)
                .arg(gatewayTypeText(gateway.deviceType))
                .arg(gateway.deviceIndex)
                .arg(sdkPortText(gateway)),
            gateway.deviceIndex);
        if (gateway.deviceType == 0x04U && gateway.comIndex == 0U) {
            appendDiagnostic(
                QStringLiteral("INFO"),
                QStringLiteral("通道"),
                QStringLiteral("%1 被 SDK 识别为 USB-CAN；其 uiComIndex=0 是 SDK 返回字段，不代表 Windows COM0，也不会传给 OpenGateway。Windows 串口另列为：%2")
                    .arg(name, serialPortText));
        }
    }

    if (m_gateways.isEmpty()) {
        m_channelCombo->addItem(QStringLiteral("未搜索到 USB-CAN 通道"));
        appendSystemMessage(errorMessage.isEmpty()
                                ? QStringLiteral("未搜索到 USB-CAN 通道")
                                : errorMessage);
        return;
    }
    appendSystemMessage(QStringLiteral("已发现 %1 个 USB-CAN 通道；尚未打开 CAN 监听。").arg(m_gateways.size()));
}

void MainWindow::toggleGateway()
{
    if (m_gateway->isGatewayOpen()) {
        appendDiagnostic(QStringLiteral("INFO"), QStringLiteral("操作"), QStringLiteral("用户请求关闭 RTCN 监听"));
        m_listenWatchdog->stop();
        m_gateway->closeGateway();
        setGatewayUiConnected(false);
        appendSystemMessage(QStringLiteral("USB-CAN 网关已关闭。"));
        return;
    }

    if (m_channelCombo->currentData().isNull()) {
        QMessageBox::information(this, QStringLiteral("请选择通道"), QStringLiteral("请先刷新并选择 USB-CAN 通道。"));
        return;
    }

    QString errorMessage;
    const quint32 deviceIndex = m_channelCombo->currentData().toUInt();
    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("操作"),
        QStringLiteral("用户请求打开通道：DeviceIndex=%1；上限位 ID=%2；下限位 ID=%3")
            .arg(deviceIndex)
            .arg(m_upperPanel.nodeIdSpin->value())
            .arg(m_lowerPanel.nodeIdSpin->value()));
    if (!m_gateway->openGateway(deviceIndex, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("打开失败"), errorMessage);
        return;
    }

    m_hardwareFrameCount = 0;
    m_hardwareNodeFrameCounts.clear();
    m_hardwareS3EventCounts.clear();
    m_gatewayOpenedAt = QDateTime::currentDateTime();
    m_lastHardwareFrameAt = QDateTime();
    m_lastNoFrameWarningSeconds = 0;
    m_listenWatchdog->start();
    setGatewayUiConnected(true);
    const QVector<quint32> nodes = m_gateway->nodeIds();
    m_nodeSummaryLabel->setText(QStringLiteral("%1；CAN 位速率返回值：%2")
                                    .arg(nodeListText(nodes))
                                    .arg(m_gateway->canBitRate()));
    appendSystemMessage(
        QStringLiteral("RTCN 注册调用已完成，正在等待真实硬件帧验证。若触发后无事件，请现场确认 S3IE/S34CON；本工具不会自动修改。"));
}

void MainWindow::setGatewayUiConnected(bool connected)
{
    m_sdkPathEdit->setEnabled(!connected);
    m_browseSdkButton->setEnabled(!connected);
    m_refreshChannelsButton->setEnabled(!connected);
    m_channelCombo->setEnabled(!connected);
    m_gatewayButton->setText(connected ? QStringLiteral("关闭监听") : QStringLiteral("打开并监听"));
    if (connected) {
        const quint32 result = m_gateway->callbackRegistrationResult();
        m_gatewayStatusLabel->setText(
            QStringLiteral("注册调用返回 %1 / 等待首帧验证")
                .arg(sdkReturnText(result)));
    } else {
        m_gatewayStatusLabel->setText(QStringLiteral("未连接"));
    }
    m_gatewayStatusLabel->setProperty("connected", connected);
    m_gatewayStatusLabel->style()->unpolish(m_gatewayStatusLabel);
    m_gatewayStatusLabel->style()->polish(m_gatewayStatusLabel);
    if (!connected) {
        m_nodeSummaryLabel->setText(QStringLiteral("节点信息将在打开网关后显示"));
    }
}

void MainWindow::handleFrame(const RawCanFrame& rawFrame)
{
    const ParsedCanFrame frame = UimCanProtocol::parse(rawFrame);
    const quint32 lowNodeBits = (rawFrame.canId >> 19U) & 0x01FU;
    const quint32 highNodeBits = (rawFrame.canId >> 3U) & 0x060U;
    const QString source = rawFrame.simulated ? QStringLiteral("模拟") : QStringLiteral("硬件");

    appendDiagnostic(
        QStringLiteral("RX"),
        source,
        QStringLiteral("DeviceIndex=%1；dwMsgLen=%2；CAN_ID=%3；IDE=%4；RTR=%5；SendType=%6；DLC=%7；DATA=[%8]")
            .arg(rawFrame.deviceIndex)
            .arg(rawFrame.callbackMessageLength)
            .arg(UimCanProtocol::canIdText(rawFrame.canId))
            .arg(rawFrame.ide)
            .arg(rawFrame.rtr)
            .arg(rawFrame.sendType)
            .arg(rawFrame.dlc)
            .arg(UimCanProtocol::dataText(rawFrame.data)));
    appendDiagnostic(
        frame.nodeSourceKnown && frame.isS3Event() ? QStringLiteral("INFO") : QStringLiteral("WARN"),
        QStringLiteral("解析"),
        QStringLiteral("节点位域：高段=%1，低段=%2，合成节点=%3；CAN RCW=0x%4；事件=%5；结果=%6")
            .arg(highNodeBits)
            .arg(lowNodeBits)
            .arg(frame.nodeSourceKnown ? QString::number(frame.nodeId) : QStringLiteral("待确认"))
            .arg(QString::number(frame.canCommand, 16).toUpper().rightJustified(2, QLatin1Char('0')))
            .arg(UimCanProtocol::eventCodeText(frame))
            .arg(frame.resultText));

    if (!rawFrame.simulated) {
        ++m_hardwareFrameCount;
        m_lastHardwareFrameAt = rawFrame.receivedAt;
        if (frame.nodeSourceKnown) {
            ++m_hardwareNodeFrameCounts[frame.nodeId];
            if (frame.isS3Event()) {
                ++m_hardwareS3EventCounts[frame.nodeId];
            }
        }
        m_gatewayStatusLabel->setText(QStringLiteral("已收到硬件帧：%1；最近 %2")
                                          .arg(m_hardwareFrameCount)
                                          .arg(rawFrame.receivedAt.toString(QStringLiteral("HH:mm:ss.zzz"))));
        if (m_hardwareFrameCount == 1) {
            appendDiagnostic(
                QStringLiteral("INFO"),
                QStringLiteral("回调"),
                QStringLiteral("已收到首个硬件回调，确认 SDK → 应用接收链路已工作"));
        }
    }

    if (!frame.nodeSourceKnown) {
        appendDiagnostic(
            QStringLiteral("WARN"),
            QStringLiteral("路由"),
            QStringLiteral("无法可靠确定驱动器来源，本帧不会推进任何限位测试"));
    } else if (frame.nodeId != static_cast<quint32>(m_upperPanel.nodeIdSpin->value())
               && frame.nodeId != static_cast<quint32>(m_lowerPanel.nodeIdSpin->value())) {
        appendDiagnostic(
            QStringLiteral("INFO"),
            QStringLiteral("路由"),
            QStringLiteral("节点 %1 不是当前上/下限位目标，本帧仅记录，不推进测试").arg(frame.nodeId));
    } else if (!frame.isS3Event()) {
        appendDiagnostic(
            QStringLiteral("WARN"),
            QStringLiteral("路由"),
            QStringLiteral("节点 %1 匹配目标，但 CAN RCW 不是已验证的 0x73/0xF3，本帧显示为待确认")
                .arg(frame.nodeId));
    }

    appendFrameRow(frame);
    updatePanelFromFrame(&m_upperPanel, frame);
    updatePanelFromFrame(&m_lowerPanel, frame);
}

void MainWindow::appendFrameRow(const ParsedCanFrame& frame)
{
    if (m_frameTable->rowCount() >= kMaximumLogRows) {
        m_frameTable->removeRow(0);
    }

    const int row = m_frameTable->rowCount();
    m_frameTable->insertRow(row);
    const QStringList values {
        displayTime(frame.raw.receivedAt),
        frame.raw.simulated ? QStringLiteral("模拟") : QStringLiteral("硬件"),
        QString::number(frame.raw.deviceIndex),
        UimCanProtocol::canIdText(frame.raw.canId),
        UimCanProtocol::frameTypeText(frame.raw),
        QString::number(frame.raw.dlc),
        UimCanProtocol::dataText(frame.raw.data),
        QString::number(frame.raw.callbackMessageLength),
        frame.nodeSourceKnown ? QString::number(frame.nodeId) : QStringLiteral("待确认"),
        QStringLiteral("0x%1").arg(QString::number(frame.canCommand, 16).toUpper().rightJustified(2, QLatin1Char('0'))),
        UimCanProtocol::eventCodeText(frame),
        frame.resultText
    };

    for (int column = 0; column < values.size(); ++column) {
        m_frameTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
    m_frameTable->scrollToBottom();
}

void MainWindow::updatePanelFromFrame(TestPanel* panel, const ParsedCanFrame& frame)
{
    const quint32 configuredNode = static_cast<quint32>(panel->nodeIdSpin->value());
    const quint32 activeNode = panel->session.isActive() ? panel->session.targetNodeId() : configuredNode;
    if (!frame.nodeSourceKnown || frame.nodeId != activeNode) {
        return;
    }

    panel->lastEventLabel->setText(UimCanProtocol::eventCodeText(frame));
    panel->lastTimeLabel->setText(displayTime(frame.raw.receivedAt));
    if (!frame.isS3Event()) {
        if (!panel->session.isActive()) {
            panel->resultLabel->setText(QStringLiteral("收到目标驱动器的非 S3 事件，待确认"));
            setPanelResultStyle(panel, QStringLiteral("warning"));
        }
        return;
    }

    panel->currentLevelLabel->setText(frame.levelText);
    const LimitTestUpdate update = panel->session.consume(frame);
    if (update == LimitTestUpdate::Ignored) {
        appendDiagnostic(
            QStringLiteral("INFO"),
            QStringLiteral("测试"),
            QStringLiteral("%1：收到节点 %2 的 %3，但当前没有等待中的测试，仅更新实时状态")
                .arg(panel->title)
                .arg(frame.nodeId)
                .arg(UimCanProtocol::edgeText(frame.s3Edge)));
        return;
    }
    if (update == LimitTestUpdate::Passed) {
        panel->timeoutTimer->stop();
        panel->nodeIdSpin->setEnabled(true);
        panel->startButton->setEnabled(true);
        panel->finishButton->setEnabled(false);
        panel->promptLabel->setText(QStringLiteral("本次人工触发、恢复测试已完成。"));
        appendDiagnostic(
            QStringLiteral("PASS"),
            QStringLiteral("测试"),
            QStringLiteral("%1通过：节点=%2；首次边沿=%3；触发时间=%4；恢复时间=%5")
                .arg(panel->title)
                .arg(frame.nodeId)
                .arg(UimCanProtocol::edgeText(panel->session.triggerEdge()))
                .arg(displayTime(panel->session.triggerTime()))
                .arg(displayTime(panel->session.restoreTime())));
    } else if (update == LimitTestUpdate::TriggerDetected) {
        panel->promptLabel->setText(QStringLiteral("已检测到状态变化，请恢复限位开关。"));
        appendDiagnostic(
            QStringLiteral("INFO"),
            QStringLiteral("测试"),
            QStringLiteral("%1：节点 %2 首次收到 %3，记为触发；等待相反边沿恢复")
                .arg(panel->title)
                .arg(frame.nodeId)
                .arg(UimCanProtocol::edgeText(frame.s3Edge)));
    } else if (update == LimitTestUpdate::RepeatedSameEdge) {
        panel->promptLabel->setText(QStringLiteral("收到相同边沿，继续等待相反边沿。"));
        appendDiagnostic(
            QStringLiteral("WARN"),
            QStringLiteral("测试"),
            QStringLiteral("%1：节点 %2 重复收到 %3；不判定恢复")
                .arg(panel->title)
                .arg(frame.nodeId)
                .arg(UimCanProtocol::edgeText(frame.s3Edge)));
    }
    refreshTestPanel(panel);
}

void MainWindow::startTest(TestPanel* panel)
{
    if (m_upperPanel.nodeIdSpin->value() == m_lowerPanel.nodeIdSpin->value()) {
        QMessageBox::warning(
            this,
            QStringLiteral("驱动器 ID 冲突"),
            QStringLiteral("上限位与下限位必须配置为不同的驱动器 ID。"));
        return;
    }

    const quint32 targetNode = static_cast<quint32>(panel->nodeIdSpin->value());
    panel->session.start(targetNode, QDateTime::currentDateTime());
    panel->hardwareFramesAtStart = m_hardwareFrameCount;
    panel->targetFramesAtStart = m_hardwareNodeFrameCounts.value(targetNode);
    panel->targetS3EventsAtStart = m_hardwareS3EventCounts.value(targetNode);
    panel->nodeIdSpin->setEnabled(false);
    panel->startButton->setEnabled(false);
    panel->finishButton->setEnabled(true);
    panel->currentLevelLabel->setText(QStringLiteral("未知 / 等待状态变化事件"));
    panel->lastEventLabel->setText(QStringLiteral("-"));
    panel->lastTimeLabel->setText(QStringLiteral("-"));
    panel->triggerTimeLabel->setText(QStringLiteral("-"));
    panel->restoreTimeLabel->setText(QStringLiteral("-"));
    panel->promptLabel->setText(QStringLiteral("请手动触发 %1 开关。当前不预设触发对应高电平或低电平。")
                                    .arg(panel->title));
    panel->timeoutTimer->start(m_timeoutSpin->value() * 1000);
    refreshTestPanel(panel);

    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("测试"),
        QStringLiteral("开始%1：目标节点=%2；超时=%3秒；网关=%4；基线硬件帧=%5/目标帧=%6/S3事件=%7")
            .arg(panel->title)
            .arg(targetNode)
            .arg(m_timeoutSpin->value())
            .arg(m_gateway->isGatewayOpen() ? QStringLiteral("已打开") : QStringLiteral("未打开，仅可使用模拟数据"))
            .arg(panel->hardwareFramesAtStart)
            .arg(panel->targetFramesAtStart)
            .arg(panel->targetS3EventsAtStart));

    if (m_gateway->isGatewayOpen() && !m_gateway->nodeIds().contains(targetNode)) {
        appendSystemMessage(QStringLiteral("提示：当前网关返回的节点列表中没有 ID=%1；测试仍保持被动等待。")
                                .arg(targetNode));
    }
}

void MainWindow::finishTest(TestPanel* panel, bool timeout)
{
    if (!panel->session.isActive()) {
        return;
    }

    const bool noEvent = panel->session.phase() == LimitTestPhase::WaitingForTrigger;
    QString reason;
    if (noEvent) {
        reason = timeout
            ? QStringLiteral("测试超时：未检测到状态变化；请确认目标 ID、接线及 S3 状态变化通知是否启用")
            : QStringLiteral("测试结束：未检测到状态变化");
    } else {
        reason = timeout
            ? QStringLiteral("测试超时：已检测到触发，但未收到相反边沿的恢复事件")
            : QStringLiteral("测试结束：触发与恢复事件未完整接收");
    }

    panel->session.finishWithoutPass(reason, QDateTime::currentDateTime());
    panel->timeoutTimer->stop();
    panel->nodeIdSpin->setEnabled(true);
    panel->startButton->setEnabled(true);
    panel->finishButton->setEnabled(false);
    panel->promptLabel->setText(
        QStringLiteral("本工具不会自动写入 MCFG/S34CON；如无 RTCN，请由现场人员核对驱动器配置。"));
    refreshTestPanel(panel);
    appendDiagnostic(
        QStringLiteral("FAIL"),
        QStringLiteral("测试"),
        QStringLiteral("%1未通过：%2").arg(panel->title, reason));
    appendTestFailureDiagnosis(panel);
}

void MainWindow::refreshTestPanel(TestPanel* panel)
{
    panel->resultLabel->setText(panel->session.resultText());
    panel->triggerTimeLabel->setText(displayTime(panel->session.triggerTime()));
    panel->restoreTimeLabel->setText(displayTime(panel->session.restoreTime()));

    switch (panel->session.phase()) {
    case LimitTestPhase::Passed:
        setPanelResultStyle(panel, QStringLiteral("passed"));
        break;
    case LimitTestPhase::Failed:
        setPanelResultStyle(panel, QStringLiteral("failed"));
        break;
    case LimitTestPhase::WaitingForTrigger:
    case LimitTestPhase::WaitingForRestore:
        setPanelResultStyle(panel, QStringLiteral("running"));
        break;
    case LimitTestPhase::Idle:
        setPanelResultStyle(panel, QStringLiteral("idle"));
        break;
    }
}

void MainWindow::setPanelResultStyle(TestPanel* panel, const QString& state)
{
    panel->resultLabel->setProperty("testState", state);
    panel->resultLabel->style()->unpolish(panel->resultLabel);
    panel->resultLabel->style()->polish(panel->resultLabel);
}

void MainWindow::injectSimulation(quint32 nodeId, S3Edge edge)
{
    RawCanFrame frame;
    frame.receivedAt = QDateTime::currentDateTime();
    frame.deviceIndex = 0;
    frame.canId = UimCanProtocol::makeSimulationCanId(nodeId, edge);
    frame.ide = 1;
    frame.rtr = 0;
    frame.dlc = 0;
    frame.callbackMessageLength = 1;
    frame.simulated = true;
    handleFrame(frame);
}

void MainWindow::saveLog()
{
    const QString defaultName = QStringLiteral("uim_limit_sensor_test_%1.csv")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存测试日志"),
        QDir::homePath() + QLatin1Char('/') + defaultName,
        QStringLiteral("CSV 日志 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), file.errorString());
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xFEFF);
    stream << csvField(QStringLiteral("测试对象")) << ','
           << csvField(QStringLiteral("驱动器 ID")) << ','
           << csvField(QStringLiteral("触发时间")) << ','
           << csvField(QStringLiteral("恢复时间")) << ','
           << csvField(QStringLiteral("测试结果")) << '\n';
    for (const TestPanel* panel : {&m_upperPanel, &m_lowerPanel}) {
        stream << csvField(panel->title) << ','
               << csvField(QString::number(panel->session.targetNodeId() == 0
                                               ? static_cast<quint32>(panel->nodeIdSpin->value())
                                               : panel->session.targetNodeId()))
               << ',' << csvField(displayTime(panel->session.triggerTime()))
               << ',' << csvField(displayTime(panel->session.restoreTime()))
               << ',' << csvField(panel->session.resultText()) << '\n';
    }
    stream << '\n';
    QStringList headers;
    for (int column = 0; column < m_frameTable->columnCount(); ++column) {
        headers.append(csvField(m_frameTable->horizontalHeaderItem(column)->text()));
    }
    stream << headers.join(QLatin1Char(',')) << '\n';

    for (int row = 0; row < m_frameTable->rowCount(); ++row) {
        QStringList fields;
        for (int column = 0; column < m_frameTable->columnCount(); ++column) {
            const QTableWidgetItem* item = m_frameTable->item(row, column);
            fields.append(csvField(item == nullptr ? QString() : item->text()));
        }
        stream << fields.join(QLatin1Char(',')) << '\n';
    }

    stream << '\n' << csvField(QStringLiteral("监听诊断日志")) << '\n';
    for (const QString& entry : m_diagnosticEntries) {
        stream << csvField(entry) << '\n';
    }

    if (!file.commit()) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), file.errorString());
        return;
    }
    appendSystemMessage(QStringLiteral("测试日志已保存：%1").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::appendSystemMessage(const QString& message)
{
    if (m_systemMessageLabel != nullptr) {
        m_systemMessageLabel->setText(QStringLiteral("%1  %2")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
    }
    statusBar()->showMessage(message, 8000);
    appendDiagnostic(QStringLiteral("INFO"), QStringLiteral("状态"), message);
}

void MainWindow::appendDiagnostic(const QString& level, const QString& category, const QString& message)
{
    QString singleLine = message;
    singleLine.replace(QLatin1Char('\r'), QLatin1Char(' '));
    singleLine.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    const QString entry = QStringLiteral("[%1] [%2] [%3] %4")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                              .arg(level, category, singleLine);
    m_diagnosticEntries.append(entry);
    while (m_diagnosticEntries.size() > kMaximumLogRows) {
        m_diagnosticEntries.removeFirst();
    }
    if (m_diagnosticLog != nullptr) {
        m_diagnosticLog->appendPlainText(entry);
        m_diagnosticLog->verticalScrollBar()->setValue(m_diagnosticLog->verticalScrollBar()->maximum());
    }
}

void MainWindow::checkListeningHealth()
{
    if (!m_gateway->isGatewayOpen() || !m_gatewayOpenedAt.isValid()) {
        return;
    }

    const int elapsedSeconds = static_cast<int>(m_gatewayOpenedAt.secsTo(QDateTime::currentDateTime()));
    if (m_hardwareFrameCount == 0) {
        m_gatewayStatusLabel->setText(QStringLiteral("注册调用已完成 / %1 秒无硬件帧").arg(elapsedSeconds));
        if (elapsedSeconds >= 5
            && (m_lastNoFrameWarningSeconds == 0 || elapsedSeconds - m_lastNoFrameWarningSeconds >= 10)) {
            m_lastNoFrameWarningSeconds = elapsedSeconds;
            appendDiagnostic(
                QStringLiteral("WARN"),
                QStringLiteral("看门狗"),
                QStringLiteral("网关打开并完成回调注册调用后 %1 秒仍没有硬件帧。可能原因：尚未发生端口变化、S3IE/S34CON 未产生 RTCN、选错通道、网关被其他程序占用、DLL/硬件版本不匹配或现场接线没有形成电平变化。")
                    .arg(elapsedSeconds));
        }
        return;
    }

    const qint64 idleSeconds = m_lastHardwareFrameAt.secsTo(QDateTime::currentDateTime());
    m_gatewayStatusLabel->setText(QStringLiteral("已收到硬件帧：%1；距最近帧 %2 秒")
                                      .arg(m_hardwareFrameCount)
                                      .arg(idleSeconds));
}

void MainWindow::appendTestFailureDiagnosis(TestPanel* panel)
{
    const quint32 targetNode = panel->session.targetNodeId();
    const quint64 allFrames = countSince(m_hardwareFrameCount, panel->hardwareFramesAtStart);
    const quint64 targetFrames = countSince(m_hardwareNodeFrameCounts.value(targetNode), panel->targetFramesAtStart);
    const quint64 targetS3Events = countSince(m_hardwareS3EventCounts.value(targetNode), panel->targetS3EventsAtStart);

    appendDiagnostic(
        QStringLiteral("INFO"),
        QStringLiteral("排查"),
        QStringLiteral("%1区间统计：全部硬件回调=%2；节点 %3 回调=%4；节点 %3 的已识别 S3 事件=%5")
            .arg(panel->title)
            .arg(allFrames)
            .arg(targetNode)
            .arg(targetFrames)
            .arg(targetS3Events));

    QString diagnosis;
    if (!m_gateway->isGatewayOpen()) {
        diagnosis = QStringLiteral("测试期间网关未打开；真实硬件不会进入本程序，只能接收模拟事件。");
    } else if (allFrames == 0) {
        diagnosis = QStringLiteral("SDK RTCN 回调没有进入应用。优先核对 S3IE/S34CON 通知、所选 DeviceIndex、网关占用、DLL 版本及接线电平是否真的变化。");
    } else if (targetFrames == 0) {
        diagnosis = QStringLiteral("回调链路已工作，但没有来自目标节点的帧。优先核对上/下限位驱动器 ID；查看报文表实际节点分布。");
    } else if (targetS3Events == 0) {
        diagnosis = QStringLiteral("目标节点有 RTCN，但 CAN ID 低 8 位不是已验证的 0x73/0xF3。请依据日志中的 CAN ID、RCW 和 DATA 核对现场固件协议，当前不会猜测为 S3。");
    } else if (panel->session.triggerTime().isValid()) {
        diagnosis = QStringLiteral("已收到至少一个 S3 边沿，但缺少相反边沿；检查开关是否确实恢复、触点抖动和现场电平方向。");
    } else {
        diagnosis = QStringLiteral("存在 S3 事件统计但测试状态未推进，请核对日志时间与测试开始时间。");
    }
    appendDiagnostic(QStringLiteral("WARN"), QStringLiteral("排查"), diagnosis);
}

}  // namespace panthera::tools::uim_limit

