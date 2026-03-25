#include "modules/planning/planning_page.h"

#include <algorithm>

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVector3D>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

PatientRecord buildFallbackPatient()
{
    return PatientRecord {
        QStringLiteral("P2026001"),
        QStringLiteral("\u5f20\u4e09"),
        24,
        QStringLiteral("\u5973"),
        QStringLiteral("\u53f3\u4e73\u6d78\u6da6\u6027\u5bfc\u7ba1\u764c II \u671f"),
        QStringLiteral("13800000001")
    };
}

QString createPlanId()
{
    return QStringLiteral("PLAN-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")));
}

QString patientDisplayLabel(const PatientRecord& patient)
{
    return QStringLiteral("%1 | %2 | %3\u5c81").arg(patient.name).arg(patient.id).arg(patient.age);
}

QString summarizePlan(const TherapyPlan& plan)
{
    int pointCount = 0;
    double durationSeconds = 0.0;
    for (const TherapySegment& segment : plan.segments) {
        pointCount += segment.points.size();
        durationSeconds += segment.plannedDurationSeconds;
    }

    return QStringLiteral("\u65b9\u6848\u7f16\u53f7\uff1a%1\n\u6cbb\u7597\u6a21\u5f0f\uff1a%2\n\u6cbb\u7597\u70b9\u6570\uff1a%3\n\u8ba1\u5212\u529f\u7387\uff1a%4 W\n\u9884\u8ba1\u603b\u65f6\u957f\uff1a%5 min")
        .arg(plan.id)
        .arg(toDisplayString(plan.pattern))
        .arg(pointCount)
        .arg(plan.plannedPowerWatts, 0, 'f', 0)
        .arg(durationSeconds / 60.0, 0, 'f', 2);
}

QString defaultChannelCoordinate(int index)
{
    const double x = -20.0 + (index * 5.0);
    return QStringLiteral("(%1, -9.53, 27)").arg(QString::number(x, 'f', 0));
}

QString defaultChannelText(int index)
{
    return QStringLiteral("[%1] \u56fe\u50cf\u901a\u9053\u91c7\u96c6\u8def\u5f84%1    %2").arg(index + 1).arg(defaultChannelCoordinate(index));
}

QString extractChannelCoordinate(const QString& itemText)
{
    const int coordinateIndex = itemText.indexOf(QLatin1Char('('));
    return coordinateIndex >= 0 ? itemText.mid(coordinateIndex).trimmed() : QStringLiteral("\u672a\u8bbe\u7f6e");
}

QString extractChannelLabel(const QString& itemText)
{
    const int coordinateIndex = itemText.indexOf(QLatin1Char('('));
    return (coordinateIndex >= 0 ? itemText.left(coordinateIndex) : itemText).trimmed();
}

QString buildStoragePath(const QString& patientId, const QString& batchToken, int channelIndex, int sliceIndex)
{
    return QStringLiteral("patient/%1/ultrasound/%2/channel_%3_slice_%4.png")
        .arg(patientId)
        .arg(batchToken)
        .arg(channelIndex + 1, 2, 10, QChar('0'))
        .arg(sliceIndex + 1, 3, 10, QChar('0'));
}

QVector3D parseCoordinateText(const QString& text)
{
    QString normalized = text;
    normalized.remove(QLatin1Char('('));
    normalized.remove(QLatin1Char(')'));
    const QStringList parts = normalized.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return QVector3D();
    }

    return QVector3D(parts.at(0).trimmed().toDouble(), parts.at(1).trimmed().toDouble(), parts.at(2).trimmed().toDouble());
}

}  // namespace

PlanningPage::PlanningPage(
    ApplicationContext* context,
    SafetyKernel* safetyKernel,
    AuditService* auditService,
    IClinicalDataRepository* clinicalDataRepository,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_safetyKernel(safetyKernel)
    , m_auditService(auditService)
    , m_clinicalDataRepository(clinicalDataRepository)
    , m_clinicalDataService(clinicalDataRepository)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);

    auto* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(10);
    leftColumn->setContentsMargins(0, 0, 0, 0);

    auto* pathCard = new QFrame();
    pathCard->setObjectName(QStringLiteral("planningSidebarCard"));
    pathCard->setMinimumWidth(292);
    pathCard->setMaximumHeight(330);
    auto* pathLayout = new QVBoxLayout(pathCard);
    pathLayout->setContentsMargins(12, 12, 12, 12);
    pathLayout->setSpacing(10);

    auto* pathHeader = new QHBoxLayout();
    auto* pathTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u901a\u9053\u91c7\u96c6\u5217\u8868"));
    pathTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* pathIcon = new QLabel(QStringLiteral("="));
    pathIcon->setObjectName(QStringLiteral("planningHeaderIcon"));
    pathHeader->addWidget(pathTitle);
    pathHeader->addStretch();
    pathHeader->addWidget(pathIcon);

    m_pathList = new QListWidget();
    m_pathList->setObjectName(QStringLiteral("planningPathList"));

    auto* pathButtons = new QHBoxLayout();
    pathButtons->setSpacing(10);
    m_addPathButton = new QPushButton(QStringLiteral("+ \u65b0\u589e\u8def\u5f84"));
    m_addPathButton->setObjectName(QStringLiteral("planningActionButton"));
    m_addPathButton->setMinimumHeight(38);
    m_removePathButton = new QPushButton(QStringLiteral("\u00d7 \u5220\u9664\u8def\u5f84"));
    m_removePathButton->setObjectName(QStringLiteral("planningGhostButton"));
    m_removePathButton->setMinimumHeight(38);
    pathButtons->addWidget(m_addPathButton);
    pathButtons->addWidget(m_removePathButton);

    pathLayout->addLayout(pathHeader);
    pathLayout->addWidget(m_pathList, 1);
    pathLayout->addLayout(pathButtons);
    leftColumn->addWidget(pathCard, 2);

    auto* captureCard = new QFrame();
    captureCard->setObjectName(QStringLiteral("planningSidebarCard"));
    captureCard->setMinimumWidth(292);
    captureCard->setMaximumHeight(240);
    auto* captureLayout = new QVBoxLayout(captureCard);
    captureLayout->setContentsMargins(12, 12, 12, 12);
    captureLayout->setSpacing(10);

    auto* captureHeader = new QHBoxLayout();
    auto* captureTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u91c7\u96c6"));
    captureTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* captureIcon = new QLabel(QStringLiteral("I"));
    captureIcon->setObjectName(QStringLiteral("planningHeaderIcon"));
    captureHeader->addWidget(captureTitle);
    captureHeader->addStretch();
    captureHeader->addWidget(captureIcon);

    auto* captureForm = new QFormLayout();
    captureForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    captureForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    captureForm->setHorizontalSpacing(18);
    captureForm->setVerticalSpacing(12);

    m_patientCombo = new QComboBox();
    m_patientCombo->setVisible(false);

    m_layerCountSpin = new QSpinBox();
    m_layerCountSpin->setRange(1, 60);
    m_layerCountSpin->setValue(20);
    m_layerCountSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_layerCountSpin->setObjectName(QStringLiteral("planningMetricSpin"));

    m_stepSpin = new QSpinBox();
    m_stepSpin->setRange(1, 20);
    m_stepSpin->setValue(1);
    m_stepSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_stepSpin->setObjectName(QStringLiteral("planningMetricSpin"));

    captureForm->addRow(QStringLiteral("\u5c42\u6570"), m_layerCountSpin);
    captureForm->addRow(QStringLiteral("\u6b65\u957f"), m_stepSpin);

    m_acquireImageButton = new QPushButton(QStringLiteral("> \u56fe\u50cf\u91c7\u96c6"));
    m_acquireImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_acquireImageButton->setMinimumHeight(38);

    captureLayout->addLayout(captureHeader);
    captureLayout->addLayout(captureForm);
    captureLayout->addWidget(m_acquireImageButton, 0, Qt::AlignLeft);
    leftColumn->addWidget(captureCard, 1);

    auto* modelCard = new QFrame();
    modelCard->setObjectName(QStringLiteral("planningSidebarCard"));
    modelCard->setMinimumWidth(292);
    modelCard->setMaximumHeight(300);
    auto* modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setContentsMargins(12, 12, 12, 12);
    modelLayout->setSpacing(10);

    m_generate3dButton = new QPushButton(QStringLiteral("+ \u4e09\u7ef4\u56fe\u50cf\u751f\u6210"));
    m_generate3dButton->setObjectName(QStringLiteral("planningPrimaryOutlineButton"));
    m_generate3dButton->setMinimumHeight(38);

    auto* modelHeader = new QHBoxLayout();
    auto* modelTitle = new QLabel(QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u5217\u8868"));
    modelTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* modelIcon = new QLabel(QStringLiteral("L"));
    modelIcon->setObjectName(QStringLiteral("planningHeaderIcon"));
    modelHeader->addWidget(modelTitle);
    modelHeader->addStretch();
    modelHeader->addWidget(modelIcon);

    m_modelList = new QListWidget();
    m_modelList->setObjectName(QStringLiteral("planningModelList"));

    modelLayout->addWidget(m_generate3dButton, 0, Qt::AlignLeft);
    modelLayout->addLayout(modelHeader);
    modelLayout->addWidget(m_modelList, 1);
    leftColumn->addWidget(modelCard, 2);

    rootLayout->addLayout(leftColumn, 21);

    auto* centerColumn = new QVBoxLayout();
    centerColumn->setSpacing(10);
    centerColumn->setContentsMargins(0, 0, 0, 0);

    auto* previewFrame = new QFrame();
    previewFrame->setObjectName(QStringLiteral("planningPreviewFrame"));
    previewFrame->setMinimumSize(720, 500);
    auto* previewFrameLayout = new QVBoxLayout(previewFrame);
    previewFrameLayout->setContentsMargins(4, 4, 4, 4);
    previewFrameLayout->setSpacing(0);

    auto* previewHeader = new QHBoxLayout();
    previewHeader->setContentsMargins(0, 0, 0, 0);
    previewHeader->setSpacing(0);
    previewHeader->addStretch();
    m_annotationButton = new QToolButton();
    m_annotationButton->setObjectName(QStringLiteral("planningIconButton"));
    m_annotationButton->setText(QStringLiteral("\u270e"));
    m_annotationButton->setCheckable(true);
    previewHeader->addWidget(m_annotationButton, 0, Qt::AlignTop | Qt::AlignRight);
    previewFrameLayout->addLayout(previewHeader);

    auto* previewStack = new QGridLayout();
    previewStack->setContentsMargins(8, 8, 8, 8);
    previewStack->setSpacing(0);
    m_preview = new MockUltrasoundView();
    m_preview->setObjectName(QStringLiteral("planningPreviewWidget"));
    m_preview->setCaption(QStringLiteral(""));

    m_previewOverlayLabel = new QLabel(QStringLiteral("\u56fe\u50cf\u663e\u793a\u533a\u57df"));
    m_previewOverlayLabel->setObjectName(QStringLiteral("planningPreviewOverlayLabel"));
    m_previewOverlayLabel->setAlignment(Qt::AlignCenter);
    m_previewOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_annotationPanel = new QFrame();
    m_annotationPanel->setObjectName(QStringLiteral("planningAnnotationPanel"));
    m_annotationPanel->setVisible(false);
    auto* annotationLayout = new QVBoxLayout(m_annotationPanel);
    annotationLayout->setContentsMargins(10, 12, 10, 12);
    annotationLayout->setSpacing(10);

    auto* annotationBrushButton = new QToolButton();
    annotationBrushButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    annotationBrushButton->setText(QStringLiteral("\u270e"));
    annotationLayout->addWidget(annotationBrushButton, 0, Qt::AlignHCenter);

    auto* separatorTop = new QFrame();
    separatorTop->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    separatorTop->setFrameShape(QFrame::HLine);
    annotationLayout->addWidget(separatorTop);

    m_annotationRedButton = new QToolButton();
    m_annotationRedButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationRedButton->setProperty("swatchColor", QStringLiteral("red"));
    annotationLayout->addWidget(m_annotationRedButton, 0, Qt::AlignHCenter);

    m_annotationBlueButton = new QToolButton();
    m_annotationBlueButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationBlueButton->setProperty("swatchColor", QStringLiteral("blue"));
    annotationLayout->addWidget(m_annotationBlueButton, 0, Qt::AlignHCenter);

    m_annotationGreenButton = new QToolButton();
    m_annotationGreenButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationGreenButton->setProperty("swatchColor", QStringLiteral("green"));
    annotationLayout->addWidget(m_annotationGreenButton, 0, Qt::AlignHCenter);

    m_annotationOrangeButton = new QToolButton();
    m_annotationOrangeButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationOrangeButton->setProperty("swatchColor", QStringLiteral("orange"));
    annotationLayout->addWidget(m_annotationOrangeButton, 0, Qt::AlignHCenter);

    auto* separatorMiddle = new QFrame();
    separatorMiddle->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    separatorMiddle->setFrameShape(QFrame::HLine);
    annotationLayout->addWidget(separatorMiddle);

    m_annotationUndoButton = new QToolButton();
    m_annotationUndoButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    m_annotationUndoButton->setText(QStringLiteral("\u21b6"));
    annotationLayout->addWidget(m_annotationUndoButton, 0, Qt::AlignHCenter);

    m_annotationClearButton = new QToolButton();
    m_annotationClearButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    m_annotationClearButton->setText(QStringLiteral("\U0001F5D1"));
    annotationLayout->addWidget(m_annotationClearButton, 0, Qt::AlignHCenter);

    auto* separatorBottom = new QFrame();
    separatorBottom->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    separatorBottom->setFrameShape(QFrame::HLine);
    annotationLayout->addWidget(separatorBottom);

    m_annotationCollapseButton = new QToolButton();
    m_annotationCollapseButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    m_annotationCollapseButton->setText(QStringLiteral("\u2303"));
    annotationLayout->addWidget(m_annotationCollapseButton, 0, Qt::AlignHCenter);

    previewStack->addWidget(m_preview, 0, 0);
    previewStack->addWidget(m_previewOverlayLabel, 0, 0);
    previewStack->addWidget(m_annotationPanel, 0, 0, Qt::AlignTop | Qt::AlignRight);
    previewFrameLayout->addLayout(previewStack, 1);
    centerColumn->addWidget(previewFrame, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(10);

    auto* chartCard = new QFrame();
    chartCard->setObjectName(QStringLiteral("planningBottomCard"));
    chartCard->setMinimumWidth(520);
    chartCard->setMinimumHeight(210);
    auto* chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(18, 12, 18, 12);
    chartLayout->setSpacing(10);

    auto* chartHeader = new QHBoxLayout();
    auto* chartTitle = new QLabel(QStringLiteral("\u80fd\u91cf\u8f93\u51fa\u66f2\u7ebf (J)"));
    chartTitle->setObjectName(QStringLiteral("planningBottomTitle"));
    m_chartSummaryLabel = new QLabel(QStringLiteral("\u5e73\u5747\u529f\u7387: 400W"));
    m_chartSummaryLabel->setObjectName(QStringLiteral("planningChartSummaryLabel"));
    chartHeader->addWidget(chartTitle);
    chartHeader->addStretch();
    chartHeader->addWidget(m_chartSummaryLabel);

    auto* chartCanvas = new QFrame();
    chartCanvas->setObjectName(QStringLiteral("planningChartCanvas"));
    chartCanvas->setMinimumHeight(150);

    chartLayout->addLayout(chartHeader);
    chartLayout->addWidget(chartCanvas, 1);
    bottomRow->addWidget(chartCard, 2);

    auto* imageOpsCard = new QFrame();
    imageOpsCard->setObjectName(QStringLiteral("planningBottomCard"));
    imageOpsCard->setMinimumWidth(260);
    imageOpsCard->setMinimumHeight(210);
    auto* imageOpsLayout = new QVBoxLayout(imageOpsCard);
    imageOpsLayout->setContentsMargins(18, 12, 18, 12);
    imageOpsLayout->setSpacing(16);

    auto* imageOpsHeader = new QHBoxLayout();
    auto* imageOpsTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u64cd\u4f5c"));
    imageOpsTitle->setObjectName(QStringLiteral("planningBottomTitle"));
    auto* imageOpsIcon = new QLabel(QStringLiteral("S"));
    imageOpsIcon->setObjectName(QStringLiteral("planningHeaderIcon"));
    imageOpsHeader->addWidget(imageOpsTitle);
    imageOpsHeader->addStretch();
    imageOpsHeader->addWidget(imageOpsIcon);

    auto* imageOpsButtons = new QHBoxLayout();
    imageOpsButtons->setSpacing(12);
    m_storeImageButton = new QPushButton(QStringLiteral("\u672c\u5730\u5b58\u50a8"));
    m_storeImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_loadImageButton = new QPushButton(QStringLiteral("\u8bfb\u53d6\u56fe\u50cf"));
    m_loadImageButton->setObjectName(QStringLiteral("planningActionButton"));
    imageOpsButtons->addWidget(m_storeImageButton);
    imageOpsButtons->addWidget(m_loadImageButton);

    imageOpsLayout->addLayout(imageOpsHeader);
    imageOpsLayout->addStretch();
    imageOpsLayout->addLayout(imageOpsButtons);
    bottomRow->addWidget(imageOpsCard, 1);

    centerColumn->addLayout(bottomRow, 0);
    rootLayout->addLayout(centerColumn, 55);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(0);
    rightColumn->setContentsMargins(0, 0, 0, 0);

    auto* controlsFrame = new QFrame();
    controlsFrame->setObjectName(QStringLiteral("planningControlFrame"));
    controlsFrame->setMinimumWidth(360);
    controlsFrame->setMaximumWidth(360);
    auto* controlsLayout = new QVBoxLayout(controlsFrame);
    controlsLayout->setContentsMargins(14, 14, 14, 14);
    controlsLayout->setSpacing(12);

    auto* modeCard = new QFrame();
    modeCard->setObjectName(QStringLiteral("planningModeCard"));
    modeCard->setMinimumHeight(228);
    auto* modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(12, 10, 12, 10);
    modeLayout->setSpacing(8);

    auto* executeRow = new QHBoxLayout();
    executeRow->setSpacing(18);
    m_directTreatmentRadio = new QRadioButton(QStringLiteral("\u76f4\u63a5\u6cbb\u7597"));
    m_segmentedTreatmentRadio = new QRadioButton(QStringLiteral("\u5206\u6bb5\u6267\u884c"));
    m_directTreatmentRadio->setChecked(true);
    auto* deliveryModeGroup = new QButtonGroup(this);
    deliveryModeGroup->addButton(m_directTreatmentRadio);
    deliveryModeGroup->addButton(m_segmentedTreatmentRadio);
    executeRow->addWidget(m_directTreatmentRadio);
    executeRow->addWidget(m_segmentedTreatmentRadio);

    auto* patternRow = new QHBoxLayout();
    patternRow->setSpacing(18);
    m_pointTreatmentRadio = new QRadioButton(QStringLiteral("\u70b9\u6cbb\u7597"));
    m_lineTreatmentRadio = new QRadioButton(QStringLiteral("\u7ebf\u6cbb\u7597"));
    m_pointTreatmentRadio->setChecked(true);
    auto* treatmentPatternGroup = new QButtonGroup(this);
    treatmentPatternGroup->addButton(m_pointTreatmentRadio);
    treatmentPatternGroup->addButton(m_lineTreatmentRadio);
    patternRow->addWidget(m_pointTreatmentRadio);
    patternRow->addWidget(m_lineTreatmentRadio);

    auto* metricsForm = new QFormLayout();
    metricsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    metricsForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    metricsForm->setHorizontalSpacing(12);
    metricsForm->setVerticalSpacing(8);
    metricsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_spacingSpin = new QDoubleSpinBox();
    m_spacingSpin->setRange(0.5, 10.0);
    m_spacingSpin->setValue(3.0);
    m_spacingSpin->setDecimals(1);
    m_spacingSpin->setSuffix(QStringLiteral(" mm"));
    m_spacingSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spacingSpin->setObjectName(QStringLiteral("planningMetricSpin"));
    m_spacingSpin->setMinimumWidth(132);

    m_dwellSpin = new QDoubleSpinBox();
    m_dwellSpin->setRange(0.1, 10.0);
    m_dwellSpin->setValue(0.3);
    m_dwellSpin->setDecimals(1);
    m_dwellSpin->setSuffix(QStringLiteral(" s"));
    m_dwellSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_dwellSpin->setObjectName(QStringLiteral("planningMetricSpin"));
    m_dwellSpin->setMinimumWidth(132);

    m_totalDurationValueLabel = new QLabel(QStringLiteral("12.45 min"));
    m_totalDurationValueLabel->setObjectName(QStringLiteral("planningMetricValueLabel"));

    metricsForm->addRow(QStringLiteral("\u6cbb\u7597\u884c\u8ddd"), m_spacingSpin);
    metricsForm->addRow(QStringLiteral("\u70b9\u7597\u65f6\u957f"), m_dwellSpin);
    metricsForm->addRow(QStringLiteral("\u6cbb\u7597\u603b\u65f6\u957f"), m_totalDurationValueLabel);
    m_generateTargetsButton = new QPushButton(QStringLiteral("\u751f\u6210\u9776\u70b9"));
    m_generateTargetsButton->setObjectName(QStringLiteral("planningActionButton"));
    m_generateTargetsButton->setMinimumHeight(34);

    modeLayout->addLayout(executeRow);
    modeLayout->addLayout(patternRow);
    modeLayout->addLayout(metricsForm);
    modeLayout->addWidget(m_generateTargetsButton, 0, Qt::AlignLeft);

    auto* powerCard = new QFrame();
    powerCard->setObjectName(QStringLiteral("planningModeCard"));
    auto* powerCardLayout = new QVBoxLayout(powerCard);
    powerCardLayout->setContentsMargins(12, 10, 12, 10);
    powerCardLayout->setSpacing(10);

    auto* powerTitle = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u529f\u7387"));
    powerTitle->setObjectName(QStringLiteral("planningSectionLabel"));

    auto* powerRow = new QHBoxLayout();
    m_powerValueLabel = new QLabel(QStringLiteral("400W"));
    m_powerValueLabel->setObjectName(QStringLiteral("planningPowerValueLabel"));
    powerRow->addWidget(powerTitle);
    powerRow->addStretch();
    powerRow->addWidget(m_powerValueLabel);

    m_powerSpin = new QDoubleSpinBox();
    m_powerSpin->setRange(20.0, 800.0);
    m_powerSpin->setValue(400.0);
    m_powerSpin->setVisible(false);

    m_powerSlider = new QSlider(Qt::Horizontal);
    m_powerSlider->setRange(20, 800);
    m_powerSlider->setValue(400);
    m_powerSlider->setObjectName(QStringLiteral("planningPowerSlider"));

    auto* respiratoryRow = new QHBoxLayout();
    auto* respiratoryTitle = new QLabel(QStringLiteral("\u547c\u5438\u8ddf\u968f\u72b6\u6001"));
    respiratoryTitle->setObjectName(QStringLiteral("planningSectionLabel"));
    respiratoryRow->addWidget(respiratoryTitle);
    respiratoryRow->addStretch();
    m_respiratoryTrackingCheck = new QCheckBox();
    m_respiratoryTrackingCheck->setObjectName(QStringLiteral("planningToggleCheck"));
    respiratoryRow->addWidget(m_respiratoryTrackingCheck);

    m_generateAssessmentButton = new QPushButton(QStringLiteral("\u751f\u6210\u65b9\u6848\u8bc4\u4f30"));
    m_generateAssessmentButton->setObjectName(QStringLiteral("planningActionButton"));
    m_generateAssessmentButton->setMinimumHeight(34);

    powerCardLayout->addLayout(powerRow);
    powerCardLayout->addWidget(m_powerSlider);
    powerCardLayout->addLayout(respiratoryRow);
    powerCardLayout->addWidget(m_generateAssessmentButton, 0, Qt::AlignLeft);

    auto* assessmentCard = new QFrame();
    assessmentCard->setObjectName(QStringLiteral("planningModeCard"));
    auto* assessmentLayout = new QVBoxLayout(assessmentCard);
    assessmentLayout->setContentsMargins(12, 10, 12, 10);
    assessmentLayout->setSpacing(8);

    auto* assessmentTitle = new QLabel(QStringLiteral("\u65b9\u6848\u8bc4\u4f30"));
    assessmentTitle->setObjectName(QStringLiteral("planningSectionLabel"));

    m_assessmentPreview = new QPlainTextEdit();
    m_assessmentPreview->setObjectName(QStringLiteral("planningSummaryEdit"));
    m_assessmentPreview->setReadOnly(true);
    m_assessmentPreview->setMinimumHeight(110);
    m_assessmentPreview->setMaximumHeight(140);

    assessmentLayout->addWidget(assessmentTitle);
    assessmentLayout->addWidget(m_assessmentPreview);

    auto* planCard = new QFrame();
    planCard->setObjectName(QStringLiteral("planningModeCard"));
    auto* planCardLayout = new QVBoxLayout(planCard);
    planCardLayout->setContentsMargins(12, 10, 12, 10);
    planCardLayout->setSpacing(8);

    auto* planOpsHeader = new QHBoxLayout();
    auto* planOpsTitle = new QLabel(QStringLiteral("\u6cbb\u7597\u65b9\u6848\u64cd\u4f5c"));
    planOpsTitle->setObjectName(QStringLiteral("planningSectionLabel"));
    m_previewPlanButton = new QPushButton(QStringLiteral("\u9884\u89c8"));
    m_previewPlanButton->setObjectName(QStringLiteral("planningActionButton"));
    m_previewPlanButton->setMinimumWidth(82);
    m_previewPlanButton->setMinimumHeight(34);
    planOpsHeader->addWidget(planOpsTitle);
    planOpsHeader->addStretch();
    planOpsHeader->addWidget(m_previewPlanButton);

    auto* planButtonRow = new QHBoxLayout();
    planButtonRow->setSpacing(8);
    m_addPlanButton = new QPushButton(QStringLiteral("+ \u6dfb\u52a0"));
    m_addPlanButton->setObjectName(QStringLiteral("planningActionButton"));
    m_addPlanButton->setMinimumHeight(34);
    m_deletePlanButton = new QPushButton(QStringLiteral("\u00d7 \u5220\u9664"));
    m_deletePlanButton->setObjectName(QStringLiteral("planningGhostButton"));
    m_deletePlanButton->setMinimumHeight(34);
    m_editPlanButton = new QToolButton();
    m_editPlanButton->setObjectName(QStringLiteral("planningIconButton"));
    m_editPlanButton->setText(QStringLiteral("\u270e"));
    m_editPlanButton->setMinimumSize(34, 34);
    planButtonRow->addWidget(m_addPlanButton);
    planButtonRow->addWidget(m_deletePlanButton);
    planButtonRow->addStretch();
    planButtonRow->addWidget(m_editPlanButton);

    m_planPreview = new QPlainTextEdit();
    m_planPreview->setObjectName(QStringLiteral("planningSummaryEdit"));
    m_planPreview->setReadOnly(true);
    m_planPreview->setMinimumHeight(170);
    m_planPreview->setMaximumHeight(210);

    m_patientSummaryLabel = new QLabel();
    m_patientSummaryLabel->setObjectName(QStringLiteral("planningContextLabel"));
    m_patientSummaryLabel->setWordWrap(true);
    m_patientSummaryLabel->hide();
    m_planSummaryLabel = new QLabel();
    m_planSummaryLabel->setObjectName(QStringLiteral("planningContextLabel"));
    m_planSummaryLabel->setWordWrap(true);
    m_planSummaryLabel->hide();

    planCardLayout->addLayout(planOpsHeader);
    planCardLayout->addLayout(planButtonRow);
    planCardLayout->addWidget(m_planPreview);

    controlsLayout->addWidget(modeCard);
    controlsLayout->addWidget(powerCard);
    controlsLayout->addWidget(assessmentCard);
    controlsLayout->addWidget(planCard);
    controlsLayout->addStretch();

    rightColumn->addWidget(controlsFrame);
    rootLayout->addLayout(rightColumn, 24);

    connect(m_addPathButton, &QPushButton::clicked, this, &PlanningPage::addPathItem);
    connect(m_removePathButton, &QPushButton::clicked, this, &PlanningPage::removeCurrentPathItem);
    connect(m_acquireImageButton, &QPushButton::clicked, this, &PlanningPage::simulateImageAcquisition);
    connect(m_generate3dButton, &QPushButton::clicked, this, &PlanningPage::generateThreeDimensionalImage);
    connect(m_storeImageButton, &QPushButton::clicked, this, &PlanningPage::storeCapturedImages);
    connect(m_loadImageButton, &QPushButton::clicked, this, &PlanningPage::loadStoredImages);
    connect(m_annotationButton, &QToolButton::clicked, this, &PlanningPage::toggleAnnotationPanel);
    connect(m_annotationCollapseButton, &QToolButton::clicked, this, &PlanningPage::toggleAnnotationPanel);
    connect(m_modelList, &QListWidget::currentRowChanged, this, &PlanningPage::onStagedSliceSelectionChanged);
    connect(m_preview, &MockUltrasoundView::annotationStrokesChanged, this, &PlanningPage::onPreviewAnnotationsChanged);
    connect(m_generateTargetsButton, &QPushButton::clicked, this, &PlanningPage::generateDraftPlan);
    connect(m_generateAssessmentButton, &QPushButton::clicked, this, &PlanningPage::generateDraftPlan);
    connect(m_previewPlanButton, &QPushButton::clicked, this, &PlanningPage::previewCurrentPlan);
    connect(m_addPlanButton, &QPushButton::clicked, this, &PlanningPage::saveCurrentPlan);
    connect(m_deletePlanButton, &QPushButton::clicked, this, &PlanningPage::deleteCurrentPlan);
    connect(m_editPlanButton, &QToolButton::clicked, this, &PlanningPage::editCurrentPlan);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &PlanningPage::updateContextSummary);
    connect(m_context, &ApplicationContext::activePlanChanged, this, [this](const TherapyPlan& plan) {
        applyPlanToUi(plan);
        updateContextSummary();
    });
    connect(m_context, &ApplicationContext::activePlanCleared, this, &PlanningPage::updateContextSummary);

    const auto refreshMetrics = [this]() {
        refreshDerivedMetrics();
    };
    connect(m_spacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [refreshMetrics](double) { refreshMetrics(); });
    connect(m_dwellSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [refreshMetrics](double) { refreshMetrics(); });
    connect(m_layerCountSpin, qOverload<int>(&QSpinBox::valueChanged), this, [refreshMetrics](int) { refreshMetrics(); });
    connect(m_pointTreatmentRadio, &QRadioButton::toggled, this, [refreshMetrics](bool) { refreshMetrics(); });
    connect(m_lineTreatmentRadio, &QRadioButton::toggled, this, [refreshMetrics](bool) { refreshMetrics(); });
    connect(m_directTreatmentRadio, &QRadioButton::toggled, this, [refreshMetrics](bool) { refreshMetrics(); });
    connect(m_segmentedTreatmentRadio, &QRadioButton::toggled, this, [refreshMetrics](bool) { refreshMetrics(); });
    connect(m_powerSlider, &QSlider::valueChanged, this, [this](int value) {
        if (std::abs(m_powerSpin->value() - value) > 0.01) {
            m_powerSpin->setValue(value);
        }
        refreshDerivedMetrics();
    });
    connect(m_powerSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        const int rounded = static_cast<int>(value);
        if (m_powerSlider->value() != rounded) {
            const QSignalBlocker blocker(m_powerSlider);
            m_powerSlider->setValue(rounded);
        }
        refreshDerivedMetrics();
    });

    const auto activateColor = [this](const QColor& color) {
        m_preview->setCurrentAnnotationColor(color);
        m_preview->setAnnotationEnabled(true);
    };
    connect(m_annotationRedButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(201, 71, 51)); });
    connect(m_annotationBlueButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(91, 158, 230)); });
    connect(m_annotationGreenButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(163, 239, 76)); });
    connect(m_annotationOrangeButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(255, 177, 75)); });
    connect(m_annotationUndoButton, &QToolButton::clicked, m_preview, &MockUltrasoundView::undoLastAnnotation);
    connect(m_annotationClearButton, &QToolButton::clicked, m_preview, &MockUltrasoundView::clearAnnotations);

    populatePatientSelector();
    refreshImagingPaths(QString());
    refreshDerivedMetrics();
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
    updateAssessmentText(&draftPlan);
    updatePlanPreviewText(&draftPlan);
    m_previewOverlayLabel->setVisible(false);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u751f\u6210\u65b9\u6848\u8349\u6848\uff1a%1").arg(draftPlan.id));
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
    updatePlanPreviewText(&approvedPlan);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u5ba1\u6279\u5e76\u9501\u5b9a\u65b9\u6848\uff1a%1").arg(approvedPlan.id));
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
    updatePlanPreviewText(&plan);
}

void PlanningPage::updateContextSummary()
{
    if (m_context->hasSelectedPatient()) {
        const PatientRecord& patient = m_context->selectedPatient();
        syncPatientSelector(patient.id);
        refreshImagingPaths(patient.id);
        m_patientSummaryLabel->setText(
            QStringLiteral("\u5f53\u524d\u60a3\u8005\n\u59d3\u540d\uff1a%1\n\u7f16\u53f7\uff1a%2\n\u5e74\u9f84\uff1a%3\n\u8bca\u65ad\uff1a%4")
                .arg(patient.name)
                .arg(patient.id)
                .arg(patient.age)
                .arg(patient.diagnosis));
    } else {
        refreshImagingPaths(QString());
        m_patientSummaryLabel->setText(QStringLiteral("\u5f53\u524d\u60a3\u8005\n\u672a\u9009\u62e9\u60a3\u8005"));
    }

    if (m_context->hasActivePlan()) {
        const TherapyPlan& plan = m_context->activePlan();
        m_planSummaryLabel->setText(
            QStringLiteral("\u6d3b\u52a8\u65b9\u6848\n\u72b6\u6001\uff1a%1\n\u6a21\u5f0f\uff1a%2\n\u547c\u5438\u8ddf\u968f\uff1a%3")
                .arg(toDisplayString(plan.approvalState))
                .arg(toDisplayString(plan.pattern))
                .arg(plan.respiratoryTrackingEnabled ? QStringLiteral("\u5f00\u542f") : QStringLiteral("\u5173\u95ed")));
        updateAssessmentText(&plan);
        updatePlanPreviewText(&plan);
        m_previewOverlayLabel->setVisible(false);
    } else {
        m_planSummaryLabel->setText(QStringLiteral("\u6d3b\u52a8\u65b9\u6848\n\u5c1a\u672a\u751f\u6210"));
        updateAssessmentText(nullptr);
        updatePlanPreviewText(nullptr);
        m_preview->clearPlan();
        if (m_stagedImageSeries.isEmpty()) {
            m_previewOverlayLabel->setText(QStringLiteral("\u56fe\u50cf\u663e\u793a\u533a\u57df"));
            m_previewOverlayLabel->setVisible(true);
        }
    }
}

TherapyPlan PlanningPage::buildPlanFromUi(ApprovalState approvalState) const
{
    TherapyPlan plan;
    plan.id = m_context->hasActivePlan() ? m_context->activePlan().id : createPlanId();
    plan.patientId = m_context->selectedPatient().id;
    plan.name = m_context->hasActivePlan() && !m_context->activePlan().name.trimmed().isEmpty()
        ? m_context->activePlan().name
        : QStringLiteral("\u6cbb\u7597\u65b9\u68481");
    plan.approvalState = approvalState;
    plan.plannedPowerWatts = m_powerSpin->value();
    plan.spacingMm = m_spacingSpin->value();
    plan.dwellSeconds = m_dwellSpin->value();
    plan.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
    plan.deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
    plan.createdAt = QDateTime::currentDateTime();

    const QVector3D coordinate = parseCoordinateText(currentChannelCoordinate());
    if (m_context->hasActivePlan()) {
        plan.coordinateX = m_context->activePlan().coordinateX;
        plan.coordinateY = m_context->activePlan().coordinateY;
        plan.coordinateZ = m_context->activePlan().coordinateZ;
        plan.depthMm = m_context->activePlan().depthMm;
    } else {
        plan.coordinateX = coordinate.x();
        plan.coordinateY = coordinate.y();
        plan.coordinateZ = coordinate.z();
        plan.depthMm = static_cast<double>(m_layerCountSpin->value() * m_stepSpin->value());
    }

    if (m_segmentedTreatmentRadio->isChecked()) {
        plan.pattern = TreatmentPattern::Segmented;
    } else if (m_lineTreatmentRadio->isChecked()) {
        plan.pattern = TreatmentPattern::Line;
    } else {
        plan.pattern = TreatmentPattern::Point;
    }

    const int totalLayers = m_layerCountSpin->value();
    const int segmentCount = plan.pattern == TreatmentPattern::Segmented ? 2 : 1;
    const int layersPerSegment = std::max(1, totalLayers / segmentCount);

    int pointIndex = 0;
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        TherapySegment segment;
        segment.id = QStringLiteral("%1-S%2").arg(plan.id).arg(segmentIndex + 1);
        segment.orderIndex = segmentIndex;
        segment.label = QStringLiteral("\u6cbb\u7597\u6bb5 %1").arg(segmentIndex + 1);

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

void PlanningPage::editCurrentPlan()
{
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    TherapyPlan editablePlan = m_context->hasActivePlan() ? m_context->activePlan() : buildPlanFromUi(ApprovalState::Draft);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("\u7f16\u8f91\u6cbb\u7597\u65b9\u6848"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);

    auto* nameEdit = new QLineEdit(editablePlan.name);
    auto* approvalCombo = new QComboBox();
    approvalCombo->addItem(QStringLiteral("\u8349\u6848"), static_cast<int>(ApprovalState::Draft));
    approvalCombo->addItem(QStringLiteral("\u5f85\u5ba1\u6838"), static_cast<int>(ApprovalState::UnderReview));
    approvalCombo->addItem(QStringLiteral("\u5df2\u5ba1\u6838"), static_cast<int>(ApprovalState::Approved));
    approvalCombo->addItem(QStringLiteral("\u5df2\u9501\u5b9a"), static_cast<int>(ApprovalState::Locked));
    approvalCombo->setCurrentIndex(std::max(0, approvalCombo->findData(static_cast<int>(editablePlan.approvalState))));

    auto* xSpin = new QDoubleSpinBox();
    auto* ySpin = new QDoubleSpinBox();
    auto* zSpin = new QDoubleSpinBox();
    auto* depthSpin = new QDoubleSpinBox();
    const QList<QDoubleSpinBox*> coordinateSpins {xSpin, ySpin, zSpin, depthSpin};
    for (QDoubleSpinBox* spin : coordinateSpins) {
        spin->setRange(-9999.0, 9999.0);
        spin->setDecimals(2);
    }
    xSpin->setValue(editablePlan.coordinateX);
    ySpin->setValue(editablePlan.coordinateY);
    zSpin->setValue(editablePlan.coordinateZ);
    depthSpin->setValue(editablePlan.depthMm);

    form->addRow(QStringLiteral("\u65b9\u6848\u540d\u79f0"), nameEdit);
    form->addRow(QStringLiteral("\u5ba1\u6838\u72b6\u6001"), approvalCombo);
    form->addRow(QStringLiteral("X"), xSpin);
    form->addRow(QStringLiteral("Y"), ySpin);
    form->addRow(QStringLiteral("Z"), zSpin);
    form->addRow(QStringLiteral("\u6df1\u5ea6(mm)"), depthSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    editablePlan.name = nameEdit->text().trimmed();
    editablePlan.approvalState = static_cast<ApprovalState>(approvalCombo->currentData().toInt());
    editablePlan.coordinateX = xSpin->value();
    editablePlan.coordinateY = ySpin->value();
    editablePlan.coordinateZ = zSpin->value();
    editablePlan.depthMm = depthSpin->value();
    if (editablePlan.approvalState == ApprovalState::Approved || editablePlan.approvalState == ApprovalState::Locked) {
        editablePlan.approvedAt = QDateTime::currentDateTime();
    } else {
        editablePlan.approvedAt = QDateTime();
        editablePlan.approvedBy.clear();
    }

    m_context->setActivePlan(editablePlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(editablePlan.approvalState);
    }
    m_preview->setPlan(editablePlan);
    updateAssessmentText(&editablePlan);
    updatePlanPreviewText(&editablePlan);
}

void PlanningPage::saveCurrentPlan()
{
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    TherapyPlan planToSave = m_context->hasActivePlan()
        ? buildPlanFromUi(m_context->activePlan().approvalState)
        : buildPlanFromUi(ApprovalState::Draft);
    if (m_context->hasActivePlan()) {
        planToSave.id = m_context->activePlan().id;
        planToSave.name = m_context->activePlan().name;
        planToSave.coordinateX = m_context->activePlan().coordinateX;
        planToSave.coordinateY = m_context->activePlan().coordinateY;
        planToSave.coordinateZ = m_context->activePlan().coordinateZ;
        planToSave.depthMm = m_context->activePlan().depthMm;
        planToSave.createdAt = m_context->activePlan().createdAt;
        planToSave.approvedAt = m_context->activePlan().approvedAt;
        planToSave.approvedBy = m_context->activePlan().approvedBy;
    }

    if (!m_clinicalDataService.saveTherapyPlan(&planToSave)) {
        updateAcquisitionSummary(
            QStringLiteral("\u65b9\u6848\u4fdd\u5b58\u5931\u8d25"),
            {
                QStringLiteral("\u65b9\u6848\uff1a%1").arg(planToSave.name),
                QStringLiteral("\u9519\u8bef\uff1a%1").arg(m_clinicalDataService.lastError())
            });
        return;
    }

    m_context->setActivePlan(planToSave);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(planToSave.approvalState);
    }
    updateAssessmentText(&planToSave);
    updatePlanPreviewText(&planToSave);
    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u4fdd\u5b58\u6cbb\u7597\u65b9\u6848\uff1a%1").arg(planToSave.id));
    }
}

void PlanningPage::deleteCurrentPlan()
{
    if (!m_context->hasActivePlan()) {
        updateAcquisitionSummary(
            QStringLiteral("\u5220\u9664\u65b9\u6848"),
            {QStringLiteral("\u5f53\u524d\u6ca1\u6709\u53ef\u5220\u9664\u7684\u6d3b\u52a8\u65b9\u6848\u3002")});
        return;
    }

    const QString planId = m_context->activePlan().id;
    if (!m_clinicalDataService.deleteTherapyPlan(planId)) {
        updateAcquisitionSummary(
            QStringLiteral("\u5220\u9664\u65b9\u6848\u5931\u8d25"),
            {
                QStringLiteral("\u65b9\u6848\u7f16\u53f7\uff1a%1").arg(planId),
                QStringLiteral("\u9519\u8bef\uff1a%1").arg(m_clinicalDataService.lastError())
            });
        return;
    }

    m_context->clearActivePlan();
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
    }
    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u5220\u9664\u6cbb\u7597\u65b9\u6848\uff1a%1").arg(planId));
    }
}

void PlanningPage::toggleAnnotationPanel()
{
    if (m_annotationPanel == nullptr) {
        return;
    }

    const bool expanded = !m_annotationPanel->isVisible();
    m_annotationPanel->setVisible(expanded);
    if (m_annotationButton != nullptr) {
        m_annotationButton->setChecked(expanded);
    }
    if (m_preview != nullptr) {
        m_preview->setAnnotationEnabled(expanded);
    }
}

void PlanningPage::onStagedSliceSelectionChanged(int row)
{
    persistCurrentSliceAnnotations();
    loadStagedSlice(row);
}

void PlanningPage::onPreviewAnnotationsChanged()
{
    persistCurrentSliceAnnotations();
}

void PlanningPage::persistCurrentSliceAnnotations()
{
    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size() || m_preview == nullptr) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    slice.annotations = m_preview->annotationStrokes();
    slice.edited = !slice.annotations.isEmpty();
}

void PlanningPage::loadStagedSlice(int row)
{
    m_currentStagedSliceIndex = row;
    if (row < 0 || row >= m_stagedSlices.size()) {
        if (m_preview != nullptr) {
            m_preview->setAnnotationStrokes({});
            m_preview->setCaption(QStringLiteral(""));
        }
        return;
    }

    const StagedSliceState& slice = m_stagedSlices.at(row);
    if (m_preview != nullptr) {
        m_preview->setAnnotationStrokes(slice.annotations);
        m_preview->setCaption(QStringLiteral("\u6682\u5b58\u5207\u7247 %1/%2").arg(row + 1).arg(m_stagedSlices.size()));
    }

    const QStringList lines {
        QStringLiteral("\u5f53\u524d\u5207\u7247\uff1a%1").arg(slice.label),
        QStringLiteral("\u6682\u5b58\u8def\u5f84\uff1a%1").arg(slice.image.storagePath),
        QStringLiteral("\u7f16\u8f91\u72b6\u6001\uff1a%1").arg(slice.edited ? QStringLiteral("\u5df2\u5708\u753b") : QStringLiteral("\u672a\u5708\u753b")),
        QStringLiteral("\u5f53\u524d\u7b14\u8ff9\u6570\uff1a%1").arg(slice.annotations.size())
    };
    updateAcquisitionSummary(QStringLiteral("\u5207\u7247\u7f16\u8f91"), lines);
}

void PlanningPage::populatePatientSelector()
{
    m_patientCombo->clear();
    if (m_clinicalDataRepository == nullptr) {
        const PatientRecord fallbackPatient = buildFallbackPatient();
        m_patientCombo->addItem(patientDisplayLabel(fallbackPatient), fallbackPatient.id);
        return;
    }

    const QVector<PatientRecord> patients = m_clinicalDataRepository->listPatients();
    for (const PatientRecord& patient : patients) {
        m_patientCombo->addItem(patientDisplayLabel(patient), patient.id);
    }

    if (m_patientCombo->count() == 0) {
        const PatientRecord fallbackPatient = buildFallbackPatient();
        m_patientCombo->addItem(patientDisplayLabel(fallbackPatient), fallbackPatient.id);
    }
}

void PlanningPage::refreshImagingPaths(const QString& patientId)
{
    Q_UNUSED(patientId);
    populateDefaultScanChannels();
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

void PlanningPage::updateAssessmentText(const TherapyPlan* plan)
{
    if (plan == nullptr) {
        m_assessmentPreview->setPlainText(
            QStringLiteral("\u5c1a\u672a\u751f\u6210\u6cbb\u7597\u65b9\u6848\u8bc4\u4f30\u3002\n\n\u8fd9\u91cc\u73b0\u5728\u4e5f\u4f1a\u663e\u793a\u91c7\u96c6\u6682\u5b58\u548c\u56fe\u50cf\u52a0\u8f7d\u72b6\u6001\u3002"));
        return;
    }

    int pointCount = 0;
    for (const TherapySegment& segment : plan->segments) {
        pointCount += segment.points.size();
    }

    m_assessmentPreview->setPlainText(
        QStringLiteral("\u65b9\u6848\u8bc4\u4f30\n\n\u60a3\u8005\uff1a%1\n\u65b9\u6848\u72b6\u6001\uff1a%2\n\u6a21\u5f0f\uff1a%3\n\u6cbb\u7597\u70b9\u6570\uff1a%4\n\u547c\u5438\u8ddf\u968f\uff1a%5\n\n\u540e\u7eed\u4f1a\u628a\u57fa\u4e8e\u5f71\u50cf\u7684\u6b63\u5f0f\u8bc4\u4f30\u5c55\u793a\u5728\u8fd9\u91cc\u3002")
            .arg(m_context->hasSelectedPatient() ? m_context->selectedPatient().name : QStringLiteral("\u672a\u9009\u62e9"))
            .arg(toDisplayString(plan->approvalState))
            .arg(toDisplayString(plan->pattern))
            .arg(pointCount)
            .arg(plan->respiratoryTrackingEnabled ? QStringLiteral("\u5f00\u542f") : QStringLiteral("\u5173\u95ed")));
}

void PlanningPage::updatePlanPreviewText(const TherapyPlan* plan)
{
    if (plan == nullptr) {
        m_planPreview->setPlainText(QStringLiteral("\u6682\u65e0\u65b9\u6848\u9884\u89c8\u3002"));
        return;
    }

    int pointCount = 0;
    double durationSeconds = 0.0;
    for (const TherapySegment& segment : plan->segments) {
        pointCount += segment.points.size();
        durationSeconds += segment.plannedDurationSeconds;
    }

    const QString deliveryText = plan->deliveryMode.isEmpty()
        ? (m_directTreatmentRadio->isChecked() ? QStringLiteral("\u76f4\u63a5\u6cbb\u7597") : QStringLiteral("\u5206\u6bb5\u6267\u884c"))
        : plan->deliveryMode;
    QString previewText =
        QStringLiteral("\u65b9\u6848\u540d\u79f0\uff1a%1\n\u6cbb\u7597\u5750\u6807\uff1aX %2   Y %3   Z %4   \u6df1\u5ea6 %5 mm\n\u6cbb\u7597\u65b9\u5f0f\uff1a%6\n\u6cbb\u7597\u529f\u7387\uff1a%7 W\n\u6cbb\u7597\u6a21\u5f0f\uff1a%8\n\u6cbb\u7597\u884c\u8ddd\uff1a%9 mm\n\u70b9\u7597\u65f6\u957f\uff1a%10 s\n\u9884\u4f30\u8017\u65f6\uff1a%11 min\n\u5ba1\u6838\u72b6\u6001\uff1a%12\n\u9756\u6001\u70b9\u6570\uff1a%13")
            .arg(plan->name)
            .arg(plan->coordinateX, 0, 'f', 2)
            .arg(plan->coordinateY, 0, 'f', 2)
            .arg(plan->coordinateZ, 0, 'f', 2)
            .arg(plan->depthMm, 0, 'f', 2)
            .arg(deliveryText)
            .arg(plan->plannedPowerWatts, 0, 'f', 0)
            .arg(toDisplayString(plan->pattern))
            .arg(plan->spacingMm, 0, 'f', 1)
            .arg(plan->dwellSeconds, 0, 'f', 1)
            .arg(durationSeconds / 60.0, 0, 'f', 2)
            .arg(toDisplayString(plan->approvalState))
            .arg(pointCount);
    if (m_context->hasSelectedPatient()) {
        previewText.prepend(
            QStringLiteral("\u60a3\u8005\uff1a%1\n\u7f16\u53f7\uff1a%2\n\n")
                .arg(m_context->selectedPatient().name)
                .arg(m_context->selectedPatient().id));
    }
    m_planPreview->setPlainText(previewText);
}

void PlanningPage::applyPlanToUi(const TherapyPlan& plan)
{
    const QSignalBlocker spacingBlocker(m_spacingSpin);
    const QSignalBlocker dwellBlocker(m_dwellSpin);
    const QSignalBlocker powerBlocker(m_powerSlider);
    const QSignalBlocker respiratoryBlocker(m_respiratoryTrackingCheck);
    const QSignalBlocker directBlocker(m_directTreatmentRadio);
    const QSignalBlocker segmentedBlocker(m_segmentedTreatmentRadio);
    const QSignalBlocker pointBlocker(m_pointTreatmentRadio);
    const QSignalBlocker lineBlocker(m_lineTreatmentRadio);

    if (plan.spacingMm > 0.0) {
        m_spacingSpin->setValue(plan.spacingMm);
    }
    if (plan.dwellSeconds > 0.0) {
        m_dwellSpin->setValue(plan.dwellSeconds);
    }
    if (plan.plannedPowerWatts > 0.0) {
        m_powerSlider->setValue(static_cast<int>(plan.plannedPowerWatts));
    }
    m_respiratoryTrackingCheck->setChecked(plan.respiratoryTrackingEnabled);
    m_directTreatmentRadio->setChecked(plan.deliveryMode != QStringLiteral("\u5206\u6bb5\u6267\u884c"));
    m_segmentedTreatmentRadio->setChecked(plan.deliveryMode == QStringLiteral("\u5206\u6bb5\u6267\u884c"));
    m_pointTreatmentRadio->setChecked(plan.pattern == TreatmentPattern::Point);
    m_lineTreatmentRadio->setChecked(plan.pattern == TreatmentPattern::Line || plan.pattern == TreatmentPattern::Segmented);
    refreshDerivedMetrics();
}

void PlanningPage::populateDefaultScanChannels()
{
    if (m_pathList->count() > 0) {
        if (m_pathList->currentRow() < 0) {
            m_pathList->setCurrentRow(0);
        }
        return;
    }

    for (int index = 0; index < 4; ++index) {
        m_pathList->addItem(defaultChannelText(index));
    }
    m_pathList->setCurrentRow(0);
}

QString PlanningPage::currentChannelLabel() const
{
    const QListWidgetItem* item = m_pathList->currentItem();
    return item == nullptr ? QStringLiteral("\u672a\u9009\u62e9\u901a\u9053") : extractChannelLabel(item->text());
}

QString PlanningPage::currentChannelCoordinate() const
{
    const QListWidgetItem* item = m_pathList->currentItem();
    return item == nullptr ? QStringLiteral("\u672a\u8bbe\u7f6e") : extractChannelCoordinate(item->text());
}

void PlanningPage::updateAcquisitionSummary(const QString& title, const QStringList& lines)
{
    QString text = title;
    text.append(QStringLiteral("\n\n"));
    text.append(lines.join(QLatin1Char('\n')));
    m_assessmentPreview->setPlainText(text);
}

void PlanningPage::addPathItem()
{
    const int nextIndex = m_pathList->count();
    m_pathList->addItem(defaultChannelText(nextIndex));
    m_pathList->setCurrentRow(m_pathList->count() - 1);
}

void PlanningPage::removeCurrentPathItem()
{
    delete m_pathList->takeItem(m_pathList->currentRow());
    if (m_pathList->count() == 0) {
        populateDefaultScanChannels();
    } else if (m_pathList->currentRow() < 0) {
        m_pathList->setCurrentRow(0);
    }
}

void PlanningPage::simulateImageAcquisition()
{
    populateDefaultScanChannels();

    const int layerCount = m_layerCountSpin->value();
    const int step = m_stepSpin->value();
    const int channelIndex = std::max(0, m_pathList->currentRow());
    const QString channelLabel = currentChannelLabel();
    const QString channelCoordinate = currentChannelCoordinate();
    const QDateTime now = QDateTime::currentDateTime();
    const QString batchToken = now.toString(QStringLiteral("yyyyMMddhhmmss"));

    m_stagedImageSeries.clear();
    m_stagedSlices.clear();
    m_stagedImageSeries.reserve(layerCount);
    m_stagedSlices.reserve(layerCount);
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        ImageSeriesRecord stagedSlice;
        stagedSlice.patientId = m_context->hasSelectedPatient() ? m_context->selectedPatient().id : QString();
        stagedSlice.type = QStringLiteral("\u8d85\u58f0\u626b\u63cf\u5207\u7247");
        stagedSlice.storagePath = QStringLiteral("staging/%1/channel_%2_slice_%3.png")
            .arg(batchToken)
            .arg(channelIndex + 1, 2, 10, QChar('0'))
            .arg(layerIndex + 1, 3, 10, QChar('0'));
        stagedSlice.acquisitionDate = now.date();
        stagedSlice.notes = QStringLiteral("staged capture | channel: %1 | origin: %2 | slice: %3/%4 | step: %5")
            .arg(channelLabel)
            .arg(channelCoordinate)
            .arg(layerIndex + 1)
            .arg(layerCount)
            .arg(step);
        stagedSlice.createdAt = now;
        m_stagedImageSeries.push_back(stagedSlice);

        StagedSliceState stagedState;
        stagedState.image = stagedSlice;
        stagedState.label = QStringLiteral("[S%1] \u6682\u5b58\u5207\u7247-%2")
            .arg(layerIndex + 1, 2, 10, QChar('0'))
            .arg(layerIndex + 1, 2, 10, QChar('0'));
        m_stagedSlices.push_back(stagedState);
    }

    {
        const QSignalBlocker blocker(m_modelList);
        m_modelList->clear();
        for (const StagedSliceState& slice : std::as_const(m_stagedSlices)) {
            m_modelList->addItem(slice.label);
        }
    }
    if (!m_stagedSlices.isEmpty()) {
        m_modelList->setCurrentRow(0);
        loadStagedSlice(0);
    } else if (m_preview != nullptr) {
        m_preview->setAnnotationStrokes({});
        m_preview->setCaption(QStringLiteral(""));
    }

    m_lastAcquisitionAt = now;
    m_previewOverlayLabel->setText(QStringLiteral("\u5df2\u6682\u5b58 %1 \u5f20\u91c7\u96c6\u56fe\u50cf").arg(layerCount));
    m_previewOverlayLabel->setVisible(true);

    updateAcquisitionSummary(
        QStringLiteral("\u56fe\u50cf\u91c7\u96c6\u5df2\u5b8c\u6210"),
        {
            QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(channelLabel),
            QStringLiteral("\u8d77\u59cb\u5750\u6807\uff1a%1").arg(channelCoordinate),
            QStringLiteral("\u5c42\u6570\uff1a%1").arg(layerCount),
            QStringLiteral("\u6b65\u957f\uff1a%1").arg(step),
            QStringLiteral("\u6682\u5b58\u56fe\u50cf\uff1a%1 \u5f20").arg(m_stagedImageSeries.size()),
            QStringLiteral("\u5f53\u524d\u4ec5\u5b8c\u6210\u6682\u5b58\uff0c\u8fd8\u6ca1\u6709\u5199\u5165\u8be5\u60a3\u8005\u7684\u5f71\u50cf\u6570\u636e\u3002"),
            QStringLiteral("\u70b9\u51fb\u53f3\u4e0b\u89d2\u201c\u672c\u5730\u5b58\u50a8\u201d\u540e\uff0c\u624d\u4f1a\u628a\u8fd9\u6279\u56fe\u50cf\u4fdd\u5b58\u5230\u5f71\u50cf\u6570\u636e\u4e2d\u3002")
        });

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("operator"),
            QStringLiteral("planning"),
            QStringLiteral("\u5b8c\u6210\u56fe\u50cf\u91c7\u96c6\u6682\u5b58\uff1a%1\uff0c\u5c42\u6570 %2\uff0c\u6b65\u957f %3").arg(channelLabel).arg(layerCount).arg(step));
    }
}

void PlanningPage::generateThreeDimensionalImage()
{
    const int nextIndex = m_modelList->count() + 1;
    m_modelList->addItem(QStringLiteral("[B%1] \u56fe\u50cf-%2").arg(nextIndex).arg(nextIndex, 2, 10, QChar('0')));
    m_modelList->setCurrentRow(m_modelList->count() - 1);
}

void PlanningPage::previewCurrentPlan()
{
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    const ApprovalState approvalState = m_context->hasActivePlan() ? m_context->activePlan().approvalState : ApprovalState::Draft;
    TherapyPlan previewPlan = buildPlanFromUi(approvalState);
    if (m_context->hasActivePlan()) {
        previewPlan.id = m_context->activePlan().id;
        previewPlan.name = m_context->activePlan().name;
        previewPlan.coordinateX = m_context->activePlan().coordinateX;
        previewPlan.coordinateY = m_context->activePlan().coordinateY;
        previewPlan.coordinateZ = m_context->activePlan().coordinateZ;
        previewPlan.depthMm = m_context->activePlan().depthMm;
        previewPlan.createdAt = m_context->activePlan().createdAt;
        previewPlan.approvedAt = m_context->activePlan().approvedAt;
        previewPlan.approvedBy = m_context->activePlan().approvedBy;
    }

    m_context->setActivePlan(previewPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(previewPlan.approvalState);
    }
    m_preview->setPlan(previewPlan);
    m_preview->setCompletedPointCount(0);
    updateAssessmentText(&previewPlan);
    updatePlanPreviewText(&previewPlan);
    m_previewOverlayLabel->setVisible(false);
}

void PlanningPage::refreshDerivedMetrics()
{
    const int segmentCount = m_segmentedTreatmentRadio->isChecked() ? 2 : 1;
    const int pointCount = m_layerCountSpin->value() * 4 * segmentCount;
    const double totalMinutes = (pointCount * m_dwellSpin->value()) / 60.0;

    m_totalDurationValueLabel->setText(QStringLiteral("%1 min").arg(totalMinutes, 0, 'f', 2));
    m_powerValueLabel->setText(QStringLiteral("%1W").arg(m_powerSlider->value()));
    m_chartSummaryLabel->setText(QStringLiteral("\u5e73\u5747\u529f\u7387: %1W").arg(m_powerSlider->value()));
}

void PlanningPage::storeCapturedImages()
{
    persistCurrentSliceAnnotations();

    if (m_stagedImageSeries.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u672c\u5730\u5b58\u50a8"),
            {
                QStringLiteral("\u5f53\u524d\u6ca1\u6709\u6682\u5b58\u56fe\u50cf\u53ef\u4fdd\u5b58\u3002"),
                QStringLiteral("\u8bf7\u5148\u5728\u5de6\u4fa7\u201c\u56fe\u50cf\u91c7\u96c6\u201d\u91cc\u5b8c\u6210\u4e00\u6b21\u91c7\u96c6\u3002")
            });
        return;
    }

    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    if (!m_context->hasSelectedPatient()) {
        updateAcquisitionSummary(QStringLiteral("\u672c\u5730\u5b58\u50a8\u5931\u8d25"), {QStringLiteral("\u672a\u9009\u62e9\u60a3\u8005\uff0c\u65e0\u6cd5\u7ee7\u7eed\u3002")});
        return;
    }

    const PatientRecord& patient = m_context->selectedPatient();
    const QString batchToken = m_lastAcquisitionAt.isValid()
        ? m_lastAcquisitionAt.toString(QStringLiteral("yyyyMMddhhmmss"))
        : QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss"));
    const int channelIndex = std::max(0, m_pathList->currentRow());

    int savedCount = 0;
    int editedCount = 0;
    for (int index = 0; index < m_stagedImageSeries.size(); ++index) {
        ImageSeriesRecord record = m_stagedImageSeries.at(index);
        record.patientId = patient.id;
        record.type = QStringLiteral("\u8d85\u58f0\u626b\u63cf\u56fe\u50cf");
        record.storagePath = buildStoragePath(patient.id, batchToken, channelIndex, index);
        const int strokeCount = index < m_stagedSlices.size() ? m_stagedSlices.at(index).annotations.size() : 0;
        if (strokeCount > 0) {
            ++editedCount;
        }
        record.notes = QStringLiteral("saved from planning page | channel: %1 | origin: %2 | slice: %3/%4 | step: %5")
            .arg(currentChannelLabel())
            .arg(currentChannelCoordinate())
            .arg(index + 1)
            .arg(m_stagedImageSeries.size())
            .arg(m_stepSpin->value())
            + QStringLiteral(" | staged_annotation_count: %1").arg(strokeCount);

        if (!m_clinicalDataService.saveImageSeries(&record)) {
            updateAcquisitionSummary(
                QStringLiteral("\u672c\u5730\u5b58\u50a8\u5931\u8d25"),
                {
                    QStringLiteral("\u60a3\u8005\uff1a%1").arg(patient.name),
                    QStringLiteral("\u5df2\u5199\u5165\uff1a%1 / %2").arg(savedCount).arg(m_stagedImageSeries.size()),
                    QStringLiteral("\u9519\u8bef\uff1a%1").arg(m_clinicalDataService.lastError())
                });
            return;
        }
        ++savedCount;
    }

    m_previewOverlayLabel->setText(QStringLiteral("\u5df2\u5199\u5165 %1 \u5f20\u60a3\u8005\u5f71\u50cf").arg(savedCount));
    m_previewOverlayLabel->setVisible(true);

    updateAcquisitionSummary(
        QStringLiteral("\u672c\u5730\u5b58\u50a8\u5b8c\u6210"),
        {
            QStringLiteral("\u60a3\u8005\uff1a%1").arg(patient.name),
            QStringLiteral("\u5199\u5165\u5f71\u50cf\u6570\u636e\uff1a%1 \u5f20").arg(savedCount),
            QStringLiteral("\u5df2\u6682\u5b58\u5708\u753b\u5207\u7247\uff1a%1 \u5f20").arg(editedCount),
            QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel()),
            QStringLiteral("\u8d77\u59cb\u5750\u6807\uff1a%1").arg(currentChannelCoordinate()),
            QStringLiteral("\u8fd9\u6279\u56fe\u50cf\u5df2\u7ecf\u4ece\u6682\u5b58\u533a\u5199\u5165\u8be5\u60a3\u8005\u7684\u5f71\u50cf\u6570\u636e\u3002")
        });
}

void PlanningPage::loadStoredImages()
{
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    if (!m_context->hasSelectedPatient()) {
        updateAcquisitionSummary(QStringLiteral("\u8bfb\u53d6\u56fe\u50cf\u5931\u8d25"), {QStringLiteral("\u672a\u9009\u62e9\u60a3\u8005\uff0c\u65e0\u6cd5\u7ee7\u7eed\u3002")});
        return;
    }

    const PatientRecord& patient = m_context->selectedPatient();
    const QVector<ImageSeriesRecord> imageSeries = m_clinicalDataService.listImageSeriesForPatient(patient.id);
    if (imageSeries.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u8bfb\u53d6\u56fe\u50cf"),
            {
                QStringLiteral("\u60a3\u8005\uff1a%1").arg(patient.name),
                QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u5df2\u5b58\u50a8\u7684\u5f71\u50cf\u6570\u636e\u3002")
            });
        return;
    }

    m_previewOverlayLabel->setText(QStringLiteral("\u5df2\u8bfb\u53d6 %1 \u5f20\u5df2\u5b58\u56fe\u50cf").arg(imageSeries.size()));
    m_previewOverlayLabel->setVisible(true);

    const ImageSeriesRecord& latest = imageSeries.constLast();
    updateAcquisitionSummary(
        QStringLiteral("\u8bfb\u53d6\u56fe\u50cf\u5b8c\u6210"),
        {
            QStringLiteral("\u60a3\u8005\uff1a%1").arg(patient.name),
            QStringLiteral("\u5df2\u8bfb\u53d6\u5f71\u50cf\uff1a%1 \u5f20").arg(imageSeries.size()),
            QStringLiteral("\u6700\u65b0\u4e00\u5f20\u8def\u5f84\uff1a%1").arg(latest.storagePath),
            QStringLiteral("\u540e\u7eed\u4f1a\u628a\u8fd9\u4e9b\u5df2\u5b58\u50a8\u56fe\u50cf\u63a5\u5230\u4e09\u7ef4\u56fe\u50cf\u751f\u6210\u548c\u9884\u89c8\u94fe\u8def\u91cc\u3002")
        });
}

}  // namespace panthera::modules
