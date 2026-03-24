#include "modules/planning/planning_page.h"

#include <algorithm>

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

PatientRecord buildFallbackPatient()
{
    return PatientRecord {
        QStringLiteral("P2026001"),
        QStringLiteral("张三"),
        24,
        QStringLiteral("女"),
        QStringLiteral("右乳浸润性导管癌 II 期"),
        QStringLiteral("13800000001")
    };
}

QString createPlanId()
{
    return QStringLiteral("PLAN-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")));
}

QString patientDisplayLabel(const PatientRecord& patient)
{
    return QStringLiteral("%1 | %2 | %3岁").arg(patient.name).arg(patient.id).arg(patient.age);
}

}  // namespace

PlanningPage::PlanningPage(
    ApplicationContext* context,
    SafetyKernel* safetyKernel,
    AuditService* auditService,
    const IClinicalDataRepository* clinicalDataRepository,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_auditService(auditService)
    , m_clinicalDataRepository(clinicalDataRepository)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* leftCard = new QGroupBox(QStringLiteral("图像通道与参数"));
    auto* leftLayout = new QVBoxLayout(leftCard);
    m_pathList = new QListWidget();

    auto* formLayout = new QFormLayout();
    m_patientCombo = new QComboBox();
    m_patternCombo = new QComboBox();
    m_patternCombo->addItems({QStringLiteral("点治疗"), QStringLiteral("线治疗"), QStringLiteral("分段治疗")});

    m_layerCountSpin = new QSpinBox();
    m_layerCountSpin->setRange(1, 30);
    m_layerCountSpin->setValue(6);

    m_spacingSpin = new QDoubleSpinBox();
    m_spacingSpin->setRange(0.5, 10.0);
    m_spacingSpin->setValue(3.0);
    m_spacingSpin->setSuffix(QStringLiteral(" mm"));

    m_dwellSpin = new QDoubleSpinBox();
    m_dwellSpin->setRange(0.1, 10.0);
    m_dwellSpin->setValue(0.3);
    m_dwellSpin->setSuffix(QStringLiteral(" s"));

    m_powerSpin = new QDoubleSpinBox();
    m_powerSpin->setRange(20.0, 800.0);
    m_powerSpin->setValue(400.0);
    m_powerSpin->setSuffix(QStringLiteral(" W"));

    m_respiratoryTrackingCheck = new QCheckBox(QStringLiteral("启用呼吸跟随"));

    formLayout->addRow(QStringLiteral("治疗患者"), m_patientCombo);
    formLayout->addRow(QStringLiteral("治疗模式"), m_patternCombo);
    formLayout->addRow(QStringLiteral("采集层数"), m_layerCountSpin);
    formLayout->addRow(QStringLiteral("治疗行距"), m_spacingSpin);
    formLayout->addRow(QStringLiteral("点疗时长"), m_dwellSpin);
    formLayout->addRow(QStringLiteral("治疗功率"), m_powerSpin);

    auto* loadPatientButton = new QPushButton(QStringLiteral("加载当前患者"));
    auto* generateButton = new QPushButton(QStringLiteral("生成方案草案"));
    auto* approveButton = new QPushButton(QStringLiteral("审批并锁定"));
    auto* revertButton = new QPushButton(QStringLiteral("撤销审批"));

    leftLayout->addWidget(m_pathList);
    leftLayout->addLayout(formLayout);
    leftLayout->addWidget(m_respiratoryTrackingCheck);
    leftLayout->addWidget(loadPatientButton);
    leftLayout->addWidget(generateButton);
    leftLayout->addWidget(approveButton);
    leftLayout->addWidget(revertButton);
    rootLayout->addWidget(leftCard, 1);

    auto* centerCard = new QGroupBox(QStringLiteral("方案预览"));
    auto* centerLayout = new QVBoxLayout(centerCard);
    m_preview = new MockUltrasoundView();
    m_preview->setCaption(QStringLiteral("治疗方案预览 / 肿瘤边界草图"));
    centerLayout->addWidget(m_preview, 1);
    rootLayout->addWidget(centerCard, 2);

    auto* rightCard = new QGroupBox(QStringLiteral("方案上下文"));
    auto* rightLayout = new QVBoxLayout(rightCard);
    m_patientSummaryLabel = new QLabel();
    m_patientSummaryLabel->setWordWrap(true);
    m_planSummaryLabel = new QLabel();
    m_planSummaryLabel->setWordWrap(true);
    rightLayout->addWidget(m_patientSummaryLabel);
    rightLayout->addWidget(m_planSummaryLabel);
    rightLayout->addStretch();
    rootLayout->addWidget(rightCard, 1);

    connect(loadPatientButton, &QPushButton::clicked, this, &PlanningPage::loadDemoPatient);
    connect(generateButton, &QPushButton::clicked, this, &PlanningPage::generateDraftPlan);
    connect(approveButton, &QPushButton::clicked, this, &PlanningPage::approveCurrentPlan);
    connect(revertButton, &QPushButton::clicked, this, &PlanningPage::revertPlanToDraft);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &PlanningPage::updateContextSummary);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &PlanningPage::updateContextSummary);
    connect(m_context, &ApplicationContext::activePlanCleared, this, &PlanningPage::updateContextSummary);

    populatePatientSelector();
    updateContextSummary();
}

void PlanningPage::loadDemoPatient()
{
    if (m_clinicalDataRepository != nullptr && m_patientCombo->count() > 0) {
        PatientRecord patient;
        const QString patientId = m_patientCombo->currentData().toString();
        if (m_clinicalDataRepository->findPatientById(patientId, &patient)) {
            m_context->selectPatient(patient);
            if (m_safetyKernel != nullptr) {
                m_safetyKernel->setPatientSelected(true);
            }
            return;
        }
    }

    const PatientRecord fallbackPatient = buildFallbackPatient();
    m_context->selectPatient(fallbackPatient);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPatientSelected(true);
    }
}

void PlanningPage::generateDraftPlan()
{
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    TherapyPlan draftPlan = buildPlanFromUi(ApprovalState::Draft);
    m_context->setActivePlan(draftPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(draftPlan.approvalState);
    }
    m_preview->setPlan(draftPlan);
    m_preview->setCompletedPointCount(0);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("生成方案草案：%1").arg(draftPlan.id));
    }
}

void PlanningPage::approveCurrentPlan()
{
    if (!m_context->hasActivePlan()) {
        generateDraftPlan();
    }

    TherapyPlan approvedPlan = buildPlanFromUi(ApprovalState::Locked);
    approvedPlan.id = m_context->activePlan().id;
    approvedPlan.approvedAt = QDateTime::currentDateTime();

    m_context->setActivePlan(approvedPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(approvedPlan.approvalState);
    }
    m_preview->setPlan(approvedPlan);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("审批并锁定方案：%1").arg(approvedPlan.id));
    }
}

void PlanningPage::revertPlanToDraft()
{
    if (!m_context->hasActivePlan()) {
        return;
    }

    TherapyPlan plan = m_context->activePlan();
    plan.approvalState = ApprovalState::Draft;
    plan.approvedAt = QDateTime {};
    m_context->setActivePlan(plan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(plan.approvalState);
    }
    m_preview->setPlan(plan);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("方案退回草案：%1").arg(plan.id));
    }
}

void PlanningPage::updateContextSummary()
{
    if (m_context->hasSelectedPatient()) {
        const PatientRecord& patient = m_context->selectedPatient();
        syncPatientSelector(patient.id);
        refreshImagingPaths(patient.id);
        m_patientSummaryLabel->setText(
            QStringLiteral("患者：%1\n编号：%2\n年龄：%3\n诊断：%4")
                .arg(patient.name)
                .arg(patient.id)
                .arg(patient.age)
                .arg(patient.diagnosis));
    } else {
        refreshImagingPaths(QString());
        m_patientSummaryLabel->setText(QStringLiteral("患者：未选择"));
    }

    if (m_context->hasActivePlan()) {
        const TherapyPlan& plan = m_context->activePlan();
        int pointCount = 0;
        for (const TherapySegment& segment : plan.segments) {
            pointCount += segment.points.size();
        }
        m_planSummaryLabel->setText(
            QStringLiteral("方案：%1\n状态：%2\n模式：%3\n治疗点数：%4\n功率：%5 W")
                .arg(plan.id)
                .arg(toDisplayString(plan.approvalState))
                .arg(toDisplayString(plan.pattern))
                .arg(pointCount)
                .arg(plan.plannedPowerWatts, 0, 'f', 0));
    } else {
        m_planSummaryLabel->setText(QStringLiteral("方案：未创建"));
    }
}

TherapyPlan PlanningPage::buildPlanFromUi(ApprovalState approvalState) const
{
    TherapyPlan plan;
    plan.id = m_context->hasActivePlan() ? m_context->activePlan().id : createPlanId();
    plan.patientId = m_context->selectedPatient().id;
    plan.name = QStringLiteral("乳腺消融方案");
    plan.approvalState = approvalState;
    plan.plannedPowerWatts = m_powerSpin->value();
    plan.spacingMm = m_spacingSpin->value();
    plan.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
    plan.createdAt = QDateTime::currentDateTime();

    switch (m_patternCombo->currentIndex()) {
    case 0:
        plan.pattern = TreatmentPattern::Point;
        break;
    case 1:
        plan.pattern = TreatmentPattern::Line;
        break;
    default:
        plan.pattern = TreatmentPattern::Segmented;
        break;
    }

    const int totalLayers = m_layerCountSpin->value();
    const int segmentCount = plan.pattern == TreatmentPattern::Segmented ? 2 : 1;
    const int layersPerSegment = std::max(1, totalLayers / segmentCount);

    int pointIndex = 0;
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        TherapySegment segment;
        segment.id = QStringLiteral("%1-S%2").arg(plan.id).arg(segmentIndex + 1);
        segment.orderIndex = segmentIndex;
        segment.label = QStringLiteral("治疗段 %1").arg(segmentIndex + 1);

        for (int layer = 0; layer < layersPerSegment; ++layer) {
            const qreal y = -18.0 + (segmentIndex * layersPerSegment + layer) * m_spacingSpin->value() * 0.8;
            for (int column = 0; column < 4; ++column) {
                TherapyPoint point;
                point.index = pointIndex++;
                point.dwellSeconds = m_dwellSpin->value();
                point.powerWatts = m_powerSpin->value();
                point.positionMm = QPointF(-18.0 + column * m_spacingSpin->value() * 2.2, y);
                segment.points.push_back(point);
            }
        }

        segment.plannedDurationSeconds = segment.points.size() * m_dwellSpin->value();
        plan.segments.push_back(segment);
    }

    return plan;
}

void PlanningPage::populatePatientSelector()
{
    m_patientCombo->clear();
    if (m_clinicalDataRepository == nullptr) {
        return;
    }

    const QVector<PatientRecord> patients = m_clinicalDataRepository->listPatients();
    for (const PatientRecord& patient : patients) {
        m_patientCombo->addItem(patientDisplayLabel(patient), patient.id);
    }
}

void PlanningPage::refreshImagingPaths(const QString& patientId)
{
    m_pathList->clear();

    QVector<ImageSeriesRecord> imageSeries;
    if (m_clinicalDataRepository != nullptr && !patientId.isEmpty()) {
        imageSeries = m_clinicalDataRepository->listImageSeriesForPatient(patientId);
    }

    if (imageSeries.isEmpty()) {
        m_pathList->addItems({
            QStringLiteral("[1] 图像通道采集路径 1"),
            QStringLiteral("[2] 图像通道采集路径 2"),
            QStringLiteral("[3] 图像通道采集路径 3")
        });
        return;
    }

    int index = 1;
    for (const ImageSeriesRecord& series : imageSeries) {
        m_pathList->addItem(QStringLiteral("[%1] %2 | %3").arg(index++).arg(series.type).arg(series.storagePath));
    }
}

void PlanningPage::syncPatientSelector(const QString& patientId)
{
    const int index = m_patientCombo->findData(patientId);
    if (index < 0 || index == m_patientCombo->currentIndex()) {
        return;
    }

    const QSignalBlocker blocker(m_patientCombo);
    m_patientCombo->setCurrentIndex(index);
}

}  // namespace panthera::modules
