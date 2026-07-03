#include "modules/planning/planning_page.h"

#include "modules/shared/system_sound_guard.h"
#include "modules/shared/ultrasound_geometry.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVector3D>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr int kPathStateKeyRole = Qt::UserRole + 1;
constexpr int kImageAcquisitionAxisNodeId = 7;
constexpr double kImageAcquisitionStepsPerMillimeter = 640.0;
constexpr int kImageAcquisitionMotorSpeed = 2400;
constexpr int kImageAcquisitionS2PositionSteps = 0;
constexpr int kImageAcquisitionS1PositionSteps = 74461;
constexpr int kImageAcquisitionMinimumPositionSteps = kImageAcquisitionS2PositionSteps;
constexpr int kImageAcquisitionMaximumPositionSteps = kImageAcquisitionS1PositionSteps;
constexpr int kImageAcquisitionReturnHomeToleranceSteps = 20;
constexpr int kImageAcquisitionMoveStartTimeoutMs = 3000;
constexpr int kImageAcquisitionStopPollMs = 200;
constexpr int kImageAcquisitionStopTimeoutMs = 90000;
constexpr int kImageAcquisitionStopStableToleranceSteps = 3;
constexpr int kImageAcquisitionStopStableSamples = 4;
constexpr int kImageAcquisitionCameraWarmupTimeoutMs = 2000;

QString imageAcquisitionOptionalIntText(bool hasValue, int value)
{
    return hasValue ? QString::number(value) : QStringLiteral("-");
}

QString imageAcquisitionBoolText(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

bool imageAcquisitionSnapshotPosition(
    const diji::adapters::uim::UimMotorSnapshot& snapshot,
    int* positionSteps,
    QString* errorMessage)
{
    if (snapshot.hasPosition) {
        if (positionSteps != nullptr) {
            *positionSteps = snapshot.position;
        }
        return true;
    }
    if (snapshot.hasEncoderPosition) {
        if (positionSteps != nullptr) {
            *positionSteps = snapshot.encoderPosition;
        }
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("无法读取 7 号电机绝对位置，POS/QEC 均无反馈");
    }
    return false;
}

bool imageAcquisitionSnapshotMoved(
    const diji::adapters::uim::UimMotorSnapshot& startSnapshot,
    const diji::adapters::uim::UimMotorSnapshot& currentSnapshot)
{
    if (startSnapshot.hasPosition && currentSnapshot.hasPosition
        && std::abs(currentSnapshot.position - startSnapshot.position) > kImageAcquisitionStopStableToleranceSteps) {
        return true;
    }
    if (startSnapshot.hasEncoderPosition && currentSnapshot.hasEncoderPosition
        && std::abs(currentSnapshot.encoderPosition - startSnapshot.encoderPosition) > kImageAcquisitionStopStableToleranceSteps) {
        return true;
    }
    return false;
}

int imageAcquisitionMotionPosition(
    const diji::adapters::uim::UimMotorSnapshot& startSnapshot,
    const diji::adapters::uim::UimMotorSnapshot& currentSnapshot,
    int fallbackPositionSteps)
{
    if (startSnapshot.hasPosition && currentSnapshot.hasPosition
        && std::abs(currentSnapshot.position - startSnapshot.position) > kImageAcquisitionStopStableToleranceSteps) {
        return currentSnapshot.position;
    }
    if (startSnapshot.hasEncoderPosition && currentSnapshot.hasEncoderPosition
        && std::abs(currentSnapshot.encoderPosition - startSnapshot.encoderPosition) > kImageAcquisitionStopStableToleranceSteps) {
        return currentSnapshot.encoderPosition;
    }
    int positionSteps = fallbackPositionSteps;
    imageAcquisitionSnapshotPosition(currentSnapshot, &positionSteps, nullptr);
    return positionSteps;
}

QString imageAcquisitionAxisStatusText(const diji::adapters::uim::UimMotorSnapshot& snapshot)
{
    QString sensorText = QStringLiteral("S1/S2/S3=-");
    if (snapshot.hasSensorFeedback) {
        sensorText = QStringLiteral("S1/S2/S3=%1/%2/%3")
            .arg(imageAcquisitionBoolText(snapshot.sensor1))
            .arg(imageAcquisitionBoolText(snapshot.sensor2))
            .arg(imageAcquisitionBoolText(snapshot.sensor3));
    }

    return QStringLiteral("ENA=%1, SPD=%2, STEP=%3, POS=%4, QEC=%5, %6")
        .arg(imageAcquisitionBoolText(snapshot.enabled))
        .arg(snapshot.speed)
        .arg(snapshot.step)
        .arg(imageAcquisitionOptionalIntText(snapshot.hasPosition, snapshot.position))
        .arg(imageAcquisitionOptionalIntText(snapshot.hasEncoderPosition, snapshot.encoderPosition))
        .arg(sensorText);
}

QString defaultImageAcquisitionSdkPath()
{
    const QStringList candidates {
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("UISimCanFunc.dll")),
        QStringLiteral("D:/PanSoftware/UIMDemo/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DIANJIDEMO2/build/mingw/apps/three_axis_motor/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DianJi/电机控制/UIMDemoNew/UIMDemo20170523/example/VC/UIMVCDemo/DLL/UISimCanFunc.dll"),
        QStringLiteral("D:/PanSoftware/DianJi/电机控制/UIMDemo20170523/example/VC/UIMVCDemo/DLL/UISimCanFunc.dll")
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(QFileInfo(candidate).absoluteFilePath());
        }
    }
    return QDir::toNativeSeparators(candidates.first());
}

QString collapseAllProfileName()
{
    return QStringLiteral("全折叠");
}

QString expandAllProfileName()
{
    return QStringLiteral("全展开");
}

bool isBuiltInPersonalizationProfile(const QString& profileName)
{
    const QString normalizedProfileName = profileName.trimmed();
    return normalizedProfileName == collapseAllProfileName() || normalizedProfileName == expandAllProfileName();
}

QString planningPersonalizationProfilesRoot()
{
    return QStringLiteral("ui/planningPersonalization/profiles");
}

QString planningPersonalizationActiveProfileKey()
{
    return QStringLiteral("ui/planningPersonalization/activeProfileName");
}

QString settingsProfileStorageKey(const QString& profileName)
{
    return QString::fromLatin1(profileName.trimmed().toUtf8().toHex());
}

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

QString sanitizedStorageToken(const QString& value, const QString& fallback)
{
    QString token = value.trimmed();
    if (token.isEmpty()) {
        token = fallback;
    }
    for (QChar& character : token) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }
    return token;
}

QString imageAcquisitionStorageDirectory(const QString& patientId, const QString& batchToken)
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("runtime/acquisitions/%1/%2")
            .arg(sanitizedStorageToken(patientId, QStringLiteral("anonymous")))
            .arg(sanitizedStorageToken(batchToken, QStringLiteral("batch"))));
}

QString imageAcquisitionSliceStoragePath(
    const QString& patientId,
    const QString& batchToken,
    int channelIndex,
    int sliceIndex,
    int axis7PositionSteps)
{
    return QDir(imageAcquisitionStorageDirectory(patientId, batchToken)).absoluteFilePath(
        QStringLiteral("channel_%1_slice_%2_axis7_%3.png")
            .arg(channelIndex + 1, 2, 10, QChar('0'))
            .arg(sliceIndex + 1, 3, 10, QChar('0'))
            .arg(axis7PositionSteps));
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

QString annotationAreaSummaryText(double areaMm2)
{
    if (areaMm2 <= 0.05) {
        return QStringLiteral("\u5708\u753b\u9762\u79ef\uff1a\u672a\u5f62\u6210\u6709\u6548\u95ed\u5408\u533a\u57df");
    }

    return QStringLiteral("\u5708\u753b\u9762\u79ef\uff1a%1 mm\u00b2 (%2 cm\u00b2)")
        .arg(areaMm2, 0, 'f', 1)
        .arg(areaMm2 / 100.0, 0, 'f', 2);
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

bool isPlanApprovedForTreatment(ApprovalState approvalState)
{
    return approvalState == ApprovalState::Approved || approvalState == ApprovalState::Locked;
}

QWidget* createSectionBody()
{
    auto* body = new QWidget();
    body->setObjectName(QStringLiteral("planningSectionBody"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    return body;
}

QLabel* createHeaderMarker(const QString& variant = QString())
{
    auto* marker = new QLabel(QStringLiteral("="));
    marker->setObjectName(QStringLiteral("planningHeaderIcon"));
    if (!variant.isEmpty()) {
        marker->setProperty("variant", variant);
    }
    marker->setAlignment(Qt::AlignCenter);
    marker->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return marker;
}

QLabel* createFormLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("planningFormLabel"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    label->setMinimumHeight(32);
    return label;
}

QLabel* createMetricValueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("planningMetricValueLabel"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setMinimumHeight(32);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return label;
}

QWidget* createMetricUnitField(QWidget* valueWidget, const QString& unitText)
{
    auto* field = new QWidget();
    field->setObjectName(QStringLiteral("planningMetricUnitField"));
    auto* layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    valueWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(valueWidget, 1);

    auto* unitLabel = new QLabel(unitText);
    unitLabel->setObjectName(QStringLiteral("planningMetricUnitLabel"));
    unitLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(unitLabel);
    return field;
}

QToolButton* createCollapseButton(QWidget* content, bool expanded = true)
{
    auto* button = new QToolButton();
    button->setObjectName(QStringLiteral("planningCollapseButton"));
    button->setCheckable(true);
    button->setChecked(expanded);
    button->setToolTip(QStringLiteral("\u5c55\u5f00/\u6536\u8d77\u529f\u80fd\u6846"));

    const auto updateButton = [button, content](bool checked) {
        if (content != nullptr) {
            content->setVisible(checked);
            for (QWidget* widget = content; widget != nullptr; widget = widget->parentWidget()) {
                widget->updateGeometry();
            }
        }
        button->setText(checked ? QStringLiteral("\u2303") : QStringLiteral("\u2304"));
    };
    updateButton(expanded);
    QObject::connect(button, &QToolButton::toggled, button, updateButton);
    return button;
}

QToolButton* createSliceNavButton(const QString& text, const QString& tooltip)
{
    auto* button = new QToolButton();
    button->setObjectName(QStringLiteral("planningSliceNavButton"));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setEnabled(false);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

void setAnnotationColorButtonsChecked(
    QToolButton* redButton,
    QToolButton* blueButton,
    QToolButton* greenButton,
    QToolButton* orangeButton,
    const QColor& color)
{
    const auto updateButton = [&color](QToolButton* button, const QColor& buttonColor) {
        if (button == nullptr) {
            return;
        }
        button->setCheckable(true);
        button->setChecked(buttonColor == color);
        button->update();
    };

    updateButton(redButton, QColor(201, 71, 51));
    updateButton(blueButton, QColor(91, 158, 230));
    updateButton(greenButton, QColor(163, 239, 76));
    updateButton(orangeButton, QColor(255, 177, 75));
}

void setSliderVisualState(QSlider* slider, const char* propertyName, bool active)
{
    if (slider == nullptr) {
        return;
    }
    if (slider->property(propertyName).toBool() == active) {
        return;
    }

    slider->setProperty(propertyName, active);
    slider->style()->unpolish(slider);
    slider->style()->polish(slider);
    slider->update();
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
    setObjectName(QStringLiteral("planningPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_rootLayout = new QHBoxLayout(this);
    m_rootLayout->setContentsMargins(12, 12, 12, 12);
    m_rootLayout->setSpacing(12);

    auto* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(10);
    leftColumn->setContentsMargins(10, 10, 10, 10);

    auto* pathCard = new QFrame();
    pathCard->setObjectName(QStringLiteral("planningSidebarCard"));
    pathCard->setMinimumWidth(292);
    pathCard->setMinimumHeight(62);
    pathCard->setMaximumHeight(330);
    pathCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* pathLayout = new QVBoxLayout(pathCard);
    pathLayout->setContentsMargins(12, 12, 12, 12);
    pathLayout->setSpacing(10);

    auto* pathHeader = new QHBoxLayout();
    auto* pathTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u901a\u9053\u91c7\u96c6\u5217\u8868"));
    pathTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* pathIcon = createHeaderMarker();
    pathHeader->addWidget(pathTitle);
    pathHeader->addStretch();
    pathHeader->addWidget(pathIcon);

    m_pathList = new QListWidget();
    m_pathList->setObjectName(QStringLiteral("planningPathList"));
    m_pathList->setMinimumHeight(180);
    m_pathList->viewport()->setObjectName(QStringLiteral("planningListViewport"));

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

    auto* pathBody = createSectionBody();
    auto* pathBodyLayout = new QVBoxLayout(pathBody);
    pathBodyLayout->setContentsMargins(0, 0, 0, 0);
    pathBodyLayout->setSpacing(10);
    pathBodyLayout->addWidget(m_pathList, 1);
    pathBodyLayout->addLayout(pathButtons);
    auto* pathCollapseButton = createCollapseButton(pathBody, false);
    pathHeader->addWidget(pathCollapseButton);
    registerCollapseSection(QStringLiteral("pathList"), pathCollapseButton);

    pathLayout->addLayout(pathHeader);
    pathLayout->addWidget(pathBody, 1);
    leftColumn->addWidget(pathCard);

    auto* captureCard = new QFrame();
    captureCard->setObjectName(QStringLiteral("planningSidebarCard"));
    captureCard->setMinimumWidth(292);
    captureCard->setMinimumHeight(62);
    captureCard->setMaximumHeight(240);
    captureCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* captureLayout = new QVBoxLayout(captureCard);
    captureLayout->setContentsMargins(12, 12, 12, 12);
    captureLayout->setSpacing(10);

    auto* captureHeader = new QHBoxLayout();
    auto* captureTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u91c7\u96c6"));
    captureTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* captureIcon = createHeaderMarker();
    captureHeader->addWidget(captureTitle);
    captureHeader->addStretch();
    captureHeader->addWidget(captureIcon);

    auto* captureForm = new QFormLayout();
    captureForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    captureForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    captureForm->setHorizontalSpacing(18);
    captureForm->setVerticalSpacing(12);
    captureForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

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
    m_totalLengthValueLabel = createMetricValueLabel(QStringLiteral("20"));

    captureForm->addRow(createFormLabel(QStringLiteral("\u5c42\u6570")), m_layerCountSpin);
    captureForm->addRow(createFormLabel(QStringLiteral("\u6b65\u957f")), createMetricUnitField(m_stepSpin, QStringLiteral("mm")));
    captureForm->addRow(createFormLabel(QStringLiteral("\u603b\u957f")), createMetricUnitField(m_totalLengthValueLabel, QStringLiteral("mm")));

    m_acquireImageButton = new QPushButton(QStringLiteral("> \u56fe\u50cf\u91c7\u96c6"));
    m_acquireImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_acquireImageButton->setMinimumHeight(38);

    auto* captureBody = createSectionBody();
    auto* captureBodyLayout = new QVBoxLayout(captureBody);
    captureBodyLayout->setContentsMargins(0, 0, 0, 0);
    captureBodyLayout->setSpacing(10);
    captureBodyLayout->addLayout(captureForm);
    captureBodyLayout->addWidget(m_acquireImageButton, 0, Qt::AlignRight);
    auto* captureCollapseButton = createCollapseButton(captureBody, false);
    captureHeader->addWidget(captureCollapseButton);
    registerCollapseSection(QStringLiteral("imageCapture"), captureCollapseButton);

    captureLayout->addLayout(captureHeader);
    captureLayout->addWidget(captureBody);
    leftColumn->addWidget(captureCard);

    auto* modelCard = new QFrame();
    modelCard->setObjectName(QStringLiteral("planningSidebarCard"));
    modelCard->setMinimumWidth(292);
    modelCard->setMinimumHeight(62);
    modelCard->setMaximumHeight(300);
    modelCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setContentsMargins(12, 12, 12, 12);
    modelLayout->setSpacing(10);

    m_generate3dButton = new QPushButton(QStringLiteral("+ \u4e09\u7ef4\u56fe\u50cf\u751f\u6210"));
    m_generate3dButton->setObjectName(QStringLiteral("planningPrimaryOutlineButton"));
    m_generate3dButton->setMinimumHeight(38);

    auto* modelHeader = new QHBoxLayout();
    auto* modelTitle = new QLabel(QStringLiteral("\u4e09\u7ef4\u56fe\u50cf\u5217\u8868"));
    modelTitle->setObjectName(QStringLiteral("planningCardTitle"));
    auto* modelIcon = createHeaderMarker();
    modelHeader->addWidget(modelTitle);
    modelHeader->addStretch();
    modelHeader->addWidget(modelIcon);

    m_modelList = new QListWidget();
    m_modelList->setObjectName(QStringLiteral("planningModelList"));
    m_modelList->setMinimumHeight(170);
    m_modelList->viewport()->setObjectName(QStringLiteral("planningListViewport"));

    auto* modelBody = createSectionBody();
    auto* modelBodyLayout = new QVBoxLayout(modelBody);
    modelBodyLayout->setContentsMargins(0, 0, 0, 0);
    modelBodyLayout->setSpacing(10);
    modelBodyLayout->addWidget(m_generate3dButton, 0, Qt::AlignLeft);
    modelBodyLayout->addWidget(m_modelList, 1);
    auto* modelCollapseButton = createCollapseButton(modelBody, false);
    modelHeader->addWidget(modelCollapseButton);
    registerCollapseSection(QStringLiteral("threeDimensionalModels"), modelCollapseButton);

    modelLayout->addLayout(modelHeader);
    modelLayout->addWidget(modelBody, 1);
    leftColumn->addWidget(modelCard);
    leftColumn->addStretch(1);

    m_leftColumnHost = new QWidget();
    m_leftColumnHost->setObjectName(QStringLiteral("planningLeftColumnHost"));
    m_leftColumnHost->setLayout(leftColumn);
    m_rootLayout->addWidget(m_leftColumnHost, 21);

    auto* centerColumn = new QVBoxLayout();
    centerColumn->setSpacing(10);
    centerColumn->setContentsMargins(0, 0, 0, 0);

    m_previewFrame = new QFrame();
    auto* previewFrame = m_previewFrame;
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
    m_historyPreview->setSyntheticImageEnabled(false);
    m_historyPreview->setImageZoomEnabled(true);
    m_historyPreview->setScaleRulerEnabled(true);
    m_historyPreview->setScaleRulerExpanded(true);

    m_historyPreviewOverlayLabel = new QLabel(QStringLiteral("\u5de6\u5c4f\u663e\u793a\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"));
    m_historyPreviewOverlayLabel->setObjectName(QStringLiteral("planningPreviewOverlayLabel"));
    m_historyPreviewOverlayLabel->setAlignment(Qt::AlignCenter);
    m_historyPreviewOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_historyMaximizeButton = new QToolButton();
    m_historyMaximizeButton->setObjectName(QStringLiteral("planningMaximizeButton"));
    m_historyMaximizeButton->setText(QString());
    m_historyMaximizeButton->setToolTip(QStringLiteral("\u91cd\u7f6e\u5de6\u4fa7\u65e2\u5f80\u5f71\u50cf\u7f29\u653e"));
    m_historyMaximizeButton->setEnabled(false);
    m_historyMaximizeButton->hide();

    historyStack->addWidget(m_historyPreview, 0, 0);
    historyStack->addWidget(m_historyPreviewOverlayLabel, 0, 0);
    historyStack->addWidget(m_historyMaximizeButton, 0, 0, Qt::AlignTop | Qt::AlignRight);
    historyLayout->addLayout(historyStack, 1);

    m_historySliceSummaryLabel = new QLabel(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf\uff1a\u6682\u65e0\u6570\u636e"));
    m_historySliceSummaryLabel->setObjectName(QStringLiteral("planningSliceInfoLabel"));
    m_historySliceSummaryLabel->setWordWrap(true);
    m_historySliceSlider = new QSlider(Qt::Horizontal);
    m_historySliceSlider->setObjectName(QStringLiteral("planningSliceSlider"));
    m_historySliceSlider->setRange(0, 0);
    m_historySliceSlider->setEnabled(false);
    m_historyPrevSliceButton = createSliceNavButton(QStringLiteral("\u2039"), QStringLiteral("\u5207\u6362\u5230\u4e0a\u4e00\u5f20\u65e2\u5f80\u5f71\u50cf"));
    m_historyNextSliceButton = createSliceNavButton(QStringLiteral("\u203a"), QStringLiteral("\u5207\u6362\u5230\u4e0b\u4e00\u5f20\u65e2\u5f80\u5f71\u50cf"));
    auto* historySliderRow = new QHBoxLayout();
    historySliderRow->setContentsMargins(0, 0, 0, 0);
    historySliderRow->setSpacing(6);
    historySliderRow->addWidget(m_historyPrevSliceButton);
    historySliderRow->addWidget(m_historySliceSlider, 1);
    historySliderRow->addWidget(m_historyNextSliceButton);
    historyLayout->addWidget(m_historySliceSummaryLabel);
    historyLayout->addLayout(historySliderRow);

    auto* currentPane = new QFrame();
    currentPane->setObjectName(QStringLiteral("planningComparePane"));
    auto* currentLayout = new QVBoxLayout(currentPane);
    currentLayout->setContentsMargins(4, 4, 4, 4);
    currentLayout->setSpacing(4);

    auto* currentHeader = new QHBoxLayout();
    currentHeader->setContentsMargins(0, 0, 0, 0);
    currentHeader->setSpacing(8);
    auto* currentTitle = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    currentTitle->setObjectName(QStringLiteral("planningPaneHeaderLabel"));
    currentHeader->addWidget(currentTitle);
    currentHeader->addStretch();
    m_currentMaximizeButton = new QToolButton();
    m_currentMaximizeButton->setObjectName(QStringLiteral("planningMaximizeButton"));
    m_currentMaximizeButton->setText(QStringLiteral("\u26f6"));
    m_currentMaximizeButton->setToolTip(QStringLiteral("\u5168\u5c4f\u67e5\u770b\u53f3\u4fa7\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    currentHeader->addWidget(m_currentMaximizeButton);
    currentLayout->addLayout(currentHeader);

    m_annotationPanel = new QFrame();
    m_annotationPanel->setObjectName(QStringLiteral("planningAnnotationPanel"));
    m_annotationPanel->setVisible(true);
    auto* annotationLayout = new QHBoxLayout(m_annotationPanel);
    annotationLayout->setContentsMargins(12, 10, 12, 10);
    annotationLayout->setSpacing(10);

    auto* annotationBrushButton = new QToolButton();
    annotationBrushButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    annotationBrushButton->setText(QStringLiteral("\u270e"));
    annotationLayout->addWidget(annotationBrushButton);

    auto* separatorTop = new QFrame();
    separatorTop->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    separatorTop->setFrameShape(QFrame::VLine);
    annotationLayout->addWidget(separatorTop);

    m_annotationRedButton = new QToolButton();
    m_annotationRedButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationRedButton->setProperty("swatchColor", QStringLiteral("red"));
    m_annotationRedButton->setToolTip(QStringLiteral("\u7ea2\u8272\u5708\u753b\u753b\u7b14"));
    annotationLayout->addWidget(m_annotationRedButton);

    m_annotationBlueButton = new QToolButton();
    m_annotationBlueButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationBlueButton->setProperty("swatchColor", QStringLiteral("blue"));
    m_annotationBlueButton->setToolTip(QStringLiteral("\u84dd\u8272\u5708\u753b\u753b\u7b14"));
    annotationLayout->addWidget(m_annotationBlueButton);

    m_annotationGreenButton = new QToolButton();
    m_annotationGreenButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationGreenButton->setProperty("swatchColor", QStringLiteral("green"));
    m_annotationGreenButton->setToolTip(QStringLiteral("\u7eff\u8272\u5708\u753b\u753b\u7b14"));
    annotationLayout->addWidget(m_annotationGreenButton);

    m_annotationOrangeButton = new QToolButton();
    m_annotationOrangeButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    m_annotationOrangeButton->setProperty("swatchColor", QStringLiteral("orange"));
    m_annotationOrangeButton->setToolTip(QStringLiteral("\u6a59\u8272\u5708\u753b\u753b\u7b14"));
    annotationLayout->addWidget(m_annotationOrangeButton);

    auto* separatorMiddle = new QFrame();
    separatorMiddle->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    separatorMiddle->setFrameShape(QFrame::VLine);
    annotationLayout->addWidget(separatorMiddle);

    m_annotationUndoButton = new QToolButton();
    m_annotationUndoButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    m_annotationUndoButton->setText(QStringLiteral("\u21b6"));
    annotationLayout->addWidget(m_annotationUndoButton);

    m_annotationClearButton = new QToolButton();
    m_annotationClearButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    m_annotationClearButton->setText(QStringLiteral("\U0001F5D1"));
    annotationLayout->addWidget(m_annotationClearButton);
    annotationLayout->addStretch();
    currentLayout->addWidget(m_annotationPanel);

    auto* previewStack = new QGridLayout();
    previewStack->setContentsMargins(0, 0, 0, 0);
    previewStack->setSpacing(0);
    m_preview = new MockUltrasoundView();
    m_preview->setObjectName(QStringLiteral("planningPreviewWidget"));
    m_preview->setMinimumSize(0, 0);
    m_preview->setCaption(QStringLiteral(""));
    m_preview->setSyntheticImageEnabled(false);
    m_preview->setImageZoomEnabled(true);
    m_preview->setScaleRulerEnabled(true);
    m_preview->setScaleRulerExpanded(true);
    m_preview->setAnnotationEnabled(false);
    setAnnotationEditingEnabled(false);

    m_previewOverlayLabel = new QLabel(QStringLiteral("\u56fe\u50cf\u663e\u793a\u533a\u57df"));
    m_previewOverlayLabel->setObjectName(QStringLiteral("planningPreviewOverlayLabel"));
    m_previewOverlayLabel->setAlignment(Qt::AlignCenter);
    m_previewOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    previewStack->addWidget(m_preview, 0, 0);
    previewStack->addWidget(m_previewOverlayLabel, 0, 0);
    currentLayout->addLayout(previewStack, 1);

    m_currentSliceSummaryLabel = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a\u7b49\u5f85\u91c7\u96c6\u6216\u9884\u89c8\u65b9\u6848"));
    m_currentSliceSummaryLabel->setObjectName(QStringLiteral("planningSliceInfoLabel"));
    m_currentSliceSummaryLabel->setWordWrap(true);
    m_currentSliceSlider = new QSlider(Qt::Horizontal);
    m_currentSliceSlider->setObjectName(QStringLiteral("planningSliceSlider"));
    m_currentSliceSlider->setRange(0, 0);
    m_currentSliceSlider->setEnabled(false);
    m_currentPrevSliceButton = createSliceNavButton(QStringLiteral("\u2039"), QStringLiteral("\u5207\u6362\u5230\u4e0a\u4e00\u5f20\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    m_currentNextSliceButton = createSliceNavButton(QStringLiteral("\u203a"), QStringLiteral("\u5207\u6362\u5230\u4e0b\u4e00\u5f20\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    auto* currentSliderRow = new QHBoxLayout();
    currentSliderRow->setContentsMargins(0, 0, 0, 0);
    currentSliderRow->setSpacing(6);
    currentSliderRow->addWidget(m_currentPrevSliceButton);
    currentSliderRow->addWidget(m_currentSliceSlider, 1);
    currentSliderRow->addWidget(m_currentNextSliceButton);
    currentLayout->addWidget(m_currentSliceSummaryLabel);
    currentLayout->addLayout(currentSliderRow);

    compareLayout->addWidget(historyPane, 1);
    compareLayout->addWidget(currentPane, 1);
    auto* compareContent = new QWidget();
    compareContent->setLayout(compareLayout);

    previewFrameLayout->addWidget(compareContent, 1);

    auto* comparisonSyncPanel = new QFrame();
    comparisonSyncPanel->setObjectName(QStringLiteral("planningComparisonSyncPanel"));
    auto* comparisonSyncLayout = new QHBoxLayout(comparisonSyncPanel);
    comparisonSyncLayout->setContentsMargins(10, 6, 10, 6);
    comparisonSyncLayout->setSpacing(8);

    m_comparisonSyncCheck = new QCheckBox(QStringLiteral("同步移动"));
    m_comparisonSyncCheck->setObjectName(QStringLiteral("planningComparisonSyncToggle"));
    m_comparisonSyncCheck->setChecked(false);
    comparisonSyncLayout->addWidget(m_comparisonSyncCheck);

    m_historySyncStartButton = new QPushButton(QStringLiteral("左起点"));
    m_historySyncStartButton->setObjectName(QStringLiteral("planningComparisonSyncButton"));
    m_historySyncStartButton->setToolTip(QStringLiteral("记录左侧既往影像当前切片为同步起点"));
    comparisonSyncLayout->addWidget(m_historySyncStartButton);

    m_historySyncEndButton = new QPushButton(QStringLiteral("左终点"));
    m_historySyncEndButton->setObjectName(QStringLiteral("planningComparisonSyncButton"));
    m_historySyncEndButton->setToolTip(QStringLiteral("记录左侧既往影像当前切片为同步终点"));
    comparisonSyncLayout->addWidget(m_historySyncEndButton);

    m_currentSyncStartButton = new QPushButton(QStringLiteral("右起点"));
    m_currentSyncStartButton->setObjectName(QStringLiteral("planningComparisonSyncButton"));
    m_currentSyncStartButton->setToolTip(QStringLiteral("记录右侧当前治疗影像当前切片为同步起点"));
    comparisonSyncLayout->addWidget(m_currentSyncStartButton);

    m_currentSyncEndButton = new QPushButton(QStringLiteral("右终点"));
    m_currentSyncEndButton->setObjectName(QStringLiteral("planningComparisonSyncButton"));
    m_currentSyncEndButton->setToolTip(QStringLiteral("记录右侧当前治疗影像当前切片为同步终点"));
    comparisonSyncLayout->addWidget(m_currentSyncEndButton);

    m_resetComparisonSyncButton = new QPushButton(QStringLiteral("重置"));
    m_resetComparisonSyncButton->setObjectName(QStringLiteral("planningComparisonSyncButton"));
    comparisonSyncLayout->addWidget(m_resetComparisonSyncButton);

    m_comparisonSyncSlider = new QSlider(Qt::Horizontal);
    m_comparisonSyncSlider->setObjectName(QStringLiteral("planningComparisonSyncSlider"));
    m_comparisonSyncSlider->setRange(0, 0);
    m_comparisonSyncSlider->setEnabled(false);
    m_comparisonSyncSlider->setToolTip(QStringLiteral("设置四个切片锚点后，拖动此滑条同比切换左右影像"));
    m_comparisonSyncSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_comparisonSyncPrevButton = new QToolButton();
    m_comparisonSyncPrevButton->setObjectName(QStringLiteral("planningComparisonSyncNavButton"));
    m_comparisonSyncPrevButton->setText(QStringLiteral("\u2039"));
    m_comparisonSyncPrevButton->setToolTip(QStringLiteral("\u540c\u6b65\u79fb\u52a8\u5230\u4e0a\u4e00\u7ec4\u5de6\u53f3\u5207\u7247"));
    m_comparisonSyncPrevButton->setEnabled(false);
    m_comparisonSyncPrevButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_comparisonSyncNextButton = new QToolButton();
    m_comparisonSyncNextButton->setObjectName(QStringLiteral("planningComparisonSyncNavButton"));
    m_comparisonSyncNextButton->setText(QStringLiteral("\u203a"));
    m_comparisonSyncNextButton->setToolTip(QStringLiteral("\u540c\u6b65\u79fb\u52a8\u5230\u4e0b\u4e00\u7ec4\u5de6\u53f3\u5207\u7247"));
    m_comparisonSyncNextButton->setEnabled(false);
    m_comparisonSyncNextButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    comparisonSyncLayout->addWidget(m_comparisonSyncPrevButton);
    comparisonSyncLayout->addWidget(m_comparisonSyncSlider, 1);
    comparisonSyncLayout->addWidget(m_comparisonSyncNextButton);

    previewFrameLayout->addWidget(comparisonSyncPanel);
    centerColumn->addWidget(previewFrame, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(12);

    auto* chartCard = new QFrame();
    chartCard->setObjectName(QStringLiteral("planningBottomCard"));
    chartCard->setMinimumWidth(300);
    chartCard->setMinimumHeight(54);
    chartCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    chartTitleRow->addWidget(createHeaderMarker());

    auto* chartSummaryRow = new QHBoxLayout();
    chartSummaryRow->setContentsMargins(0, 0, 0, 0);
    chartSummaryRow->addStretch();
    chartSummaryRow->addWidget(m_chartSummaryLabel);

    chartHeader->addLayout(chartTitleRow);

    m_energyOutputChart = new EnergyOutputChartWidget();
    auto* chartBody = createSectionBody();
    auto* chartBodyLayout = new QVBoxLayout(chartBody);
    chartBodyLayout->setContentsMargins(0, 0, 0, 0);
    chartBodyLayout->setSpacing(8);
    chartBodyLayout->addLayout(chartSummaryRow);
    chartBodyLayout->addWidget(m_energyOutputChart, 1);

    auto* chartCollapseButton = createCollapseButton(chartBody, false);
    chartTitleRow->addWidget(chartCollapseButton);
    registerCollapseSection(QStringLiteral("energyChart"), chartCollapseButton);

    chartLayout->addLayout(chartHeader);
    chartLayout->addWidget(chartBody, 1);
    bottomRow->addWidget(chartCard, 1);

    auto* statusCard = new QFrame();
    statusCard->setObjectName(QStringLiteral("planningBottomCard"));
    statusCard->setMinimumWidth(320);
    statusCard->setMinimumHeight(54);
    statusCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(18, 12, 18, 12);
    statusLayout->setSpacing(8);
    auto* statusHeader = new QHBoxLayout();
    statusHeader->setContentsMargins(0, 0, 0, 0);
    auto* statusTitle = new QLabel(QStringLiteral("\u65b9\u6848\u8be6\u60c5"));
    statusTitle->setObjectName(QStringLiteral("planningBottomTitle"));
    auto* statusBody = createSectionBody();
    auto* statusBodyLayout = new QVBoxLayout(statusBody);
    statusBodyLayout->setContentsMargins(0, 0, 0, 0);
    statusBodyLayout->setSpacing(0);
    statusHeader->addWidget(statusTitle);
    statusHeader->addStretch();
    statusHeader->addWidget(createHeaderMarker());
    auto* statusCollapseButton = createCollapseButton(statusBody, false);
    statusHeader->addWidget(statusCollapseButton);
    registerCollapseSection(QStringLiteral("planDetails"), statusCollapseButton);
    statusLayout->addLayout(statusHeader);
    statusLayout->addWidget(statusBody, 1);

    auto* imageOpsCard = new QFrame();
    imageOpsCard->setObjectName(QStringLiteral("planningBottomCard"));
    imageOpsCard->setMinimumWidth(220);
    imageOpsCard->setMaximumWidth(260);
    imageOpsCard->setMinimumHeight(54);
    imageOpsCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* imageOpsLayout = new QVBoxLayout(imageOpsCard);
    imageOpsLayout->setContentsMargins(18, 12, 18, 12);
    imageOpsLayout->setSpacing(16);
    auto* imageOpsTopSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    auto* imageOpsBottomSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);

    auto* imageOpsHeader = new QHBoxLayout();
    imageOpsHeader->setContentsMargins(0, 0, 0, 0);
    imageOpsHeader->setSpacing(8);
    auto* imageOpsTitle = new QLabel(QStringLiteral("\u56fe\u50cf\u64cd\u4f5c"));
    imageOpsTitle->setObjectName(QStringLiteral("planningBottomTitle"));
    auto* imageOpsIcon = createHeaderMarker();
    imageOpsHeader->addWidget(imageOpsTitle, 0, Qt::AlignTop);
    imageOpsHeader->addStretch();
    imageOpsHeader->addWidget(imageOpsIcon, 0, Qt::AlignTop);

    auto* imageOpsButtons = new QVBoxLayout();
    imageOpsButtons->setContentsMargins(0, 0, 0, 0);
    imageOpsButtons->setSpacing(10);
    m_storeImageButton = new QPushButton(QStringLiteral("\u672c\u5730\u5b58\u50a8"));
    m_storeImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_storeImageButton->setToolTip(QStringLiteral("\u5c06\u5f53\u524d\u8def\u5f84\u4e0a\u7684\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u5bfc\u51fa\u5230\u672c\u5730 PNG \u6587\u4ef6"));
    m_storeImageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_storeImageButton->setMinimumHeight(40);
    m_loadImageButton = new QPushButton(QStringLiteral("\u8bfb\u53d6\u56fe\u50cf"));
    m_loadImageButton->setObjectName(QStringLiteral("planningActionButton"));
    m_loadImageButton->setToolTip(QStringLiteral("\u4ece\u672c\u5730\u591a\u9009\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf\uff0c\u5e76\u5728\u5de6\u5c4f\u5bf9\u6bd4\u663e\u793a"));
    m_loadImageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_loadImageButton->setMinimumHeight(40);
    imageOpsButtons->addWidget(m_storeImageButton);
    imageOpsButtons->addWidget(m_loadImageButton);

    auto* imageOpsBody = createSectionBody();
    imageOpsBody->setObjectName(QStringLiteral("planningImageOpsBody"));
    imageOpsBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    imageOpsBody->setMinimumHeight(128);
    auto* imageOpsBodyLayout = new QVBoxLayout(imageOpsBody);
    imageOpsBodyLayout->setContentsMargins(0, 0, 0, 0);
    imageOpsBodyLayout->setSpacing(0);
    imageOpsBodyLayout->addStretch(1);
    imageOpsBodyLayout->addLayout(imageOpsButtons);
    imageOpsBodyLayout->addStretch(1);
    auto* imageOpsCollapseButton = createCollapseButton(imageOpsBody, false);
    imageOpsHeader->addWidget(imageOpsCollapseButton, 0, Qt::AlignTop);
    registerCollapseSection(QStringLiteral("imageOperations"), imageOpsCollapseButton);

    const auto updateImageOpsCollapseLayout = [imageOpsLayout, imageOpsCard, imageOpsTopSpacer, imageOpsBottomSpacer](bool expanded) {
        const QSizePolicy::Policy spacerPolicy = expanded ? QSizePolicy::Fixed : QSizePolicy::Expanding;
        imageOpsTopSpacer->changeSize(0, 0, QSizePolicy::Minimum, spacerPolicy);
        imageOpsBottomSpacer->changeSize(0, 0, QSizePolicy::Minimum, spacerPolicy);
        imageOpsLayout->setSpacing(expanded ? 16 : 0);
        imageOpsLayout->invalidate();
        imageOpsCard->updateGeometry();
    };
    updateImageOpsCollapseLayout(false);
    connect(imageOpsCollapseButton, &QToolButton::toggled, imageOpsCard, updateImageOpsCollapseLayout);

    imageOpsLayout->addItem(imageOpsTopSpacer);
    imageOpsLayout->addLayout(imageOpsHeader);
    imageOpsLayout->addWidget(imageOpsBody, 1);
    imageOpsLayout->addItem(imageOpsBottomSpacer);
    bottomRow->addWidget(imageOpsCard);
    bottomRow->addWidget(statusCard, 1);
    bottomRow->setStretch(0, 7);
    bottomRow->setStretch(1, 0);
    bottomRow->setStretch(2, 6);

    centerColumn->addLayout(bottomRow, 0);
    m_centerColumnHost = new QWidget();
    m_centerColumnHost->setObjectName(QStringLiteral("planningCenterColumnHost"));
    m_centerColumnHost->setLayout(centerColumn);
    m_rootLayout->addWidget(m_centerColumnHost, 55);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(0);
    rightColumn->setContentsMargins(0, 0, 0, 0);

    auto* controlsFrame = new QFrame();
    controlsFrame->setObjectName(QStringLiteral("planningControlFrame"));
    controlsFrame->setMinimumWidth(360);
    controlsFrame->setMaximumWidth(360);
    controlsFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    auto* controlsLayout = new QVBoxLayout(controlsFrame);
    controlsLayout->setContentsMargins(14, 14, 14, 14);
    controlsLayout->setSpacing(12);
    controlsLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* modeCard = new QFrame();
    modeCard->setObjectName(QStringLiteral("planningModeCard"));
    modeCard->setMinimumHeight(54);
    modeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(12, 10, 12, 10);
    modeLayout->setSpacing(8);
    auto* modeHeader = new QHBoxLayout();
    modeHeader->setContentsMargins(0, 0, 0, 0);
    auto* modeTitle = new QLabel(QStringLiteral("\u6cbb\u7597\u53c2\u6570"));
    modeTitle->setObjectName(QStringLiteral("planningSectionLabel"));
    auto* modeBody = createSectionBody();
    auto* modeBodyLayout = new QVBoxLayout(modeBody);
    modeBodyLayout->setContentsMargins(0, 0, 0, 0);
    modeBodyLayout->setSpacing(8);
    modeHeader->addWidget(modeTitle);
    modeHeader->addStretch();
    modeHeader->addWidget(createHeaderMarker(QStringLiteral("compact")));
    auto* modeCollapseButton = createCollapseButton(modeBody, false);
    modeHeader->addWidget(modeCollapseButton);
    registerCollapseSection(QStringLiteral("treatmentParameters"), modeCollapseButton);

    auto* executeRow = new QHBoxLayout();
    executeRow->setSpacing(18);
    m_directTreatmentRadio = new QRadioButton(QStringLiteral("\u76f4\u63a5\u6cbb\u7597"));
    m_segmentedTreatmentRadio = new QRadioButton(QStringLiteral("\u5206\u6bb5\u6267\u884c"), modeCard);
    m_directTreatmentRadio->setChecked(true);
    // Segmented execution is temporarily hidden from the planning page while
    // retaining the control and existing logic for future restoration.
    m_segmentedTreatmentRadio->setVisible(false);
    auto* deliveryModeGroup = new QButtonGroup(this);
    deliveryModeGroup->addButton(m_directTreatmentRadio);
    deliveryModeGroup->addButton(m_segmentedTreatmentRadio);
    executeRow->addWidget(m_directTreatmentRadio);

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
    m_spacingSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spacingSpin->setObjectName(QStringLiteral("planningMetricSpin"));
    m_spacingSpin->setMinimumWidth(132);

    m_dwellSpin = new QDoubleSpinBox();
    m_dwellSpin->setRange(0.1, 10.0);
    m_dwellSpin->setValue(0.3);
    m_dwellSpin->setDecimals(1);
    m_dwellSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_dwellSpin->setObjectName(QStringLiteral("planningMetricSpin"));
    m_dwellSpin->setMinimumWidth(132);

    m_totalDurationValueLabel = createMetricValueLabel(QStringLiteral("12.45"));

    metricsForm->addRow(createFormLabel(QStringLiteral("\u6cbb\u7597\u884c\u8ddd")), createMetricUnitField(m_spacingSpin, QStringLiteral("mm")));
    metricsForm->addRow(createFormLabel(QStringLiteral("\u70b9\u7597\u65f6\u957f")), createMetricUnitField(m_dwellSpin, QStringLiteral("s")));
    metricsForm->addRow(createFormLabel(QStringLiteral("\u6cbb\u7597\u603b\u65f6\u957f")), createMetricUnitField(m_totalDurationValueLabel, QStringLiteral("min")));
    m_generateTargetsButton = new QPushButton(QStringLiteral("\u4e00\u952e\u751f\u6210\u9776\u70b9"));
    m_generateTargetsButton->setObjectName(QStringLiteral("planningActionButton"));
    m_generateTargetsButton->setMinimumHeight(34);

    modeBodyLayout->addLayout(executeRow);
    modeBodyLayout->addLayout(patternRow);
    modeBodyLayout->addLayout(metricsForm);
    modeBodyLayout->addWidget(m_generateTargetsButton, 0, Qt::AlignLeft);
    modeLayout->addLayout(modeHeader);
    modeLayout->addWidget(modeBody);

    auto* powerCard = new QFrame();
    powerCard->setObjectName(QStringLiteral("planningModeCard"));
    powerCard->setMinimumHeight(54);
    powerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* powerCardLayout = new QVBoxLayout(powerCard);
    powerCardLayout->setContentsMargins(12, 10, 12, 10);
    powerCardLayout->setSpacing(10);
    auto* powerBody = createSectionBody();
    auto* powerBodyLayout = new QVBoxLayout(powerBody);
    powerBodyLayout->setContentsMargins(0, 0, 0, 0);
    powerBodyLayout->setSpacing(10);

    auto* powerTitle = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u529f\u7387"));
    powerTitle->setObjectName(QStringLiteral("planningSectionLabel"));

    auto* powerRow = new QHBoxLayout();
    m_powerValueLabel = new QLabel(QStringLiteral("400W"));
    m_powerValueLabel->setObjectName(QStringLiteral("planningPowerValueLabel"));
    powerRow->addWidget(powerTitle);
    powerRow->addStretch();
    powerRow->addWidget(m_powerValueLabel);
    powerRow->addWidget(createHeaderMarker(QStringLiteral("compact")));
    auto* powerCollapseButton = createCollapseButton(powerBody, false);
    powerRow->addWidget(powerCollapseButton);
    registerCollapseSection(QStringLiteral("powerAndAssessment"), powerCollapseButton);

    m_powerSpin = new QDoubleSpinBox();
    m_powerSpin->setRange(20.0, 800.0);
    m_powerSpin->setValue(400.0);
    m_powerSpin->setDecimals(0);
    m_powerSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_powerSpin->setObjectName(QStringLiteral("planningMetricSpin"));
    m_powerSpin->setMinimumWidth(132);

    m_powerSlider = new QSlider(Qt::Horizontal);
    m_powerSlider->setRange(20, 800);
    m_powerSlider->setValue(400);
    m_powerSlider->setObjectName(QStringLiteral("planningPowerSlider"));
    m_powerSlider->setVisible(false);

    auto* respiratoryRow = new QHBoxLayout();
    respiratoryRow->setContentsMargins(0, 0, 0, 0);
    respiratoryRow->setSpacing(12);
    auto* respiratoryTitle = new QLabel(QStringLiteral("\u547c\u5438\u8ddf\u968f\u72b6\u6001"));
    respiratoryTitle->setObjectName(QStringLiteral("planningRespiratoryTitleLabel"));
    respiratoryTitle->setMinimumHeight(36);
    respiratoryRow->addWidget(respiratoryTitle);
    respiratoryRow->addStretch();
    m_respiratoryTrackingCheck = new QCheckBox(QStringLiteral("\u547c\u5438\u8ddf\u968f"));
    m_respiratoryTrackingCheck->setObjectName(QStringLiteral("planningModeOption"));
    m_respiratoryTrackingCheck->setMinimumHeight(32);
    m_respiratoryTrackingCheck->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    respiratoryRow->addWidget(m_respiratoryTrackingCheck, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_generateAssessmentButton = new QPushButton(QStringLiteral("\u751f\u6210\u65b9\u6848\u8bc4\u4f30"));
    m_generateAssessmentButton->setObjectName(QStringLiteral("planningActionButton"));
    m_generateAssessmentButton->setMinimumHeight(34);

    powerBodyLayout->addWidget(createMetricUnitField(m_powerSpin, QStringLiteral("W")));
    powerBodyLayout->addWidget(m_powerSlider);
    powerBodyLayout->addLayout(respiratoryRow);
    powerBodyLayout->addWidget(m_generateAssessmentButton, 0, Qt::AlignLeft);
    powerCardLayout->addLayout(powerRow);
    powerCardLayout->addWidget(powerBody);

    auto* assessmentCard = new QFrame();
    assessmentCard->setObjectName(QStringLiteral("planningModeCard"));
    assessmentCard->setMinimumHeight(54);
    assessmentCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* assessmentLayout = new QVBoxLayout(assessmentCard);
    assessmentLayout->setContentsMargins(12, 10, 12, 10);
    assessmentLayout->setSpacing(8);

    auto* assessmentHeader = new QHBoxLayout();
    assessmentHeader->setContentsMargins(0, 0, 0, 0);
    auto* assessmentTitle = new QLabel(QStringLiteral("\u65b9\u6848\u8bc4\u4f30"));
    assessmentTitle->setObjectName(QStringLiteral("planningSectionLabel"));
    auto* assessmentBody = createSectionBody();
    auto* assessmentBodyLayout = new QVBoxLayout(assessmentBody);
    assessmentBodyLayout->setContentsMargins(0, 0, 0, 0);
    assessmentBodyLayout->setSpacing(8);
    assessmentHeader->addWidget(assessmentTitle);
    assessmentHeader->addStretch();
    assessmentHeader->addWidget(createHeaderMarker(QStringLiteral("compact")));
    auto* assessmentCollapseButton = createCollapseButton(assessmentBody, false);
    assessmentHeader->addWidget(assessmentCollapseButton);
    registerCollapseSection(QStringLiteral("assessmentMetrics"), assessmentCollapseButton);

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
    m_assessmentPreview->viewport()->setObjectName(QStringLiteral("planningSummaryViewport"));

    assessmentMetricsLayout->addLayout(plannedVolumeRow);
    assessmentMetricsLayout->addLayout(ablatedVolumeRow);
    assessmentMetricsLayout->addLayout(coverageRatioRow);
    assessmentMetricsLayout->addWidget(m_coverageProgressBar);
    assessmentBodyLayout->addWidget(assessmentMetricsCard);
    assessmentLayout->addLayout(assessmentHeader);
    assessmentLayout->addWidget(assessmentBody);
    statusBodyLayout->addWidget(m_assessmentPreview, 1);

    auto* planCard = new QFrame();
    planCard->setObjectName(QStringLiteral("planningModeCard"));
    planCard->setMinimumHeight(54);
    planCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* planCardLayout = new QVBoxLayout(planCard);
    planCardLayout->setContentsMargins(12, 10, 12, 10);
    planCardLayout->setSpacing(8);

    auto* planOpsHeader = new QHBoxLayout();
    auto* planOpsTitle = new QLabel(QStringLiteral("\u6cbb\u7597\u65b9\u6848\u64cd\u4f5c"));
    planOpsTitle->setObjectName(QStringLiteral("planningSectionLabel"));
    m_previewPlanButton = new QPushButton(QStringLiteral("\u9884\u89c8"));
    m_previewPlanButton->setObjectName(QStringLiteral("planningActionButton"));
    m_previewPlanButton->setToolTip(QStringLiteral("\u9884\u89c8\u5f53\u524d\u6cbb\u7597\u65b9\u6848"));
    m_previewPlanButton->setMinimumWidth(82);
    m_previewPlanButton->setMinimumHeight(34);
    m_editPlanButton = new QToolButton();
    m_editPlanButton->setObjectName(QStringLiteral("planningApprovalButton"));
    m_editPlanButton->setText(QStringLiteral("\u5ba1\u6838"));
    m_editPlanButton->setToolTip(QStringLiteral("\u5ba1\u6838\u5f53\u524d\u6cbb\u7597\u65b9\u6848"));
    m_editPlanButton->setMinimumSize(58, 34);
    planOpsHeader->addWidget(planOpsTitle);
    planOpsHeader->addStretch();
    planOpsHeader->addWidget(m_previewPlanButton);
    planOpsHeader->addWidget(m_editPlanButton);

    m_planPreview = new QPlainTextEdit(this);
    m_planPreview->setObjectName(QStringLiteral("planningSummaryEdit"));
    m_planPreview->setReadOnly(true);
    m_planPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_planPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_planPreview->setMinimumHeight(170);
    m_planPreview->setMaximumHeight(210);
    m_planPreview->viewport()->setObjectName(QStringLiteral("planningSummaryViewport"));
    m_planPreview->hide();

    m_patientSummaryLabel = new QLabel();
    m_patientSummaryLabel->setObjectName(QStringLiteral("planningContextLabel"));
    m_patientSummaryLabel->setWordWrap(true);
    m_patientSummaryLabel->hide();
    m_planSummaryLabel = new QLabel();
    m_planSummaryLabel->setObjectName(QStringLiteral("planningContextLabel"));
    m_planSummaryLabel->setWordWrap(true);
    m_planSummaryLabel->hide();

    planCardLayout->addLayout(planOpsHeader);

    controlsLayout->addWidget(modeCard);
    controlsLayout->addWidget(powerCard);
    controlsLayout->addWidget(assessmentCard);
    controlsLayout->addWidget(planCard);
    controlsLayout->addStretch();

    auto* controlsScroll = new QScrollArea();
    controlsScroll->setObjectName(QStringLiteral("planningControlScroll"));
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    controlsScroll->viewport()->setObjectName(QStringLiteral("planningControlScrollViewport"));
    controlsScroll->setWidget(controlsFrame);

    rightColumn->addWidget(controlsScroll);
    m_rightColumnHost = new QWidget();
    m_rightColumnHost->setObjectName(QStringLiteral("planningRightColumnHost"));
    m_rightColumnHost->setMinimumWidth(380);
    m_rightColumnHost->setMaximumWidth(380);
    m_rightColumnHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_rightColumnHost->setLayout(rightColumn);
    m_rootLayout->addWidget(m_rightColumnHost, 0);

    connect(m_addPathButton, &QPushButton::clicked, this, &PlanningPage::addPathItem);
    connect(m_removePathButton, &QPushButton::clicked, this, &PlanningPage::removeCurrentPathItem);
    connect(m_acquireImageButton, &QPushButton::clicked, this, &PlanningPage::simulateImageAcquisition);
    connect(m_pathList, &QListWidget::currentRowChanged, this, &PlanningPage::onPathSelectionChanged);
    connect(m_generate3dButton, &QPushButton::clicked, this, &PlanningPage::generateThreeDimensionalImage);
    connect(m_storeImageButton, &QPushButton::clicked, this, &PlanningPage::storeCapturedImages);
    connect(m_loadImageButton, &QPushButton::clicked, this, &PlanningPage::loadStoredImages);
    connect(m_historySliceSlider, &QSlider::valueChanged, this, [this](int value) {
        if (isComparisonSyncActive() && !m_applyingComparisonSync) {
            const QSignalBlocker blocker(m_historySliceSlider);
            m_historySliceSlider->setValue(m_currentHistorySliceIndex >= 0 ? m_currentHistorySliceIndex : m_historySliceSlider->minimum());
            return;
        }
        loadHistoricalSlice(value, true);
    });
    connect(m_historySliceSlider, &QSlider::rangeChanged, this, [this](int, int) {
        updateSliceNavigationButtons();
    });
    connect(m_historyPrevSliceButton, &QToolButton::clicked, this, [this]() {
        if (m_historySliceSlider != nullptr && m_historySliceSlider->isEnabled()) {
            m_historySliceSlider->setValue(std::max(m_historySliceSlider->minimum(), m_historySliceSlider->value() - 1));
        }
    });
    connect(m_historyNextSliceButton, &QToolButton::clicked, this, [this]() {
        if (m_historySliceSlider != nullptr && m_historySliceSlider->isEnabled()) {
            m_historySliceSlider->setValue(std::min(m_historySliceSlider->maximum(), m_historySliceSlider->value() + 1));
        }
    });
    connect(m_historyMaximizeButton, &QToolButton::clicked, this, &PlanningPage::showHistoryPreviewMaximized);
    connect(m_historyPreview, &MockUltrasoundView::imageZoomChanged, this, [this](qreal) {
        updateHistoryMaximizeButtonState();
    });
    connect(m_currentMaximizeButton, &QToolButton::clicked, this, &PlanningPage::showCurrentPreviewMaximized);
    connect(m_currentSliceSlider, &QSlider::valueChanged, this, [this](int value) {
        if (isComparisonSyncActive() && !m_applyingComparisonSync) {
            const QSignalBlocker blocker(m_currentSliceSlider);
            m_currentSliceSlider->setValue(m_currentStagedSliceIndex >= 0 ? m_currentStagedSliceIndex : m_currentSliceSlider->minimum());
            return;
        }
        if (m_modelList != nullptr && value < m_modelList->count() && m_modelList->currentRow() != value) {
            m_modelList->setCurrentRow(value);
            return;
        }
        loadStagedSlice(value);
    });
    connect(m_currentSliceSlider, &QSlider::rangeChanged, this, [this](int, int) {
        updateSliceNavigationButtons();
    });
    connect(m_currentPrevSliceButton, &QToolButton::clicked, this, [this]() {
        if (m_currentSliceSlider != nullptr && m_currentSliceSlider->isEnabled()) {
            m_currentSliceSlider->setValue(std::max(m_currentSliceSlider->minimum(), m_currentSliceSlider->value() - 1));
        }
    });
    connect(m_currentNextSliceButton, &QToolButton::clicked, this, [this]() {
        if (m_currentSliceSlider != nullptr && m_currentSliceSlider->isEnabled()) {
            m_currentSliceSlider->setValue(std::min(m_currentSliceSlider->maximum(), m_currentSliceSlider->value() + 1));
        }
    });
    connect(m_modelList, &QListWidget::currentRowChanged, this, &PlanningPage::onStagedSliceSelectionChanged);
    connect(m_preview, &MockUltrasoundView::annotationStrokesChanged, this, &PlanningPage::onPreviewAnnotationsChanged);
    connect(m_comparisonSyncCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && !hasComparisonSyncCalibration()) {
            const QSignalBlocker blocker(m_comparisonSyncCheck);
            m_comparisonSyncCheck->setChecked(false);
            enabled = false;
        }
        updateComparisonSyncState();
        if (enabled && m_comparisonSyncSlider != nullptr) {
            if (m_comparisonSyncSlider->value() != 0) {
                m_comparisonSyncSlider->setValue(0);
            } else {
                applyComparisonSyncSliderValue(0);
            }
        }
    });
    connect(m_historySyncStartButton, &QPushButton::clicked, this, [this]() {
        markComparisonSyncAnchor(true, true);
    });
    connect(m_historySyncEndButton, &QPushButton::clicked, this, [this]() {
        markComparisonSyncAnchor(true, false);
    });
    connect(m_currentSyncStartButton, &QPushButton::clicked, this, [this]() {
        markComparisonSyncAnchor(false, true);
    });
    connect(m_currentSyncEndButton, &QPushButton::clicked, this, [this]() {
        markComparisonSyncAnchor(false, false);
    });
    connect(m_resetComparisonSyncButton, &QPushButton::clicked, this, &PlanningPage::resetComparisonSyncCalibration);
    connect(m_comparisonSyncSlider, &QSlider::valueChanged, this, &PlanningPage::applyComparisonSyncSliderValue);
    connect(m_comparisonSyncPrevButton, &QToolButton::clicked, this, [this]() {
        if (m_comparisonSyncSlider != nullptr && m_comparisonSyncSlider->isEnabled()) {
            m_comparisonSyncSlider->setValue(std::max(m_comparisonSyncSlider->minimum(), m_comparisonSyncSlider->value() - 1));
        }
    });
    connect(m_comparisonSyncNextButton, &QToolButton::clicked, this, [this]() {
        if (m_comparisonSyncSlider != nullptr && m_comparisonSyncSlider->isEnabled()) {
            m_comparisonSyncSlider->setValue(std::min(m_comparisonSyncSlider->maximum(), m_comparisonSyncSlider->value() + 1));
        }
    });
    connect(m_generateTargetsButton, &QPushButton::clicked, this, &PlanningPage::generateTargetsForCurrentSlice);
    connect(m_generateAssessmentButton, &QPushButton::clicked, this, &PlanningPage::generateAssessmentForCurrentPlan);
    connect(m_previewPlanButton, &QPushButton::clicked, this, &PlanningPage::previewCurrentPlan);
    connect(m_editPlanButton, &QToolButton::clicked, this, &PlanningPage::approveCurrentPlan);
    connect(m_respiratoryTrackingCheck, &QCheckBox::toggled, this, &PlanningPage::onRespiratoryTrackingToggled);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, [this](const PatientRecord&) {
        const bool refreshHistoricalImages = !m_suppressNextPatientHistoryRefresh;
        m_suppressNextPatientHistoryRefresh = false;
        if (m_deferStartupContextSummary) {
            if (m_context->hasSelectedPatient()) {
                syncPatientSelector(m_context->selectedPatient().id);
            }
            return;
        }
        refreshContextSummary(refreshHistoricalImages);
    });
    connect(m_context, &ApplicationContext::activePlanChanged, this, [this](const TherapyPlan& plan) {
        if (m_deferStartupContextSummary) {
            return;
        }
        applyPlanToUi(plan);
        refreshContextSummary(false);
    });
    connect(m_context, &ApplicationContext::activePlanCleared, this, [this]() {
        if (m_deferStartupContextSummary) {
            return;
        }
        refreshContextSummary(false);
    });
    connect(m_context, &ApplicationContext::treatmentLayerVisualizationRequested, this, &PlanningPage::showTreatmentComparisonLayer);

    const auto refreshMetrics = [this]() {
        refreshDerivedMetrics();
    };
    connect(m_spacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, refreshMetrics](double value) {
        const bool hasInvalidatedTargets = std::any_of(m_stagedSlices.cbegin(), m_stagedSlices.cend(), [value](const StagedSliceState& slice) {
            return slice.targetsGenerated && std::abs(slice.spacingMm - value) > 0.001;
        });
        applyCurrentControlsToAllSlices();
        if (hasInvalidatedTargets && !m_initializingUi) {
            invalidateAllSliceTargets(
                QStringLiteral("\u6cbb\u7597\u884c\u8ddd\u8c03\u6574"),
                QStringLiteral("\u5df2\u66f4\u65b0\u5168\u90e8\u5207\u7247\u7684\u884c\u8ddd\uff0c\u65e7\u9776\u70b9\u5df2\u5931\u6548\uff0c\u8bf7\u91cd\u65b0\u70b9\u51fb\u201c\u4e00\u952e\u751f\u6210\u9776\u70b9\u201d\u3002"));
        }
        refreshMetrics();
    });
    connect(m_dwellSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [refreshMetrics](double) { refreshMetrics(); });
    connect(m_layerCountSpin, qOverload<int>(&QSpinBox::valueChanged), this, [refreshMetrics](int) { refreshMetrics(); });
    connect(m_stepSpin, qOverload<int>(&QSpinBox::valueChanged), this, [refreshMetrics](int) { refreshMetrics(); });
    const auto onPatternModeToggled = [this, refreshMetrics](bool checked) {
        refreshMetrics();
        if (!checked || m_initializingUi) {
            return;
        }
        if (m_stagedSlices.isEmpty()) {
            applyCurrentControlsToAllSlices();
            return;
        }

        const TreatmentPattern selectedPattern =
            m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
        const bool hasInvalidatedTargets = std::any_of(m_stagedSlices.cbegin(), m_stagedSlices.cend(), [selectedPattern](const StagedSliceState& slice) {
            return slice.targetsGenerated && slice.pattern != selectedPattern;
        });
        if (!hasInvalidatedTargets) {
            applyCurrentControlsToAllSlices();
            return;
        }

        applyCurrentControlsToAllSlices();
        invalidateAllSliceTargets(
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
        updateAnnotationColorButtonSelection(color);
    };
    connect(m_annotationRedButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(201, 71, 51)); });
    connect(m_annotationBlueButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(91, 158, 230)); });
    connect(m_annotationGreenButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(163, 239, 76)); });
    connect(m_annotationOrangeButton, &QToolButton::clicked, this, [activateColor]() { activateColor(QColor(255, 177, 75)); });
    connect(m_annotationUndoButton, &QToolButton::clicked, m_preview, &MockUltrasoundView::undoLastAnnotation);
    connect(m_annotationClearButton, &QToolButton::clicked, m_preview, &MockUltrasoundView::clearAnnotations);
    updateAnnotationColorButtonSelection(m_activeAnnotationColor);

    if (m_simulationDevice != nullptr) {
        m_latestDeviceSnapshot = m_simulationDevice->latestSnapshot();
        m_hasDeviceSnapshot = true;
        connect(m_simulationDevice, &adapters::SimulationDeviceFacade::snapshotUpdated, this, &PlanningPage::onDeviceSnapshotUpdated);
    }

    populatePatientSelector();
    refreshImagingPaths(QString());
    refreshDerivedMetrics();
    clearStartupDisplay();
    updateComparisonSyncState();
    m_activePathStateKey = pathStateKeyForRow(m_pathList != nullptr ? m_pathList->currentRow() : -1);
    restoreLastPersonalizationProfile();
    m_initializingUi = false;
    QTimer::singleShot(0, this, [this]() {
        m_deferStartupContextSummary = false;
    });
}

QStringList PlanningPage::personalizationProfileNames() const
{
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.beginGroup(planningPersonalizationProfilesRoot());
    const QStringList storageKeys = settings.childGroups();
    settings.endGroup();

    QStringList profileNames;
    profileNames.reserve(storageKeys.size());
    for (const QString& storageKey : storageKeys) {
        settings.beginGroup(planningPersonalizationProfilesRoot() + QStringLiteral("/") + storageKey);
        const QString displayName = settings.value(QStringLiteral("displayName")).toString().trimmed();
        settings.endGroup();
        if (!displayName.isEmpty()) {
            profileNames.push_back(displayName);
        }
    }

    profileNames.sort(Qt::CaseInsensitive);
    return profileNames;
}

QString PlanningPage::activePersonalizationProfileName() const
{
    return m_activePersonalizationProfileName;
}

void PlanningPage::applyPersonalizationProfile(const QString& profileName)
{
    const QString normalizedProfileName = profileName.trimmed();
    if (normalizedProfileName.isEmpty()) {
        return;
    }

    QVariantMap stateMap;
    if (normalizedProfileName == collapseAllProfileName()) {
        for (auto it = m_collapseButtonsByKey.cbegin(); it != m_collapseButtonsByKey.cend(); ++it) {
            stateMap.insert(it.key(), false);
        }
    } else if (normalizedProfileName == expandAllProfileName()) {
        for (auto it = m_collapseButtonsByKey.cbegin(); it != m_collapseButtonsByKey.cend(); ++it) {
            stateMap.insert(it.key(), true);
        }
    } else {
        stateMap = loadSavedCollapseStateMap(normalizedProfileName);
    }

    if (stateMap.isEmpty() && normalizedProfileName != collapseAllProfileName() && normalizedProfileName != expandAllProfileName()) {
        return;
    }

    applyCollapseStateMap(stateMap);
    setActivePersonalizationProfileName(normalizedProfileName);
}

bool PlanningPage::saveCurrentPersonalizationProfile(const QString& profileName)
{
    const QString normalizedProfileName = profileName.trimmed();
    if (normalizedProfileName.isEmpty() || isBuiltInPersonalizationProfile(normalizedProfileName)) {
        return false;
    }

    const QString storageKey = settingsProfileStorageKey(normalizedProfileName);
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.beginGroup(planningPersonalizationProfilesRoot() + QStringLiteral("/") + storageKey);
    settings.remove(QString());
    settings.setValue(QStringLiteral("displayName"), normalizedProfileName);

    const QVariantMap stateMap = currentCollapseStateMap();
    for (auto it = stateMap.cbegin(); it != stateMap.cend(); ++it) {
        settings.setValue(QStringLiteral("states/%1").arg(it.key()), it.value().toBool());
    }
    settings.endGroup();
    settings.sync();

    setActivePersonalizationProfileName(normalizedProfileName);
    return true;
}

bool PlanningPage::deletePersonalizationProfile(const QString& profileName)
{
    const QString normalizedProfileName = profileName.trimmed();
    if (normalizedProfileName.isEmpty() || isBuiltInPersonalizationProfile(normalizedProfileName)) {
        return false;
    }

    const QString storageKey = settingsProfileStorageKey(normalizedProfileName);
    const QString profileRoot = planningPersonalizationProfilesRoot();
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.beginGroup(profileRoot);
    const bool profileExists = settings.childGroups().contains(storageKey);
    settings.endGroup();
    if (!profileExists) {
        return false;
    }

    settings.remove(profileRoot + QStringLiteral("/") + storageKey);
    if (m_activePersonalizationProfileName == normalizedProfileName
        || settings.value(planningPersonalizationActiveProfileKey()).toString().trimmed() == normalizedProfileName) {
        clearActivePersonalizationProfileName();
    }
    settings.sync();
    return true;
}

void PlanningPage::registerCollapseSection(const QString& key, QToolButton* button)
{
    if (key.trimmed().isEmpty() || button == nullptr) {
        return;
    }

    m_collapseButtonsByKey.insert(key, button);
    connect(button, &QToolButton::toggled, this, [this]() {
        if (m_applyingPersonalizationProfile) {
            return;
        }
        const QString activeProfileName = m_activePersonalizationProfileName.trimmed();
        if (!activeProfileName.isEmpty() && !isBuiltInPersonalizationProfile(activeProfileName)) {
            saveCurrentPersonalizationProfile(activeProfileName);
            return;
        }
        clearActivePersonalizationProfileName();
    });
}

QVariantMap PlanningPage::currentCollapseStateMap() const
{
    QVariantMap stateMap;
    for (auto it = m_collapseButtonsByKey.cbegin(); it != m_collapseButtonsByKey.cend(); ++it) {
        stateMap.insert(it.key(), it.value() != nullptr && it.value()->isChecked());
    }
    return stateMap;
}

void PlanningPage::applyCollapseStateMap(const QVariantMap& stateMap)
{
    m_applyingPersonalizationProfile = true;
    for (auto it = m_collapseButtonsByKey.cbegin(); it != m_collapseButtonsByKey.cend(); ++it) {
        QToolButton* button = it.value();
        if (button == nullptr || !stateMap.contains(it.key())) {
            continue;
        }
        const bool expanded = stateMap.value(it.key()).toBool();
        if (button->isChecked() != expanded) {
            button->setChecked(expanded);
        }
    }
    m_applyingPersonalizationProfile = false;
}

QVariantMap PlanningPage::loadSavedCollapseStateMap(const QString& profileName) const
{
    const QString normalizedProfileName = profileName.trimmed();
    if (normalizedProfileName.isEmpty()) {
        return {};
    }

    QVariantMap stateMap;
    const QString storageKey = settingsProfileStorageKey(normalizedProfileName);
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.beginGroup(planningPersonalizationProfilesRoot() + QStringLiteral("/") + storageKey + QStringLiteral("/states"));
    const QStringList sectionKeys = settings.childKeys();
    for (const QString& sectionKey : sectionKeys) {
        stateMap.insert(sectionKey, settings.value(sectionKey).toBool());
    }
    settings.endGroup();
    return stateMap;
}

void PlanningPage::setActivePersonalizationProfileName(const QString& profileName)
{
    m_activePersonalizationProfileName = profileName.trimmed();
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.setValue(planningPersonalizationActiveProfileKey(), m_activePersonalizationProfileName);
    settings.sync();
}

void PlanningPage::clearActivePersonalizationProfileName()
{
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    if (m_activePersonalizationProfileName.isEmpty() && !settings.contains(planningPersonalizationActiveProfileKey())) {
        return;
    }

    m_activePersonalizationProfileName.clear();
    settings.remove(planningPersonalizationActiveProfileKey());
    settings.sync();
}

void PlanningPage::restoreLastPersonalizationProfile()
{
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    const QString profileName = settings.value(planningPersonalizationActiveProfileKey(), collapseAllProfileName()).toString().trimmed();
    if (profileName.isEmpty()) {
        return;
    }
    applyPersonalizationProfile(profileName);
}

void PlanningPage::loadDemoPatient(bool refreshHistoricalImages)
{
    const auto selectPatient = [this, refreshHistoricalImages](const PatientRecord& patient) {
        m_suppressNextPatientHistoryRefresh = !refreshHistoricalImages;
        m_context->selectPatient(patient);
        m_suppressNextPatientHistoryRefresh = false;
        if (m_safetyKernel != nullptr) {
            m_safetyKernel->setPatientSelected(true);
        }
    };

    if (m_clinicalDataRepository != nullptr && m_patientCombo->count() > 0) {
        PatientRecord patient;
        const QString patientId = m_patientCombo->currentData().toString();
        if (m_clinicalDataRepository->findPatientById(patientId, &patient)) {
            selectPatient(patient);
            return;
        }
    }

    const PatientRecord fallbackPatient = buildFallbackPatient();
    selectPatient(fallbackPatient);
}

void PlanningPage::generateDraftPlan()
{
    activatePlanningWorkspace();
    if (!m_context->hasSelectedPatient()) {
        loadDemoPatient(false);
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
        loadDemoPatient(false);
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    if (m_stagedSlices.isEmpty()) {
        updateAcquisitionSummary(
            QStringLiteral("\u751f\u6210\u9776\u70b9"),
            {
                QStringLiteral("\u5f53\u524d\u8fd8\u6ca1\u6709\u53ef\u7f16\u8f91\u7684\u91c7\u96c6\u5207\u7247\u3002"),
                QStringLiteral("\u8bf7\u5148\u5bf9\u67d0\u4e00\u6761\u8def\u5f84\u6267\u884c\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }

    applyCurrentControlsToAllSlices();

    int generatedSliceCount = 0;
    int totalTargetCount = 0;
    QStringList generatedSliceLines;
    QStringList skippedSliceLines;
    for (int index = 0; index < m_stagedSlices.size(); ++index) {
        StagedSliceState& slice = m_stagedSlices[index];
        if (slice.annotations.isEmpty()) {
            skippedSliceLines.push_back(QStringLiteral("%1 | \u65e0\u589e\u8bb0\u7b14\u8ff9").arg(slice.label));
            continue;
        }

        const QRectF bounds = annotationRegionBoundsMm(slice.annotations);
        if (!bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            skippedSliceLines.push_back(QStringLiteral("%1 | \u589e\u8bb0\u533a\u57df\u65e0\u6548").arg(slice.label));
            continue;
        }

        slice.targets.clear();
        slice.targets = generateTherapyTargetsFromAnnotations(
            slice.annotations,
            slice.pattern,
            slice.spacingMm,
            slice.dwellSeconds,
            slice.powerWatts);

        slice.annotatedAreaMm2 = annotationRegionAreaMm2(slice.annotations);
        slice.estimatedVolumeCm3 = (slice.annotatedAreaMm2 * std::max(1, m_stepSpin->value())) / 1000.0;
        const double ablationFactor = slice.pattern == TreatmentPattern::Line ? 0.82 : 0.62;
        slice.ablatedVolumeCm3 = std::min(
            slice.estimatedVolumeCm3,
            (slice.targets.size() * slice.spacingMm * std::max(1, m_stepSpin->value()) * slice.spacingMm * ablationFactor) / 1000.0);
        slice.edited = true;
        slice.targetsGenerated = true;
        recalculateRespiratoryTrackingForSlice(index);

        ++generatedSliceCount;
        totalTargetCount += slice.targets.size();
        generatedSliceLines.push_back(
            QStringLiteral("%1 | %2 | %3 | %4")
                .arg(slice.label)
                .arg(patternSummaryText(slice.pattern))
                .arg(annotationAreaSummaryText(slice.annotatedAreaMm2))
                .arg(targetSummaryText(slice.pattern, slice.targets)));
    }

    if (generatedSliceCount == 0) {
        updateAcquisitionSummary(
            QStringLiteral("\u751f\u6210\u9776\u70b9"),
            {
                QStringLiteral("\u5f53\u524d\u6240\u6709\u5207\u7247\u90fd\u8fd8\u6ca1\u6709\u53ef\u7528\u7684\u589e\u8bb0\u7b14\u8ff9\u3002"),
                QStringLiteral("\u8bf7\u5148\u5728\u53f3\u4fa7\u591a\u5f20\u5f71\u50cf\u4e0a\u5708\u753b\u80bf\u7624\u533a\u57df\uff0c\u518d\u70b9\u51fb\u201c\u4e00\u952e\u751f\u6210\u9776\u70b9\u201d\u3002")
            });
        return;
    }

    refreshCurrentSliceVisualization();
    updateSliceAssessmentMetrics();

    TherapyPlan draftPlan = buildPlanFromSlices(ApprovalState::Draft);
    m_context->setActivePlan(draftPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(draftPlan.approvalState);
    }
    updatePlanPreviewText(&draftPlan);

    QStringList summaryLines {
        QStringLiteral("\u5df2\u751f\u6210\u5207\u7247\uff1a%1 / %2").arg(generatedSliceCount).arg(m_stagedSlices.size()),
        QStringLiteral("\u6cbb\u7597\u65b9\u5f0f\uff1a%1 | %2")
            .arg(m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597"))
            .arg(m_lineTreatmentRadio->isChecked() ? QStringLiteral("\u7ebf\u6cbb\u7597") : QStringLiteral("\u70b9\u6cbb\u7597")),
        QStringLiteral("\u603b\u9776\u70b9\u6570\uff1a%1").arg(totalTargetCount),
        QStringLiteral("\u5168\u5c40\u884c\u8ddd\uff1a%1 mm | \u70b9\u7597\u65f6\u957f\uff1a%2 s | \u529f\u7387\uff1a%3 W")
            .arg(m_spacingSpin->value(), 0, 'f', 1)
            .arg(m_dwellSpin->value(), 0, 'f', 1)
            .arg(m_powerSpin->value(), 0, 'f', 0)
    };
    if (!generatedSliceLines.isEmpty()) {
        summaryLines << QString() << QStringLiteral("\u5df2\u751f\u6210\uff1a") << generatedSliceLines;
    }
    if (!skippedSliceLines.isEmpty()) {
        summaryLines << QStringLiteral("\u672a\u751f\u6210\uff1a") << skippedSliceLines;
    }
    updateAcquisitionSummary(QStringLiteral("\u9776\u70b9\u751f\u6210\u5b8c\u6210"), summaryLines);
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

    if (!m_context->hasActivePlan()) {
        updatePlanApprovalButtonState();
        return;
    }

    if (isPlanApprovedForTreatment(m_context->activePlan().approvalState)) {
        updatePlanApprovalButtonState();
        updateAcquisitionSummary(
            QStringLiteral("\u65b9\u6848\u5ba1\u6838"),
            {
                QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u5df2\u901a\u8fc7\u5ba1\u6838\u3002"),
                QStringLiteral("\u6cbb\u7597\u9636\u6bb5\u5df2\u53ef\u9009\u7528\u8be5\u65b9\u6848\u3002")
            });
        return;
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    TherapyPlan approvedPlan = hasGeneratedSliceTargets() ? buildPlanFromSlices(ApprovalState::Approved) : m_context->activePlan();
    const TherapyPlan previousPlan = m_context->activePlan();
    approvedPlan.id = previousPlan.id;
    if (!previousPlan.name.trimmed().isEmpty()) {
        approvedPlan.name = previousPlan.name;
    }
    approvedPlan.coordinateX = previousPlan.coordinateX;
    approvedPlan.coordinateY = previousPlan.coordinateY;
    approvedPlan.coordinateZ = previousPlan.coordinateZ;
    approvedPlan.depthMm = previousPlan.depthMm;
    approvedPlan.createdAt = previousPlan.createdAt;
    approvedPlan.approvalState = ApprovalState::Approved;
    approvedPlan.approvedAt = QDateTime::currentDateTime();
    approvedPlan.approvedBy = QStringLiteral("physician");

    if (m_clinicalDataRepository != nullptr) {
        TherapyPlan persistedPlan = approvedPlan;
        if (m_clinicalDataService.saveTherapyPlan(&persistedPlan)) {
            approvedPlan = persistedPlan;
        } else if (m_auditService != nullptr) {
            m_auditService->appendEntry(
                QStringLiteral("system"),
                QStringLiteral("planning"),
                QStringLiteral("\u5ba1\u6838\u65b9\u6848\u5df2\u8fdb\u5165\u5f53\u524d\u4e0a\u4e0b\u6587\uff0c\u4f46\u5199\u5165\u4e34\u5e8a\u6570\u636e\u4ed3\u5931\u8d25\uff1a%1").arg(m_clinicalDataService.lastError()));
        }
    }

    m_context->setActivePlan(approvedPlan);
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(approvedPlan.approvalState);
    }
    if (m_stagedSlices.isEmpty()) {
        m_preview->setPlan(approvedPlan);
    } else {
        refreshCurrentSliceVisualization();
    }
    updatePlanPreviewText(&approvedPlan);
    updatePlanApprovalButtonState();
    updateAcquisitionSummary(
        QStringLiteral("\u65b9\u6848\u5ba1\u6838\u5b8c\u6210"),
        {
            QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u5df2\u901a\u8fc7\u5ba1\u6838\u3002"),
            QStringLiteral("\u6cbb\u7597\u9636\u6bb5\u5df2\u53ef\u9009\u7528\u8be5\u65b9\u6848\u3002")
        });
    saveCurrentPathState();

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("physician"), QStringLiteral("planning"), QStringLiteral("\u5ba1\u6838\u901a\u8fc7\u65b9\u6848\uff1a%1").arg(approvedPlan.id));
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
    refreshContextSummary(true);
}

void PlanningPage::refreshContextSummary(bool refreshHistoricalImages)
{
    if (m_context->hasSelectedPatient()) {
        const PatientRecord& patient = m_context->selectedPatient();
        syncPatientSelector(patient.id);
        refreshImagingPaths(patient.id);
        if (refreshHistoricalImages) {
            loadHistoricalImages(false);
        }
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
                m_preview->setSyntheticImageEnabled(false);
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
            m_preview->setSyntheticImageEnabled(false);
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
            const qreal y = std::min(
                kUltrasoundDepthRangeMm - 6.0,
                12.0 + (segmentIndex * layersPerSegment + layer) * m_spacingSpin->value() * 0.8);
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
    for (int sliceIndex = 0; sliceIndex < m_stagedSlices.size(); ++sliceIndex) {
        const StagedSliceState& slice = m_stagedSlices.at(sliceIndex);
        if (!slice.targetsGenerated || slice.targets.isEmpty()) {
            continue;
        }

        TherapySegment segment;
        segment.id = QStringLiteral("%1-S%2").arg(plan.id).arg(segmentIndex + 1);
        segment.orderIndex = sliceIndex;
        segment.sourceSliceIndex = sliceIndex;
        segment.axis7PositionSteps = slice.acquisitionAxis7PositionSteps;
        segment.sourceImagePath = slice.image.storagePath;
        segment.label = QStringLiteral("%1 | %2").arg(slice.label, patternSummaryText(slice.pattern));
        segment.points = slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated && !slice.respiratoryAdjustedTargets.isEmpty()
            ? slice.respiratoryAdjustedTargets
            : slice.targets;
        segment.plannedDurationSeconds = totalDwellSeconds(segment.points);

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
        loadDemoPatient(false);
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

void PlanningPage::toggleAnnotationPanel()
{
    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setVisible(true);
    }
    setAnnotationEditingEnabled(m_imageAcquisitionCompleted);
}

void PlanningPage::updateAnnotationColorButtonSelection(const QColor& color)
{
    m_activeAnnotationColor = color;
    setAnnotationColorButtonsChecked(
        m_annotationRedButton,
        m_annotationBlueButton,
        m_annotationGreenButton,
        m_annotationOrangeButton,
        m_activeAnnotationColor);
    if (m_preview != nullptr) {
        m_preview->setCurrentAnnotationColor(m_activeAnnotationColor);
        m_preview->setAnnotationEnabled(m_imageAcquisitionCompleted && !m_treatmentComparisonFocusMode);
    }
}

void PlanningPage::setAnnotationEditingEnabled(bool enabled)
{
    const bool allowEditing = enabled && !m_treatmentComparisonFocusMode;
    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setEnabled(enabled);
        for (QToolButton* button : m_annotationPanel->findChildren<QToolButton*>()) {
            button->setEnabled(enabled);
        }
    }
    if (m_preview != nullptr) {
        m_preview->setAnnotationEnabled(allowEditing);
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
        return;
    }
    refreshCurrentSliceVisualization();
    updateSliceAssessmentMetrics();
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
    slice.annotatedAreaMm2 = slice.annotations.isEmpty() ? 0.0 : annotationRegionAreaMm2(slice.annotations);
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
            m_preview->setSyntheticImageEnabled(false);
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
        updateSliceNavigationButtons();
        setAnnotationEditingEnabled(false);
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
        slice.acquisitionAxis7PositionSteps >= 0
            ? QStringLiteral("7号绝对位置：%1 步").arg(slice.acquisitionAxis7PositionSteps)
            : QStringLiteral("7号绝对位置：未记录"),
        QStringLiteral("\u7f16\u8f91\u72b6\u6001\uff1a%1").arg(slice.edited ? QStringLiteral("\u5df2\u5708\u753b") : QStringLiteral("\u672a\u5708\u753b")),
        QStringLiteral("\u5f53\u524d\u7b14\u8ff9\u6570\uff1a%1").arg(slice.annotations.size()),
        annotationAreaSummaryText(slice.annotatedAreaMm2),
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
    updateSliceNavigationButtons();
    setAnnotationEditingEnabled(m_imageAcquisitionCompleted);
}

QPixmap PlanningPage::renderCurrentSlicePixmap(int row, const QSize& size) const
{
    if (row < 0 || row >= m_stagedSlices.size() || !size.isValid()) {
        return {};
    }

    MockUltrasoundView renderView;
    renderView.resize(size);
    configureCurrentPreviewView(&renderView, row);

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
        m_preview->setSyntheticImageEnabled(false);
        if (!m_context->hasActivePlan()) {
            m_preview->clearPlan();
        }
        return;
    }

    const StagedSliceState& slice = m_stagedSlices.at(m_currentStagedSliceIndex);
    configureCurrentPreviewView(m_preview, m_currentStagedSliceIndex, true);

    if (m_previewOverlayLabel != nullptr) {
        m_previewOverlayLabel->setVisible(false);
    }
    if (m_currentSliceSummaryLabel != nullptr) {
        m_currentSliceSummaryLabel->setText(
            QStringLiteral("\u7b2c %1/%2 \u5f20 | \u7b14\u8ff9 %3 | %4%5")
                .arg(m_currentStagedSliceIndex + 1)
                .arg(m_stagedSlices.size())
                .arg(slice.annotations.size())
                .arg(QStringLiteral("%1 | %2").arg(annotationAreaSummaryText(slice.annotatedAreaMm2), targetSummaryText(slice.pattern, slice.targets)))
                .arg(slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated
                    ? QStringLiteral(" | \u547c\u5438\u8865\u507f dX %1 / dY %2")
                          .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 2)
                          .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 2)
                    : QString()));
        m_currentSliceSummaryLabel->setToolTip(QString());
    }
}

void PlanningPage::configureCurrentPreviewView(MockUltrasoundView* preview, int row, bool useActivePreviewAnnotations) const
{
    if (preview == nullptr) {
        return;
    }

    preview->setCompletedPointCount(0);

    if (row >= 0 && row < m_stagedSlices.size()) {
        const StagedSliceState& slice = m_stagedSlices.at(row);
        const QVector<AnnotationStroke> annotations = useActivePreviewAnnotations && preview != m_preview && m_preview != nullptr
            ? m_preview->annotationStrokes()
            : slice.annotations;
        if (slice.capturedFrame.isNull()) {
            preview->clearBackgroundImage();
            preview->setSyntheticImageEnabled(true);
        } else {
            preview->setBackgroundImageStretchToFill(false);
            preview->setBackgroundImage(slice.capturedFrame);
            preview->setSyntheticImageEnabled(false);
        }
        preview->setAnnotationStrokes(annotations);
        preview->setSliceContext(row, m_stagedSlices.size());
        const QString respiratoryCaption = slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated
            ? QStringLiteral(" | \u547c\u5438\u8865\u507f dX %1 dY %2")
                  .arg(slice.respiratoryOffsetMm.x(), 0, 'f', 1)
                  .arg(slice.respiratoryOffsetMm.y(), 0, 'f', 1)
            : QString();
        preview->setCaption(
            QStringLiteral("\u5f53\u524d\u6cbb\u7597 %1/%2%3")
                .arg(row + 1)
                .arg(m_stagedSlices.size())
                .arg(respiratoryCaption));
        if (slice.targetsGenerated && !slice.targets.isEmpty()) {
            const QVector<TherapyPoint>& previewPoints =
                slice.respiratoryTrackingEnabled && slice.respiratoryTrackingCalibrated && !slice.respiratoryAdjustedTargets.isEmpty()
                ? slice.respiratoryAdjustedTargets
                : slice.targets;
            TherapyPlan previewPlan;
            previewPlan.pattern = slice.pattern;
            previewPlan.spacingMm = slice.spacingMm > 0.0 ? slice.spacingMm : m_spacingSpin->value();
            previewPlan.dwellSeconds = slice.dwellSeconds > 0.0 ? slice.dwellSeconds : m_dwellSpin->value();
            previewPlan.plannedPowerWatts = slice.powerWatts > 0.0 ? slice.powerWatts : m_powerSpin->value();
            TherapySegment segment;
            segment.id = QStringLiteral("SLICE-%1").arg(row + 1);
            segment.orderIndex = 0;
            segment.sourceSliceIndex = row;
            segment.axis7PositionSteps = slice.acquisitionAxis7PositionSteps;
            segment.sourceImagePath = slice.image.storagePath;
            segment.label = slice.label;
            segment.points = previewPoints;
            segment.plannedDurationSeconds = totalDwellSeconds(previewPoints);
            previewPlan.segments.push_back(segment);
            preview->setPlan(previewPlan);
        } else {
            preview->clearPlan();
        }
        return;
    }

    preview->setAnnotationStrokes({});
    preview->setSliceContext(0, 0);
    if (m_context->hasActivePlan()) {
        if (useActivePreviewAnnotations && m_preview != nullptr && preview != m_preview) {
            preview->setAnnotationStrokes(m_preview->annotationStrokes());
        }
        preview->clearBackgroundImage();
        preview->setSyntheticImageEnabled(false);
        preview->setPlan(m_context->activePlan());
        preview->setCaption(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u9884\u89c8"));
    } else {
        preview->clearBackgroundImage();
        preview->setSyntheticImageEnabled(false);
        preview->clearPlan();
        preview->setCaption(QStringLiteral(""));
    }
}

void PlanningPage::applyCurrentControlsToAllSlices()
{
    if (m_stagedSlices.isEmpty()) {
        return;
    }

    const TreatmentPattern selectedPattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
    const QString deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
    for (StagedSliceState& slice : m_stagedSlices) {
        slice.pattern = selectedPattern;
        slice.spacingMm = m_spacingSpin->value();
        slice.dwellSeconds = m_dwellSpin->value();
        slice.powerWatts = m_powerSpin->value();
        slice.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
        slice.deliveryMode = deliveryMode;
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
    slice.powerWatts = m_powerSpin->value();
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
    slice.annotatedAreaMm2 = slice.annotations.isEmpty() ? 0.0 : annotationRegionAreaMm2(slice.annotations);
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

void PlanningPage::invalidateAllSliceTargets(const QString& title, const QString& detail)
{
    bool clearedAnyTargets = false;
    for (StagedSliceState& slice : m_stagedSlices) {
        if (slice.targetsGenerated || !slice.targets.isEmpty()) {
            clearedAnyTargets = true;
        }
        slice.targets.clear();
        slice.targetsGenerated = false;
        slice.annotatedAreaMm2 = slice.annotations.isEmpty() ? 0.0 : annotationRegionAreaMm2(slice.annotations);
        slice.estimatedVolumeCm3 = 0.0;
        slice.ablatedVolumeCm3 = 0.0;
        clearRespiratoryTrackingState(slice);
    }

    if (!clearedAnyTargets) {
        return;
    }

    refreshCurrentSliceVisualization();
    updateSliceAssessmentMetrics();
    refreshDerivedMetrics();

    if (m_context != nullptr && m_context->hasActivePlan()) {
        m_context->clearActivePlan();
    }
    if (m_safetyKernel != nullptr) {
        m_safetyKernel->setPlanApprovalState(ApprovalState::Draft);
    }
    updatePlanPreviewText(nullptr);

    if (!title.trimmed().isEmpty()) {
        QStringList lines {
            QStringLiteral("\u5df2\u5c06\u6240\u6709\u5207\u7247\u7684\u65e7\u9776\u70b9\u6807\u8bb0\u4e3a\u5931\u6548\u3002")
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
        slice.annotatedAreaMm2 = clearAnnotations || slice.annotations.isEmpty() ? 0.0 : annotationRegionAreaMm2(slice.annotations);
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
        updatePlanApprovalButtonState();
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
    updatePlanApprovalButtonState();
}

void PlanningPage::applyPlanToUi(const TherapyPlan& plan)
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

    if (plan.spacingMm > 0.0) {
        m_spacingSpin->setValue(plan.spacingMm);
    }
    if (plan.dwellSeconds > 0.0) {
        m_dwellSpin->setValue(plan.dwellSeconds);
    }
    if (plan.plannedPowerWatts > 0.0) {
        m_powerSlider->setValue(static_cast<int>(plan.plannedPowerWatts));
        m_powerSpin->setValue(plan.plannedPowerWatts);
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

    if (m_pathList->count() == 0) {
        m_pathList->addItem(createPathListItem(0));
    }
    if (m_pathList->count() > 0 && m_pathList->currentRow() < 0) {
        m_pathList->setCurrentRow(0);
    }
    updatePathActionState();
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
        m_acquireImageButton->setEnabled(!m_imageAcquisitionRunning);
    }
    if (m_generate3dButton != nullptr) {
        m_generate3dButton->setEnabled(hasPathSelection && !m_stagedSlices.isEmpty());
    }
    updatePlanApprovalButtonState();
    refreshPowerCurve();
}

void PlanningPage::updatePlanApprovalButtonState()
{
    if (m_editPlanButton == nullptr) {
        return;
    }

    const bool approved = m_context != nullptr
        && m_context->hasActivePlan()
        && isPlanApprovedForTreatment(m_context->activePlan().approvalState);
    const bool canApprove = !approved
        && ((m_context != nullptr && m_context->hasActivePlan()) || hasGeneratedSliceTargets());

    m_editPlanButton->setEnabled(approved || canApprove);
    m_editPlanButton->setText(approved ? QStringLiteral("\u5df2\u5ba1") : QStringLiteral("\u5ba1\u6838"));
    m_editPlanButton->setToolTip(
        approved
            ? QStringLiteral("\u65b9\u6848\u5df2\u5ba1\u6838\u901a\u8fc7\uff0c\u6cbb\u7597\u9636\u6bb5\u53ef\u9009\u7528")
            : QStringLiteral("\u70b9\u51fb\u5ba1\u6838\u901a\u8fc7\u5f53\u524d\u6cbb\u7597\u65b9\u6848"));
    m_editPlanButton->setProperty("approvalState", approved ? QStringLiteral("approved") : QStringLiteral("pending"));
    m_editPlanButton->style()->unpolish(m_editPlanButton);
    m_editPlanButton->style()->polish(m_editPlanButton);
    m_editPlanButton->update();
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
    const double setPowerWatts = m_powerSpin != nullptr ? m_powerSpin->value() : 0.0;
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
    state.annotationPanelExpanded = true;
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
    m_imageAcquisitionCompleted = false;

    rebuildModelList();
    if (m_currentSliceSlider != nullptr) {
        const QSignalBlocker blocker(m_currentSliceSlider);
        m_currentSliceSlider->setRange(0, 0);
        m_currentSliceSlider->setValue(0);
        m_currentSliceSlider->setEnabled(false);
    }
    updateSliceNavigationButtons();

    if (m_preview != nullptr) {
        m_preview->setAnnotationStrokes({});
        m_preview->clearBackgroundImage();
        m_preview->setSyntheticImageEnabled(false);
        m_preview->clearPlan();
        m_preview->setCompletedPointCount(0);
        m_preview->setSliceContext(0, 0);
        m_preview->setCaption(QString());
    }

    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setVisible(true);
    }
    setAnnotationEditingEnabled(false);

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
    resetComparisonSyncCalibration();
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
    m_imageAcquisitionCompleted = !m_stagedSlices.isEmpty();
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
        m_annotationPanel->setVisible(true);
    }
    setAnnotationEditingEnabled(m_imageAcquisitionCompleted);
    updatePathActionState();
}

void PlanningPage::showTreatmentComparisonLayer(const QString& planId, int layerIndex, bool treatmentActive)
{
    Q_UNUSED(planId)

    if (!treatmentActive) {
        setTreatmentComparisonFocusMode(false);
        updateComparisonSyncState();
        return;
    }

    setTreatmentComparisonFocusMode(true);

    if (m_stagedSlices.isEmpty()) {
        refreshCurrentSliceVisualization();
        updateComparisonSyncState();
        updateAcquisitionSummary(
            QStringLiteral("\u6cbb\u7597\u8054\u52a8\u5f71\u50cf\u5bf9\u6bd4"),
            {
                QStringLiteral("\u5df2\u8fdb\u5165\u6cbb\u7597\u5bf9\u6bd4\u805a\u7126\u6a21\u5f0f"),
                QStringLiteral("\u5f53\u524d\u6ca1\u6709\u53ef\u8df3\u8f6c\u7684\u91c7\u96c6\u5f71\u50cf\uff0c\u8bf7\u5148\u5728\u65b9\u6848\u9875\u5b8c\u6210\u56fe\u50cf\u91c7\u96c6\u3002")
            });
        return;
    }

    const int safeLayerIndex = qBound(0, layerIndex, static_cast<int>(m_stagedSlices.size()) - 1);
    if (m_modelList != nullptr && m_modelList->currentRow() != safeLayerIndex) {
        const QSignalBlocker blocker(m_modelList);
        m_modelList->setCurrentRow(safeLayerIndex);
    }
    loadStagedSlice(safeLayerIndex);
    configureTreatmentComparisonSyncForLayer(safeLayerIndex);

    updateAcquisitionSummary(
        QStringLiteral("\u6cbb\u7597\u8054\u52a8\u5f71\u50cf\u5bf9\u6bd4"),
        {
            QStringLiteral("\u5df2\u8fdb\u5165\u6cbb\u7597\u5bf9\u6bd4\u805a\u7126\u6a21\u5f0f"),
            QStringLiteral("\u53f3\u4fa7\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\uff1a\u7b2c %1 / %2 \u5f20").arg(safeLayerIndex + 1).arg(m_stagedSlices.size()),
            m_historyImageSeries.isEmpty()
                ? QStringLiteral("\u5de6\u4fa7\u65e2\u5f80\u5f71\u50cf\uff1a\u6682\u65e0\u53ef\u540c\u6b65\u5f71\u50cf")
                : QStringLiteral("\u5de6\u4fa7\u65e2\u5f80\u5f71\u50cf\u5df2\u6309\u53f3\u4fa7\u5c42\u53f7\u540c\u6b65\u8df3\u8f6c"),
            QStringLiteral("\u82e5\u5de6\u53f3\u5f71\u50cf\u6570\u91cf\u4e0d\u540c\uff0c\u540c\u6b65\u79fb\u52a8\u4f1a\u6309\u8d77\u70b9/\u7ec8\u70b9\u6bd4\u4f8b\u6620\u5c04\u3002")
        });
}

void PlanningPage::setTreatmentComparisonFocusMode(bool enabled)
{
    if (m_treatmentComparisonFocusMode == enabled) {
        return;
    }

    m_treatmentComparisonFocusMode = enabled;
    setUpdatesEnabled(false);

    if (m_rootLayout != nullptr) {
        m_rootLayout->setContentsMargins(enabled ? QMargins(0, 0, 0, 0) : QMargins(12, 12, 12, 12));
        m_rootLayout->setSpacing(enabled ? 0 : 12);
        if (m_leftColumnHost != nullptr) {
            m_rootLayout->setStretchFactor(m_leftColumnHost, enabled ? 0 : 21);
        }
        if (m_centerColumnHost != nullptr) {
            m_rootLayout->setStretchFactor(m_centerColumnHost, enabled ? 1 : 55);
        }
        if (m_rightColumnHost != nullptr) {
            m_rootLayout->setStretchFactor(m_rightColumnHost, 0);
        }
    }
    if (m_leftColumnHost != nullptr) {
        m_leftColumnHost->setVisible(!enabled);
    }
    if (m_rightColumnHost != nullptr) {
        m_rightColumnHost->setVisible(!enabled);
    }
    if (m_previewFrame != nullptr) {
        m_previewFrame->setMinimumSize(enabled ? QSize(0, 0) : QSize(720, 500));
    }

    const auto setFramesVisible = [this](const QString& objectName, bool visible) {
        for (QFrame* frame : findChildren<QFrame*>(objectName)) {
            frame->setVisible(visible);
        }
    };

    setFramesVisible(QStringLiteral("planningSidebarCard"), !enabled);
    setFramesVisible(QStringLiteral("planningBottomCard"), !enabled);
    setFramesVisible(QStringLiteral("planningControlFrame"), !enabled);

    if (m_annotationPanel != nullptr) {
        m_annotationPanel->setVisible(!enabled);
    }
    if (m_currentMaximizeButton != nullptr) {
        m_currentMaximizeButton->setVisible(!enabled);
    }
    if (m_historyPreview != nullptr) {
        m_historyPreview->setBackgroundImageStretchToFill(false);
    }
    if (m_preview != nullptr) {
        m_preview->setBackgroundImageStretchToFill(false);
    }
    setAnnotationEditingEnabled(m_imageAcquisitionCompleted);

    setUpdatesEnabled(true);
    updateGeometry();
    update();
}

void PlanningPage::configureTreatmentComparisonSyncForLayer(int layerIndex)
{
    if (m_stagedSlices.isEmpty()) {
        resetComparisonSyncCalibration();
        return;
    }

    const int currentCount = static_cast<int>(m_stagedSlices.size());
    const int historyCount = static_cast<int>(m_historyImageSeries.size());
    const int safeCurrentIndex = qBound(0, layerIndex, currentCount - 1);

    if (historyCount > 0) {
        int historyIndex = qBound(0, safeCurrentIndex, historyCount - 1);
        if (currentCount > 1 && historyCount > 1) {
            const double ratio = static_cast<double>(safeCurrentIndex) / static_cast<double>(currentCount - 1);
            historyIndex = qBound(0, qRound(ratio * static_cast<double>(historyCount - 1)), historyCount - 1);
        }
        loadHistoricalSlice(historyIndex, false);
    }

    if (currentCount <= 1 || historyCount <= 1) {
        resetComparisonSyncCalibration();
        updateComparisonSyncState();
        return;
    }

    m_historySyncStartIndex = 0;
    m_historySyncEndIndex = historyCount - 1;
    m_currentSyncStartIndex = 0;
    m_currentSyncEndIndex = currentCount - 1;
    m_hasHistorySyncStartPoint = true;
    m_hasHistorySyncEndPoint = true;
    m_hasCurrentSyncStartPoint = true;
    m_hasCurrentSyncEndPoint = true;

    if (m_comparisonSyncCheck != nullptr && !m_comparisonSyncCheck->isChecked()) {
        m_comparisonSyncCheck->setChecked(true);
    }
    updateComparisonSyncState();

    if (m_comparisonSyncSlider != nullptr) {
        const int syncValue = syncValueForCurrentSliceIndex(safeCurrentIndex);
        m_comparisonSyncSlider->setValue(syncValue);
        applyComparisonSyncSliderValue(syncValue);
    }
}

int PlanningPage::syncValueForCurrentSliceIndex(int currentIndex) const
{
    if (!hasComparisonSyncCalibration()) {
        return 0;
    }

    const int syncRange = comparisonSyncRange();
    const int currentDelta = m_currentSyncEndIndex - m_currentSyncStartIndex;
    if (syncRange <= 0 || currentDelta == 0) {
        return 0;
    }

    const double ratio = static_cast<double>(currentIndex - m_currentSyncStartIndex) / static_cast<double>(currentDelta);
    return qBound(0, qRound(ratio * static_cast<double>(syncRange)), syncRange);
}

void PlanningPage::showHistoryPreviewMaximized()
{
    if (m_historyPreview == nullptr || m_historyImageSeries.isEmpty()) {
        return;
    }
    m_historyPreview->resetImageZoom();
    updateHistoryMaximizeButtonState();
}

void PlanningPage::markComparisonSyncAnchor(bool historySide, bool startPoint)
{
    QSlider* slider = historySide ? m_historySliceSlider : m_currentSliceSlider;
    const bool hasSlices = historySide ? !m_historyImageSeries.isEmpty() : !m_stagedSlices.isEmpty();
    if (slider == nullptr || !hasSlices || slider->maximum() < slider->minimum()) {
        return;
    }

    const int index = qBound(slider->minimum(), slider->value(), slider->maximum());
    if (historySide) {
        if (startPoint) {
            m_historySyncStartIndex = index;
            m_hasHistorySyncStartPoint = true;
        } else {
            m_historySyncEndIndex = index;
            m_hasHistorySyncEndPoint = true;
        }
    } else {
        if (startPoint) {
            m_currentSyncStartIndex = index;
            m_hasCurrentSyncStartPoint = true;
        } else {
            m_currentSyncEndIndex = index;
            m_hasCurrentSyncEndPoint = true;
        }
    }

    updateComparisonSyncState();
}

void PlanningPage::resetComparisonSyncCalibration()
{
    m_hasHistorySyncStartPoint = false;
    m_hasHistorySyncEndPoint = false;
    m_hasCurrentSyncStartPoint = false;
    m_hasCurrentSyncEndPoint = false;
    m_historySyncStartIndex = -1;
    m_historySyncEndIndex = -1;
    m_currentSyncStartIndex = -1;
    m_currentSyncEndIndex = -1;

    if (m_historyPreview != nullptr) {
        m_historyPreview->clearComparisonCalibrationPoints();
    }
    if (m_preview != nullptr) {
        m_preview->clearComparisonCalibrationPoints();
    }
    if (m_comparisonSyncCheck != nullptr) {
        const QSignalBlocker blocker(m_comparisonSyncCheck);
        m_comparisonSyncCheck->setChecked(false);
    }
    if (m_comparisonSyncSlider != nullptr) {
        const QSignalBlocker blocker(m_comparisonSyncSlider);
        m_comparisonSyncSlider->setRange(0, 0);
        m_comparisonSyncSlider->setValue(0);
        m_comparisonSyncSlider->setEnabled(false);
    }
    updateComparisonSyncState();
}

bool PlanningPage::hasComparisonSyncCalibration() const
{
    if (!m_hasHistorySyncStartPoint || !m_hasHistorySyncEndPoint
        || !m_hasCurrentSyncStartPoint || !m_hasCurrentSyncEndPoint) {
        return false;
    }

    const auto inRange = [](const QSlider* slider, int index) {
        return slider != nullptr && index >= slider->minimum() && index <= slider->maximum();
    };

    return inRange(m_historySliceSlider, m_historySyncStartIndex)
        && inRange(m_historySliceSlider, m_historySyncEndIndex)
        && inRange(m_currentSliceSlider, m_currentSyncStartIndex)
        && inRange(m_currentSliceSlider, m_currentSyncEndIndex)
        && m_historySyncStartIndex != m_historySyncEndIndex
        && m_currentSyncStartIndex != m_currentSyncEndIndex;
}

bool PlanningPage::isComparisonSyncActive() const
{
    return m_comparisonSyncCheck != nullptr
        && m_comparisonSyncCheck->isChecked()
        && hasComparisonSyncCalibration();
}

int PlanningPage::comparisonSyncRange() const
{
    if (!hasComparisonSyncCalibration()) {
        return 0;
    }

    return std::max(
        std::abs(m_historySyncEndIndex - m_historySyncStartIndex),
        std::abs(m_currentSyncEndIndex - m_currentSyncStartIndex));
}

int PlanningPage::mappedComparisonSliceIndex(int syncValue, int startIndex, int endIndex) const
{
    const int syncRange = comparisonSyncRange();
    if (syncRange <= 0) {
        return startIndex;
    }

    const double ratio = qBound(0.0, static_cast<double>(syncValue) / static_cast<double>(syncRange), 1.0);
    return qRound(static_cast<double>(startIndex) + static_cast<double>(endIndex - startIndex) * ratio);
}

void PlanningPage::updateComparisonSyncState()
{
    const bool hasAnyPoint = m_hasHistorySyncStartPoint || m_hasHistorySyncEndPoint
        || m_hasCurrentSyncStartPoint || m_hasCurrentSyncEndPoint;
    const bool ready = hasComparisonSyncCalibration();
    const bool enabled = ready && m_comparisonSyncCheck != nullptr && m_comparisonSyncCheck->isChecked();
    const QString lockedSliderToolTip = QStringLiteral("\u540c\u6b65\u79fb\u52a8\u5df2\u5f00\u542f\uff0c\u8bf7\u4f7f\u7528\u4e0b\u65b9\u540c\u6b65\u79fb\u52a8\u6ed1\u52a8\u6761\u5207\u6362\u5de6\u53f3\u5f71\u50cf\u3002");
    const QString syncSliderToolTip = ready
        ? (enabled
            ? QStringLiteral("\u62d6\u52a8\u6216\u70b9\u51fb\u4e24\u4fa7\u6309\u94ae\uff0c\u540c\u6b65\u5207\u6362\u5de6\u53f3\u5f71\u50cf\u3002")
            : QStringLiteral("\u52fe\u9009\u201c\u540c\u6b65\u79fb\u52a8\u201d\u540e\uff0c\u6b64\u6ed1\u52a8\u6761\u53ef\u7528\u3002"))
        : QStringLiteral("\u8bf7\u5148\u8bbe\u7f6e\u5de6\u53f3\u8d77\u70b9\u548c\u7ec8\u70b9\uff0c\u518d\u542f\u7528\u540c\u6b65\u79fb\u52a8\u3002");

    const auto updateAnchorButtonText = [](QPushButton* button, const QString& baseText, bool hasAnchor, int index) {
        if (button == nullptr) {
            return;
        }
        button->setText(hasAnchor ? QStringLiteral("%1 %2").arg(baseText).arg(index + 1) : baseText);
    };

    updateAnchorButtonText(m_historySyncStartButton, QStringLiteral("左起点"), m_hasHistorySyncStartPoint, m_historySyncStartIndex);
    updateAnchorButtonText(m_historySyncEndButton, QStringLiteral("左终点"), m_hasHistorySyncEndPoint, m_historySyncEndIndex);
    updateAnchorButtonText(m_currentSyncStartButton, QStringLiteral("右起点"), m_hasCurrentSyncStartPoint, m_currentSyncStartIndex);
    updateAnchorButtonText(m_currentSyncEndButton, QStringLiteral("右终点"), m_hasCurrentSyncEndPoint, m_currentSyncEndIndex);

    if (m_comparisonSyncCheck != nullptr) {
        m_comparisonSyncCheck->setEnabled(ready);
        if (!ready && m_comparisonSyncCheck->isChecked()) {
            const QSignalBlocker blocker(m_comparisonSyncCheck);
            m_comparisonSyncCheck->setChecked(false);
        }
    }
    if (m_resetComparisonSyncButton != nullptr) {
        m_resetComparisonSyncButton->setEnabled(hasAnyPoint);
    }
    if (m_comparisonSyncSlider != nullptr) {
        const int range = ready ? comparisonSyncRange() : 0;
        const QSignalBlocker blocker(m_comparisonSyncSlider);
        m_comparisonSyncSlider->setRange(0, range);
        m_comparisonSyncSlider->setEnabled(enabled);
        m_comparisonSyncSlider->setToolTip(syncSliderToolTip);
        setSliderVisualState(m_comparisonSyncSlider, "syncInactive", !enabled);
        if (!ready || m_comparisonSyncSlider->value() > range) {
            m_comparisonSyncSlider->setValue(0);
        }
    }
    if (m_comparisonSyncPrevButton != nullptr) {
        m_comparisonSyncPrevButton->setEnabled(enabled
            && m_comparisonSyncSlider != nullptr
            && m_comparisonSyncSlider->value() > m_comparisonSyncSlider->minimum());
        m_comparisonSyncPrevButton->setToolTip(syncSliderToolTip);
    }
    if (m_comparisonSyncNextButton != nullptr) {
        m_comparisonSyncNextButton->setEnabled(enabled
            && m_comparisonSyncSlider != nullptr
            && m_comparisonSyncSlider->value() < m_comparisonSyncSlider->maximum());
        m_comparisonSyncNextButton->setToolTip(syncSliderToolTip);
    }

    const auto updateSliceSlider = [enabled, &lockedSliderToolTip](QSlider* slider, bool hasMultipleSlices, const QString& unlockedToolTip) {
        if (slider == nullptr) {
            return;
        }
        slider->setEnabled(hasMultipleSlices && !enabled);
        slider->setToolTip(enabled ? lockedSliderToolTip : unlockedToolTip);
        setSliderVisualState(slider, "syncLocked", enabled);
    };
    updateSliceSlider(
        m_historySliceSlider,
        m_historySliceSlider != nullptr && m_historySliceSlider->maximum() > m_historySliceSlider->minimum(),
        QStringLiteral("\u5207\u6362\u5de6\u4fa7\u65e2\u5f80\u5f71\u50cf"));
    updateSliceSlider(
        m_currentSliceSlider,
        m_currentSliceSlider != nullptr && m_currentSliceSlider->maximum() > m_currentSliceSlider->minimum(),
        QStringLiteral("\u5207\u6362\u53f3\u4fa7\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));

    const auto updateButtons = [](QSlider* slider, QToolButton* previousButton, QToolButton* nextButton) {
        const bool canNavigate = slider != nullptr && slider->isEnabled() && slider->maximum() > slider->minimum();
        if (previousButton != nullptr) {
            previousButton->setEnabled(canNavigate && slider->value() > slider->minimum());
        }
        if (nextButton != nullptr) {
            nextButton->setEnabled(canNavigate && slider->value() < slider->maximum());
        }
    };
    updateButtons(m_historySliceSlider, m_historyPrevSliceButton, m_historyNextSliceButton);
    updateButtons(m_currentSliceSlider, m_currentPrevSliceButton, m_currentNextSliceButton);

    if (enabled && m_comparisonSyncSlider != nullptr) {
        applyComparisonSyncSliderValue(m_comparisonSyncSlider->value());
    }
}

void PlanningPage::applyComparisonSyncSliderValue(int value)
{
    if (m_applyingComparisonSync
        || m_comparisonSyncCheck == nullptr
        || !m_comparisonSyncCheck->isChecked()
        || !hasComparisonSyncCalibration()) {
        return;
    }

    const int historyIndex = qBound(
        m_historySliceSlider->minimum(),
        mappedComparisonSliceIndex(value, m_historySyncStartIndex, m_historySyncEndIndex),
        m_historySliceSlider->maximum());
    const int currentIndex = qBound(
        m_currentSliceSlider->minimum(),
        mappedComparisonSliceIndex(value, m_currentSyncStartIndex, m_currentSyncEndIndex),
        m_currentSliceSlider->maximum());

    m_applyingComparisonSync = true;
    if (m_historySliceSlider != nullptr && m_historySliceSlider->value() != historyIndex) {
        m_historySliceSlider->setValue(historyIndex);
    }
    if (m_currentSliceSlider != nullptr && m_currentSliceSlider->value() != currentIndex) {
        m_currentSliceSlider->setValue(currentIndex);
    }
    m_applyingComparisonSync = false;
    if (m_comparisonSyncPrevButton != nullptr && m_comparisonSyncSlider != nullptr) {
        m_comparisonSyncPrevButton->setEnabled(m_comparisonSyncSlider->value() > m_comparisonSyncSlider->minimum());
    }
    if (m_comparisonSyncNextButton != nullptr && m_comparisonSyncSlider != nullptr) {
        m_comparisonSyncNextButton->setEnabled(m_comparisonSyncSlider->value() < m_comparisonSyncSlider->maximum());
    }
}

void PlanningPage::showCurrentPreviewMaximized()
{
    if (m_preview == nullptr) {
        return;
    }

    persistCurrentSliceAnnotations();
    storeCurrentSliceControls();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("planningCurrentPreviewDialog"));
    dialog.setWindowTitle(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf\u5168\u5c4f\u67e5\u770b"));
    dialog.resize(1440, 900);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto* titleLabel = new QLabel(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    titleLabel->setObjectName(QStringLiteral("planningCardTitle"));
    auto* closeButton = new QPushButton(QStringLiteral("\u5173\u95ed"));
    closeButton->setObjectName(QStringLiteral("planningActionButton"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(closeButton);
    layout->addLayout(titleRow);

    auto* dialogAnnotationPanel = new QFrame();
    dialogAnnotationPanel->setObjectName(QStringLiteral("planningAnnotationPanel"));
    auto* dialogAnnotationLayout = new QHBoxLayout(dialogAnnotationPanel);
    dialogAnnotationLayout->setContentsMargins(12, 10, 12, 10);
    dialogAnnotationLayout->setSpacing(10);

    auto* dialogBrushButton = new QToolButton();
    dialogBrushButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    dialogBrushButton->setText(QStringLiteral("\u270e"));
    dialogAnnotationLayout->addWidget(dialogBrushButton);

    auto* dialogSeparatorTop = new QFrame();
    dialogSeparatorTop->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    dialogSeparatorTop->setFrameShape(QFrame::VLine);
    dialogAnnotationLayout->addWidget(dialogSeparatorTop);

    auto* dialogRedButton = new QToolButton();
    dialogRedButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    dialogRedButton->setProperty("swatchColor", QStringLiteral("red"));
    dialogRedButton->setToolTip(QStringLiteral("\u7ea2\u8272\u5708\u753b\u753b\u7b14"));
    dialogAnnotationLayout->addWidget(dialogRedButton);

    auto* dialogBlueButton = new QToolButton();
    dialogBlueButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    dialogBlueButton->setProperty("swatchColor", QStringLiteral("blue"));
    dialogBlueButton->setToolTip(QStringLiteral("\u84dd\u8272\u5708\u753b\u753b\u7b14"));
    dialogAnnotationLayout->addWidget(dialogBlueButton);

    auto* dialogGreenButton = new QToolButton();
    dialogGreenButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    dialogGreenButton->setProperty("swatchColor", QStringLiteral("green"));
    dialogGreenButton->setToolTip(QStringLiteral("\u7eff\u8272\u5708\u753b\u753b\u7b14"));
    dialogAnnotationLayout->addWidget(dialogGreenButton);

    auto* dialogOrangeButton = new QToolButton();
    dialogOrangeButton->setObjectName(QStringLiteral("planningAnnotationColorButton"));
    dialogOrangeButton->setProperty("swatchColor", QStringLiteral("orange"));
    dialogOrangeButton->setToolTip(QStringLiteral("\u6a59\u8272\u5708\u753b\u753b\u7b14"));
    dialogAnnotationLayout->addWidget(dialogOrangeButton);

    auto* dialogSeparatorMiddle = new QFrame();
    dialogSeparatorMiddle->setObjectName(QStringLiteral("planningAnnotationSeparator"));
    dialogSeparatorMiddle->setFrameShape(QFrame::VLine);
    dialogAnnotationLayout->addWidget(dialogSeparatorMiddle);

    auto* dialogUndoButton = new QToolButton();
    dialogUndoButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    dialogUndoButton->setText(QStringLiteral("\u21b6"));
    dialogAnnotationLayout->addWidget(dialogUndoButton);

    auto* dialogClearButton = new QToolButton();
    dialogClearButton->setObjectName(QStringLiteral("planningAnnotationToolButton"));
    dialogClearButton->setText(QStringLiteral("\U0001F5D1"));
    dialogAnnotationLayout->addWidget(dialogClearButton);
    dialogAnnotationLayout->addStretch();
    layout->addWidget(dialogAnnotationPanel);

    const QSize basePreviewSize = m_preview != nullptr && m_preview->size().isValid()
        ? m_preview->size()
        : QSize(720, 540);
    const QRect availableGeometry = QGuiApplication::primaryScreen() != nullptr
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1440, 900);
    const QSize previewLimit(
        std::max(640, availableGeometry.width() - 180),
        std::max(420, availableGeometry.height() - 300));
    const QSize expandedPreviewSize = basePreviewSize.scaled(previewLimit, Qt::KeepAspectRatio);
    dialog.resize(
        std::min(expandedPreviewSize.width() + 60, availableGeometry.width() - 40),
        std::min(expandedPreviewSize.height() + 220, availableGeometry.height() - 40));

    auto* previewArea = new QWidget();
    previewArea->setObjectName(QStringLiteral("planningDialogPreviewArea"));
    auto* previewAreaLayout = new QVBoxLayout(previewArea);
    previewAreaLayout->setContentsMargins(0, 0, 0, 0);
    previewAreaLayout->setSpacing(6);

    auto* previewStackHost = new QWidget();
    previewStackHost->setObjectName(QStringLiteral("planningDialogPreviewStack"));
    auto* previewStack = new QGridLayout(previewStackHost);
    previewStack->setContentsMargins(0, 0, 0, 0);
    previewStack->setSpacing(0);
    auto* dialogPreview = new MockUltrasoundView();
    dialogPreview->setObjectName(QStringLiteral("planningPreviewWidget"));
    dialogPreview->setMinimumSize(expandedPreviewSize);
    dialogPreview->setMaximumSize(expandedPreviewSize);
    dialogPreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    dialogPreview->setImageZoomEnabled(true);
    dialogPreview->setScaleRulerEnabled(true);
    dialogPreview->setScaleRulerExpanded(m_preview != nullptr ? m_preview->isScaleRulerExpanded() : true);
    dialogPreview->setAnnotationEnabled(m_imageAcquisitionCompleted);
    dialogPreview->setCurrentAnnotationColor(m_activeAnnotationColor);
    dialogAnnotationPanel->setEnabled(m_imageAcquisitionCompleted);
    for (QToolButton* button : dialogAnnotationPanel->findChildren<QToolButton*>()) {
        button->setEnabled(m_imageAcquisitionCompleted);
    }
    setAnnotationColorButtonsChecked(dialogRedButton, dialogBlueButton, dialogGreenButton, dialogOrangeButton, m_activeAnnotationColor);
    const int dialogSliceCount = m_stagedSlices.size();
    int dialogSliceIndex = m_currentStagedSliceIndex >= 0 && m_currentStagedSliceIndex < dialogSliceCount
        ? m_currentStagedSliceIndex
        : 0;
    configureCurrentPreviewView(dialogPreview, dialogSliceIndex, true);

    auto* overlayLabel = new QLabel();
    overlayLabel->setObjectName(QStringLiteral("planningPreviewOverlayLabel"));
    overlayLabel->setAlignment(Qt::AlignCenter);
    overlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    const bool showOverlay = m_previewOverlayLabel != nullptr && m_previewOverlayLabel->isVisible();
    overlayLabel->setText(showOverlay && m_previewOverlayLabel != nullptr ? m_previewOverlayLabel->text() : QString());
    overlayLabel->setVisible(showOverlay);

    auto* dialogPreviousSliceButton = createSliceNavButton(
        QStringLiteral("\u2039"),
        QStringLiteral("\u5207\u6362\u5230\u4e0a\u4e00\u5f20\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    auto* dialogNextSliceButton = createSliceNavButton(
        QStringLiteral("\u203a"),
        QStringLiteral("\u5207\u6362\u5230\u4e0b\u4e00\u5f20\u5f53\u524d\u6cbb\u7597\u5f71\u50cf"));
    auto* dialogSliceSlider = new QSlider(Qt::Horizontal);
    dialogSliceSlider->setObjectName(QStringLiteral("planningSliceSlider"));
    dialogSliceSlider->setRange(0, std::max(0, dialogSliceCount - 1));
    dialogSliceSlider->setEnabled(dialogSliceCount > 1);
    dialogSliceSlider->setValue(dialogSliceIndex);

    auto* dialogSliceNavigationPanel = new QFrame();
    dialogSliceNavigationPanel->setObjectName(QStringLiteral("planningDialogSliceNavigationPanel"));
    dialogSliceNavigationPanel->setMinimumWidth(expandedPreviewSize.width());
    dialogSliceNavigationPanel->setMaximumWidth(expandedPreviewSize.width());
    dialogSliceNavigationPanel->setMinimumHeight(52);
    dialogSliceNavigationPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* dialogSliceNavigationRow = new QHBoxLayout(dialogSliceNavigationPanel);
    dialogSliceNavigationRow->setContentsMargins(10, 6, 10, 6);
    dialogSliceNavigationRow->setSpacing(8);
    dialogSliceNavigationRow->addWidget(dialogPreviousSliceButton);
    dialogSliceNavigationRow->addWidget(dialogSliceSlider, 1);
    dialogSliceNavigationRow->addWidget(dialogNextSliceButton);

    previewStack->addWidget(dialogPreview, 0, 0, Qt::AlignCenter);
    previewStack->addWidget(overlayLabel, 0, 0);
    previewAreaLayout->addWidget(previewStackHost, 1, Qt::AlignHCenter);
    previewAreaLayout->addWidget(dialogSliceNavigationPanel, 0, Qt::AlignHCenter);
    layout->addWidget(previewArea, 1);

    auto* hintLabel = new QLabel(QStringLiteral("\u5168\u5c4f\u9884\u89c8\u53ef\u7528\u6eda\u8f6e\u7f29\u653e\uff0c\u5de6\u952e\u7ee7\u7eed\u753b\u7b14\u8ff9\uff0c\u53f3\u952e\u62d6\u52a8\u5e73\u79fb\uff0c\u7b14\u8ff9\u4f1a\u968f\u56fe\u50cf\u540c\u6b65\u663e\u793a\u3002"));
    hintLabel->setObjectName(QStringLiteral("planningSliceInfoLabel"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    const auto activateDialogColor = [this, dialogPreview, dialogRedButton, dialogBlueButton, dialogGreenButton, dialogOrangeButton](const QColor& color) {
        m_activeAnnotationColor = color;
        setAnnotationColorButtonsChecked(dialogRedButton, dialogBlueButton, dialogGreenButton, dialogOrangeButton, color);
        updateAnnotationColorButtonSelection(color);
        dialogPreview->setCurrentAnnotationColor(color);
        dialogPreview->setAnnotationEnabled(m_imageAcquisitionCompleted);
    };
    connect(dialogRedButton, &QToolButton::clicked, this, [activateDialogColor]() { activateDialogColor(QColor(201, 71, 51)); });
    connect(dialogBlueButton, &QToolButton::clicked, this, [activateDialogColor]() { activateDialogColor(QColor(91, 158, 230)); });
    connect(dialogGreenButton, &QToolButton::clicked, this, [activateDialogColor]() { activateDialogColor(QColor(163, 239, 76)); });
    connect(dialogOrangeButton, &QToolButton::clicked, this, [activateDialogColor]() { activateDialogColor(QColor(255, 177, 75)); });
    connect(dialogUndoButton, &QToolButton::clicked, dialogPreview, &MockUltrasoundView::undoLastAnnotation);
    connect(dialogClearButton, &QToolButton::clicked, dialogPreview, &MockUltrasoundView::clearAnnotations);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    const auto updateDialogSliceNavigation = [&]() {
        const bool canNavigate = dialogSliceSlider->isEnabled() && dialogSliceSlider->maximum() > dialogSliceSlider->minimum();
        dialogPreviousSliceButton->setEnabled(canNavigate && dialogSliceSlider->value() > dialogSliceSlider->minimum());
        dialogNextSliceButton->setEnabled(canNavigate && dialogSliceSlider->value() < dialogSliceSlider->maximum());
    };
    const auto saveDialogSliceAnnotations = [&]() {
        if (dialogSliceIndex < 0 || dialogSliceIndex >= m_stagedSlices.size()) {
            return;
        }

        const QVector<AnnotationStroke> rawAnnotations = dialogPreview->annotationStrokes();
        const QVector<AnnotationStroke> normalizedAnnotations = normalizeClosedAnnotations(rawAnnotations);
        if (!annotationStrokesEqual(rawAnnotations, normalizedAnnotations)) {
            const QSignalBlocker blocker(dialogPreview);
            dialogPreview->setAnnotationStrokes(normalizedAnnotations);
        }

        StagedSliceState& slice = m_stagedSlices[dialogSliceIndex];
        slice.annotations = normalizedAnnotations;
        slice.edited = !slice.annotations.isEmpty();
        slice.annotatedAreaMm2 = slice.annotations.isEmpty() ? 0.0 : annotationRegionAreaMm2(slice.annotations);
    };
    const auto loadDialogSlice = [&](int row) {
        if (m_stagedSlices.isEmpty()) {
            updateDialogSliceNavigation();
            return;
        }

        saveDialogSliceAnnotations();
        dialogSliceIndex = qBound(0, row, m_stagedSlices.size() - 1);
        recalculateRespiratoryTrackingForSlice(dialogSliceIndex);
        dialogPreview->resetImageZoom();
        configureCurrentPreviewView(dialogPreview, dialogSliceIndex, false);
        overlayLabel->setVisible(false);
        updateDialogSliceNavigation();
    };
    connect(dialogSliceSlider, &QSlider::valueChanged, this, loadDialogSlice);
    connect(dialogPreviousSliceButton, &QToolButton::clicked, this, [dialogSliceSlider]() {
        if (dialogSliceSlider->isEnabled()) {
            dialogSliceSlider->setValue(std::max(dialogSliceSlider->minimum(), dialogSliceSlider->value() - 1));
        }
    });
    connect(dialogNextSliceButton, &QToolButton::clicked, this, [dialogSliceSlider]() {
        if (dialogSliceSlider->isEnabled()) {
            dialogSliceSlider->setValue(std::min(dialogSliceSlider->maximum(), dialogSliceSlider->value() + 1));
        }
    });
    updateDialogSliceNavigation();

    dialog.show();
    dialog.exec();
    if (m_preview != nullptr) {
        m_preview->setScaleRulerExpanded(dialogPreview->isScaleRulerExpanded());
        saveDialogSliceAnnotations();
        if (dialogSliceIndex >= 0 && dialogSliceIndex < m_stagedSlices.size()) {
            if (m_modelList != nullptr && m_modelList->currentRow() != dialogSliceIndex) {
                m_modelList->setCurrentRow(dialogSliceIndex);
            } else {
                loadStagedSlice(dialogSliceIndex);
            }
        } else {
            m_preview->setAnnotationStrokes(dialogPreview->annotationStrokes());
        }
    }
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
    updateHistoryMaximizeButtonState();

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
    updateHistoryMaximizeButtonState();

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
            QStringLiteral("\u7b2c %1/%2 \u5f20 | %3")
                .arg(safeRow + 1)
                .arg(m_historyImageSeries.size())
                .arg(dateText));
        m_historySliceSummaryLabel->setToolTip(QString());
    }
    updateHistoryMaximizeButtonState();
    updateSliceNavigationButtons();

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
        m_historyPreview->setSyntheticImageEnabled(false);
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
    resetComparisonSyncCalibration();
    updateHistoryMaximizeButtonState();
    updateSliceNavigationButtons();
}

void PlanningPage::updateHistoryMaximizeButtonState()
{
    if (m_historyMaximizeButton == nullptr) {
        return;
    }

    const bool hasHistoryImage = !m_historyImageSeries.isEmpty();
    const bool canResetZoom = hasHistoryImage
        && m_historyPreview != nullptr
        && m_historyPreview->imageZoomFactor() > 1.001;
    m_historyMaximizeButton->setEnabled(canResetZoom);
}

void PlanningPage::updateSliceNavigationButtons()
{
    const auto updateButtons = [](QSlider* slider, QToolButton* previousButton, QToolButton* nextButton) {
        const bool canNavigate = slider != nullptr && slider->isEnabled() && slider->maximum() > slider->minimum();
        if (previousButton != nullptr) {
            previousButton->setEnabled(canNavigate && slider->value() > slider->minimum());
        }
        if (nextButton != nullptr) {
            nextButton->setEnabled(canNavigate && slider->value() < slider->maximum());
        }
    };

    updateButtons(m_historySliceSlider, m_historyPrevSliceButton, m_historyNextSliceButton);
    updateButtons(m_currentSliceSlider, m_currentPrevSliceButton, m_currentNextSliceButton);
    updateComparisonSyncState();
}

bool PlanningPage::prepareImageAcquisitionMotor(QString* errorMessage)
{
    if (!m_imageAcquisitionMotorGateway.isSdkLoaded()) {
        const QString sdkPath = defaultImageAcquisitionSdkPath();
        if (!m_imageAcquisitionMotorGateway.loadSdk(sdkPath, errorMessage)) {
            return false;
        }
    }

    if (!m_imageAcquisitionMotorGateway.isGatewayOpen()) {
        m_imageAcquisitionMotorDevices = m_imageAcquisitionMotorGateway.searchGateways(errorMessage);
        if (m_imageAcquisitionMotorDevices.isEmpty()) {
            if (errorMessage != nullptr && errorMessage->trimmed().isEmpty()) {
                *errorMessage = QStringLiteral("未搜索到 USB-CAN 网关");
            }
            return false;
        }
        QString openError;
        if (!m_imageAcquisitionMotorGateway.openGateway(m_imageAcquisitionMotorDevices.first().deviceIndex, &openError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1。请确认设备监控页没有打开同一个 USB-CAN 网关。").arg(openError);
            }
            return false;
        }
    }

    m_imageAcquisitionMotorNodes = m_imageAcquisitionMotorGateway.nodes();
    const bool hasAcquisitionAxis = std::any_of(
        m_imageAcquisitionMotorNodes.cbegin(),
        m_imageAcquisitionMotorNodes.cend(),
        [](const diji::adapters::uim::UimNodeInfo& node) {
            return static_cast<int>(node.nodeId) == kImageAcquisitionAxisNodeId;
        });
    if (!hasAcquisitionAxis) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未发现 7 号左右电机节点");
        }
        return false;
    }

    return m_imageAcquisitionMotorGateway.selectNode(kImageAcquisitionAxisNodeId, errorMessage);
}

bool PlanningPage::armImageAcquisitionMotor(QString* errorMessage)
{
    if (!m_imageAcquisitionMotorGateway.selectNode(kImageAcquisitionAxisNodeId, errorMessage)) {
        return false;
    }
    if (!m_imageAcquisitionMotorGateway.enableMotor(errorMessage)) {
        return false;
    }
    return m_imageAcquisitionMotorGateway.setSpeed(kImageAcquisitionMotorSpeed, errorMessage);
}

bool PlanningPage::readImageAcquisitionAxisSnapshot(
    diji::adapters::uim::UimMotorSnapshot* snapshot,
    QString* errorMessage)
{
    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机状态输出为空");
        }
        return false;
    }
    if (!m_imageAcquisitionMotorGateway.selectNode(kImageAcquisitionAxisNodeId, errorMessage)) {
        return false;
    }
    if (!m_imageAcquisitionMotorGateway.refreshSnapshot(errorMessage)) {
        return false;
    }

    *snapshot = m_imageAcquisitionMotorGateway.latestSnapshot();
    return true;
}

bool PlanningPage::readImageAcquisitionAxisPosition(int* positionSteps, QString* errorMessage)
{
    if (positionSteps == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机位置输出为空");
        }
        return false;
    }

    diji::adapters::uim::UimMotorSnapshot snapshot;
    if (!readImageAcquisitionAxisSnapshot(&snapshot, errorMessage)) {
        return false;
    }

    QString positionError;
    if (!imageAcquisitionSnapshotPosition(snapshot, positionSteps, &positionError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1。当前状态：%2")
                .arg(positionError, imageAcquisitionAxisStatusText(snapshot));
        }
        return false;
    }
    return true;
}

bool PlanningPage::validateImageAcquisitionTravel(int startPositionSteps, int stepSteps, int layerCount, QString* errorMessage) const
{
    if (stepSteps <= 0 || layerCount <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("图像采集层数或步长无效");
        }
        return false;
    }
    if (startPositionSteps < kImageAcquisitionMinimumPositionSteps
        || startPositionSteps > kImageAcquisitionMaximumPositionSteps) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机当前位置 %1 超出 S2(%2) 到 S1(%3) 的安全范围")
                .arg(startPositionSteps)
                .arg(kImageAcquisitionS2PositionSteps)
                .arg(kImageAcquisitionS1PositionSteps);
        }
        return false;
    }

    const qint64 plannedEndPosition = static_cast<qint64>(startPositionSteps)
        + (static_cast<qint64>(stepSteps) * layerCount);
    if (plannedEndPosition > kImageAcquisitionS1PositionSteps) {
        const int remainingSteps = std::max(0, kImageAcquisitionS1PositionSteps - startPositionSteps);
        const int maxLayers = stepSteps > 0 ? remainingSteps / stepSteps : 0;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("本次采集将到达 %1 步，超过 S1 绝对位置 %2。当前位置 %3，当前步长 %4 步，最多允许 %5 层。")
                .arg(plannedEndPosition)
                .arg(kImageAcquisitionS1PositionSteps)
                .arg(startPositionSteps)
                .arg(stepSteps)
                .arg(maxLayers);
        }
        return false;
    }

    return true;
}

bool PlanningPage::validateImageAcquisitionTravelFromSnapshot(
    const diji::adapters::uim::UimMotorSnapshot& snapshot,
    int stepSteps,
    int layerCount,
    int* startPositionSteps,
    QString* errorMessage) const
{
    int positionSteps = 0;
    QString positionError;
    if (!imageAcquisitionSnapshotPosition(snapshot, &positionSteps, &positionError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1。当前状态：%2")
                .arg(positionError, imageAcquisitionAxisStatusText(snapshot));
        }
        return false;
    }

    if (startPositionSteps != nullptr) {
        *startPositionSteps = positionSteps;
    }
    if (!validateImageAcquisitionTravel(positionSteps, stepSteps, layerCount, errorMessage)) {
        return false;
    }

    if (!snapshot.hasSensorFeedback) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机未取到 S1/S2/S3 传感器状态，禁止向 S1 执行图像采集。当前状态：%1")
                .arg(imageAcquisitionAxisStatusText(snapshot));
        }
        return false;
    }
    if (!snapshot.sensor1) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机 S1=0，已触发或处于 S1 限位，禁止继续向 S1 执行图像采集。当前状态：%1")
                .arg(imageAcquisitionAxisStatusText(snapshot));
        }
        return false;
    }

    return true;
}

bool PlanningPage::moveImageAcquisitionAxisMm(double millimeters, QString* errorMessage)
{
    const int steps = static_cast<int>(std::lround(std::max(0.0, millimeters) * kImageAcquisitionStepsPerMillimeter));
    if (steps <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("图像采集步长必须大于 0 mm");
        }
        return false;
    }
    diji::adapters::uim::UimMotorSnapshot snapshot;
    QString travelError;
    if (!readImageAcquisitionAxisSnapshot(&snapshot, &travelError)
        || !validateImageAcquisitionTravelFromSnapshot(snapshot, steps, 1, nullptr, &travelError)) {
        if (errorMessage != nullptr) {
            *errorMessage = travelError.trimmed().isEmpty()
                ? QStringLiteral("7号左右电机行程安全检查失败")
                : travelError;
        }
        return false;
    }
    if (!armImageAcquisitionMotor(errorMessage)) {
        return false;
    }
    return m_imageAcquisitionMotorGateway.setStep(steps, errorMessage);
}

bool PlanningPage::waitForImageAcquisitionAxisStop(int expectedPositionSteps, int* stoppedPositionSteps, QString* errorMessage)
{
    if (stoppedPositionSteps == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("7号电机停止位置输出为空");
        }
        return false;
    }

    diji::adapters::uim::UimMotorSnapshot startSnapshot;
    if (!readImageAcquisitionAxisSnapshot(&startSnapshot, errorMessage)) {
        stopImageAcquisitionMotorQuietly();
        return false;
    }

    int latestPositionSteps = 0;
    QString positionError;
    if (!imageAcquisitionSnapshotPosition(startSnapshot, &latestPositionSteps, &positionError)) {
        stopImageAcquisitionMotorQuietly();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1。当前状态：%2")
                .arg(positionError, imageAcquisitionAxisStatusText(startSnapshot));
        }
        return false;
    }

    const int startPositionSteps = latestPositionSteps;
    bool hasObservedMovement =
        std::abs(expectedPositionSteps - startPositionSteps) <= kImageAcquisitionStopStableToleranceSteps;
    int stableSampleCount = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kImageAcquisitionStopTimeoutMs) {
        waitForImageAcquisitionSettle(kImageAcquisitionStopPollMs);
        QCoreApplication::processEvents();

        diji::adapters::uim::UimMotorSnapshot polledSnapshot;
        QString readError;
        if (!readImageAcquisitionAxisSnapshot(&polledSnapshot, &readError)) {
            stopImageAcquisitionMotorQuietly();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("等待 7号电机停止时无法读取绝对位置：%1").arg(readError);
            }
            return false;
        }
        int polledPositionSteps = imageAcquisitionMotionPosition(startSnapshot, polledSnapshot, latestPositionSteps);

        if (polledPositionSteps < kImageAcquisitionMinimumPositionSteps
            || polledPositionSteps > kImageAcquisitionMaximumPositionSteps) {
            stopImageAcquisitionMotorQuietly();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("等待 7号电机停止时位置 %1 超出安全范围 %2-%3。当前状态：%4")
                    .arg(polledPositionSteps)
                    .arg(kImageAcquisitionMinimumPositionSteps)
                    .arg(kImageAcquisitionMaximumPositionSteps)
                    .arg(imageAcquisitionAxisStatusText(polledSnapshot));
            }
            return false;
        }
        if (polledSnapshot.hasSensorFeedback
            && !polledSnapshot.sensor1
            && polledPositionSteps < expectedPositionSteps - kImageAcquisitionReturnHomeToleranceSteps) {
            stopImageAcquisitionMotorQuietly();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("7号电机已触发 S1 下降沿但尚未到达本层目标，已主动停止。当前位置 %1，目标 %2。当前状态：%3")
                    .arg(polledPositionSteps)
                    .arg(expectedPositionSteps)
                    .arg(imageAcquisitionAxisStatusText(polledSnapshot));
            }
            return false;
        }

        if (imageAcquisitionSnapshotMoved(startSnapshot, polledSnapshot)) {
            hasObservedMovement = true;
        }
        if (!hasObservedMovement && timer.elapsed() >= kImageAcquisitionMoveStartTimeoutMs) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                                    "7号电机运动命令已下发，但 %1 ms 内 POS/QEC 均未变化。请确认7号已上电、采集方向为 S2->S1、S1 传感器没有持续触发、设备监控页未占用 USB-CAN。当前状态：%2")
                                    .arg(kImageAcquisitionMoveStartTimeoutMs)
                                    .arg(imageAcquisitionAxisStatusText(polledSnapshot));
            }
            return false;
        }
        if (std::abs(polledPositionSteps - latestPositionSteps) <= kImageAcquisitionStopStableToleranceSteps) {
            ++stableSampleCount;
        } else {
            stableSampleCount = 0;
        }
        latestPositionSteps = polledPositionSteps;

        const bool reachedExpectedPosition =
            std::abs(latestPositionSteps - expectedPositionSteps) <= kImageAcquisitionReturnHomeToleranceSteps;
        const bool stoppedAfterMovement =
            hasObservedMovement && stableSampleCount >= kImageAcquisitionStopStableSamples;
        if (reachedExpectedPosition || stoppedAfterMovement) {
            *stoppedPositionSteps = latestPositionSteps;
            return true;
        }
    }

    if (errorMessage != nullptr) {
        stopImageAcquisitionMotorQuietly();
        diji::adapters::uim::UimMotorSnapshot timeoutSnapshot;
        QString timeoutStatus;
        if (readImageAcquisitionAxisSnapshot(&timeoutSnapshot, nullptr)) {
            timeoutStatus = imageAcquisitionAxisStatusText(timeoutSnapshot);
        } else {
            timeoutStatus = QStringLiteral("无法读取");
        }
        *errorMessage = QStringLiteral("等待 7号电机停止超时，目标 %1，当前位置 %2。当前状态：%3")
            .arg(expectedPositionSteps)
            .arg(latestPositionSteps)
            .arg(timeoutStatus);
    }
    return false;
}

void PlanningPage::stopImageAcquisitionMotorQuietly()
{
    if (!m_imageAcquisitionMotorGateway.isGatewayOpen()) {
        return;
    }

    QString ignoredError;
    if (!m_imageAcquisitionMotorGateway.selectNode(kImageAcquisitionAxisNodeId, &ignoredError)) {
        return;
    }
    ignoredError.clear();
    m_imageAcquisitionMotorGateway.setSpeed(0, &ignoredError);
    ignoredError.clear();
    m_imageAcquisitionMotorGateway.setStep(0, &ignoredError);
}

void PlanningPage::waitForImageAcquisitionSettle(int milliseconds)
{
    if (milliseconds <= 0) {
        return;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(milliseconds);
    loop.exec(QEventLoop::AllEvents);
}

bool PlanningPage::waitForLatestAcquisitionFrame(int timeoutMs) const
{
    if (m_context == nullptr) {
        return false;
    }
    if (m_context->hasLatestTreatmentCameraFrame()) {
        return true;
    }
    if (timeoutMs <= 0) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_context->hasLatestTreatmentCameraFrame() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer waitTimer;
        waitTimer.setSingleShot(true);
        connect(&waitTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        waitTimer.start(50);
        loop.exec(QEventLoop::AllEvents);
    }
    return m_context->hasLatestTreatmentCameraFrame();
}

QPixmap PlanningPage::latestAcquisitionFramePixmap() const
{
    if (m_context != nullptr && m_context->hasLatestTreatmentCameraFrame()) {
        return QPixmap::fromImage(m_context->latestTreatmentCameraFrame());
    }
    return {};
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
    if (m_imageAcquisitionRunning) {
        updateAcquisitionSummary(
            QStringLiteral("图像采集"),
            {
                QStringLiteral("当前已有一轮图像采集正在执行，请等待完成。")
            });
        return;
    }
    if (!hasActivePathSelection()) {
        m_imageAcquisitionCompleted = false;
        setAnnotationEditingEnabled(false);
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
    const int stepMotorSteps = static_cast<int>(std::lround(step * kImageAcquisitionStepsPerMillimeter));
    const int channelIndex = std::max(0, m_pathList->currentRow());
    const QString channelLabel = currentChannelLabel();
    const QString channelCoordinate = currentChannelCoordinate();
    const QDateTime now = QDateTime::currentDateTime();
    const QString batchToken = now.toString(QStringLiteral("yyyyMMddhhmmss"));

    m_imageAcquisitionRunning = true;
    if (m_acquireImageButton != nullptr) {
        m_acquireImageButton->setEnabled(false);
    }
    const auto finishAcquisition = [this]() {
        m_imageAcquisitionRunning = false;
        updatePathActionState();
    };

    {
        ScopedSystemBeepMute muteSystemBeeps;

        QString motorError;
        if (!prepareImageAcquisitionMotor(&motorError)) {
            const QString detail = motorError.trimmed().isEmpty() ? QStringLiteral("7号左右电机未准备好") : motorError;
            updateAcquisitionSummary(
                QStringLiteral("图像采集未启动"),
                {
                    QStringLiteral("7号左右电机未能进入采集安全检查状态。"),
                    detail,
                    QStringLiteral("请确认 UIM SDK 已部署、USB-CAN 已连接、7号节点存在，并且设备监控页没有占用同一个网关。")
                });
            finishAcquisition();
            return;
        }

        updateAcquisitionSummary(
            QStringLiteral("图像采集安全检查中"),
            {
                QStringLiteral("7号左右电机：正在读取当前位置并预判整轮采集终点。"),
                QStringLiteral("层数：%1，步长：%2，每层 %3 步").arg(layerCount).arg(formatStepSize(step)).arg(stepMotorSteps),
                QStringLiteral("请确认医生已通过三电机控制把探头移动到起始切片位置。")
            });
        QCoreApplication::processEvents();
    }

    int acquisitionStartPositionSteps = 0;
    QString positionError;
    diji::adapters::uim::UimMotorSnapshot acquisitionStartSnapshot;
    if (!readImageAcquisitionAxisSnapshot(&acquisitionStartSnapshot, &positionError)
        || !validateImageAcquisitionTravelFromSnapshot(
            acquisitionStartSnapshot,
            stepMotorSteps,
            layerCount,
            &acquisitionStartPositionSteps,
            &positionError)) {
        stopImageAcquisitionMotorQuietly();
        const QString detail = positionError.trimmed().isEmpty() ? QStringLiteral("7号电机行程安全检查失败") : positionError;
        updateAcquisitionSummary(
            QStringLiteral("图像采集未启动"),
            {
                QStringLiteral("7号左右电机行程安全检查未通过。"),
                detail,
                QStringLiteral("采集方向固定为当前位置 -> S1，S2=%1，S1=%2。请先通过三电机控制调整 7 号当前位置，或减少层数/步长。")
                    .arg(kImageAcquisitionS2PositionSteps)
                    .arg(kImageAcquisitionS1PositionSteps)
            });
        finishAcquisition();
        return;
    }

    updateAcquisitionSummary(
        QStringLiteral("图像采集准备中"),
        {
            QStringLiteral("7号左右电机：整轮行程预判通过，起点 %1 步，终点 %2 步。")
                .arg(acquisitionStartPositionSteps)
                .arg(acquisitionStartPositionSteps + stepMotorSteps * layerCount),
            QStringLiteral("采集速度：%1").arg(kImageAcquisitionMotorSpeed),
            QStringLiteral("每层运动前仍会重新读取位置和 S1 状态。")
        });
    QCoreApplication::processEvents();

    const QString patientId = m_context->hasSelectedPatient() ? m_context->selectedPatient().id : QString();
    if (m_context != nullptr && !m_context->hasLatestTreatmentCameraFrame()) {
        waitForLatestAcquisitionFrame(kImageAcquisitionCameraWarmupTimeoutMs);
    }
    bool sawTreatmentCameraFrame = m_context != nullptr && m_context->hasLatestTreatmentCameraFrame();

    const QString automaticStorageDirectory = imageAcquisitionStorageDirectory(patientId, batchToken);
    QDir automaticStorageDir(automaticStorageDirectory);
    if (!automaticStorageDir.exists() && !automaticStorageDir.mkpath(QStringLiteral("."))) {
        const QString detail = QStringLiteral("无法创建图像采集存储目录：%1").arg(automaticStorageDirectory);
        updateAcquisitionSummary(
            QStringLiteral("图像采集未启动"),
            {
                detail,
                QStringLiteral("请确认程序目录可写，或以有权限的用户重新启动软件。")
            });
        finishAcquisition();
        return;
    }

    QString failureMessage;

    m_stagedImageSeries.clear();
    m_stagedSlices.clear();
    m_imageAcquisitionCompleted = false;
    setAnnotationEditingEnabled(false);
    m_stagedImageSeries.reserve(layerCount);
    m_stagedSlices.reserve(layerCount);
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        const bool cameraReadyForLayer = m_context != nullptr && m_context->hasLatestTreatmentCameraFrame();
        updateAcquisitionSummary(
            QStringLiteral("图像采集中"),
            {
                QStringLiteral("当前通道：%1").arg(channelLabel),
                QStringLiteral("进度：%1 / %2").arg(layerIndex + 1).arg(layerCount),
                QStringLiteral("7号左右电机：当前位置 -> S1，准备移动 %1（%2 步）").arg(formatStepSize(step)).arg(stepMotorSteps),
                cameraReadyForLayer
                    ? QStringLiteral("治疗屏画面：电机停止后捕捉当前帧")
                    : QStringLiteral("治疗屏画面：等待 USB 相机实时帧")
            });
        QCoreApplication::processEvents();

        int currentPositionSteps = 0;
        diji::adapters::uim::UimMotorSnapshot currentSnapshot;
        QString travelError;
        if (!readImageAcquisitionAxisSnapshot(&currentSnapshot, &travelError)
            || !validateImageAcquisitionTravelFromSnapshot(
                currentSnapshot,
                stepMotorSteps,
                layerCount - layerIndex,
                &currentPositionSteps,
                &travelError)) {
            failureMessage = QStringLiteral("第 %1 层前安全检查失败：%2").arg(layerIndex + 1).arg(travelError);
            break;
        }

        QString moveError;
        if (!moveImageAcquisitionAxisMm(step, &moveError)) {
            failureMessage = QStringLiteral("第 %1 层移动失败：%2").arg(layerIndex + 1).arg(moveError);
            break;
        }

        int slicePositionSteps = -1;
        QString stopError;
        const int expectedPositionSteps = currentPositionSteps + stepMotorSteps;
        if (!waitForImageAcquisitionAxisStop(expectedPositionSteps, &slicePositionSteps, &stopError)) {
            failureMessage = QStringLiteral("第 %1 层等待 7 号电机停止失败：%2").arg(layerIndex + 1).arg(stopError);
            break;
        }
        if (slicePositionSteps < kImageAcquisitionMinimumPositionSteps
            || slicePositionSteps > kImageAcquisitionMaximumPositionSteps) {
            failureMessage = QStringLiteral("第 %1 层 7 号绝对位置 %2 超出安全范围 %3-%4")
                .arg(layerIndex + 1)
                .arg(slicePositionSteps)
                .arg(kImageAcquisitionMinimumPositionSteps)
                .arg(kImageAcquisitionMaximumPositionSteps);
            break;
        }

        waitForImageAcquisitionSettle(120);
        if (m_context != nullptr && !m_context->hasLatestTreatmentCameraFrame()) {
            waitForLatestAcquisitionFrame(kImageAcquisitionCameraWarmupTimeoutMs);
        }
        const bool capturedFromTreatmentScreen = m_context != nullptr && m_context->hasLatestTreatmentCameraFrame();
        sawTreatmentCameraFrame = sawTreatmentCameraFrame || capturedFromTreatmentScreen;
        const QPixmap capturedFrame = latestAcquisitionFramePixmap();
        if (capturedFrame.isNull()) {
            failureMessage = QStringLiteral("第 %1 层未捕捉到治疗屏实时画面，已中止采集").arg(layerIndex + 1);
            break;
        }

        const QString sliceStoragePath = imageAcquisitionSliceStoragePath(
            patientId,
            batchToken,
            channelIndex,
            layerIndex,
            slicePositionSteps);
        if (!capturedFrame.save(sliceStoragePath, "PNG")) {
            failureMessage = QStringLiteral("第 %1 层图像保存失败：%2").arg(layerIndex + 1).arg(sliceStoragePath);
            break;
        }

        ImageSeriesRecord stagedSlice;
        stagedSlice.patientId = patientId;
        stagedSlice.type = QStringLiteral("\u8d85\u58f0\u626b\u63cf\u5207\u7247");
        stagedSlice.storagePath = QDir::toNativeSeparators(sliceStoragePath);
        stagedSlice.acquisitionDate = now.date();
        stagedSlice.notes = QStringLiteral("staged capture | channel: %1 | origin: %2 | slice: %3/%4 | step: %5")
            .arg(channelLabel)
            .arg(channelCoordinate)
            .arg(layerIndex + 1)
            .arg(layerCount)
            .arg(formatStepSize(step));
        stagedSlice.notes += QStringLiteral(" | motor: %1 | axis7_abs_steps: %2 | image: %3")
            .arg(QStringLiteral("axis7 current->S1 +%1 steps").arg(stepMotorSteps))
            .arg(slicePositionSteps)
            .arg(capturedFromTreatmentScreen ? QStringLiteral("treatment-screen") : QStringLiteral("usb-camera"));
        stagedSlice.createdAt = now;
        m_stagedImageSeries.push_back(stagedSlice);

        StagedSliceState stagedState;
        stagedState.image = stagedSlice;
        stagedState.capturedFrame = capturedFrame;
        stagedState.acquisitionAxis7PositionSteps = slicePositionSteps;
        stagedState.label = QStringLiteral("[S%1] \u6682\u5b58\u5207\u7247-%2")
            .arg(layerIndex + 1, 2, 10, QChar('0'))
            .arg(layerIndex + 1, 2, 10, QChar('0'));
        stagedState.pattern = m_lineTreatmentRadio->isChecked() ? TreatmentPattern::Line : TreatmentPattern::Point;
        stagedState.spacingMm = m_spacingSpin->value();
        stagedState.dwellSeconds = m_dwellSpin->value();
        stagedState.powerWatts = m_powerSpin->value();
        stagedState.respiratoryTrackingEnabled = m_respiratoryTrackingCheck->isChecked();
        stagedState.deliveryMode = m_segmentedTreatmentRadio->isChecked() ? QStringLiteral("\u5206\u6bb5\u6267\u884c") : QStringLiteral("\u76f4\u63a5\u6cbb\u7597");
        m_stagedSlices.push_back(stagedState);
    }
    if (!failureMessage.isEmpty()) {
        stopImageAcquisitionMotorQuietly();
    }

    {
        const QSignalBlocker blocker(m_modelList);
        m_modelList->clear();
        for (const StagedSliceState& slice : std::as_const(m_stagedSlices)) {
            m_modelList->addItem(slice.label);
        }
    }
    if (!m_stagedSlices.isEmpty()) {
        m_imageAcquisitionCompleted = true;
        setAnnotationEditingEnabled(true);
        m_modelList->setCurrentRow(0);
        loadStagedSlice(0);
    } else if (m_preview != nullptr) {
        m_imageAcquisitionCompleted = false;
        setAnnotationEditingEnabled(false);
        m_preview->setAnnotationStrokes({});
        m_preview->setSliceContext(0, 0);
        m_preview->setSyntheticImageEnabled(false);
        m_preview->setCaption(QStringLiteral(""));
    }

    m_lastAcquisitionAt = QDateTime::currentDateTime();
    updateAssessmentMetricsPanel(0.0, 0.0);
    if (m_previewOverlayLabel != nullptr) {
        m_previewOverlayLabel->setVisible(m_stagedSlices.isEmpty() && !m_context->hasActivePlan());
    }

    const int capturedFrameCount = std::count_if(m_stagedSlices.cbegin(), m_stagedSlices.cend(), [](const StagedSliceState& slice) {
        return !slice.capturedFrame.isNull();
    });
    const int savedFrameCount = std::count_if(m_stagedImageSeries.cbegin(), m_stagedImageSeries.cend(), [](const ImageSeriesRecord& image) {
        return QFileInfo::exists(image.storagePath);
    });
    QStringList summaryLines {
        QStringLiteral("当前通道：%1").arg(channelLabel),
        QStringLiteral("起始坐标：%1").arg(channelCoordinate),
        QStringLiteral("层数：%1").arg(layerCount),
        QStringLiteral("步长：%1").arg(formatStepSize(step)),
        QStringLiteral("运动换算：1 mm = %1 步，当前每层 %2 步").arg(kImageAcquisitionStepsPerMillimeter, 0, 'f', 0).arg(stepMotorSteps),
        QStringLiteral("7号左右电机：S2=%1，S1=%2，本次采集起点=%3")
            .arg(kImageAcquisitionS2PositionSteps)
            .arg(kImageAcquisitionS1PositionSteps)
            .arg(acquisitionStartPositionSteps),
        QStringLiteral("7号左右电机：已按每层 %1 移动，并在停止后捕捉治疗屏画面").arg(formatStepSize(step)),
        QStringLiteral("暂存图像：%1 张").arg(m_stagedImageSeries.size()),
        QStringLiteral("实时相机帧：%1 张").arg(capturedFrameCount),
        QStringLiteral("已保存图像：%1 张").arg(savedFrameCount),
        QStringLiteral("自动存储目录：%1").arg(QDir::toNativeSeparators(automaticStorageDirectory)),
        QStringLiteral("采集结束后已显示到右侧当前治疗影像，可直接进行圈画。")
    };
    if (!sawTreatmentCameraFrame || capturedFrameCount == 0) {
        summaryLines.insert(6, QStringLiteral("治疗屏画面：未取到实时帧，本次采集没有生成切片。"));
    }
    if (!failureMessage.isEmpty()) {
        for (QString& line : summaryLines) {
            if (line.startsWith(QStringLiteral("7号左右电机：已按每层"))) {
                line = QStringLiteral("7号左右电机：采集中止，未完成全部层位移动。");
            } else if (line.startsWith(QStringLiteral("采集结束后已显示"))) {
                line = QStringLiteral("采集中止，未更新当前治疗影像。");
            }
        }
    }
    if (!failureMessage.isEmpty()) {
        summaryLines.prepend(failureMessage);
    }

    updateAcquisitionSummary(
        failureMessage.isEmpty() ? QStringLiteral("图像采集已完成") : QStringLiteral("图像采集中止"),
        summaryLines);

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("operator"),
            QStringLiteral("planning"),
            QStringLiteral("\u5b8c\u6210\u56fe\u50cf\u91c7\u96c6\u6682\u5b58\uff1a%1\uff0c\u5c42\u6570 %2\uff0c\u6b65\u957f %3").arg(channelLabel).arg(layerCount).arg(step));
    }
    finishAcquisition();
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
        loadDemoPatient(false);
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

    updatePlanPreviewText(&previewPlan);

    QStringList previewLines {
        QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u5b8c\u6574\u4fe1\u606f"),
        QString()
    };
    const QString planPreviewText = m_planPreview != nullptr ? m_planPreview->toPlainText().trimmed() : QString();
    previewLines << (planPreviewText.isEmpty() ? summarizePlan(previewPlan) : planPreviewText);
    previewLines << QString()
                 << QStringLiteral("\u5f53\u524d\u901a\u9053\uff1a%1").arg(currentChannelLabel())
                 << QStringLiteral("\u5750\u6807\uff1a%1").arg(currentChannelCoordinate());

    const QString assessmentText = m_assessmentPreview != nullptr ? m_assessmentPreview->toPlainText().trimmed() : QString();
    if (!assessmentText.isEmpty()) {
        previewLines << QString() << QStringLiteral("\u65b9\u6848\u8bc4\u4f30\u4e0e\u8fc7\u7a0b\u8bb0\u5f55\uff1a") << assessmentText;
    }

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
    dialog.setObjectName(QStringLiteral("planningPlanPreviewDialog"));
    dialog.resize(860, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* headerLabel = new QLabel(QStringLiteral("\u5f53\u524d\u9875\u9762\u4ec5\u4fdd\u7559\u64cd\u4f5c\u5165\u53e3\uff0c\u5b8c\u6574\u65b9\u6848\u4fe1\u606f\u5728\u6b64\u9884\u89c8\u3002"));
    headerLabel->setWordWrap(true);
    auto* previewText = new QPlainTextEdit();
    previewText->setObjectName(QStringLiteral("planningSummaryEdit"));
    previewText->setReadOnly(true);
    previewText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    previewText->viewport()->setObjectName(QStringLiteral("planningSummaryViewport"));
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

    if (m_totalLengthValueLabel != nullptr) {
        m_totalLengthValueLabel->setText(QString::number(m_layerCountSpin->value() * m_stepSpin->value()));
    }
    m_totalDurationValueLabel->setText(QStringLiteral("%1").arg(totalMinutes, 0, 'f', 2));
    m_powerValueLabel->setText(QStringLiteral("%1W").arg(m_powerSpin->value(), 0, 'f', 0));
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
