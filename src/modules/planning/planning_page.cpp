#include "modules/planning/planning_page.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTime>
#include <QTimer>
#include <QVector3D>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr int kPathStateKeyRole = Qt::UserRole + 1;

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

QString previewPathForStorage(const QString& storagePath)
{
    if (storagePath.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo storageInfo(storagePath);
    if (!storageInfo.exists()) {
        return {};
    }
    if (storageInfo.isFile()) {
        return storageInfo.absoluteFilePath();
    }
    if (!storageInfo.isDir()) {
        return {};
    }

    QDirIterator iterator(
        storageInfo.absoluteFilePath(),
        {QStringLiteral("*.png"),
         QStringLiteral("*.jpg"),
         QStringLiteral("*.jpeg"),
         QStringLiteral("*.bmp")},
        QDir::Files,
        QDirIterator::Subdirectories);
    return iterator.hasNext() ? iterator.next() : QString();
}

QPixmap loadHistoryPixmap(const QString& storagePath)
{
    const QString previewPath = previewPathForStorage(storagePath);
    if (previewPath.isEmpty()) {
        return {};
    }

    QImageReader reader(previewPath);
    reader.setAutoTransform(true);
    return QPixmap::fromImage(reader.read());
}

QString formatStepSize(int stepMm)
{
    return QStringLiteral("%1 mm").arg(stepMm);
}

QString targetSummaryText(TreatmentPattern pattern, const QVector<TherapyPoint>& points)
{
    if (pattern == TreatmentPattern::Line) {
        return QStringLiteral("治疗线 %1 条 | 采样点 %2 个")
            .arg(therapyLineGroupCount(points))
            .arg(points.size());
    }
    return QStringLiteral("靶点 %1 个").arg(points.size());
}

double totalDwellSeconds(const QVector<TherapyPoint>& points)
{
    double durationSeconds = 0.0;
    for (int index = 0; index < points.size(); ++index) {
        durationSeconds += points.at(index).dwellSeconds;
    }
    return durationSeconds;
}

bool annotationStrokesEqual(const QVector<AnnotationStroke>& left, const QVector<AnnotationStroke>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (int index = 0; index < left.size(); ++index) {
        if (left.at(index).color != right.at(index).color
            || left.at(index).normalizedPoints != right.at(index).normalizedPoints) {
            return false;
        }
    }
    return true;
}

QString patternSummaryText(TreatmentPattern pattern)
{
    switch (pattern) {
    case TreatmentPattern::Line:
        return QStringLiteral("\u7ebf\u6cbb\u7597");
    case TreatmentPattern::Segmented:
        return QStringLiteral("\u5206\u6bb5\u6267\u884c");
    case TreatmentPattern::Point:
    default:
        return QStringLiteral("\u70b9\u6cbb\u7597");
    }
}

}  // namespace

PlanningPage::PlanningPage(
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
    m_stepSpin->setSuffix(QStringLiteral(" mm"));

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
    previewFrameLayout->setSpacing(4);

    auto* compareLayout = new QHBoxLayout();
    compareLayout->setContentsMargins(0, 0, 0, 0);
    compareLayout->setSpacing(4);

    auto* historyPane = new QFrame();
    historyPane->setObjectName(QStringLiteral("planningComparePane"));
    auto* historyLayout = new QVBoxLayout(historyPane);
    historyLayout->setContentsMargins(4, 4, 4, 4);
    historyLayout->setSpacing(4);

    auto* historyTitle = new QLabel(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    historyTitle->setObjectName(QStringLiteral("planningPaneHeaderLabel"));
    historyLayout->addWidget(historyTitle);

    auto* historyStack = new QGridLayout();
    historyStack->setContentsMargins(0, 0, 0, 0);
    historyStack->setSpacing(0);
    m_historyPreview = new MockUltrasoundView();
    m_historyPreview->setObjectName(QStringLiteral("planningPreviewWidget"));
    m_historyPreview->setMinimumSize(0, 0);
    m_historyPreview->setCaption(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    m_historyPreview->setAnnotationEnabled(false);

    m_historyPreviewOverlayLabel = new QLabel(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    m_historyPreviewOverlayLabel->setObjectName(QStringLiteral("planningPreviewOverlayLabel"));
    m_historyPreviewOverlayLabel->setAlignment(Qt::AlignCenter);
    m_historyPreviewOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    historyStack->addWidget(m_historyPreview, 0, 0);
    historyStack->addWidget(m_historyPreviewOverlayLabel, 0, 0);
    historyLayout->addLayout(historyStack, 1);

    m_historySliceSummaryLabel = new QLabel(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf\uff1a\u6682\u65e0\u6570\u636e"));
    m_historySliceSummaryLabel->setObjectName(QStringLiteral("planningSliceInfoLabel"));
    m_historySliceSummaryLabel->setWordWrap(true);
    m_historySliceSlider = new QSlider(Qt::Horizontal);
    m_historySliceSlider->setObjectName(QStringLiteral("planningSliceSlider"));
    m_historySliceSlider->setRange(0, 0);
    m_historySliceSlider->setEnabled(false);
    historyLayout->addWidget(m_historySliceSummaryLabel);
    historyLayout->addWidget(m_historySliceSlider);

    auto* currentPane = new QFrame();
    currentPane->setObjectName(QStringLiteral("planningComparePane"));
    auto* currentLayout = new QVBoxLayout(currentPane);
    currentLayout->setContentsMargins(4, 4, 4, 4);
    currentLayout->setSpacing(4);

    auto* currentTitle = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    currentTitle->setObjectName(QStringLiteral("planningPaneHeaderLabel"));
    currentLayout->addWidget(currentTitle);

    auto* previewStack = new QGridLayout();
    previewStack->setContentsMargins(0, 0, 0, 0);
    previewStack->setSpacing(0);
    m_preview = new MockUltrasoundView();
    m_preview->setObjectName(QStringLiteral("planningPreviewWidget"));
    m_preview->setMinimumSize(0, 0);
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
    currentLayout->addLayout(previewStack, 1);

    m_currentSliceSummaryLabel = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a\u7b49\u5f85\u91c7\u96c6\u6216\u9884\u89c8\u65b9\u6848"));
    m_currentSliceSummaryLabel->setObjectName(QStringLiteral("planningSliceInfoLabel"));
    m_currentSliceSummaryLabel->setWordWrap(true);
    m_currentSliceSlider = new QSlider(Qt::Horizontal);
    m_currentSliceSlider->setObjectName(QStringLiteral("planningSliceSlider"));
    m_currentSliceSlider->setRange(0, 0);
    m_currentSliceSlider->setEnabled(false);
    currentLayout->addWidget(m_currentSliceSummaryLabel);
    currentLayout->addWidget(m_currentSliceSlider);

    compareLayout->addWidget(historyPane, 1);
    compareLayout->addWidget(currentPane, 1);
    auto* compareContent = new QWidget();
    compareContent->setLayout(compareLayout);

    auto* compareStack = new QGridLayout();
    compareStack->setContentsMargins(0, 0, 0, 0);
    compareStack->setSpacing(0);

    m_annotationButton = new QToolButton();
    m_annotationButton->setObjectName(QStringLiteral("planningIconButton"));
    m_annotationButton->setText(QStringLiteral("\u270e"));
    m_annotationButton->setCheckable(true);

    compareStack->addWidget(compareContent, 0, 0);
    compareStack->addWidget(m_annotationButton, 0, 0, Qt::AlignTop | Qt::AlignRight);
    previewFrameLayout->addLayout(compareStack, 1);
    centerColumn->addWidget(previewFrame, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(12);

    auto* chartCard = new QFrame();
    chartCard->setObjectName(QStringLiteral("planningBottomCard"));
    chartCard->setMinimumWidth(300);
    chartCard->setMinimumHeight(210);
    chartCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(18, 12, 18, 12);
    chartLayout->setSpacing(8);

    auto* chartHeader = new QVBoxLayout();
    chartHeader->setContentsMargins(0, 0, 0, 0);
    chartHeader->setSpacing(2);
    auto* chartTitle = new QLabel(QStringLiteral("\u80fd\u91cf\u8f93\u51fa\u66f2\u7ebf (J)"));
    chartTitle->setObjectName(QStringLiteral("planningBottomTitle"));
    chartTitle->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_chartSummaryLabel = new QLabel(QStringLiteral("\u5b9e\u65f6\u529f\u7387: --"));
    m_chartSummaryLabel->setObjectName(QStringLiteral("planningChartSummaryLabel"));
    m_chartSummaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* chartTitleRow = new QHBoxLayout();
    chartTitleRow->setContentsMargins(0, 0, 0, 0);
    chartTitleRow->addWidget(chartTitle);
    chartTitleRow->addStretch();

    auto* chartSummaryRow = new QHBoxLayout();
    chartSummaryRow->setContentsMargins(0, 0, 0, 0);
    chartSummaryRow->addStretch();
    chartSummaryRow->addWidget(m_chartSummaryLabel);

    chartHeader->addLayout(chartTitleRow);
    chartHeader->addLayout(chartSummaryRow);

    m_energyOutputChart = new EnergyOutputChartWidget();

    chartLayout->addLayout(chartHeader);
    chartLayout->addWidget(m_energyOutputChart, 1);
    bottomRow->addWidget(chartCard, 1);

    auto* statusCard = new QFrame();
    statusCard->setObjectName(QStringLiteral("planningBottomCard"));
    statusCard->setMinimumWidth(220);
    statusCard->setMinimumHeight(210);
    statusCard->setMaximumWidth(340);
    statusCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    auto* imageOpsCard = new QFrame();
    imageOpsCard->setObjectName(QStringLiteral("planningBottomCard"));
    imageOpsCard->setMinimumWidth(280);
    imageOpsCard->setMinimumHeight(210);
    imageOpsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    m_storeImageButton->setToolTip(QStringLiteral("\u5c06\u5f53\u524d\u8def\u5f84\u4e0a\u7684\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u5bfc\u51fa\u5230\u672c\u5730 PNG \u6587\u4ef6"));
    m_storeImageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_loadImageButton = new QPushButton(QStringLiteral("\u8bfb\u53d6\u56fe\u50cf"));
    m_loadImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_loadImageButton->setToolTip(QStringLiteral("\u4ece\u672c\u5730\u591a\u9009\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf\uff0c\u5e76\u5728\u5de6\u5c4f\u5bf9\u6bd4\u663e\u793a"));
    m_loadImageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    imageOpsButtons->addWidget(m_storeImageButton, 1);
    imageOpsButtons->addWidget(m_loadImageButton, 1);

    imageOpsLayout->addLayout(imageOpsHeader);
    imageOpsLayout->addStretch();
    imageOpsLayout->addLayout(imageOpsButtons);
    bottomRow->addWidget(imageOpsCard, 5);
    bottomRow->addWidget(statusCard, 4);
    bottomRow->setStretch(0, 6);
    bottomRow->setStretch(1, 5);
    bottomRow->setStretch(2, 4);

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

    auto* assessmentMetricsCard = new QFrame();
    assessmentMetricsCard->setObjectName(QStringLiteral("planningAssessmentMetricsCard"));
    auto* assessmentMetricsLayout = new QVBoxLayout(assessmentMetricsCard);
    assessmentMetricsLayout->setContentsMargins(10, 10, 10, 10);
    assessmentMetricsLayout->setSpacing(8);

    auto* plannedVolumeRow = new QHBoxLayout();
    auto* plannedVolumeLabel = new QLabel(QStringLiteral("\u9884\u5b9a\u4f53\u79ef"));
    plannedVolumeLabel->setObjectName(QStringLiteral("planningAssessmentMetricLabel"));
    m_estimatedVolumeValueLabel = new QLabel(QStringLiteral("0.00 cm\u00b3"));
    m_estimatedVolumeValueLabel->setObjectName(QStringLiteral("planningAssessmentMetricValueLabel"));
    plannedVolumeRow->addWidget(plannedVolumeLabel);
    plannedVolumeRow->addStretch();
    plannedVolumeRow->addWidget(m_estimatedVolumeValueLabel);

    auto* ablatedVolumeRow = new QHBoxLayout();
    auto* ablatedVolumeLabel = new QLabel(QStringLiteral("\u5df2\u6cbb\u7597\u4f53\u79ef"));
    ablatedVolumeLabel->setObjectName(QStringLiteral("planningAssessmentMetricLabel"));
    m_ablatedVolumeValueLabel = new QLabel(QStringLiteral("0.00 cm\u00b3"));
    m_ablatedVolumeValueLabel->setObjectName(QStringLiteral("planningAssessmentMetricAccentLabel"));
    ablatedVolumeRow->addWidget(ablatedVolumeLabel);
    ablatedVolumeRow->addStretch();
    ablatedVolumeRow->addWidget(m_ablatedVolumeValueLabel);

    auto* coverageRatioRow = new QHBoxLayout();
    auto* coverageRatioLabel = new QLabel(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6"));
    coverageRatioLabel->setObjectName(QStringLiteral("planningAssessmentMetricLabel"));
    m_coverageRatioValueLabel = new QLabel(QStringLiteral("0%"));
    m_coverageRatioValueLabel->setObjectName(QStringLiteral("planningAssessmentMetricValueLabel"));
    coverageRatioRow->addWidget(coverageRatioLabel);
    coverageRatioRow->addStretch();
    coverageRatioRow->addWidget(m_coverageRatioValueLabel);

    m_coverageProgressBar = new QProgressBar();
    m_coverageProgressBar->setRange(0, 100);
    m_coverageProgressBar->setValue(0);
    m_coverageProgressBar->setTextVisible(false);

    m_assessmentPreview = new QPlainTextEdit();
    m_assessmentPreview->setObjectName(QStringLiteral("planningSummaryEdit"));
    m_assessmentPreview->setReadOnly(true);
    m_assessmentPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_assessmentPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_assessmentPreview->setMinimumHeight(0);
    m_assessmentPreview->setMaximumHeight(QWIDGETSIZE_MAX);

    assessmentMetricsLayout->addLayout(plannedVolumeRow);
    assessmentMetricsLayout->addLayout(ablatedVolumeRow);
    assessmentMetricsLayout->addLayout(coverageRatioRow);
    assessmentMetricsLayout->addWidget(m_coverageProgressBar);
    assessmentLayout->addWidget(assessmentTitle);
    assessmentLayout->addWidget(assessmentMetricsCard);
    statusLayout->addWidget(m_assessmentPreview, 1);

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
    m_addPlanButton->setToolTip(QStringLiteral("\u5c06\u5f53\u524d\u8def\u5f84\u4e0a\u5df2\u7f16\u8f91\u7684\u6240\u6709\u5207\u7247\u4fdd\u5b58\u4e3a\u4e00\u4e2a\u6cbb\u7597\u65b9\u6848"));
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
    m_planPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_planPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    connect(m_pathList, &QListWidget::currentRowChanged, this, &PlanningPage::onPathSelectionChanged);
    connect(m_generate3dButton, &QPushButton::clicked, this, &PlanningPage::generateThreeDimensionalImage);
    connect(m_storeImageButton, &QPushButton::clicked, this, &PlanningPage::storeCapturedImages);
    connect(m_loadImageButton, &QPushButton::clicked, this, &PlanningPage::loadStoredImages);
    connect(m_historySliceSlider, &QSlider::valueChanged, this, [this](int value) {
        loadHistoricalSlice(value, true);
    });
    connect(m_currentSliceSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_modelList != nullptr && value < m_modelList->count() && m_modelList->currentRow() != value) {
            m_modelList->setCurrentRow(value);
            return;
        }
        loadStagedSlice(value);
    });
    connect(m_annotationButton, &QToolButton::clicked, this, &PlanningPage::toggleAnnotationPanel);
    connect(m_annotationCollapseButton, &QToolButton::clicked, this, &PlanningPage::toggleAnnotationPanel);
    connect(m_modelList, &QListWidget::currentRowChanged, this, &PlanningPage::onStagedSliceSelectionChanged);
    connect(m_preview, &MockUltrasoundView::annotationStrokesChanged, this, &PlanningPage::onPreviewAnnotationsChanged);
    connect(m_generateTargetsButton, &QPushButton::clicked, this, &PlanningPage::generateTargetsForCurrentSlice);
    connect(m_generateAssessmentButton, &QPushButton::clicked, this, &PlanningPage::generateAssessmentForCurrentPlan);
    connect(m_previewPlanButton, &QPushButton::clicked, this, &PlanningPage::previewCurrentPlan);
    connect(m_addPlanButton, &QPushButton::clicked, this, &PlanningPage::saveCurrentPlan);
    connect(m_deletePlanButton, &QPushButton::clicked, this, &PlanningPage::deleteCurrentPlan);
    connect(m_editPlanButton, &QToolButton::clicked, this, &PlanningPage::editCurrentPlan);
    connect(m_respiratoryTrackingCheck, &QCheckBox::toggled, this, &PlanningPage::onRespiratoryTrackingToggled);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, [this](const PatientRecord&) {
        if (m_deferStartupContextSummary) {
            if (m_context->hasSelectedPatient()) {
                syncPatientSelector(m_context->selectedPatient().id);
            }
            return;
        }
        updateContextSummary();
    });
    connect(m_context, &ApplicationContext::activePlanChanged, this, [this](const TherapyPlan& plan) {
        if (m_deferStartupContextSummary) {
            return;
        }
        applyPlanToUi(plan);
        updateContextSummary();
    });
    connect(m_context, &ApplicationContext::activePlanCleared, this, [this]() {
        if (m_deferStartupContextSummary) {
            return;
        }
        updateContextSummary();
    });

    const auto refreshMetrics = [this]() {
        refreshDerivedMetrics();
    };
    connect(m_spacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [refreshMetrics](double) { refreshMetrics(); });
    connect(m_dwellSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [refreshMetrics](double) { refreshMetrics(); });
    connect(m_layerCountSpin, qOverload<int>(&QSpinBox::valueChanged), this, [refreshMetrics](int) { refreshMetrics(); });
    const auto onPatternModeToggled = [this, refreshMetrics](bool checked) {
        refreshMetrics();
        if (!checked || m_initializingUi) {
            return;
        }
        if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
            return;
        }

        const TreatmentPattern selectedPattern =
            m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
        const StagedSliceState& slice = m_stagedSlices.at(m_currentStagedSliceIndex);
        if (!slice.targetsGenerated || slice.pattern == selectedPattern) {
            return;
        }

        invalidateCurrentSliceTargets(
            QStringLiteral("\u6cbb\u7597\u6a21\u5f0f\u5207\u6362"),
            QStringLiteral("\u5df2\u5207\u6362\u4e3a%1\uff0c\u65e7\u9776\u70b9\u5df2\u5931\u6548\uff0c\u8bf7\u91cd\u65b0\u70b9\u51fb\u201c\u751f\u6210\u9776\u70b9\u201d\u3002")
                .arg(selectedPattern == TreatmentPattern::Line
                        ? QStringLiteral("\u7ebf\u6cbb\u7597")
                        : QStringLiteral("\u70b9\u6cbb\u7597")));
    };
    connect(m_pointTreatmentRadio, &QRadioButton::toggled, this, onPatternModeToggled);
    connect(m_lineTreatmentRadio, &QRadioButton::toggled, this, onPatternModeToggled);
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

    if (m_simulationDevice != nullptr) {
        m_latestDeviceSnapshot = m_simulationDevice->latestSnapshot();
        m_hasDeviceSnapshot = true;
        connect(m_simulationDevice, &adapters::SimulationDeviceFacade::snapshotUpdated, this, &PlanningPage::onDeviceSnapshotUpdated);
    }

    populatePatientSelector();
    refreshImagingPaths(QString());
    refreshDerivedMetrics();
    clearStartupDisplay();
    m_activePathStateKey = pathStateKeyForRow(m_pathList != nullptr ? m_pathList->currentRow() : -1);
    m_initializingUi = false;
    QTimer::singleShot(0, this, [this]() {
        m_deferStartupContextSummary = false;
    });
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
    activatePlanningWorkspace();
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    storeCurrentSliceControls();
    TherapyPlan draftPlan = hasGeneratedSliceTargets() ? buildPlanFromSlices(ApprovalState::Draft) : buildPlanFromUi(ApprovalState::Draft);
    m_context->setActivePlan(draftPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(draftPlan.approvalState);
    }
    refreshCurrentSliceVisualization();
    updateAssessmentText(&draftPlan);
    updatePlanPreviewText(&draftPlan);
    m_previewOverlayLabel->setVisible(false);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u751f\u6210\u65b9\u6848\u8349\u6848\uff1a%1").arg(draftPlan.id));
    }
}

void PlanningPage::generateTargetsForCurrentSlice()
{
    activatePlanningWorkspace();
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        updateAcquisitionSummary(
            QStringLiteral("\u751f\u6210\u9776\u70b9"),
            {
                QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u53ef\u7f16\u8f91\u7684\u91c7\u96c6\u5207\u7247\u3002"),
                QStringLiteral("\u8bf7\u5148\u5bf9\u67d0\u4e00\u6761\u8def\u5f84\u6267\u884c\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    if (slice.annotations.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u751f\u6210\u9776\u70b9"),
            {
                QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
                QStringLiteral("\u8bf7\u5148\u4f7f\u7528\u753b\u7b14\u5728\u53f3\u4fa7\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u4e0a\u5708\u753b\u8096\u7624\u533a\u57df\u3002")
            });
        return;
    }

    const QVector<QPointF> contourMm = extractContourFromAnnotations(slice.annotations);
    const QRectF bounds = contourBoundsMm(contourMm);
    if (!bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        updateAcquisitionSummary(
            QStringLiteral("\u751f\u6210\u9776\u70b9"),
            {
                QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
                QStringLiteral("\u672a\u80fd\u4ece\u753b\u7b14\u8f68\u8ff9\u91cc\u8ba1\u7b97\u51fa\u6709\u6548\u7684\u8096\u7624\u533a\u57df\u3002")
            });
        return;
    }

    slice.targets.clear();
    slice.pattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
    slice.spacingMm = m_spacingSpin->value();
    slice.dwellSeconds = m_dwellSpin->value();
    slice.powerWatts = m_powerSlider->value();
    slice.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
    slice.deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
    slice.targets = generateTherapyTargetsWithinContour(
        contourMm,
        slice.pattern,
        slice.spacingMm,
        slice.dwellSeconds,
        slice.powerWatts);

    slice.annotatedAreaMm2 = contourAreaMm2(contourMm);
    slice.estimatedVolumeCm3 = (slice.annotatedAreaMm2 * std::max(1, m_stepSpin->value())) / 1000.0;
    const double ablationFactor = slice.pattern == TreatmentPattern::Line ? 0.82 : 0.62;
    slice.ablatedVolumeCm3 = std::min(
        slice.estimatedVolumeCm3,
        (slice.targets.size() * slice.spacingMm * std::max(1, m_stepSpin->value()) * slice.spacingMm * ablationFactor) / 1000.0);
    slice.edited = true;
    slice.targetsGenerated = true;
    recalculateRespiratoryTrackingForSlice(m_currentStagedSliceIndex);

    refreshCurrentSliceVisualization();
    updateSliceAssessmentMetrics();

    TherapyPlan draftPlan = buildPlanFromSlices(ApprovalState::Draft);
    m_context->setActivePlan(draftPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(draftPlan.approvalState);
    }
    updatePlanPreviewText(&draftPlan);

    updateAcquisitionSummary(
        QStringLiteral("\u9776\u70b9\u751f\u6210\u5b8c\u6210"),
        {
            QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
            QStringLiteral("\u6cbb\u7597\u65b9\u5f0f\uff1a%1 | %2").arg(slice.deliveryMode, patternSummaryText(slice.pattern)),
            QStringLiteral("\u5df2\u751f\u6210\u9776\u70b9\uff1a%1 \u4e2a").arg(slice.targets.size()),
            QStringLiteral("\u5f53\u524d\u529f\u7387\uff1a%1 W").arg(slice.powerWatts, 0, 'f', 0),
            QStringLiteral("\u547c\u5438\u8ddf\u968f\uff1a%1").arg(slice.respiratoryTrackingEnabled ? QStringLiteral("\u5f00\u542f") : QStringLiteral("\u5173\u95ed")),
            slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated
                ? QStringLiteral("\u547c\u5438\u8865\u507f\uff1adX %1 mm | dY %2 mm | \u5b9e\u65f6\u9776\u70b9 %3 \u4e2a")
                      .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 2)
                      .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 2)
                      .arg(slice.respiratoryAdjustedTargets.size())
                : QStringLiteral("\u547c\u5438\u8865\u507f\uff1a%1").arg(
                    slice.respiratoryTrackingEnabled
                        ? QStringLiteral("\u7b49\u5f85\u547c\u5438\u6807\u5b9a")
                        : QStringLiteral("\u672a\u5f00\u542f")),
            QStringLiteral("\u9884\u4f30\u4f53\u79ef\uff1a%1 cm\u00b3 | \u5df2\u6cbb\u7597\u4f53\u79ef\uff1a%2 cm\u00b3")
                .arg(slice.estimatedVolumeCm3, 0, 'f', 2)
                .arg(slice.ablatedVolumeCm3, 0, 'f', 2)
        });
}

void PlanningPage::generateAssessmentForCurrentPlan()
{
    activatePlanningWorkspace();
    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (!hasGeneratedSliceTargets()) {
        updateAssessmentMetricsPanel(0.0, 0.0);
        updateAcquisitionSummary(
            QStringLiteral("\u65b9\u6848\u8bc4\u4f30"),
            {
                QStringLiteral("\u8bf7\u5148\u5bf9\u6bcf\u5f20\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u5708\u753b\u8096\u7624\u533a\u57df\uff0c\u5e76\u70b9\u51fb\u201c\u751f\u6210\u9776\u70b9\u201d\u3002")
            });
        return;
    }

    double estimatedVolumeCm3 = 0.0;
    double ablatedVolumeCm3 = 0.0;
    int generatedSliceCount = 0;
    int totalTargetCount = 0;
    int respiratoryTrackedSliceCount = 0;
    double maxRespiratoryCompensationMm = 0.0;
    QStringList sliceLines;
    for (const StagedSliceState& slice : m_stagedSlices) {
        if (!slice.targetsGenerated || slice.targets.isEmpty()) {
            continue;
        }

        ++generatedSliceCount;
        totalTargetCount += slice.targets.size();
        estimatedVolumeCm3 += slice.estimatedVolumeCm3;
        ablatedVolumeCm3 += slice.ablatedVolumeCm3;
        if (slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated) {
            ++respiratoryTrackedSliceCount;
            maxRespiratoryCompensationMm = std::max(
                maxRespiratoryCompensationMm,
                std::hypot(slice.respiratoryOffsetMm.x(), slice.respiratoryOffsetMm.y()));
        }
        sliceLines.push_back(
            QStringLiteral("%1 | %2 | %3 | \u9884\u5b9a %4 cm\u00b3 | \u5df2\u6cbb\u7597 %5 cm\u00b3 | \u547c\u5438 %6")
                .arg(slice.label)
                .arg(patternSummaryText(slice.pattern))
                .arg(targetSummaryText(slice.pattern, slice.targets))
                .arg(slice.estimatedVolumeCm3, 0, 'f', 2)
                .arg(slice.ablatedVolumeCm3, 0, 'f', 2)
                .arg(slice.respiratoryTrackingEnabled
                    ? (slice.respiratoryTrackingCalibrated
                        ? QStringLiteral("dX %1 / dY %2")
                              .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 2)
                              .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 2)
                        : QStringLiteral("\u5f00\u542f\u5f85\u6807\u5b9a"))
                    : QStringLiteral("\u5173")));
    }

    updateAssessmentMetricsPanel(estimatedVolumeCm3, ablatedVolumeCm3);

    QStringList lines {
        QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel()),
        QStringLiteral("\u5df2\u8bc4\u4f30\u5207\u7247\uff1a%1 / %2").arg(generatedSliceCount).arg(m_stagedSlices.size()),
        QStringLiteral("\u603b\u9776\u70b9\u6570\uff1a%1").arg(totalTargetCount),
        QStringLiteral("\u547c\u5438\u8ddf\u968f\u5207\u7247\uff1a%1 | \u6700\u5927\u8865\u507f\uff1a%2 mm")
            .arg(respiratoryTrackedSliceCount)
            .arg(maxRespiratoryCompensationMm, 0, 'f', 2),
        QStringLiteral("\u9884\u5b9a\u4f53\u79ef\uff1a%1 cm\u00b3").arg(estimatedVolumeCm3, 0, 'f', 2),
        QStringLiteral("\u5df2\u6cbb\u7597\u4f53\u79ef\uff1a%1 cm\u00b3").arg(ablatedVolumeCm3, 0, 'f', 2),
        QStringLiteral("\u8986\u76d6\u7387\uff1a%1%").arg(estimatedVolumeCm3 <= 0.0 ? 0 : static_cast<int>(std::round((ablatedVolumeCm3 / estimatedVolumeCm3) * 100.0)))
    };
    if (!sliceLines.isEmpty()) {
        lines << QString() << QStringLiteral("\u5207\u7247\u660e\u7ec6\uff1a") << sliceLines;
    }
    updateAcquisitionSummary(QStringLiteral("\u65b9\u6848\u8bc4\u4f30"), lines);
}

void PlanningPage::approveCurrentPlan()
{
    activatePlanningWorkspace();
    if (!m_context->hasActivePlan()) {
        generateDraftPlan();
    }

    storeCurrentSliceControls();
    TherapyPlan approvedPlan = hasGeneratedSliceTargets() ? buildPlanFromSlices(ApprovalState::Locked) : buildPlanFromUi(ApprovalState::Locked);
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
    activatePlanningWorkspace();
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
    if (m_stagedSlices.isEmpty()) {
        m_preview->setPlan(plan);
    } else {
        refreshCurrentSliceVisualization();
    }
    updatePlanPreviewText(&plan);
}

void PlanningPage::updateContextSummary()
{
    if (m_context->hasSelectedPatient()) {
        const PatientRecord& patient = m_context->selectedPatient();
        syncPatientSelector(patient.id);
        refreshImagingPaths(patient.id);
        loadHistoricalImages(false);
        m_patientSummaryLabel->setText(
            QStringLiteral("\u5f53\u524d\u60a3\u8005\n\u59d3\u540d\uff1a%1\n\u7f16\u53f7\uff1a%2\n\u5e74\u9f84\uff1a%3\n\u8bca\u65ad\uff1a%4")
                .arg(patient.name)
                .arg(patient.id)
                .arg(patient.age)
                .arg(patient.diagnosis));
    } else {
        refreshImagingPaths(QString());
        m_loadedHistoryPatientId.clear();
        clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        updateAssessmentMetricsPanel(0.0, 0.0);
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
        if (m_stagedSlices.isEmpty()) {
            if (m_preview != nullptr) {
                m_preview->clearBackgroundImage();
                m_preview->setPlan(plan);
                m_preview->setSliceContext(0, 0);
                m_preview->setCaption(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u9884\u89c8"));
            }
            m_previewOverlayLabel->setVisible(false);
        } else {
            loadStagedSlice(m_currentStagedSliceIndex >= 0 ? m_currentStagedSliceIndex : 0);
        }
    } else {
        m_planSummaryLabel->setText(QStringLiteral("\u6d3b\u52a8\u65b9\u6848\n\u5c1a\u672a\u751f\u6210"));
        updateAssessmentText(nullptr);
        updatePlanPreviewText(nullptr);
        m_preview->clearPlan();
        if (m_stagedImageSeries.isEmpty()) {
            m_preview->setSliceContext(0, 0);
            m_preview->setCaption(QStringLiteral(""));
            m_previewOverlayLabel->setText(QStringLiteral("\u53f3\u5c4f\u663e\u793a\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
            m_previewOverlayLabel->setVisible(true);
        } else {
            loadStagedSlice(m_currentStagedSliceIndex >= 0 ? m_currentStagedSliceIndex : 0);
            m_previewOverlayLabel->setVisible(false);
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

TherapyPlan PlanningPage::buildPlanFromSlices(ApprovalState approvalState) const
{
    TherapyPlan plan = buildPlanFromUi(approvalState);
    plan.segments.clear();
    plan.pattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;

    int segmentIndex = 0;
    int totalTargetCount = 0;
    double totalDurationSeconds = 0.0;
    bool containsLineSlice = false;
    bool containsRespiratoryTracking = false;
    for (const StagedSliceState& slice : m_stagedSlices) {
        if (!slice.targetsGenerated || slice.targets.isEmpty()) {
            continue;
        }

        TherapySegment segment;
        segment.id = QStringLiteral("%1-S%2").arg(plan.id).arg(segmentIndex + 1);
        segment.orderIndex = segmentIndex;
        segment.label = QStringLiteral("%1 | %2").arg(slice.label, patternSummaryText(slice.pattern));
        segment.points = slice.targets;
        segment.plannedDurationSeconds = totalDwellSeconds(slice.targets);

        totalTargetCount += segment.points.size();
        totalDurationSeconds += segment.plannedDurationSeconds;
        containsLineSlice = containsLineSlice || slice.pattern == TreatmentPattern::Line;
        containsRespiratoryTracking = containsRespiratoryTracking || slice.respiratoryTrackingEnabled;
        plan.segments.push_back(segment);
        ++segmentIndex;
    }

    if (containsLineSlice) {
        plan.pattern = TreatmentPattern::Line;
    }
    if (m_segmentedTreatmentRadio->isChecked()) {
        plan.pattern = TreatmentPattern::Segmented;
    }
    if (!plan.segments.isEmpty()) {
        plan.name = m_context->hasActivePlan() && !m_context->activePlan().name.trimmed().isEmpty()
            ? m_context->activePlan().name
            : QStringLiteral("%1-\u6cbb\u7597\u65b9\u6848").arg(currentChannelLabel());
        plan.depthMm = static_cast<double>(m_stagedSlices.size() * m_stepSpin->value());
        plan.respiratoryTrackingEnabled = containsRespiratoryTracking;
    }

    Q_UNUSED(totalTargetCount)
    Q_UNUSED(totalDurationSeconds)
    return plan;
}

void PlanningPage::editCurrentPlan()
{
    activatePlanningWorkspace();
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

    form->addRow(QStringLiteral("\u65b9\u6848\u540d\u79f0"), nameEdit);
    form->addRow(QStringLiteral("\u5ba1\u6838\u72b6\u6001"), approvalCombo);

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
    activatePlanningWorkspace();
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (!hasGeneratedSliceTargets()) {
        updateAcquisitionSummary(
            QStringLiteral("\u65b9\u6848\u4fdd\u5b58"),
            {
                QStringLiteral("\u8bf7\u5148\u9488\u5bf9\u8be5\u8def\u5f84\u4e0a\u7684\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u9010\u5f20\u5708\u753b\u8096\u7624\u533a\u57df\uff0c\u5e76\u751f\u6210\u9776\u70b9\u3002")
            });
        return;
    }

    const ApprovalState saveState = (!m_context->hasActivePlan() || m_context->activePlan().approvalState == ApprovalState::Draft)
        ? ApprovalState::Locked
        : m_context->activePlan().approvalState;
    TherapyPlan planToSave = buildPlanFromSlices(saveState);
    if (m_context->hasActivePlan()) {
        planToSave.id = m_context->activePlan().id;
        if (!m_context->activePlan().name.trimmed().isEmpty()) {
            planToSave.name = m_context->activePlan().name;
        }
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
    updateSliceAssessmentMetrics();
    int sliceCount = 0;
    int targetCount = 0;
    for (const TherapySegment& segment : planToSave.segments) {
        ++sliceCount;
        targetCount += segment.points.size();
    }
    updateAcquisitionSummary(
        QStringLiteral("\u65b9\u6848\u4fdd\u5b58\u5b8c\u6210"),
        {
            QStringLiteral("\u65b9\u6848\uff1a%1").arg(planToSave.name),
            QStringLiteral("\u72b6\u6001\uff1a%1").arg(toDisplayString(planToSave.approvalState)),
            QStringLiteral("\u5207\u7247\u6570\uff1a%1").arg(sliceCount),
            QStringLiteral("\u603b\u9776\u70b9\u6570\uff1a%1").arg(targetCount),
            QStringLiteral("\u8be5\u65b9\u6848\u5df2\u53ef\u5728\u6cbb\u7597\u9636\u6bb5\u4e2d\u88ab\u9009\u7528\u3002")
        });
    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u4fdd\u5b58\u6cbb\u7597\u65b9\u6848\uff1a%1").arg(planToSave.id));
    }
}

void PlanningPage::deleteCurrentPlan()
{
    activatePlanningWorkspace();
    if (!m_context->hasActivePlan()) {
        if (hasGeneratedSliceTargets()) {
            clearSliceTargets(true);
            updateAcquisitionSummary(
                QStringLiteral("\u5220\u9664\u65b9\u6848"),
                {
                    QStringLiteral("\u5f53\u524d\u672a\u4fdd\u5b58\u7684\u5207\u7247\u7f16\u8f91\u5df2\u6e05\u7a7a\uff0c\u8bf7\u91cd\u65b0\u5bf9\u6bcf\u5f20\u5f71\u50cf\u8fdb\u884c\u5708\u753b\u548c\u751f\u6210\u9776\u70b9\u3002")
                });
            return;
        }
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
    clearSliceTargets(true);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
    }
    updateAcquisitionSummary(
        QStringLiteral("\u5220\u9664\u65b9\u6848"),
        {
            QStringLiteral("\u65b9\u6848\u7f16\u53f7\uff1a%1").arg(planId),
            QStringLiteral("\u5f53\u524d\u8def\u5f84\u4e0a\u7684\u5207\u7247\u7f16\u8f91\u548c\u9776\u70b9\u5df2\u88ab\u6e05\u7a7a\uff0c\u8bf7\u91cd\u65b0\u7f16\u8f91\u3002")
        });
    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u5220\u9664\u6cbb\u7597\u65b9\u6848\uff1a%1").arg(planId));
    }
}

void PlanningPage::toggleAnnotationPanel()
{
    if (!m_initializingUi) {
        activatePlanningWorkspace();
    }
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

void PlanningPage::onPathSelectionChanged(int row)
{
    if (row < 0) {
        updatePathActionState();
        return;
    }
    if (!m_initializingUi) {
        activatePlanningWorkspace();
    }

    const QString newPathStateKey = pathStateKeyForRow(row);
    if (newPathStateKey.isEmpty()) {
        return;
    }
    if (!m_activePathStateKey.isEmpty() && m_activePathStateKey == newPathStateKey) {
        return;
    }

    saveCurrentPathState();
    loadPathState(row);
    updatePathActionState();
}

void PlanningPage::onStagedSliceSelectionChanged(int row)
{
    if (!m_initializingUi) {
        activatePlanningWorkspace();
    }
    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();
    loadStagedSlice(row);
}

void PlanningPage::onPreviewAnnotationsChanged()
{
    persistCurrentSliceAnnotations();
    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    if (slice.targetsGenerated) {
        invalidateCurrentSliceTargets();
    }
}

void PlanningPage::onRespiratoryTrackingToggled(bool enabled)
{
    if (!m_initializingUi) {
        activatePlanningWorkspace();
    }

    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        refreshDerivedMetrics();
        return;
    }

    storeCurrentSliceControls();
    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    if (!enabled) {
        clearRespiratoryTrackingState(slice);
        refreshCurrentSliceVisualization();
        updateAcquisitionSummary(
            QStringLiteral("\u547c\u5438\u8ddf\u968f"),
            {
                QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
                QStringLiteral("\u5df2\u5173\u95ed\u547c\u5438\u8ddf\u968f\u8865\u507f\u3002")
            });
        return;
    }

    if (!slice.targetsGenerated || slice.annotations.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u547c\u5438\u8ddf\u968f"),
            {
                QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
                QStringLiteral("\u8bf7\u5148\u5708\u753b\u80bf\u7624\u533a\u57df\u5e76\u751f\u6210\u9776\u70b9\uff0c\u7cfb\u7edf\u624d\u80fd\u5b8c\u6210\u547c\u5438\u6807\u5b9a\u3002")
            });
        return;
    }

    recalculateRespiratoryTrackingForSlice(m_currentStagedSliceIndex);
    refreshCurrentSliceVisualization();
    updateAcquisitionSummary(
        QStringLiteral("\u547c\u5438\u8ddf\u968f"),
        {
            QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label),
            slice.respiratorySummary.isEmpty()
                ? QStringLiteral("\u5f53\u524d\u56fe\u50cf\u8fd8\u65e0\u6cd5\u5b8c\u6210\u547c\u5438\u6807\u5b9a\u3002")
                : slice.respiratorySummary
        });
}

void PlanningPage::onDeviceSnapshotUpdated(const DeviceSnapshot& snapshot)
{
    m_latestDeviceSnapshot = snapshot;
    m_hasDeviceSnapshot = true;
    refreshPowerCurve();

    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        return;
    }

    const StagedSliceState& slice = m_stagedSlices.at(m_currentStagedSliceIndex);
    if (!slice.respiratoryTrackingEnabled || !slice.targetsGenerated) {
        return;
    }

    recalculateRespiratoryTrackingForSlice(m_currentStagedSliceIndex);
    refreshCurrentSliceVisualization();
}

void PlanningPage::persistCurrentSliceAnnotations()
{
    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size() || m_preview == nullptr) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    const QVector<AnnotationStroke> rawAnnotations = m_preview->annotationStrokes();
    const QVector<AnnotationStroke> normalizedAnnotations = normalizeClosedAnnotations(rawAnnotations);
    if (!annotationStrokesEqual(rawAnnotations, normalizedAnnotations)) {
        m_preview->setAnnotationStrokes(normalizedAnnotations);
    }
    slice.annotations = normalizedAnnotations;
    slice.edited = !slice.annotations.isEmpty();
}

void PlanningPage::loadStagedSlice(int row)
{
    const int sliceCount = m_stagedSlices.size();
    if (m_currentSliceSlider != nullptr) {
        const QSignalBlocker blocker(m_currentSliceSlider);
        m_currentSliceSlider->setRange(0, std::max(0, sliceCount - 1));
        m_currentSliceSlider->setEnabled(sliceCount > 1);
        if (sliceCount == 0) {
            m_currentSliceSlider->setValue(0);
        }
    }

    if (sliceCount == 0) {
        m_currentStagedSliceIndex = -1;
        if (m_preview != nullptr) {
            m_preview->setAnnotationStrokes({});
            m_preview->setSliceContext(0, 0);
            if (!m_context->hasActivePlan()) {
                m_preview->setCaption(QStringLiteral(""));
            }
        }
        if (m_currentSliceSummaryLabel != nullptr) {
            const QString summaryText = m_context->hasActivePlan()
                ? QStringLiteral("\u5f53\u524d\u6cbb\u7597\u9884\u89c8 | \u5c1a\u672a\u91c7\u96c6\u5207\u7247")
                : QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a\u7b49\u5f85\u91c7\u96c6\u6216\u9884\u89c8\u65b9\u6848");
            m_currentSliceSummaryLabel->setText(summaryText);
            m_currentSliceSummaryLabel->setToolTip(QString());
        }
        return;
    }

    const int safeRow = qBound(0, row, sliceCount - 1);
    m_currentStagedSliceIndex = safeRow;

    if (m_currentSliceSlider != nullptr && m_currentSliceSlider->value() != safeRow) {
        const QSignalBlocker blocker(m_currentSliceSlider);
        m_currentSliceSlider->setValue(safeRow);
    }

    restoreSliceControls(safeRow);
    recalculateRespiratoryTrackingForSlice(safeRow);
    refreshCurrentSliceVisualization();

    const StagedSliceState& slice = m_stagedSlices.at(safeRow);
    const QStringList lines {
        QStringLiteral("\u5f53\u524d\u5207\u7247\uff1a%1").arg(slice.label),
        QStringLiteral("\u6682\u5b58\u8def\u5f84\uff1a%1").arg(slice.image.storagePath),
        QStringLiteral("\u7f16\u8f91\u72b6\u6001\uff1a%1").arg(slice.edited ? QStringLiteral("\u5df2\u5708\u753b") : QStringLiteral("\u672a\u5708\u753b")),
        QStringLiteral("\u5f53\u524d\u7b14\u8ff9\u6570\uff1a%1").arg(slice.annotations.size()),
        QStringLiteral("\u5f53\u524d\u9776\u70b9\u6570\uff1a%1").arg(slice.targets.size()),
        QStringLiteral("\u547c\u5438\u8ddf\u968f\uff1a%1").arg(
            slice.respiratoryTrackingEnabled
                ? (slice.respiratoryTrackingCalibrated
                    ? QStringLiteral("\u5df2\u6807\u5b9a | dX %1 mm / dY %2 mm")
                          .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 2)
                          .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 2)
                    : QStringLiteral("\u5df2\u5f00\u542f\uff0c\u5f85\u6807\u5b9a"))
                : QStringLiteral("\u672a\u5f00\u542f"))
    };
    updateAcquisitionSummary(QStringLiteral("\u5207\u7247\u7f16\u8f91"), lines);
}

QPixmap PlanningPage::renderCurrentSlicePixmap(int row, const QSize& size) const
{
    if (row < 0 || row >= m_stagedSlices.size() || !size.isValid()) {
        return {};
    }

    const StagedSliceState& slice = m_stagedSlices.at(row);
    MockUltrasoundView renderView;
    renderView.resize(size);
    renderView.setCaption(QStringLiteral("%1 | %2").arg(slice.label, patternSummaryText(slice.pattern)));
    renderView.setSliceContext(row, m_stagedSlices.size());
    renderView.setAnnotationStrokes(slice.annotations);
    if (slice.targetsGenerated && !slice.targets.isEmpty()) {
        const QVector<TherapyPoint>& previewPoints =
            slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated && !slice.respiratoryAdjustedTargets.isEmpty()
            ? slice.respiratoryAdjustedTargets
            : slice.targets;
        TherapyPlan previewPlan;
        previewPlan.pattern = slice.pattern;
        TherapySegment segment;
        segment.id = QStringLiteral("PREVIEW-%1").arg(row + 1);
        segment.orderIndex = 0;
        segment.label = slice.label;
        segment.points = previewPoints;
        segment.plannedDurationSeconds = totalDwellSeconds(previewPoints);
        previewPlan.segments.push_back(segment);
        renderView.setPlan(previewPlan);
        renderView.setCompletedPointCount(0);
    } else {
        renderView.clearPlan();
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    renderView.render(&pixmap);
    return pixmap;
}

void PlanningPage::refreshCurrentSliceVisualization()
{
    if (m_preview == nullptr) {
        return;
    }

    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        m_preview->clearBackgroundImage();
        if (!m_context->hasActivePlan()) {
            m_preview->clearPlan();
        }
        return;
    }

    const StagedSliceState& slice = m_stagedSlices.at(m_currentStagedSliceIndex);
    m_preview->clearBackgroundImage();
    m_preview->setAnnotationStrokes(slice.annotations);
    m_preview->setSliceContext(m_currentStagedSliceIndex, m_stagedSlices.size());
    const QString respiratoryCaption = slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated
        ? QStringLiteral(" | \u547c\u5438\u8865\u507f dX %1 dY %2")
              .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 1)
              .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 1)
        : QString();
    m_preview->setCaption(
        QStringLiteral("\u5f53\u524d\u6cbb\u7597 %1/%2%3")
            .arg(m_currentStagedSliceIndex + 1)
            .arg(m_stagedSlices.size())
            .arg(respiratoryCaption));
    m_preview->setCompletedPointCount(0);
    if (slice.targetsGenerated && !slice.targets.isEmpty()) {
        const QVector<TherapyPoint>& previewPoints =
            slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated && !slice.respiratoryAdjustedTargets.isEmpty()
            ? slice.respiratoryAdjustedTargets
            : slice.targets;
        TherapyPlan previewPlan;
        previewPlan.pattern = slice.pattern;
        TherapySegment segment;
        segment.id = QStringLiteral("SLICE-%1").arg(m_currentStagedSliceIndex + 1);
        segment.orderIndex = 0;
        segment.label = slice.label;
        segment.points = previewPoints;
        segment.plannedDurationSeconds = totalDwellSeconds(previewPoints);
        previewPlan.segments.push_back(segment);
        m_preview->setPlan(previewPlan);
    } else {
        m_preview->clearPlan();
    }

    if (m_previewOverlayLabel != nullptr) {
        m_previewOverlayLabel->setVisible(false);
    }
    if (m_currentSliceSummaryLabel != nullptr) {
        m_currentSliceSummaryLabel->setText(
            QStringLiteral("\u7b2c %1/%2 \u5f20 | %3 | \u7b14\u8ff9 %4 | %5%6")
                .arg(m_currentStagedSliceIndex + 1)
                .arg(m_stagedSlices.size())
                .arg(slice.image.storagePath)
                .arg(slice.annotations.size())
                .arg(targetSummaryText(slice.pattern, slice.targets))
                .arg(slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated
                    ? QStringLiteral(" | \u547c\u5438\u8865\u507f dX %1 / dY %2")
                          .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 2)
                          .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 2)
                    : QString()));
        m_currentSliceSummaryLabel->setToolTip(slice.image.storagePath);
    }
}

void PlanningPage::restoreSliceControls(int row)
{
    if (row < 0 || row >= m_stagedSlices.size()) {
        return;
    }

    const StagedSliceState& slice = m_stagedSlices.at(row);
    const QSignalBlocker spacingBlocker(m_spacingSpin);
    const QSignalBlocker dwellBlocker(m_dwellSpin);
    const QSignalBlocker powerSliderBlocker(m_powerSlider);
    const QSignalBlocker powerSpinBlocker(m_powerSpin);
    const QSignalBlocker respiratoryBlocker(m_respiratoryTrackingCheck);
    const QSignalBlocker directBlocker(m_directTreatmentRadio);
    const QSignalBlocker segmentedBlocker(m_segmentedTreatmentRadio);
    const QSignalBlocker pointBlocker(m_pointTreatmentRadio);
    const QSignalBlocker lineBlocker(m_lineTreatmentRadio);

    if (slice.spacingMm > 0.0) {
        m_spacingSpin->setValue(slice.spacingMm);
    }
    if (slice.dwellSeconds > 0.0) {
        m_dwellSpin->setValue(slice.dwellSeconds);
    }
    if (slice.powerWatts > 0.0) {
        m_powerSlider->setValue(static_cast<int>(slice.powerWatts));
        m_powerSpin->setValue(slice.powerWatts);
    }
    m_respiratoryTrackingCheck->setChecked(slice.respiratoryTrackingEnabled);
    m_segmentedTreatmentRadio->setChecked(slice.deliveryMode == QStringLiteral("\u5206\u6bb5\u6267\u884c"));
    m_directTreatmentRadio->setChecked(slice.deliveryMode != QStringLiteral("\u5206\u6bb5\u6267\u884c"));
    m_lineTreatmentRadio->setChecked(slice.pattern == TreatmentPattern::Line);
    m_pointTreatmentRadio->setChecked(slice.pattern != TreatmentPattern::Line);
    refreshDerivedMetrics();
}

void PlanningPage::storeCurrentSliceControls()
{
    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    slice.pattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
    slice.spacingMm = m_spacingSpin->value();
    slice.dwellSeconds = m_dwellSpin->value();
    slice.powerWatts = m_powerSlider->value();
    slice.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
    slice.deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
}

void PlanningPage::invalidateCurrentSliceTargets(const QString& title, const QString& detail)
{
    if (m_currentStagedSliceIndex < 0 || m_currentStagedSliceIndex >= m_stagedSlices.size()) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[m_currentStagedSliceIndex];
    slice.targets.clear();
    slice.targetsGenerated = false;
    slice.annotatedAreaMm2 = 0.0;
    slice.estimatedVolumeCm3 = 0.0;
    slice.ablatedVolumeCm3 = 0.0;
    clearRespiratoryTrackingState(slice);
    storeCurrentSliceControls();

    refreshCurrentSliceVisualization();
    updateSliceAssessmentMetrics();
    refreshDerivedMetrics();

    if (hasGeneratedSliceTargets()) {
        const ApprovalState previewState = m_context->hasActivePlan() ? m_context->activePlan().approvalState : ApprovalState::Draft;
        const TherapyPlan refreshedPlan = buildPlanFromSlices(previewState);
        m_context->setActivePlan(refreshedPlan);
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(refreshedPlan.approvalState);
        }
        updatePlanPreviewText(&refreshedPlan);
    } else {
        if (m_context != nullptr && m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        }
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
        }
        updatePlanPreviewText(nullptr);
    }

    if (!title.trimmed().isEmpty()) {
        QStringList lines {
            QStringLiteral("\u5207\u7247\uff1a%1").arg(slice.label)
        };
        if (!detail.trimmed().isEmpty()) {
            lines.push_back(detail);
        }
        updateAcquisitionSummary(title, lines);
    }
}

void PlanningPage::clearRespiratoryTrackingState(StagedSliceState& slice)
{
    slice.respiratoryAdjustedTargets.clear();
    slice.respiratoryCalibrationBoxMm = QRectF {};
    slice.respiratoryBaselineCentroidMm = QPointF {};
    slice.respiratoryLiveCentroidMm = QPointF {};
    slice.respiratoryOffsetMm = QPointF {};
    slice.respiratorySummary.clear();
    slice.respiratoryTrackingCalibrated = false;
}

void PlanningPage::recalculateRespiratoryTrackingForSlice(int row)
{
    if (row < 0 || row >= m_stagedSlices.size()) {
        return;
    }

    StagedSliceState& slice = m_stagedSlices[row];
    if (!slice.respiratoryTrackingEnabled || !slice.targetsGenerated || slice.targets.isEmpty() || slice.annotations.isEmpty()) {
        clearRespiratoryTrackingState(slice);
        return;
    }

    const DeviceSnapshot* snapshot = m_hasDeviceSnapshot ? &m_latestDeviceSnapshot : nullptr;
    const RespiratoryFollowResult result = computeRespiratoryFollowResult(
        slice.annotations,
        slice.targets,
        row,
        m_stagedSlices.size(),
        snapshot);
    if (!result.valid) {
        clearRespiratoryTrackingState(slice);
        return;
    }

    slice.respiratoryAdjustedTargets = result.correctedTargets;
    slice.respiratoryCalibrationBoxMm = result.calibrationBoxMm;
    slice.respiratoryBaselineCentroidMm = result.baselineCentroidMm;
    slice.respiratoryLiveCentroidMm = result.liveCentroidMm;
    slice.respiratoryOffsetMm = result.deltaMm;
    slice.respiratorySummary = result.summary;
    slice.respiratoryTrackingCalibrated = true;
}

void PlanningPage::clearSliceTargets(bool clearAnnotations)
{
    for (StagedSliceState& slice : m_stagedSlices) {
        slice.targets.clear();
        slice.targetsGenerated = false;
        slice.annotatedAreaMm2 = 0.0;
        slice.estimatedVolumeCm3 = 0.0;
        slice.ablatedVolumeCm3 = 0.0;
        clearRespiratoryTrackingState(slice);
        if (clearAnnotations) {
            slice.annotations.clear();
            slice.edited = false;
        }
    }

    if (clearAnnotations && m_preview != nullptr) {
        m_preview->setAnnotationStrokes({});
    }
    updateSliceAssessmentMetrics();
    refreshCurrentSliceVisualization();
    refreshDerivedMetrics();
    if (!m_context->hasActivePlan()) {
        updatePlanPreviewText(nullptr);
    }
}

void PlanningPage::updateSliceAssessmentMetrics()
{
    double estimatedVolumeCm3 = 0.0;
    double ablatedVolumeCm3 = 0.0;
    for (const StagedSliceState& slice : m_stagedSlices) {
        if (!slice.targetsGenerated || slice.targets.isEmpty()) {
            continue;
        }
        estimatedVolumeCm3 += slice.estimatedVolumeCm3;
        ablatedVolumeCm3 += slice.ablatedVolumeCm3;
    }
    updateAssessmentMetricsPanel(estimatedVolumeCm3, ablatedVolumeCm3);
}

void PlanningPage::updateAssessmentMetricsPanel(double estimatedVolumeCm3, double ablatedVolumeCm3)
{
    const double clampedEstimated = std::max(0.0, estimatedVolumeCm3);
    const double clampedAblated = std::max(0.0, ablatedVolumeCm3);
    const int coverageRatio = clampedEstimated <= 0.0
        ? 0
        : static_cast<int>(std::round(std::min(1.0, clampedAblated / clampedEstimated) * 100.0));

    if (m_estimatedVolumeValueLabel != nullptr) {
        m_estimatedVolumeValueLabel->setText(QStringLiteral("%1 cm\u00b3").arg(clampedEstimated, 0, 'f', 2));
    }
    if (m_ablatedVolumeValueLabel != nullptr) {
        m_ablatedVolumeValueLabel->setText(QStringLiteral("%1 cm\u00b3").arg(clampedAblated, 0, 'f', 2));
    }
    if (m_coverageRatioValueLabel != nullptr) {
        m_coverageRatioValueLabel->setText(QStringLiteral("%1%").arg(coverageRatio));
    }
    if (m_coverageProgressBar != nullptr) {
        m_coverageProgressBar->setValue(coverageRatio);
    }
}

bool PlanningPage::hasGeneratedSliceTargets() const
{
    return std::any_of(m_stagedSlices.cbegin(), m_stagedSlices.cend(), [](const StagedSliceState& slice) {
        return slice.targetsGenerated && !slice.targets.isEmpty();
    });
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
    updatePathActionState();
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
    if (m_pathList == nullptr) {
        return;
    }

    if (m_pathList->count() > 0 && m_pathList->currentRow() < 0) {
        const QSignalBlocker blocker(m_pathList);
        m_pathList->setCurrentRow(0);
    }
}

bool PlanningPage::hasActivePathSelection() const
{
    return m_pathList != nullptr && m_pathList->currentRow() >= 0 && m_pathList->currentRow() < m_pathList->count();
}

void PlanningPage::updatePathActionState()
{
    const bool hasPathSelection = hasActivePathSelection();
    if (m_removePathButton != nullptr) {
        m_removePathButton->setEnabled(hasPathSelection);
    }
    if (m_acquireImageButton != nullptr) {
        m_acquireImageButton->setEnabled(hasPathSelection);
    }
    if (m_generate3dButton != nullptr) {
        m_generate3dButton->setEnabled(hasPathSelection && !m_stagedSlices.isEmpty());
    }
    refreshPowerCurve();
}

void PlanningPage::refreshPowerCurve()
{
    if (m_chartSummaryLabel == nullptr || m_energyOutputChart == nullptr) {
        return;
    }

    if (!hasActivePathSelection()) {
        m_chartSummaryLabel->setText(QStringLiteral("\u5b9e\u65f6\u529f\u7387: --"));
        m_energyOutputChart->clearPowerCurve(QStringLiteral("\u65b0\u589e\u8def\u5f84\u540e\u663e\u793a\u8d85\u58f0\u5934\u529f\u7387\u66f2\u7ebf"));
        return;
    }

    const double realtimePowerWatts = currentRealtimeTransducerPowerWatts();
    m_chartSummaryLabel->setText(QStringLiteral("\u5b9e\u65f6\u529f\u7387: %1W").arg(realtimePowerWatts, 0, 'f', 0));
    m_energyOutputChart->setRealtimePowerWatts(realtimePowerWatts);
}

double PlanningPage::currentRealtimeTransducerPowerWatts() const
{
    const double setPowerWatts = m_powerSlider != nullptr ? static_cast<double>(m_powerSlider->value()) : 0.0;
    if (setPowerWatts <= 0.0) {
        return 0.0;
    }

    if (!m_hasDeviceSnapshot) {
        return setPowerWatts;
    }

    if (m_latestDeviceSnapshot.outputPowerWatts > 1.0) {
        return m_latestDeviceSnapshot.outputPowerWatts;
    }

    const double efficiency = std::clamp(m_latestDeviceSnapshot.conversionEfficiencyPercent / 100.0, 0.55, 0.98);
    const double thermalPenalty = std::clamp(
        1.0 - (std::max(0.0, m_latestDeviceSnapshot.transducerTemperatureCelsius - 28.0) / 40.0),
        0.82,
        1.0);
    const double motionPenalty = std::clamp(1.0 - (m_latestDeviceSnapshot.motionAccuracyMm / 5.0), 0.70, 1.0);
    return setPowerWatts * efficiency * thermalPenalty * motionPenalty;
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

QListWidgetItem* PlanningPage::createPathListItem(int index)
{
    auto* item = new QListWidgetItem(defaultChannelText(index));
    item->setData(kPathStateKeyRole, QStringLiteral("planning-path-%1").arg(m_nextPathStateId++));
    return item;
}

QString PlanningPage::pathStateKeyForRow(int row) const
{
    if (m_pathList == nullptr || row < 0 || row >= m_pathList->count()) {
        return {};
    }

    const QListWidgetItem* item = m_pathList->item(row);
    return item == nullptr ? QString() : item->data(kPathStateKeyRole).toString();
}

void PlanningPage::saveCurrentPathState()
{
    if (m_activePathStateKey.isEmpty()) {
        return;
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    PathEditingState state;
    state.stagedImageSeries = m_stagedImageSeries;
    state.stagedSlices = m_stagedSlices;
    state.assessmentText = m_assessmentPreview != nullptr ? m_assessmentPreview->toPlainText() : QString();
    state.planPreviewText = m_planPreview != nullptr ? m_planPreview->toPlainText() : QString();
    state.lastAcquisitionAt = m_lastAcquisitionAt;
    state.currentStagedSliceIndex = m_currentStagedSliceIndex;
    state.annotationPanelExpanded = m_annotationPanel != nullptr && m_annotationPanel->isVisible();
    state.hasActivePlan = m_context != nullptr && m_context->hasActivePlan();
    if (state.hasActivePlan) {
        state.activePlan = m_context->activePlan();
    }

    m_pathEditingStates.insert(m_activePathStateKey, state);
}

void PlanningPage::activatePlanningWorkspace()
{
    m_deferStartupContextSummary = false;
}

void PlanningPage::clearStartupDisplay()
{
    if (m_pathList != nullptr) {
        const QSignalBlocker blocker(m_pathList);
        m_pathList->clear();
    }
    m_pathEditingStates.clear();
    m_activePathStateKey.clear();
    m_loadedHistoryPatientId.clear();
    clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    resetActivePathWorkspace();
    if (m_assessmentPreview != nullptr) {
        m_assessmentPreview->clear();
    }
    if (m_planPreview != nullptr) {
        m_planPreview->clear();
    }
    updatePathActionState();
}

void PlanningPage::rebuildModelList()
{
    if (m_modelList == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_modelList);
    m_modelList->clear();
    for (const StagedSliceState& slice : std::as_const(m_stagedSlices)) {
        m_modelList->addItem(slice.label);
    }
}

void PlanningPage::resetActivePathWorkspace()
{
    m_stagedImageSeries.clear();
    m_stagedSlices.clear();
    m_currentStagedSliceIndex = -1;
    m_lastAcquisitionAt = QDateTime {};

    rebuildModelList();
    if (m_currentSliceSlider != nullptr) {
        const QSignalBlocker blocker(m_currentSliceSlider);
        m_currentSliceSlider->setRange(0, 0);
        m_currentSliceSlider->setValue(0);
        m_currentSliceSlider->setEnabled(false);
    }

    if (m_preview != nullptr) {
        m_preview->setAnnotationStrokes({});
        m_preview->clearBackgroundImage();
        m_preview->clearPlan();
        m_preview->setCompletedPointCount(0);
        m_preview->setSliceContext(0, 0);
        m_preview->setCaption(QString());
        m_preview->setAnnotationEnabled(false);
    }

    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setVisible(false);
    }
    if (m_annotationButton != nullptr) {
        m_annotationButton->setChecked(false);
    }

    {
        const QSignalBlocker spacingBlocker(m_spacingSpin);
        const QSignalBlocker dwellBlocker(m_dwellSpin);
        const QSignalBlocker powerSliderBlocker(m_powerSlider);
        const QSignalBlocker powerSpinBlocker(m_powerSpin);
        const QSignalBlocker respiratoryBlocker(m_respiratoryTrackingCheck);
        const QSignalBlocker directBlocker(m_directTreatmentRadio);
        const QSignalBlocker segmentedBlocker(m_segmentedTreatmentRadio);
        const QSignalBlocker pointBlocker(m_pointTreatmentRadio);
        const QSignalBlocker lineBlocker(m_lineTreatmentRadio);

        m_spacingSpin->setValue(3.0);
        m_dwellSpin->setValue(0.3);
        m_powerSlider->setValue(400);
        m_powerSpin->setValue(400.0);
        m_respiratoryTrackingCheck->setChecked(false);
        m_directTreatmentRadio->setChecked(true);
        m_segmentedTreatmentRadio->setChecked(false);
        m_pointTreatmentRadio->setChecked(true);
        m_lineTreatmentRadio->setChecked(false);
    }

    if (m_currentSliceSummaryLabel != nullptr) {
        m_currentSliceSummaryLabel->setText(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a\u7b49\u5f85\u91c7\u96c6\u6216\u9884\u89c8\u65b9\u6848"));
        m_currentSliceSummaryLabel->setToolTip(QString());
    }
    if (m_previewOverlayLabel != nullptr) {
        m_previewOverlayLabel->setText(QStringLiteral("\u53f3\u5c4f\u663e\u793a\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
        m_previewOverlayLabel->setVisible(true);
    }

    updateAssessmentMetricsPanel(0.0, 0.0);
    updateAssessmentText(nullptr);
    updatePlanPreviewText(nullptr);
    refreshDerivedMetrics();
    updatePathActionState();
}

void PlanningPage::loadPathState(int row)
{
    const QString pathKey = pathStateKeyForRow(row);
    m_activePathStateKey = pathKey;
    if (pathKey.isEmpty()) {
        resetActivePathWorkspace();
        updatePathActionState();
        return;
    }

    const auto stateIt = m_pathEditingStates.constFind(pathKey);
    if (stateIt == m_pathEditingStates.cend()) {
        if (m_context != nullptr && m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        }
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
        }
        resetActivePathWorkspace();
        updatePathActionState();
        return;
    }

    const PathEditingState state = stateIt.value();
    m_stagedImageSeries = state.stagedImageSeries;
    m_stagedSlices = state.stagedSlices;
    m_currentStagedSliceIndex = state.currentStagedSliceIndex;
    m_lastAcquisitionAt = state.lastAcquisitionAt;
    rebuildModelList();

    if (state.hasActivePlan && m_context != nullptr) {
        m_context->setActivePlan(state.activePlan);
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(state.activePlan.approvalState);
        }
    } else {
        if (m_context != nullptr && m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        }
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
        }
    }

    updateSliceAssessmentMetrics();
    if (m_stagedSlices.isEmpty()) {
        resetActivePathWorkspace();
    } else {
        const int safeRow = qBound(0, state.currentStagedSliceIndex < 0 ? 0 : state.currentStagedSliceIndex, m_stagedSlices.size() - 1);
        if (m_modelList != nullptr) {
            const QSignalBlocker blocker(m_modelList);
            m_modelList->setCurrentRow(safeRow);
        }
        loadStagedSlice(safeRow);
    }

    if (m_assessmentPreview != nullptr && !state.assessmentText.trimmed().isEmpty()) {
        m_assessmentPreview->setPlainText(state.assessmentText);
    }
    if (m_planPreview != nullptr) {
        if (!state.planPreviewText.trimmed().isEmpty()) {
            m_planPreview->setPlainText(state.planPreviewText);
        } else {
            updatePlanPreviewText(state.hasActivePlan ? &state.activePlan : nullptr);
        }
    }
    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setVisible(state.annotationPanelExpanded);
    }
    if (m_annotationButton != nullptr) {
        m_annotationButton->setChecked(state.annotationPanelExpanded);
    }
    if (m_preview != nullptr) {
        m_preview->setAnnotationEnabled(state.annotationPanelExpanded);
    }
    updatePathActionState();
}

void PlanningPage::loadHistoricalImages(bool announce, bool forceReload)
{
    if (!m_context->hasSelectedPatient()) {
        clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        return;
    }

    const PatientRecord& patient = m_context->selectedPatient();
    if (!forceReload && patient.id == m_loadedHistoryPatientId) {
        if (m_historyImageSeries.isEmpty()) {
            clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        } else {
            loadHistoricalSlice(m_currentHistorySliceIndex >= 0 ? m_currentHistorySliceIndex : m_historyImageSeries.size() - 1, announce);
        }
        return;
    }

    m_historyLoadedFromLocalFiles = false;
    m_loadedHistoryPatientId = patient.id;
    m_historyImageSeries = m_clinicalDataService.listImageSeriesForPatient(patient.id);
    m_historyPixmaps.clear();
    std::sort(m_historyImageSeries.begin(), m_historyImageSeries.end(), [](const ImageSeriesRecord& left, const ImageSeriesRecord& right) {
        const QDateTime leftTime = left.createdAt.isValid() ? left.createdAt : QDateTime(left.acquisitionDate, QTime(0, 0));
        const QDateTime rightTime = right.createdAt.isValid() ? right.createdAt : QDateTime(right.acquisitionDate, QTime(0, 0));
        if (leftTime == rightTime) {
            return left.storagePath < right.storagePath;
        }
        return leftTime < rightTime;
    });

    if (m_historyImageSeries.isEmpty()) {
        clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        if (announce) {
            updateAcquisitionSummary(
                QStringLiteral("\u8bfb\u53d6\u56fe\u50cf"),
                {
                    QStringLiteral("\u60a3\u8005\uff1a%1").arg(patient.name),
                    QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u5df2\u5b58\u50a8\u7684\u5f71\u50cf\u6570\u636e\u3002")
                });
        }
        return;
    }

    m_historyPixmaps.reserve(m_historyImageSeries.size());
    for (const ImageSeriesRecord& image : m_historyImageSeries) {
        m_historyPixmaps.push_back(loadHistoryPixmap(image.storagePath));
    }

    if (m_historySliceSlider != nullptr) {
        const QSignalBlocker blocker(m_historySliceSlider);
        m_historySliceSlider->setRange(0, m_historyImageSeries.size() - 1);
        m_historySliceSlider->setEnabled(m_historyImageSeries.size() > 1);
        m_historySliceSlider->setValue(m_historyImageSeries.size() - 1);
    }

    loadHistoricalSlice(m_historyImageSeries.size() - 1, announce);
}

void PlanningPage::loadHistoricalFiles(const QStringList& filePaths)
{
    m_historyImageSeries.clear();
    m_historyPixmaps.clear();
    m_historyLoadedFromLocalFiles = true;
    m_loadedHistoryPatientId = m_context->hasSelectedPatient() ? m_context->selectedPatient().id : QStringLiteral("LOCAL");

    for (const QString& filePath : filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }

        ImageSeriesRecord image;
        image.id = fileInfo.completeBaseName();
        image.patientId = m_context->hasSelectedPatient() ? m_context->selectedPatient().id : QString();
        image.type = QStringLiteral("\u672c\u5730\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf");
        image.storagePath = fileInfo.absoluteFilePath();
        image.acquisitionDate = fileInfo.lastModified().date();
        image.createdAt = fileInfo.lastModified();
        m_historyImageSeries.push_back(image);
        m_historyPixmaps.push_back(loadHistoryPixmap(fileInfo.absoluteFilePath()));
    }

    if (m_historyImageSeries.isEmpty()) {
        clearHistoricalComparison(QStringLiteral("\u672a\u9009\u62e9\u53ef\u7528\u7684\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        return;
    }

    if (m_historySliceSlider != nullptr) {
        const QSignalBlocker blocker(m_historySliceSlider);
        m_historySliceSlider->setRange(0, m_historyImageSeries.size() - 1);
        m_historySliceSlider->setEnabled(m_historyImageSeries.size() > 1);
        m_historySliceSlider->setValue(0);
    }

    loadHistoricalSlice(0, true);
}

void PlanningPage::loadHistoricalSlice(int row, bool announce)
{
    if (m_historyImageSeries.isEmpty()) {
        clearHistoricalComparison(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        return;
    }

    const int safeRow = qBound(0, row, m_historyImageSeries.size() - 1);
    m_currentHistorySliceIndex = safeRow;

    if (m_historySliceSlider != nullptr && m_historySliceSlider->value() != safeRow) {
        const QSignalBlocker blocker(m_historySliceSlider);
        m_historySliceSlider->setValue(safeRow);
    }

    const ImageSeriesRecord& image = m_historyImageSeries.at(safeRow);
    const QString dateText = image.acquisitionDate.isValid()
        ? image.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd"))
        : QStringLiteral("\u65e5\u671f\u672a\u8bb0\u5f55");
    const QString pathText = image.storagePath.trimmed().isEmpty() ? QStringLiteral("\u672a\u914d\u7f6e\u8def\u5f84") : image.storagePath;
    const QPixmap pixmap = safeRow < m_historyPixmaps.size() ? m_historyPixmaps.at(safeRow) : QPixmap {};

    if (m_historyPreview != nullptr) {
        m_historyPreview->clearPlan();
        if (!pixmap.isNull()) {
            m_historyPreview->setBackgroundImage(pixmap);
        } else {
            m_historyPreview->clearBackgroundImage();
        }
        m_historyPreview->setCompletedPointCount(0);
        m_historyPreview->setSliceContext(safeRow, m_historyImageSeries.size());
        m_historyPreview->setCaption(QStringLiteral("\u65e2\u5f80\u6cbb\u7597 %1/%2").arg(safeRow + 1).arg(m_historyImageSeries.size()));
    }
    if (m_historyPreviewOverlayLabel != nullptr) {
        m_historyPreviewOverlayLabel->setText(pixmap.isNull() ? QStringLiteral("\u672a\u627e\u5230\u8fd9\u5f20\u5386\u53f2\u5f71\u50cf\u7684\u53ef\u9884\u89c8\u6587\u4ef6") : QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
        m_historyPreviewOverlayLabel->setVisible(pixmap.isNull());
    }
    if (m_historySliceSummaryLabel != nullptr) {
        m_historySliceSummaryLabel->setText(
            QStringLiteral("\u7b2c %1/%2 \u5f20 | %3 | %4")
                .arg(safeRow + 1)
                .arg(m_historyImageSeries.size())
                .arg(dateText)
                .arg(pathText));
        m_historySliceSummaryLabel->setToolTip(pathText);
    }

    if (announce) {
        updateAcquisitionSummary(
            QStringLiteral("\u5386\u53f2\u5f71\u50cf\u5bf9\u6bd4"),
            {
                QStringLiteral("\u60a3\u8005\uff1a%1").arg(m_context->hasSelectedPatient() ? m_context->selectedPatient().name : QStringLiteral("\u672a\u9009\u62e9")),
                QStringLiteral("\u5df2\u52a0\u8f7d\u5386\u53f2\u5f71\u50cf\uff1a%1 \u5f20").arg(m_historyImageSeries.size()),
                QStringLiteral("\u5f53\u524d\u67e5\u770b\uff1a%1 / %2").arg(safeRow + 1).arg(m_historyImageSeries.size()),
                QStringLiteral("\u5f53\u524d\u8def\u5f84\uff1a%1").arg(pathText),
                QStringLiteral("\u5de6\u5c4f\u53ef\u901a\u8fc7\u4e0b\u65b9\u6ed1\u52a8\u6761\u5207\u6362\u65e2\u5f80\u5f71\u50cf\u3002")
            });
    }
}

void PlanningPage::clearHistoricalComparison(const QString& overlayText)
{
    m_historyImageSeries.clear();
    m_historyPixmaps.clear();
    m_historyLoadedFromLocalFiles = false;
    m_currentHistorySliceIndex = -1;

    if (m_historyPreview != nullptr) {
        m_historyPreview->clearPlan();
        m_historyPreview->clearBackgroundImage();
        m_historyPreview->setCompletedPointCount(0);
        m_historyPreview->setSliceContext(0, 0);
        m_historyPreview->setCaption(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    }
    if (m_historyPreviewOverlayLabel != nullptr) {
        m_historyPreviewOverlayLabel->setText(overlayText);
        m_historyPreviewOverlayLabel->setVisible(true);
    }
    if (m_historySliceSlider != nullptr) {
        const QSignalBlocker blocker(m_historySliceSlider);
        m_historySliceSlider->setRange(0, 0);
        m_historySliceSlider->setValue(0);
        m_historySliceSlider->setEnabled(false);
    }
    if (m_historySliceSummaryLabel != nullptr) {
        m_historySliceSummaryLabel->setText(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf\uff1a\u6682\u65e0\u6570\u636e"));
        m_historySliceSummaryLabel->setToolTip(QString());
    }
}

void PlanningPage::addPathItem()
{
    activatePlanningWorkspace();
    const int nextIndex = m_pathList->count();
    m_pathList->addItem(createPathListItem(nextIndex));
    m_pathList->setCurrentRow(m_pathList->count() - 1);
    updatePathActionState();
}

void PlanningPage::removeCurrentPathItem()
{
    activatePlanningWorkspace();
    const int currentRow = m_pathList->currentRow();
    if (currentRow < 0) {
        return;
    }

    const QString removedPathKey = pathStateKeyForRow(currentRow);
    {
        const QSignalBlocker blocker(m_pathList);
        delete m_pathList->takeItem(currentRow);
        m_pathEditingStates.remove(removedPathKey);
        if (m_pathList->count() > 0) {
            m_pathList->setCurrentRow(std::min(currentRow, m_pathList->count() - 1));
        }
    }

    if (m_activePathStateKey == removedPathKey) {
        m_activePathStateKey.clear();
    }
    if (m_pathList->count() == 0) {
        if (m_context != nullptr && m_context->hasActivePlan()) {
            m_context->clearActivePlan();
        }
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
        }
        resetActivePathWorkspace();
        updatePathActionState();
        return;
    }
    if (m_pathList->currentRow() >= 0) {
        loadPathState(m_pathList->currentRow());
    }
    updatePathActionState();
}

void PlanningPage::simulateImageAcquisition()
{
    activatePlanningWorkspace();
    populateDefaultScanChannels();
    if (!hasActivePathSelection()) {
        updateAcquisitionSummary(
            QStringLiteral("\u56fe\u50cf\u901a\u9053\u91c7\u96c6"),
            {
                QStringLiteral("\u7cfb\u7edf\u542f\u52a8\u540e\u8def\u5f84\u5217\u8868\u9ed8\u8ba4\u4e3a\u7a7a\u3002"),
                QStringLiteral("\u8bf7\u5148\u70b9\u51fb\u201c\u65b0\u589e\u8def\u5f84\u201d\uff0c\u518d\u9488\u5bf9\u7b2c\u4e00\u6761\u91c7\u96c6\u8def\u5f84\u8fdb\u884c\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }

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
            .arg(formatStepSize(step));
        stagedSlice.createdAt = now;
        m_stagedImageSeries.push_back(stagedSlice);

        StagedSliceState stagedState;
        stagedState.image = stagedSlice;
        stagedState.label = QStringLiteral("[S%1] \u6682\u5b58\u5207\u7247-%2")
            .arg(layerIndex + 1, 2, 10, QChar('0'))
            .arg(layerIndex + 1, 2, 10, QChar('0'));
        stagedState.pattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
        stagedState.spacingMm = m_spacingSpin->value();
        stagedState.dwellSeconds = m_dwellSpin->value();
        stagedState.powerWatts = m_powerSlider->value();
        stagedState.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
        stagedState.deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
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
        m_preview->setSliceContext(0, 0);
        m_preview->setCaption(QStringLiteral(""));
    }

    m_lastAcquisitionAt = now;
    updateAssessmentMetricsPanel(0.0, 0.0);
    if (m_previewOverlayLabel != nullptr) {
        m_previewOverlayLabel->setVisible(m_stagedSlices.isEmpty() && !m_context->hasActivePlan());
    }
    updatePathActionState();

    updateAcquisitionSummary(
        QStringLiteral("\u56fe\u50cf\u91c7\u96c6\u5df2\u5b8c\u6210"),
        {
            QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(channelLabel),
            QStringLiteral("\u8d77\u59cb\u5750\u6807\uff1a%1").arg(channelCoordinate),
            QStringLiteral("\u5c42\u6570\uff1a%1").arg(layerCount),
            QStringLiteral("\u6b65\u957f\uff1a%1").arg(formatStepSize(step)),
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
    activatePlanningWorkspace();
    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (!hasActivePathSelection()) {
        updateAcquisitionSummary(
            QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u751f\u6210"),
            {
                QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u53ef\u7528\u7684\u91c7\u96c6\u8def\u5f84\u3002"),
                QStringLiteral("\u8bf7\u5148\u65b0\u589e\u8def\u5f84\uff0c\u518d\u6267\u884c\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }
    if (m_stagedSlices.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u751f\u6210"),
            {
                QStringLiteral("\u5f53\u524d\u8def\u5f84\u4e0a\u8fd8\u6ca1\u6709\u4e8c\u7ef4\u5207\u7247\u6570\u636e\u3002"),
                QStringLiteral("\u8bf7\u5148\u5b8c\u6210\u56fe\u50cf\u91c7\u96c6\uff0c\u518d\u70b9\u51fb\u201c\u4e09\u7ef4\u56fe\u50cf\u751f\u6210\u201d\u3002")
            });
        return;
    }

    QVector<VolumeContourSlice> contourSlices;
    contourSlices.reserve(m_stagedSlices.size());
    for (int index = 0; index < m_stagedSlices.size(); ++index) {
        const StagedSliceState& slice = m_stagedSlices.at(index);
        QVector<QPointF> contourMm = extractContourFromAnnotations(slice.annotations);
        const bool usesAnnotation = contourMm.size() >= 3;
        if (!usesAnnotation) {
            contourMm = buildFallbackLesionContourMm(index, m_stagedSlices.size());
        }

        VolumeContourSlice contourSlice;
        contourSlice.sliceIndex = index;
        contourSlice.derivedFromAnnotation = usesAnnotation;
        contourSlice.contourMm = contourMm;
        contourSlices.push_back(contourSlice);
    }

    const VolumeReconstructionResult reconstruction = buildVolumeReconstructionResult(
        contourSlices,
        std::max(1, m_stepSpin->value()),
        QSize(980, 620));
    if (!reconstruction.valid) {
        updateAcquisitionSummary(
            QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u751f\u6210"),
            {
                QStringLiteral("\u5f53\u524d\u5207\u7247\u8fd8\u65e0\u6cd5\u751f\u6210\u6709\u6548\u7684\u4e09\u7ef4\u4f53\u6570\u636e\u3002"),
                QStringLiteral("\u53ef\u5148\u5bf9\u5207\u7247\u8fdb\u884c\u80bf\u7624\u5708\u753b\uff0c\u6216\u91cd\u65b0\u6267\u884c\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }

    showThreeDimensionalPreviewDialog(reconstruction);

    QStringList lines {
        QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel()),
        QStringLiteral("\u8d77\u59cb\u5750\u6807\uff1a%1").arg(currentChannelCoordinate()),
        QStringLiteral("\u91cd\u5efa\u5207\u7247\uff1a%1 \u5f20").arg(reconstruction.sliceCount),
        QStringLiteral("\u4eba\u5de5\u6807\u6ce8\u5207\u7247\uff1a%1 \u5f20 | \u81ea\u52a8\u63a8\u65ad\u5207\u7247\uff1a%2 \u5f20")
            .arg(reconstruction.annotatedSliceCount)
            .arg(reconstruction.inferredSliceCount)
    };
    lines << reconstruction.summary.split(QLatin1Char('\n'));
    updateAcquisitionSummary(QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u751f\u6210"), lines);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("operator"),
            QStringLiteral("planning"),
            QStringLiteral("\u751f\u6210\u4e09\u7ef4\u56fe\u50cf\uff1a%1\uff0c\u5207\u7247 %2 \u5f20\uff0c\u4f53\u79ef %3 cm3")
                .arg(currentChannelLabel())
                .arg(reconstruction.sliceCount)
                .arg(reconstruction.estimatedVolumeCm3, 0, 'f', 2));
    }
}

void PlanningPage::showThreeDimensionalPreviewDialog(const VolumeReconstructionResult& result) const
{
    if (!result.valid) {
        return;
    }

    QDialog dialog(const_cast<PlanningPage*>(this));
    dialog.setWindowTitle(QStringLiteral("\u4e09\u7ef4\u91cd\u5efa\u9884\u89c8"));
    dialog.resize(1080, 760);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* headerLabel = new QLabel(QStringLiteral("\u5df2\u57fa\u4e8e\u5f53\u524d\u8def\u5f84\u4e0b\u7684\u4e8c\u7ef4\u5207\u7247\u8f6e\u5ed3\u751f\u6210\u4e09\u7ef4\u4f53\u9884\u89c8\u3002"));
    headerLabel->setWordWrap(true);

    auto* previewLabel = new QLabel();
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setPixmap(result.preview);

    auto* previewScrollArea = new QScrollArea();
    previewScrollArea->setWidgetResizable(true);
    previewScrollArea->setWidget(previewLabel);

    auto* summaryEdit = new QPlainTextEdit();
    summaryEdit->setReadOnly(true);
    summaryEdit->setMaximumHeight(130);
    summaryEdit->setPlainText(result.summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(headerLabel);
    layout->addWidget(previewScrollArea, 1);
    layout->addWidget(summaryEdit);
    layout->addWidget(buttons);

    dialog.exec();
}

void PlanningPage::previewCurrentPlan()
{
    activatePlanningWorkspace();
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient();
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    TherapyPlan previewPlan;
    if (hasGeneratedSliceTargets()) {
        const ApprovalState approvalState = m_context->hasActivePlan() ? m_context->activePlan().approvalState : ApprovalState::Draft;
        previewPlan = buildPlanFromSlices(approvalState);
    } else if (m_context->hasActivePlan()) {
        previewPlan = m_context->activePlan();
    } else {
        updateAcquisitionSummary(
            QStringLiteral("\u65b9\u6848\u9884\u89c8"),
            {
                QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u53ef\u9884\u89c8\u7684\u65b9\u6848\u3002"),
                QStringLiteral("\u8bf7\u5148\u5bf9\u91c7\u96c6\u5207\u7247\u751f\u6210\u9776\u70b9\uff0c\u6216\u9009\u62e9\u5df2\u5b58\u5728\u7684\u65b9\u6848\u3002")
            });
        return;
    }

    QStringList previewLines {
        summarizePlan(previewPlan),
        QString(),
        QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel()),
        QStringLiteral("\u5750\u6807\uff1a%1").arg(currentChannelCoordinate())
    };

    if (!m_stagedSlices.isEmpty()) {
        previewLines << QString() << QStringLiteral("\u5207\u7247\u603b\u89c8\uff1a");
        for (const StagedSliceState& slice : m_stagedSlices) {
            previewLines << QStringLiteral("%1 | %2 | \u7b14\u8ff9 %3 | \u9776\u70b9 %4 | \u529f\u7387 %5W | \u547c\u5438\u8ddf\u968f %6")
                .arg(slice.label)
                .arg(patternSummaryText(slice.pattern))
                .arg(slice.annotations.size())
                .arg(slice.targets.size())
                .arg(slice.powerWatts, 0, 'f', 0)
                .arg(slice.respiratoryTrackingEnabled ? QStringLiteral("\u5f00") : QStringLiteral("\u5173"));
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("\u6cbb\u7597\u65b9\u6848\u9884\u89c8"));
    dialog.resize(760, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* headerLabel = new QLabel(QStringLiteral("\u8be5\u5f39\u7a97\u6309\u8def\u5f84\u603b\u89c8\u6bcf\u5f20\u5f71\u50cf\u7684\u6cbb\u7597\u9009\u62e9\u4e0e\u9776\u70b9\u7ed3\u679c\u3002"));
    headerLabel->setWordWrap(true);
    auto* previewText = new QPlainTextEdit();
    previewText->setReadOnly(true);
    previewText->setPlainText(previewLines.join(QLatin1Char('\n')));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(headerLabel);
    layout->addWidget(previewText, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void PlanningPage::refreshDerivedMetrics()
{
    int pointCount = 0;
    if (hasGeneratedSliceTargets()) {
        for (const StagedSliceState& slice : m_stagedSlices) {
            pointCount += slice.targets.size();
        }
    } else {
        const int segmentCount = m_segmentedTreatmentRadio->isChecked() ? 2 : 1;
        pointCount = m_layerCountSpin->value() * 4 * segmentCount;
    }
    const double totalMinutes = (pointCount * m_dwellSpin->value()) / 60.0;

    m_totalDurationValueLabel->setText(QStringLiteral("%1 min").arg(totalMinutes, 0, 'f', 2));
    m_powerValueLabel->setText(QStringLiteral("%1W").arg(m_powerSlider->value()));
    refreshPowerCurve();
}

void PlanningPage::storeCapturedImages()
{
    activatePlanningWorkspace();
    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (m_stagedImageSeries.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u672c\u5730\u5b58\u50a8"),
            {
                QStringLiteral("\u5f53\u524d\u6ca1\u6709\u6682\u5b58\u56fe\u50cf\u53ef\u4fdd\u5b58\u3002"),
                QStringLiteral("\u8bf7\u5148\u5728\u5de6\u4fa7\u201c\u56fe\u50cf\u91c7\u96c6\u201d\u91cc\u5b8c\u6210\u4e00\u6b21\u91c7\u96c6\u3002")
            });
        return;
    }

    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("\u9009\u62e9\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u7684\u672c\u5730\u5b58\u50a8\u76ee\u5f55"),
        QDir::homePath());
    if (outputDirectory.trimmed().isEmpty()) {
        return;
    }

    const QString batchToken = m_lastAcquisitionAt.isValid()
        ? m_lastAcquisitionAt.toString(QStringLiteral("yyyyMMddhhmmss"))
        : QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss"));
    int savedCount = 0;
    for (int index = 0; index < m_stagedSlices.size(); ++index) {
        const QString filePath = QDir(outputDirectory).filePath(
            QStringLiteral("%1_%2_slice_%3.png")
                .arg(batchToken)
                .arg(currentChannelLabel().remove(QLatin1Char(' ')))
                .arg(index + 1, 3, 10, QChar('0')));
        const QPixmap pixmap = renderCurrentSlicePixmap(index, QSize(960, 720));
        if (pixmap.isNull() || !pixmap.save(filePath, "PNG")) {
            updateAcquisitionSummary(
                QStringLiteral("\u672c\u5730\u5b58\u50a8\u5931\u8d25"),
                {
                    QStringLiteral("\u5b58\u50a8\u76ee\u5f55\uff1a%1").arg(outputDirectory),
                    QStringLiteral("\u5df2\u5bfc\u51fa\uff1a%1 / %2").arg(savedCount).arg(m_stagedSlices.size()),
                    QStringLiteral("\u5931\u8d25\u6587\u4ef6\uff1a%1").arg(filePath)
                });
            return;
        }

        if (index < m_stagedImageSeries.size()) {
            m_stagedImageSeries[index].storagePath = filePath;
        }
        ++savedCount;
    }

    updateAcquisitionSummary(
        QStringLiteral("\u672c\u5730\u5b58\u50a8\u5b8c\u6210"),
        {
            QStringLiteral("\u5b58\u50a8\u76ee\u5f55\uff1a%1").arg(outputDirectory),
            QStringLiteral("\u5bfc\u51fa\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a%1 \u5f20").arg(savedCount),
            QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel()),
            QStringLiteral("\u8d77\u59cb\u5750\u6807\uff1a%1").arg(currentChannelCoordinate()),
            QStringLiteral("\u53ef\u5c06\u8fd9\u4e9b PNG \u56fe\u50cf\u518d\u901a\u8fc7\u201c\u8bfb\u53d6\u56fe\u50cf\u201d\u5bfc\u5165\u5de6\u5c4f\u4f5c\u4e3a\u65e2\u5f80\u6cbb\u7597\u5bf9\u6bd4\u3002")
        });
}

void PlanningPage::loadStoredImages()
{
    activatePlanningWorkspace();
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("\u9009\u62e9\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"),
        QDir::homePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (filePaths.isEmpty()) {
        return;
    }

    loadHistoricalFiles(filePaths);
}

}  // namespace panthera::modules
