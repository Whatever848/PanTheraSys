#include "modules/treatment/treatment_page.h"

#include <algorithm>

#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

TreatmentPage::TreatmentPage(
    ApplicationContext* context,
    SafetyKernel* safetyKernel,
    AuditService* auditService,
    adapters::SimulationDeviceFacade* simulationDevice,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_auditService(auditService)
    , m_simulationDevice(simulationDevice)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* imageCard = new QGroupBox(QStringLiteral("治疗执行视图"));
    auto* imageLayout = new QVBoxLayout(imageCard);
    m_preview = new MockUltrasoundView();
    m_preview->setCaption(QStringLiteral("治疗执行监视 / 焦点覆盖示意"));
    imageLayout->addWidget(m_preview);
    rootLayout->addWidget(imageCard, 2);

    auto* controlCard = new QGroupBox(QStringLiteral("治疗控制"));
    auto* controlLayout = new QVBoxLayout(controlCard);
    m_patientLabel = new QLabel(QStringLiteral("患者：未选择"));
    m_planLabel = new QLabel(QStringLiteral("方案：未锁定"));
    m_modeLabel = new QLabel(QStringLiteral("模式：%1").arg(toDisplayString(m_safetyKernel->mode())));
    m_safetyLabel = new QLabel(QStringLiteral("安全状态：%1").arg(m_safetyKernel->snapshot().message));
    m_progressLabel = new QLabel(QStringLiteral("治疗进度：0 / 0"));
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(220);

    m_startButton = new QPushButton(QStringLiteral("开始治疗"));
    m_pauseButton = new QPushButton(QStringLiteral("暂停"));
    m_resumeButton = new QPushButton(QStringLiteral("继续"));
    m_stopButton = new QPushButton(QStringLiteral("终止"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_pauseButton);
    buttonRow->addWidget(m_resumeButton);
    buttonRow->addWidget(m_stopButton);

    controlLayout->addWidget(m_patientLabel);
    controlLayout->addWidget(m_planLabel);
    controlLayout->addWidget(m_modeLabel);
    controlLayout->addWidget(m_safetyLabel);
    controlLayout->addWidget(m_progressLabel);
    controlLayout->addWidget(m_progressBar);
    controlLayout->addLayout(buttonRow);
    controlLayout->addWidget(m_logView);
    rootLayout->addWidget(controlCard, 1);

    m_progressTimer.setInterval(450);

    connect(m_startButton, &QPushButton::clicked, this, &TreatmentPage::startTreatment);
    connect(m_pauseButton, &QPushButton::clicked, this, &TreatmentPage::pauseTreatment);
    connect(m_resumeButton, &QPushButton::clicked, this, &TreatmentPage::resumeTreatment);
    connect(m_stopButton, &QPushButton::clicked, this, &TreatmentPage::stopTreatment);
    connect(&m_progressTimer, &QTimer::timeout, this, &TreatmentPage::advanceProgress);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &TreatmentPage::onActivePlanChanged);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &TreatmentPage::onPatientChanged);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &TreatmentPage::onSafetyChanged);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, [this](SystemMode mode) {
        m_modeLabel->setText(QStringLiteral("模式：%1").arg(toDisplayString(mode)));
    });
    connect(m_safetyKernel, &SafetyKernel::treatmentAbortRequested, this, &TreatmentPage::onAbortRequested);

    setButtonState(false, false, false, false);
}

void TreatmentPage::startTreatment()
{
    // 页面不直接依据按钮状态判断是否能开始治疗，而是统一向安全内核申请权限。
    QString reason;
    if (!m_safetyKernel->requestTreatmentStart(&reason)) {
        appendLog(QStringLiteral("拒绝开始治疗：%1").arg(reason));
        return;
    }

    QString powerError;
    if (!m_simulationDevice->setTreatmentOutputEnabled(true, &powerError)) {
        appendLog(QStringLiteral("功率链路未就绪：%1").arg(powerError));
        m_safetyKernel->stopTreatment();
        return;
    }

    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    m_progressBar->setValue(0);
    m_progressTimer.start();
    setButtonState(false, true, false, true);
    appendLog(QStringLiteral("开始治疗执行"));
}

void TreatmentPage::pauseTreatment()
{
    QString reason;
    if (!m_safetyKernel->pauseTreatment(&reason)) {
        appendLog(QStringLiteral("暂停失败：%1").arg(reason));
        return;
    }

    m_progressTimer.stop();
    m_simulationDevice->setTreatmentOutputEnabled(false);
    setButtonState(false, false, true, true);
    appendLog(QStringLiteral("治疗已暂停"));
}

void TreatmentPage::resumeTreatment()
{
    QString reason;
    if (!m_safetyKernel->resumeTreatment(&reason)) {
        appendLog(QStringLiteral("继续失败：%1").arg(reason));
        return;
    }

    if (!m_simulationDevice->setTreatmentOutputEnabled(true, &reason)) {
        appendLog(QStringLiteral("功率链路未就绪：%1").arg(reason));
        return;
    }

    m_progressTimer.start();
    setButtonState(false, true, false, true);
    appendLog(QStringLiteral("治疗继续执行"));
}

void TreatmentPage::stopTreatment()
{
    appendLog(QStringLiteral("治疗被手动终止"));
    finalizeTreatment(QStringLiteral("终止"));
}

void TreatmentPage::advanceProgress()
{
    // 这里的定时器用于模拟未来执行引擎的检查点回调。
    const int totalPoints = totalPointCount();
    if (totalPoints <= 0) {
        finalizeTreatment(QStringLiteral("失败"));
        return;
    }

    ++m_completedPointCount;
    const TherapyPlan& plan = m_context->activePlan();
    const double dwellSeconds = plan.segments.isEmpty() || plan.segments.first().points.isEmpty() ? 0.3 : plan.segments.first().points.first().dwellSeconds;
    m_deliveredEnergyJ += plan.plannedPowerWatts * dwellSeconds;

    const int percentage = static_cast<int>((static_cast<double>(m_completedPointCount) / totalPoints) * 100.0);
    m_progressBar->setValue(std::min(percentage, 100));
    m_progressLabel->setText(QStringLiteral("治疗进度：%1 / %2，累计能量 %3 J").arg(m_completedPointCount).arg(totalPoints).arg(m_deliveredEnergyJ, 0, 'f', 0));
    m_preview->setCompletedPointCount(m_completedPointCount);

    if (m_completedPointCount >= totalPoints) {
        appendLog(QStringLiteral("治疗按预定方案执行完成"));
        finalizeTreatment(QStringLiteral("完成"));
    }
}

void TreatmentPage::onActivePlanChanged(const TherapyPlan& plan)
{
    m_planLabel->setText(QStringLiteral("方案：%1（%2）").arg(plan.id, toDisplayString(plan.approvalState)));
    m_preview->setPlan(plan);
    m_preview->setCompletedPointCount(0);
    m_progressLabel->setText(QStringLiteral("治疗进度：0 / %1").arg(totalPointCount()));
    setButtonState(plan.approvalState == ApprovalState::Approved || plan.approvalState == ApprovalState::Locked, false, false, false);
}

void TreatmentPage::onPatientChanged(const PatientRecord& patient)
{
    m_patientLabel->setText(QStringLiteral("患者：%1（%2）").arg(patient.name, patient.id));
}

void TreatmentPage::onSafetyChanged(const SafetySnapshot& snapshot)
{
    m_safetyLabel->setText(QStringLiteral("安全状态：%1").arg(snapshot.message));
    if (m_progressTimer.isActive()) {
        setButtonState(false, snapshot.state != SafetyState::Red, false, true);
    } else {
        const bool approved = m_context->hasActivePlan() && (m_context->activePlan().approvalState == ApprovalState::Approved || m_context->activePlan().approvalState == ApprovalState::Locked);
        setButtonState(snapshot.canStartTreatment && approved, false, m_safetyKernel->mode() == SystemMode::Paused, false);
    }
}

void TreatmentPage::onAbortRequested(const QString& reason)
{
    appendLog(QStringLiteral("联锁触发自动中止：%1").arg(reason));
    finalizeTreatment(QStringLiteral("联锁中止"));
}

void TreatmentPage::setButtonState(bool canStart, bool canPause, bool canResume, bool canStop)
{
    m_startButton->setEnabled(canStart);
    m_pauseButton->setEnabled(canPause);
    m_resumeButton->setEnabled(canResume);
    m_stopButton->setEnabled(canStop);
}

int TreatmentPage::totalPointCount() const
{
    if (!m_context->hasActivePlan()) {
        return 0;
    }

    int total = 0;
    for (const TherapySegment& segment : m_context->activePlan().segments) {
        total += segment.points.size();
    }
    return total;
}

void TreatmentPage::appendLog(const QString& line)
{
    const QString timestamped = QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), line);
    m_logView->appendPlainText(timestamped);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("executor"), QStringLiteral("treatment"), line);
    }
}

void TreatmentPage::finalizeTreatment(const QString& status)
{
    m_progressTimer.stop();
    // 在切换工作流模式之前先关闭输出，避免模拟设备状态和界面状态出现偏差。
    m_simulationDevice->setTreatmentOutputEnabled(false);
    if (m_safetyKernel->mode() != SystemMode::Alarm) {
        m_safetyKernel->stopTreatment();
    }
    setButtonState(m_safetyKernel->snapshot().canStartTreatment, false, false, false);
    appendLog(QStringLiteral("治疗流程结束，状态：%1").arg(status));
}

}  // panthera::modules 命名空间
