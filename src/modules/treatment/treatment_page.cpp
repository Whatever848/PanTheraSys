#include "modules/treatment/treatment_page.h"

#include <algorithm>

#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

TreatmentPage::TreatmentPage(
    ApplicationContext* context,
    SafetyKernel* safetyKernel,
    AuditService* auditService,
    IClinicalDataRepository* clinicalDataRepository,
    adapters::SimulationDeviceFacade* simulationDevice,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_auditService(auditService)
    , m_clinicalDataRepository(clinicalDataRepository)
    , m_clinicalDataService(clinicalDataRepository)
    , m_simulationDevice(simulationDevice)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* imageCard = new QGroupBox(QStringLiteral("\u6cbb\u7597\u6267\u884c\u89c6\u56fe"));
    auto* imageLayout = new QVBoxLayout(imageCard);
    m_preview = new MockUltrasoundView();
    m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
    imageLayout->addWidget(m_preview);
    rootLayout->addWidget(imageCard, 2);

    auto* controlCard = new QGroupBox(QStringLiteral("\u6cbb\u7597\u63a7\u5236"));
    auto* controlLayout = new QVBoxLayout(controlCard);
    m_patientLabel = new QLabel(QStringLiteral("\u60a3\u8005\uff1a\u672a\u9009\u62e9"));
    m_planLabel = new QLabel(QStringLiteral("\u65b9\u6848\uff1a\u672a\u9501\u5b9a"));
    m_planCombo = new QComboBox();
    m_planCombo->setMinimumWidth(220);
    m_modeLabel = new QLabel(QStringLiteral("\u6a21\u5f0f\uff1a%1").arg(toDisplayString(m_safetyKernel->mode())));
    m_safetyLabel = new QLabel(QStringLiteral("\u5b89\u5168\u72b6\u6001\uff1a%1").arg(m_safetyKernel->snapshot().message));
    m_progressLabel = new QLabel(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(220);

    m_startButton = new QPushButton(QStringLiteral("\u5f00\u59cb\u6cbb\u7597"));
    m_pauseButton = new QPushButton(QStringLiteral("\u6682\u505c"));
    m_resumeButton = new QPushButton(QStringLiteral("\u7ee7\u7eed"));
    m_stopButton = new QPushButton(QStringLiteral("\u7ec8\u6b62"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_pauseButton);
    buttonRow->addWidget(m_resumeButton);
    buttonRow->addWidget(m_stopButton);

    controlLayout->addWidget(m_patientLabel);
    controlLayout->addWidget(m_planLabel);
    controlLayout->addWidget(m_planCombo);
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
    connect(m_planCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &TreatmentPage::onPlanSelectionChanged);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &TreatmentPage::onSafetyChanged);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, [this](SystemMode mode) {
        m_modeLabel->setText(QStringLiteral("\u6a21\u5f0f\uff1a%1").arg(toDisplayString(mode)));
    });
    connect(m_safetyKernel, &SafetyKernel::treatmentAbortRequested, this, &TreatmentPage::onAbortRequested);

    setButtonState(false, false, false, false);
    refreshAvailablePlans();
}

void TreatmentPage::startTreatment()
{
    QString reason;
    if (!m_safetyKernel->requestTreatmentStart(&reason)) {
        appendLog(QStringLiteral("\u62d2\u7edd\u5f00\u59cb\u6cbb\u7597\uff1a%1").arg(reason));
        return;
    }

    if (!m_simulationDevice->setTreatmentOutputEnabled(true, &reason)) {
        appendLog(QStringLiteral("\u529f\u7387\u94fe\u8def\u672a\u5c31\u7eea\uff1a%1").arg(reason));
        m_safetyKernel->stopTreatment();
        return;
    }

    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    m_progressBar->setValue(0);
    m_progressTimer.start();
    setButtonState(false, true, false, true);
    appendLog(QStringLiteral("\u5f00\u59cb\u6cbb\u7597\u6267\u884c"));
}

void TreatmentPage::pauseTreatment()
{
    QString reason;
    if (!m_safetyKernel->pauseTreatment(&reason)) {
        appendLog(QStringLiteral("\u6682\u505c\u5931\u8d25\uff1a%1").arg(reason));
        return;
    }

    m_progressTimer.stop();
    m_simulationDevice->setTreatmentOutputEnabled(false);
    setButtonState(false, false, true, true);
    appendLog(QStringLiteral("\u6cbb\u7597\u5df2\u6682\u505c"));
}

void TreatmentPage::resumeTreatment()
{
    QString reason;
    if (!m_safetyKernel->resumeTreatment(&reason)) {
        appendLog(QStringLiteral("\u7ee7\u7eed\u5931\u8d25\uff1a%1").arg(reason));
        return;
    }

    if (!m_simulationDevice->setTreatmentOutputEnabled(true, &reason)) {
        appendLog(QStringLiteral("\u529f\u7387\u94fe\u8def\u672a\u5c31\u7eea\uff1a%1").arg(reason));
        return;
    }

    m_progressTimer.start();
    setButtonState(false, true, false, true);
    appendLog(QStringLiteral("\u6cbb\u7597\u7ee7\u7eed\u6267\u884c"));
}

void TreatmentPage::stopTreatment()
{
    appendLog(QStringLiteral("\u6cbb\u7597\u88ab\u624b\u52a8\u7ec8\u6b62"));
    finalizeTreatment(QStringLiteral("\u7ec8\u6b62"));
}

void TreatmentPage::advanceProgress()
{
    const int totalPoints = totalPointCount();
    if (totalPoints <= 0) {
        finalizeTreatment(QStringLiteral("\u5931\u8d25"));
        return;
    }

    ++m_completedPointCount;
    const TherapyPlan& plan = m_context->activePlan();
    const double dwellSeconds = plan.dwellSeconds > 0.0
        ? plan.dwellSeconds
        : (plan.segments.isEmpty() || plan.segments.first().points.isEmpty() ? 0.3 : plan.segments.first().points.first().dwellSeconds);
    m_deliveredEnergyJ += plan.plannedPowerWatts * dwellSeconds;

    const int percentage = static_cast<int>((static_cast<double>(m_completedPointCount) / totalPoints) * 100.0);
    m_progressBar->setValue(std::min(percentage, 100));
    m_progressLabel->setText(
        QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a%1 / %2\uff0c\u7d2f\u8ba1\u80fd\u91cf %3 J")
            .arg(m_completedPointCount)
            .arg(totalPoints)
            .arg(m_deliveredEnergyJ, 0, 'f', 0));
    m_preview->setCompletedPointCount(m_completedPointCount);

    if (m_completedPointCount >= totalPoints) {
        appendLog(QStringLiteral("\u6cbb\u7597\u6309\u9884\u5b9a\u65b9\u6848\u6267\u884c\u5b8c\u6210"));
        finalizeTreatment(QStringLiteral("\u5b8c\u6210"));
    }
}

void TreatmentPage::onActivePlanChanged(const TherapyPlan& plan)
{
    m_planLabel->setText(QStringLiteral("\u65b9\u6848\uff1a%1 | %2").arg(plan.name, toDisplayString(plan.approvalState)));
    const int comboIndex = m_planCombo->findData(plan.id);
    if (comboIndex >= 0 && comboIndex != m_planCombo->currentIndex()) {
        const QSignalBlocker blocker(m_planCombo);
        m_planCombo->setCurrentIndex(comboIndex);
    }
    m_preview->setPlan(plan);
    m_preview->setCompletedPointCount(0);
    m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / %1").arg(totalPointCount()));
    setButtonState(plan.approvalState == ApprovalState::Approved || plan.approvalState == ApprovalState::Locked, false, false, false);
}

void TreatmentPage::onPatientChanged(const PatientRecord& patient)
{
    m_patientLabel->setText(QStringLiteral("\u60a3\u8005\uff1a%1 | %2").arg(patient.name, patient.id));
    refreshAvailablePlans();
}

void TreatmentPage::onSafetyChanged(const SafetySnapshot& snapshot)
{
    m_safetyLabel->setText(QStringLiteral("\u5b89\u5168\u72b6\u6001\uff1a%1").arg(snapshot.message));
    if (m_progressTimer.isActive()) {
        setButtonState(false, snapshot.state != SafetyState::Red, false, true);
        return;
    }

    const bool approved = m_context->hasActivePlan()
        && (m_context->activePlan().approvalState == ApprovalState::Approved
            || m_context->activePlan().approvalState == ApprovalState::Locked);
    setButtonState(snapshot.canStartTreatment && approved, false, m_safetyKernel->mode() == SystemMode::Paused, false);
}

void TreatmentPage::onAbortRequested(const QString& reason)
{
    appendLog(QStringLiteral("\u8054\u9501\u89e6\u53d1\u81ea\u52a8\u4e2d\u6b62\uff1a%1").arg(reason));
    finalizeTreatment(QStringLiteral("\u8054\u9501\u4e2d\u6b62"));
}

void TreatmentPage::onPlanSelectionChanged(int index)
{
    if (index < 0) {
        return;
    }

    const QString planId = m_planCombo->itemData(index).toString();
    if (planId.trimmed().isEmpty()) {
        return;
    }

    TherapyPlan therapyPlan;
    if (!m_clinicalDataService.findTherapyPlanById(planId, &therapyPlan)) {
        appendLog(QStringLiteral("\u65b9\u6848\u52a0\u8f7d\u5931\u8d25\uff1a%1").arg(m_clinicalDataService.lastError()));
        return;
    }

    m_context->setActivePlan(therapyPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(therapyPlan.approvalState);
    }
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
    m_simulationDevice->setTreatmentOutputEnabled(false);
    if (m_safetyKernel->mode() != SystemMode::Alarm) {
        m_safetyKernel->stopTreatment();
    }

    const bool approved = m_context->hasActivePlan()
        && (m_context->activePlan().approvalState == ApprovalState::Approved
            || m_context->activePlan().approvalState == ApprovalState::Locked);
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && approved, false, false, false);
    appendLog(QStringLiteral("\u6cbb\u7597\u6d41\u7a0b\u7ed3\u675f\uff0c\u72b6\u6001\uff1a%1").arg(status));
}

void TreatmentPage::refreshAvailablePlans()
{
    QSignalBlocker blocker(m_planCombo);
    m_planCombo->clear();

    if (!m_context->hasSelectedPatient()) {
        m_planCombo->addItem(QStringLiteral("\u65e0\u53ef\u7528\u65b9\u6848"));
        return;
    }

    const QVector<TherapyPlan> therapyPlans = m_clinicalDataService.listTherapyPlansForPatient(m_context->selectedPatient().id);
    for (const TherapyPlan& plan : therapyPlans) {
        if (!(plan.approvalState == ApprovalState::Approved || plan.approvalState == ApprovalState::Locked)) {
            continue;
        }
        m_planCombo->addItem(QStringLiteral("%1 | %2").arg(plan.name, toDisplayString(plan.approvalState)), plan.id);
    }

    if (m_planCombo->count() == 0) {
        m_planCombo->addItem(QStringLiteral("\u65e0\u53ef\u7528\u65b9\u6848"));
        return;
    }

    int preferredIndex = 0;
    if (m_context->hasActivePlan()) {
        const int existingIndex = m_planCombo->findData(m_context->activePlan().id);
        if (existingIndex >= 0) {
            preferredIndex = existingIndex;
        }
    }
    m_planCombo->setCurrentIndex(preferredIndex);

    blocker.unblock();
    onPlanSelectionChanged(preferredIndex);
}

}  // namespace panthera::modules
