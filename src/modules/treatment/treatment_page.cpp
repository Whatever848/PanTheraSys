#include "modules/treatment/treatment_page.h"

#include <algorithm>

#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

bool isSeedPlanId(const QString& planId)
{
    const QString trimmedId = planId.trimmed();
    return trimmedId == QStringLiteral("PLAN-20260102091500")
        || trimmedId == QStringLiteral("PLAN-20260104083000");
}

bool isSuppressedSystemPlan(const TherapyPlan& plan)
{
    if (isSeedPlanId(plan.id)) {
        return true;
    }

    return plan.name.trimmed().compare(QStringLiteral("Imported treatment plan"), Qt::CaseInsensitive) == 0;
}

}  // namespace

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
    m_planCombo = new QComboBox();
    m_planCombo->setMinimumWidth(220);
    m_planSummaryLabel = new QLabel(QStringLiteral("\u5f53\u524d\u65b9\u6848\u6982\u51b5\n\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848"));
    m_planSummaryLabel->setWordWrap(true);
    m_planSummaryLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_planSummaryLabel->setMinimumHeight(110);
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
    controlLayout->addWidget(m_planCombo);
    controlLayout->addWidget(m_planSummaryLabel);
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
    connect(m_context, &ApplicationContext::activePlanCleared, this, &TreatmentPage::onActivePlanCleared);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &TreatmentPage::onPatientChanged);
    connect(m_planCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &TreatmentPage::onPlanSelectionChanged);
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &TreatmentPage::onSafetyChanged);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, [this](SystemMode mode) {
        m_modeLabel->setText(QStringLiteral("\u6a21\u5f0f\uff1a%1").arg(toDisplayString(mode)));
    });
    connect(m_safetyKernel, &SafetyKernel::treatmentAbortRequested, this, &TreatmentPage::onAbortRequested);

    setButtonState(false, false, false, false);
    refreshAvailablePlans(true);
}

void TreatmentPage::startTreatment()
{
    if (!m_context->hasActivePlan()) {
        appendLog(QStringLiteral("\u5f53\u524d\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848\uff0c\u65e0\u6cd5\u5f00\u59cb\u6cbb\u7597"));
        return;
    }
    if (!isPlanTreatable(m_context->activePlan())) {
        appendLog(QStringLiteral("\u5f53\u524d\u65b9\u6848\u5c1a\u672a\u5ba1\u6838\u901a\u8fc7\uff0c\u65e0\u6cd5\u5f00\u59cb\u6cbb\u7597"));
        return;
    }

    QString reason;
    if (!m_safetyKernel->requestTreatmentStart(&reason)) {
        appendLog(QStringLiteral("\u62d2\u7edd\u5f00\u59cb\u6cbb\u7597\uff1a%1").arg(reason));
        return;
    }

    if (m_simulationDevice != nullptr && !m_simulationDevice->setTreatmentOutputEnabled(true, &reason)) {
        appendLog(QStringLiteral("\u529f\u7387\u94fe\u8def\u672a\u5c31\u7eea\uff1a%1").arg(reason));
        m_safetyKernel->stopTreatment();
        return;
    }

    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    m_progressBar->setValue(0);
    m_progressTimer.start();
    m_planCombo->setEnabled(false);
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
    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }
    m_planCombo->setEnabled(false);
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

    if (m_simulationDevice != nullptr && !m_simulationDevice->setTreatmentOutputEnabled(true, &reason)) {
        appendLog(QStringLiteral("\u529f\u7387\u94fe\u8def\u672a\u5c31\u7eea\uff1a%1").arg(reason));
        return;
    }

    m_progressTimer.start();
    m_planCombo->setEnabled(false);
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
    if (isSuppressedSystemPlan(plan)) {
        if (m_context->hasActivePlan() && m_context->activePlan().id == plan.id) {
            m_context->clearActivePlan();
        } else {
            onActivePlanCleared();
        }
        return;
    }

    syncPlanComboEntry(plan);

    if (m_deferStartupPlanSelection) {
        const int placeholderIndex = m_planCombo->findData(QString());
        if (placeholderIndex >= 0 && m_planCombo->currentIndex() != placeholderIndex) {
            const QSignalBlocker blocker(m_planCombo);
            m_planCombo->setCurrentIndex(placeholderIndex);
        }
        updatePlanSummary(nullptr);
        m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
        m_progressBar->setValue(0);
        m_preview->clearPlan();
        m_preview->setCompletedPointCount(0);
        m_planCombo->setEnabled(hasSelectablePlans());
        setButtonState(false, false, false, false);
        return;
    }

    const int comboIndex = m_planCombo->findData(plan.id);
    if (comboIndex >= 0 && comboIndex != m_planCombo->currentIndex()) {
        const QSignalBlocker blocker(m_planCombo);
        m_planCombo->setCurrentIndex(comboIndex);
    }
    updatePlanSummary(&plan);
    m_preview->setPlan(plan);
    m_preview->setCompletedPointCount(0);
    m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / %1").arg(totalPointCount()));
    m_progressBar->setValue(0);
    m_planCombo->setEnabled(true);
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && isPlanTreatable(plan), false, false, false);
}

void TreatmentPage::onActivePlanCleared()
{
    updatePlanSummary(nullptr);
    m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
    m_progressBar->setValue(0);
    m_preview->clearPlan();
    m_preview->setCompletedPointCount(0);
    m_planCombo->setEnabled(hasSelectablePlans());
    setButtonState(false, false, false, false);
}

void TreatmentPage::onPatientChanged(const PatientRecord& patient)
{
    m_patientLabel->setText(QStringLiteral("\u60a3\u8005\uff1a%1 | %2").arg(patient.name, patient.id));
    m_deferStartupPlanSelection = true;
    refreshAvailablePlans(true);
}

void TreatmentPage::onSafetyChanged(const SafetySnapshot& snapshot)
{
    m_safetyLabel->setText(QStringLiteral("\u5b89\u5168\u72b6\u6001\uff1a%1").arg(snapshot.message));
    if (m_progressTimer.isActive()) {
        setButtonState(false, snapshot.state != SafetyState::Red, false, true);
        return;
    }

    const bool approved = m_context->hasActivePlan() && isPlanTreatable(m_context->activePlan());
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
        m_deferStartupPlanSelection = true;
        if (m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        } else {
            onActivePlanCleared();
        }
        return;
    }

    TherapyPlan therapyPlan;
    if (!m_clinicalDataService.findTherapyPlanById(planId, &therapyPlan)) {
        appendLog(QStringLiteral("\u65b9\u6848\u52a0\u8f7d\u5931\u8d25\uff1a%1").arg(m_clinicalDataService.lastError()));
        return;
    }

    if (isSuppressedSystemPlan(therapyPlan)) {
        const QSignalBlocker blocker(m_planCombo);
        m_planCombo->setCurrentIndex(0);
        m_deferStartupPlanSelection = true;
        if (m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        } else {
            onActivePlanCleared();
        }
        return;
    }

    m_deferStartupPlanSelection = false;
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

bool TreatmentPage::isPlanTreatable(const TherapyPlan& plan) const
{
    if (isSuppressedSystemPlan(plan)) {
        return false;
    }

    return plan.approvalState == ApprovalState::Approved || plan.approvalState == ApprovalState::Locked;
}

QString TreatmentPage::planComboText(const TherapyPlan& plan) const
{
    return QStringLiteral("%1 | %2").arg(plan.name, toDisplayString(plan.approvalState));
}

bool TreatmentPage::hasSelectablePlans() const
{
    if (m_planCombo == nullptr) {
        return false;
    }

    for (int index = 0; index < m_planCombo->count(); ++index) {
        if (!m_planCombo->itemData(index).toString().trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
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

void TreatmentPage::syncPlanComboEntry(const TherapyPlan& plan)
{
    if (m_planCombo == nullptr || plan.id.trimmed().isEmpty() || isSuppressedSystemPlan(plan)) {
        return;
    }

    const QString displayText = planComboText(plan);
    const int existingIndex = m_planCombo->findData(plan.id);
    const QSignalBlocker blocker(m_planCombo);
    if (existingIndex >= 0) {
        m_planCombo->setItemText(existingIndex, displayText);
    } else {
        const bool hasPlaceholder = m_planCombo->count() > 0 && m_planCombo->itemData(0).toString().trimmed().isEmpty();
        m_planCombo->insertItem(hasPlaceholder ? 1 : 0, displayText, plan.id);
    }
}

void TreatmentPage::updatePlanSummary(const TherapyPlan* plan)
{
    if (m_planSummaryLabel == nullptr) {
        return;
    }

    if (plan == nullptr) {
        m_planSummaryLabel->setText(
            QStringLiteral("\u5f53\u524d\u65b9\u6848\u6982\u51b5\n\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848\n\u5f00\u59cb\u6cbb\u7597\u524d\uff0c\u8bf7\u5148\u5728\u4e0a\u65b9\u4e0b\u62c9\u5217\u8868\u91cc\u9009\u62e9\u4e00\u4e2a\u6cbb\u7597\u65b9\u6848\u3002"));
        return;
    }

    int pointCount = 0;
    double durationSeconds = 0.0;
    for (const TherapySegment& segment : plan->segments) {
        pointCount += segment.points.size();
        durationSeconds += segment.plannedDurationSeconds;
    }

    const QString approvedAtText = plan->approvedAt.isValid()
        ? plan->approvedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
        : QStringLiteral("\u672a\u8bb0\u5f55");
    const QString deliveryText = plan->deliveryMode.trimmed().isEmpty() ? QStringLiteral("\u672a\u8bbe\u7f6e") : plan->deliveryMode;
    m_planSummaryLabel->setText(
        QStringLiteral(
            "\u5f53\u524d\u65b9\u6848\u6982\u51b5\n"
            "\u540d\u79f0\uff1a%1\n"
            "\u5ba1\u6838\u72b6\u6001\uff1a%2\n"
            "\u6cbb\u7597\u65b9\u5f0f\uff1a%3 / %4\n"
            "\u6cbb\u7597\u53c2\u6570\uff1a%5 W | \u884c\u8ddd %6 mm | \u70b9\u7597 %7 s\n"
            "\u6cbb\u7597\u5750\u6807\uff1aX %8  Y %9  Z %10  \u6df1\u5ea6 %11 mm\n"
            "\u6cbb\u7597\u6bb5/\u9776\u70b9\uff1a%12 \u6bb5 / %13 \u70b9 | \u9884\u8ba1 %14 min\n"
            "\u5ba1\u6279\u65f6\u95f4\uff1a%15")
            .arg(plan->name)
            .arg(toDisplayString(plan->approvalState))
            .arg(deliveryText)
            .arg(toDisplayString(plan->pattern))
            .arg(plan->plannedPowerWatts, 0, 'f', 0)
            .arg(plan->spacingMm, 0, 'f', 1)
            .arg(plan->dwellSeconds, 0, 'f', 1)
            .arg(plan->coordinateX, 0, 'f', 2)
            .arg(plan->coordinateY, 0, 'f', 2)
            .arg(plan->coordinateZ, 0, 'f', 2)
            .arg(plan->depthMm, 0, 'f', 2)
            .arg(plan->segments.size())
            .arg(pointCount)
            .arg(durationSeconds / 60.0, 0, 'f', 2)
            .arg(approvedAtText));
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
    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }
    if (m_safetyKernel->mode() != SystemMode::Alarm) {
        m_safetyKernel->stopTreatment();
    }

    m_planCombo->setEnabled(true);
    const bool approved = m_context->hasActivePlan() && isPlanTreatable(m_context->activePlan());
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && approved, false, false, false);
    appendLog(QStringLiteral("\u6cbb\u7597\u6d41\u7a0b\u7ed3\u675f\uff0c\u72b6\u6001\uff1a%1").arg(status));
}

void TreatmentPage::refreshAvailablePlans(bool keepSelectionBlank)
{
    QSignalBlocker blocker(m_planCombo);
    m_planCombo->clear();

    if (!m_context->hasSelectedPatient()) {
        m_planCombo->addItem(QStringLiteral("\u8bf7\u5148\u9009\u62e9\u60a3\u8005"), QString());
        m_planCombo->setEnabled(false);
        onActivePlanCleared();
        return;
    }

    QVector<TherapyPlan> therapyPlans = m_clinicalDataService.listTherapyPlansForPatient(m_context->selectedPatient().id);
    therapyPlans.erase(
        std::remove_if(
            therapyPlans.begin(),
            therapyPlans.end(),
            [](const TherapyPlan& plan) { return isSuppressedSystemPlan(plan); }),
        therapyPlans.end());
    std::sort(therapyPlans.begin(), therapyPlans.end(), [](const TherapyPlan& left, const TherapyPlan& right) {
        if (left.createdAt == right.createdAt) {
            return left.name < right.name;
        }
        return left.createdAt > right.createdAt;
    });
    m_planCombo->addItem(QStringLiteral("\u8bf7\u9009\u62e9\u6cbb\u7597\u65b9\u6848"), QString());
    for (const TherapyPlan& plan : therapyPlans) {
        m_planCombo->addItem(planComboText(plan), plan.id);
    }

    const bool hasSuppressedActivePlan = m_context->hasActivePlan() && isSuppressedSystemPlan(m_context->activePlan());
    if (m_context->hasActivePlan()
        && !hasSuppressedActivePlan
        && m_planCombo->findData(m_context->activePlan().id) < 0) {
        m_planCombo->insertItem(1, planComboText(m_context->activePlan()), m_context->activePlan().id);
    }

    if (m_planCombo->count() <= 1) {
        m_planCombo->setItemText(0, QStringLiteral("\u5f53\u524d\u60a3\u8005\u6682\u65e0\u6cbb\u7597\u65b9\u6848"));
        m_planCombo->setEnabled(false);
        blocker.unblock();
        if (hasSuppressedActivePlan) {
            m_context->clearActivePlan();
        } else {
            onActivePlanCleared();
        }
        return;
    }

    if (keepSelectionBlank) {
        m_planCombo->setCurrentIndex(0);
        m_planCombo->setEnabled(true);
        blocker.unblock();
        if (m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        } else {
            onActivePlanCleared();
        }
        return;
    }

    int preferredIndex = 0;
    if (m_context->hasActivePlan()) {
        const int existingIndex = m_planCombo->findData(m_context->activePlan().id);
        if (existingIndex >= 0) {
            preferredIndex = existingIndex;
        }
    } else {
        preferredIndex = 1;
        for (int index = 0; index < therapyPlans.size(); ++index) {
            if (isPlanTreatable(therapyPlans.at(index))) {
                preferredIndex = index + 1;
                break;
            }
        }
    }
    m_planCombo->setCurrentIndex(preferredIndex);
    m_planCombo->setEnabled(true);

    blocker.unblock();
    if (hasSuppressedActivePlan) {
        m_context->clearActivePlan();
    }
    onPlanSelectionChanged(preferredIndex);
}

}  // namespace panthera::modules
