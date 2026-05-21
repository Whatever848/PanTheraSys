#include "modules/treatment/treatment_page.h"

#include "modules/shared/therapy_imaging_algorithms.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
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

QString formatTreatmentDuration(double seconds)
{
    if (seconds <= 0.0) {
        return QStringLiteral("0 s");
    }

    if (seconds < 60.0) {
        return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
    }

    const int roundedSeconds = static_cast<int>(std::ceil(seconds));
    const int hours = roundedSeconds / 3600;
    const int minutes = (roundedSeconds % 3600) / 60;
    const int remainingSeconds = roundedSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(remainingSeconds, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(remainingSeconds, 2, 10, QChar('0'));
}

struct TreatmentVolumeTarget {
    int layerIndex {0};
    int pointIndex {0};
    QPointF positionMm;
    double zMm {0.0};
    bool treated {false};
};

QPointF projectTreatmentVolumePoint(const QVector3D& point)
{
    return QPointF(point.x() + (point.z() * 0.58), -point.y() - (point.z() * 0.42));
}

bool treatmentVolumePointLess(const QPointF& left, const QPointF& right)
{
    if (!qFuzzyCompare(left.x(), right.x())) {
        return left.x() < right.x();
    }
    return left.y() < right.y();
}

double treatmentVolumeCross(const QPointF& origin, const QPointF& left, const QPointF& right)
{
    return ((left.x() - origin.x()) * (right.y() - origin.y()))
        - ((left.y() - origin.y()) * (right.x() - origin.x()));
}

QVector<QPointF> buildTreatmentVolumeHull(QVector<QPointF> points)
{
    if (points.size() <= 3) {
        return points;
    }

    std::sort(points.begin(), points.end(), treatmentVolumePointLess);
    points.erase(
        std::unique(points.begin(), points.end(), [](const QPointF& left, const QPointF& right) {
            return qFuzzyCompare(left.x(), right.x()) && qFuzzyCompare(left.y(), right.y());
        }),
        points.end());

    if (points.size() <= 3) {
        return points;
    }

    QVector<QPointF> lower;
    for (const QPointF& point : points) {
        while (lower.size() >= 2
            && treatmentVolumeCross(lower.at(lower.size() - 2), lower.constLast(), point) <= 0.0) {
            lower.removeLast();
        }
        lower.push_back(point);
    }

    QVector<QPointF> upper;
    for (auto it = points.crbegin(); it != points.crend(); ++it) {
        while (upper.size() >= 2
            && treatmentVolumeCross(upper.at(upper.size() - 2), upper.constLast(), *it) <= 0.0) {
            upper.removeLast();
        }
        upper.push_back(*it);
    }

    lower.removeLast();
    upper.removeLast();
    lower += upper;
    return lower;
}

QVector<QPointF> buildTreatmentVolumeContour(
    const TherapySegment& segment,
    int layerIndex,
    int layerCount,
    double spacingMm)
{
    if (segment.points.isEmpty()) {
        return buildFallbackLesionContourMm(layerIndex, layerCount);
    }

    QVector<QPointF> points;
    points.reserve(segment.points.size());
    for (const TherapyPoint& point : segment.points) {
        points.push_back(point.positionMm);
    }

    const double marginMm = std::clamp(spacingMm * 0.55, 1.5, 4.5);
    QVector<QPointF> hull = buildTreatmentVolumeHull(points);
    if (hull.size() >= 3 && contourAreaMm2(hull) > 0.1) {
        QPointF centroid;
        for (const QPointF& point : hull) {
            centroid += point;
        }
        centroid /= static_cast<qreal>(hull.size());

        QVector<QPointF> expandedHull;
        expandedHull.reserve(hull.size());
        for (const QPointF& point : hull) {
            const QPointF delta = point - centroid;
            const double length = std::hypot(delta.x(), delta.y());
            if (length <= 0.001) {
                expandedHull.push_back(point);
                continue;
            }
            const double scale = (length + marginMm) / length;
            expandedHull.push_back(QPointF(
                centroid.x() + (delta.x() * scale),
                centroid.y() + (delta.y() * scale)));
        }
        return expandedHull;
    }

    QRectF bounds(points.constFirst(), points.constFirst());
    for (const QPointF& point : points) {
        bounds = bounds.united(QRectF(point, point));
    }

    const qreal minimumSpan = std::max<qreal>(4.0, spacingMm * 1.5);
    if (bounds.width() < minimumSpan) {
        const qreal delta = (minimumSpan - bounds.width()) * 0.5;
        bounds.adjust(-delta, 0.0, delta, 0.0);
    }
    if (bounds.height() < minimumSpan) {
        const qreal delta = (minimumSpan - bounds.height()) * 0.5;
        bounds.adjust(0.0, -delta, 0.0, delta);
    }
    bounds = bounds.adjusted(-marginMm, -marginMm, marginMm, marginMm);

    return {
        bounds.topLeft(),
        bounds.topRight(),
        bounds.bottomRight(),
        bounds.bottomLeft()
    };
}

QPixmap renderTreatmentVolumeProgressPreview(
    const VolumeReconstructionResult& reconstruction,
    const QVector<VolumeContourSlice>& contourSlices,
    const QVector<TreatmentVolumeTarget>& targets,
    double sliceSpacingMm,
    int selectedLayerIndex)
{
    QPixmap preview = reconstruction.preview;
    if (preview.isNull()) {
        preview = QPixmap(980, 620);
        preview.fill(QColor(12, 20, 33));
    }

    const QRectF plotRect = preview.rect().adjusted(34, 28, -34, -52);
    QRectF rawBounds;
    bool hasProjectedPoint = false;
    const double clampedSpacing = std::max(0.5, sliceSpacingMm);

    const auto includeProjectedPoint = [&](const QPointF& point) {
        if (!hasProjectedPoint) {
            rawBounds = QRectF(point, point);
            hasProjectedPoint = true;
            return;
        }
        rawBounds = rawBounds.united(QRectF(point, point));
    };

    for (const VolumeContourSlice& slice : contourSlices) {
        const double zMm = slice.sliceIndex * clampedSpacing;
        for (const QPointF& point : slice.contourMm) {
            includeProjectedPoint(projectTreatmentVolumePoint(QVector3D(point.x(), point.y(), zMm)));
        }
    }

    if (!hasProjectedPoint) {
        return preview;
    }

    const qreal scaleX = plotRect.width() / std::max<qreal>(rawBounds.width(), 1.0);
    const qreal scaleY = plotRect.height() / std::max<qreal>(rawBounds.height(), 1.0);
    const qreal scale = std::min(scaleX, scaleY);

    const auto mapProjectedPoint = [&](const QPointF& point) {
        return QPointF(
            plotRect.left() + ((point.x() - rawBounds.left()) * scale),
            plotRect.top() + ((point.y() - rawBounds.top()) * scale));
    };

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 62));
    painter.drawRoundedRect(plotRect.adjusted(6, 6, -6, -6), 14, 14);

    const auto drawTarget = [&](const TreatmentVolumeTarget& target) {
        const QPointF rawPoint = projectTreatmentVolumePoint(QVector3D(
            target.positionMm.x(),
            target.positionMm.y(),
            target.zMm));
        const QPointF mappedPoint = mapProjectedPoint(rawPoint);
        const bool currentLayer = target.layerIndex == selectedLayerIndex;
        const qreal radius = target.treated ? 5.4 : (currentLayer ? 5.1 : 4.6);

        if (target.treated) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(52, 216, 111, 58));
            painter.drawEllipse(mappedPoint, radius + 4.0, radius + 4.0);
            painter.setBrush(QColor(52, 216, 111, 225));
            painter.setPen(QPen(QColor(199, 255, 221, 232), 1.1));
        } else {
            painter.setBrush(QColor(28, 165, 255, currentLayer ? 94 : 66));
            painter.setPen(QPen(currentLayer ? QColor(255, 209, 92, 232) : QColor(33, 218, 255, 218), 1.4));
        }
        painter.drawEllipse(mappedPoint, radius, radius);
    };

    painter.setClipRect(plotRect.adjusted(-2, -2, 2, 2));
    for (const TreatmentVolumeTarget& target : targets) {
        if (!target.treated) {
            drawTarget(target);
        }
    }
    for (const TreatmentVolumeTarget& target : targets) {
        if (target.treated) {
            drawTarget(target);
        }
    }
    painter.setClipping(false);

    const QRectF legendRect(plotRect.right() - 264.0, plotRect.top() + 10.0, 252.0, 92.0);
    painter.setPen(QPen(QColor(69, 100, 138, 210), 1.0));
    painter.setBrush(QColor(10, 22, 38, 218));
    painter.drawRoundedRect(legendRect, 12.0, 12.0);

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
    painter.setPen(QColor(238, 248, 255));
    painter.drawText(
        legendRect.adjusted(14.0, 8.0, -12.0, -62.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("\u6cbb\u7597\u4e09\u7ef4\u5b9e\u65f6\u9884\u89c8"));

    const auto drawLegendEntry = [&](const QPointF& center, const QColor& fill, const QColor& stroke, const QString& text) {
        painter.setBrush(fill);
        painter.setPen(QPen(stroke, 1.4));
        painter.drawEllipse(center, 6.0, 6.0);
        painter.setPen(QColor(214, 230, 248));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::Bold));
        painter.drawText(QRectF(center.x() + 14.0, center.y() - 10.0, 108.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter, text);
    };

    drawLegendEntry(
        QPointF(legendRect.left() + 22.0, legendRect.top() + 48.0),
        QColor(52, 216, 111, 225),
        QColor(199, 255, 221, 232),
        QStringLiteral("\u5df2\u6cbb\u7597\u7ec6\u80de"));
    drawLegendEntry(
        QPointF(legendRect.left() + 142.0, legendRect.top() + 48.0),
        QColor(28, 165, 255, 94),
        QColor(33, 218, 255, 218),
        QStringLiteral("\u5f85\u6cbb\u7597\u7ec6\u80de"));

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    painter.setPen(QColor(166, 190, 220));
    painter.drawText(
        legendRect.adjusted(14.0, 62.0, -12.0, -8.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("\u9ec4\u8272\u8f6e\u5ed3\u8868\u793a\u5f53\u524d\u6cbb\u7597\u5c42"));

    return preview;
}

QToolButton* createLayerNavButton(const QString& text, const QString& tooltip)
{
    auto* button = new QToolButton();
    button->setObjectName(QStringLiteral("treatmentLayerNavButton"));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setEnabled(false);
    return button;
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
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* imageCard = new QGroupBox(QStringLiteral("\u6cbb\u7597\u6267\u884c\u89c6\u56fe"));
    auto* imageLayout = new QVBoxLayout(imageCard);
    m_preview = new MockUltrasoundView();
    m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
    imageLayout->addWidget(m_preview);
    rootLayout->addWidget(imageCard, 3);

    auto* controlCard = new QGroupBox(QStringLiteral("\u6cbb\u7597\u63a7\u5236"));
    controlCard->setMinimumWidth(400);
    controlCard->setMaximumWidth(440);
    auto* controlLayout = new QVBoxLayout(controlCard);
    controlLayout->setContentsMargins(12, 12, 12, 12);
    controlLayout->setSpacing(8);
    m_patientLabel = new QLabel(QStringLiteral("\u60a3\u8005\uff1a\u672a\u9009\u62e9"));
    m_patientLabel->setParent(controlCard);
    m_patientLabel->setVisible(false);
    m_planCombo = new QComboBox();
    m_planCombo->setMinimumWidth(0);
    m_planSummaryLabel = new QLabel(QStringLiteral("\u6267\u884c\u4fe1\u606f\uff1a\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848"));
    m_planSummaryLabel->setObjectName(QStringLiteral("treatmentExecutionSummaryLabel"));
    m_planSummaryLabel->setWordWrap(true);
    m_planSummaryLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_planSummaryLabel->setMinimumHeight(58);
    m_planSummaryLabel->setMaximumHeight(72);
    m_layerLabel = new QLabel(QStringLiteral("\u6cbb\u7597\u5c42\uff1a\u672a\u9009\u62e9"));
    m_layerLabel->setObjectName(QStringLiteral("treatmentLayerLabel"));
    m_layerLabel->setWordWrap(true);
    m_layerSlider = new QSlider(Qt::Horizontal);
    m_layerSlider->setObjectName(QStringLiteral("treatmentLayerSlider"));
    m_layerSlider->setRange(0, 0);
    m_layerSlider->setSingleStep(1);
    m_layerSlider->setPageStep(1);
    m_layerSlider->setEnabled(false);
    m_previousLayerButton = createLayerNavButton(QStringLiteral("\u2039"), QStringLiteral("\u5207\u6362\u5230\u4e0a\u4e00\u5c42\u6cbb\u7597\u5f71\u50cf"));
    m_nextLayerButton = createLayerNavButton(QStringLiteral("\u203a"), QStringLiteral("\u5207\u6362\u5230\u4e0b\u4e00\u5c42\u6cbb\u7597\u5f71\u50cf"));
    m_modeLabel = new QLabel(QStringLiteral("\u6a21\u5f0f\uff1a%1").arg(toDisplayString(m_safetyKernel->mode())));
    m_modeLabel->setParent(controlCard);
    m_modeLabel->setVisible(false);
    m_safetyLabel = new QLabel(QStringLiteral("\u5b89\u5168\u72b6\u6001\uff1a%1").arg(m_safetyKernel->snapshot().message));
    m_safetyLabel->setParent(controlCard);
    m_safetyLabel->setVisible(false);
    m_progressLabel = new QLabel(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
    m_progressLabel->setObjectName(QStringLiteral("treatmentProgressLabel"));
    m_timeSummaryLabel = new QLabel(QStringLiteral("\u65f6\u95f4\uff1a\u672c\u5c42\u5269\u4f59 -- | \u672c\u5c42\u603b\u65f6\u957f -- | \u6cbb\u7597\u603b\u65f6\u957f --"));
    m_timeSummaryLabel->setObjectName(QStringLiteral("treatmentTimeSummaryLabel"));
    m_timeSummaryLabel->setWordWrap(true);
    m_timeSummaryLabel->setParent(controlCard);
    m_timeSummaryLabel->setVisible(false);
    auto createTimeMetricCard = [](const QString& title, QLabel** valueLabel) {
        auto* card = new QFrame();
        card->setObjectName(QStringLiteral("treatmentTimeMetricCard"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(4);
        auto* titleLabel = new QLabel(title);
        titleLabel->setObjectName(QStringLiteral("treatmentTimeMetricTitleLabel"));
        auto* value = new QLabel(QStringLiteral("--"));
        value->setObjectName(QStringLiteral("treatmentTimeMetricValueLabel"));
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(titleLabel);
        layout->addWidget(value);
        *valueLabel = value;
        return card;
    };
    auto* timeMetricsGrid = new QGridLayout();
    timeMetricsGrid->setContentsMargins(0, 0, 0, 0);
    timeMetricsGrid->setHorizontalSpacing(8);
    timeMetricsGrid->setVerticalSpacing(8);
    timeMetricsGrid->addWidget(createTimeMetricCard(QStringLiteral("\u672c\u5c42\u5269\u4f59"), &m_layerRemainingValueLabel), 0, 0);
    timeMetricsGrid->addWidget(createTimeMetricCard(QStringLiteral("\u672c\u5c42\u603b\u65f6\u95f4"), &m_layerTotalValueLabel), 0, 1);
    timeMetricsGrid->addWidget(createTimeMetricCard(QStringLiteral("\u5168\u90e8\u5269\u4f59"), &m_planRemainingValueLabel), 1, 0);
    timeMetricsGrid->addWidget(createTimeMetricCard(QStringLiteral("\u5168\u90e8\u65f6\u95f4"), &m_planTotalValueLabel), 1, 1);
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_logView = new QPlainTextEdit();
    m_logView->setObjectName(QStringLiteral("treatmentLogView"));
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(84);
    m_logView->setMaximumHeight(100);
    m_logView->document()->setMaximumBlockCount(120);

    m_startButton = new QPushButton(QStringLiteral("\u5f00\u59cb"));
    m_pauseButton = new QPushButton(QStringLiteral("\u6682\u505c"));
    m_stopButton = new QPushButton(QStringLiteral("\u7ec8\u6b62"));
    m_startButton->setObjectName(QStringLiteral("treatmentControlButton"));
    m_pauseButton->setObjectName(QStringLiteral("treatmentControlButton"));
    m_stopButton->setObjectName(QStringLiteral("treatmentControlButton"));
    m_generate3dButton = new QPushButton(QStringLiteral("\u4e09\u7ef4\u56fe\u5f62\u751f\u6210"));
    m_generate3dButton->setObjectName(QStringLiteral("treatmentVolumeButton"));
    m_generate3dButton->setEnabled(false);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_pauseButton);
    buttonRow->addWidget(m_stopButton);

    auto* layerSliderRow = new QHBoxLayout();
    layerSliderRow->setContentsMargins(0, 0, 0, 0);
    layerSliderRow->setSpacing(6);
    layerSliderRow->addWidget(m_previousLayerButton);
    layerSliderRow->addWidget(m_layerSlider, 1);
    layerSliderRow->addWidget(m_nextLayerButton);

    auto* logTitle = new QLabel(QStringLiteral("\u6267\u884c\u65e5\u5fd7"));
    logTitle->setObjectName(QStringLiteral("treatmentLogTitleLabel"));

    controlLayout->addWidget(m_planCombo);
    controlLayout->addWidget(m_planSummaryLabel);
    controlLayout->addWidget(m_layerLabel);
    controlLayout->addLayout(layerSliderRow);
    controlLayout->addWidget(m_progressLabel);
    controlLayout->addLayout(timeMetricsGrid);
    controlLayout->addWidget(m_progressBar);
    controlLayout->addWidget(m_generate3dButton);
    controlLayout->addLayout(buttonRow);
    controlLayout->addWidget(logTitle);
    controlLayout->addWidget(m_logView);
    rootLayout->addWidget(controlCard, 1);

    m_progressTimer.setInterval(450);

    connect(m_startButton, &QPushButton::clicked, this, &TreatmentPage::startTreatment);
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (m_pauseButton->property("pauseResumeState").toString() == QStringLiteral("resume")) {
            resumeTreatment();
            return;
        }
        pauseTreatment();
    });
    connect(m_stopButton, &QPushButton::clicked, this, &TreatmentPage::stopTreatment);
    connect(m_generate3dButton, &QPushButton::clicked, this, &TreatmentPage::generateThreeDimensionalImage);
    connect(&m_progressTimer, &QTimer::timeout, this, &TreatmentPage::advanceProgress);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &TreatmentPage::onActivePlanChanged);
    connect(m_context, &ApplicationContext::activePlanCleared, this, &TreatmentPage::onActivePlanCleared);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &TreatmentPage::onPatientChanged);
    connect(m_planCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &TreatmentPage::onPlanSelectionChanged);
    connect(m_layerSlider, &QSlider::valueChanged, this, &TreatmentPage::onLayerSelectionChanged);
    connect(m_previousLayerButton, &QToolButton::clicked, this, [this]() {
        if (m_layerSlider == nullptr || !m_layerSlider->isEnabled()) {
            return;
        }
        m_layerSlider->setValue(std::max(m_layerSlider->minimum(), m_layerSlider->value() - 1));
    });
    connect(m_nextLayerButton, &QToolButton::clicked, this, [this]() {
        if (m_layerSlider == nullptr || !m_layerSlider->isEnabled()) {
            return;
        }
        m_layerSlider->setValue(std::min(m_layerSlider->maximum(), m_layerSlider->value() + 1));
    });
    connect(m_safetyKernel, &SafetyKernel::safetySnapshotChanged, this, &TreatmentPage::onSafetyChanged);
    connect(m_safetyKernel, &SafetyKernel::systemModeChanged, this, [this](SystemMode mode) {
        m_modeLabel->setText(QStringLiteral("\u6a21\u5f0f\uff1a%1").arg(toDisplayString(mode)));
    });
    connect(m_safetyKernel, &SafetyKernel::treatmentAbortRequested, this, &TreatmentPage::onAbortRequested);

    setButtonState(false, false, false, false);
    refreshAvailablePlans(!m_context->hasActivePlan());
}

void TreatmentPage::startTreatment()
{
    if (!m_context->hasActivePlan()) {
        appendLog(QStringLiteral("\u5f53\u524d\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848\uff0c\u65e0\u6cd5\u5f00\u59cb\u6cbb\u7597"));
        return;
    }
    if (m_deferStartupPlanSelection) {
        appendLog(QStringLiteral("\u8bf7\u5148\u5728\u4e0a\u65b9\u4e0b\u62c9\u5217\u8868\u91cc\u9009\u62e9\u6cbb\u7597\u65b9\u6848"));
        return;
    }
    if (!isPlanTreatable(m_context->activePlan())) {
        appendLog(QStringLiteral("\u5f53\u524d\u65b9\u6848\u5c1a\u672a\u5ba1\u6838\u901a\u8fc7\uff0c\u65e0\u6cd5\u5f00\u59cb\u6cbb\u7597"));
        return;
    }
    if (totalPointCount() <= 0) {
        appendLog(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5c42\u6ca1\u6709\u9776\u70b9\uff0c\u8bf7\u9009\u62e9\u5176\u4ed6\u5c42"));
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

    const TherapyPlan& plan = m_context->activePlan();
    ensureLayerProgressStorage(plan);
    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    if (m_selectedLayerIndex >= 0 && m_selectedLayerIndex < m_layerCompletedPointCounts.size()) {
        m_layerCompletedPointCounts[m_selectedLayerIndex] = 0;
    }
    updateLayerPreview();
    m_progressTimer.start();
    m_planCombo->setEnabled(false);
    m_layerSlider->setEnabled(false);
    updateLayerNavigationButtons();
    setButtonState(false, true, false, true);
    appendLog(
        QStringLiteral("\u5f00\u59cb\u7b2c%1/%2\u5c42\u6cbb\u7597\u6267\u884c")
            .arg(normalizedLayerIndex(plan) + 1)
            .arg(layerCount(&plan)));
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
    m_layerSlider->setEnabled(false);
    updateLayerNavigationButtons();
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
    m_layerSlider->setEnabled(false);
    updateLayerNavigationButtons();
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
    ensureLayerProgressStorage(plan);
    if (m_selectedLayerIndex >= 0 && m_selectedLayerIndex < m_layerCompletedPointCounts.size()) {
        m_layerCompletedPointCounts[m_selectedLayerIndex] = std::min(m_completedPointCount, totalPoints);
    }
    const TherapySegment* segment = selectedLayerSegment();
    const int maximumPointIndex = segment == nullptr || segment->points.isEmpty()
        ? -1
        : static_cast<int>(segment->points.size()) - 1;
    const int pointIndex = segment == nullptr || segment->points.isEmpty()
        ? -1
        : std::clamp(m_completedPointCount - 1, 0, maximumPointIndex);
    const TherapyPoint* point = pointIndex >= 0 ? &segment->points.at(pointIndex) : nullptr;
    const double dwellSeconds = point != nullptr ? pointDwellSeconds(*point, plan) : (plan.dwellSeconds > 0.0 ? plan.dwellSeconds : 0.3);
    const double powerWatts = point != nullptr && point->powerWatts > 0.0
        ? point->powerWatts
        : plan.plannedPowerWatts;
    m_deliveredEnergyJ += powerWatts * dwellSeconds;

    updateProgressText();
    m_preview->setCompletedPointCount(m_completedPointCount);

    if (m_completedPointCount >= totalPoints) {
        appendLog(
            QStringLiteral("\u7b2c%1/%2\u5c42\u6cbb\u7597\u6267\u884c\u5b8c\u6210")
                .arg(normalizedLayerIndex(plan) + 1)
                .arg(layerCount(&plan)));
        finalizeTreatment(QStringLiteral("\u7b2c%1\u5c42\u5b8c\u6210").arg(normalizedLayerIndex(plan) + 1));
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
        configureLayerSelector(nullptr);
        updateProgressText();
        m_preview->clearPlan();
        m_preview->setCompletedPointCount(0);
        m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
        m_planCombo->setEnabled(hasSelectablePlans());
        setButtonState(false, false, false, false);
        updateVolumeButtonState();
        return;
    }

    const int comboIndex = m_planCombo->findData(plan.id);
    if (comboIndex >= 0 && comboIndex != m_planCombo->currentIndex()) {
        const QSignalBlocker blocker(m_planCombo);
        m_planCombo->setCurrentIndex(comboIndex);
    }
    m_selectedLayerIndex = 0;
    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    m_layerCompletedPointCounts.fill(0, plan.segments.size());
    updatePlanSummary(&plan);
    updateLayerPreview();
    m_planCombo->setEnabled(true);
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && canTreatSelectedLayer(), false, false, false);
    updateVolumeButtonState();
}

void TreatmentPage::onActivePlanCleared()
{
    m_layerCompletedPointCounts.clear();
    updatePlanSummary(nullptr);
    configureLayerSelector(nullptr);
    updateProgressText();
    m_preview->clearPlan();
    m_preview->setCompletedPointCount(0);
    m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
    m_planCombo->setEnabled(hasSelectablePlans());
    setButtonState(false, false, false, false);
    updateVolumeButtonState();
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

    if (m_safetyKernel->mode() == SystemMode::Paused) {
        m_layerSlider->setEnabled(false);
        updateLayerNavigationButtons();
        setButtonState(false, false, snapshot.state != SafetyState::Red, true);
        return;
    }

    if (m_context->hasActivePlan() && !m_deferStartupPlanSelection) {
        configureLayerSelector(&m_context->activePlan());
    }
    setButtonState(snapshot.canStartTreatment && canTreatSelectedLayer(), false, false, false);
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
    if (m_context->hasActivePlan() && m_context->activePlan().id == planId) {
        therapyPlan = m_context->activePlan();
    } else if (!m_clinicalDataService.findTherapyPlanById(planId, &therapyPlan)) {
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

void TreatmentPage::onLayerSelectionChanged(int index)
{
    if (!m_context->hasActivePlan() || m_deferStartupPlanSelection || m_progressTimer.isActive()) {
        return;
    }

    const TherapyPlan& plan = m_context->activePlan();
    if (plan.segments.isEmpty()) {
        configureLayerSelector(&plan);
        return;
    }

    const int safeIndex = std::clamp(index, 0, static_cast<int>(plan.segments.size()) - 1);
    if (safeIndex == m_selectedLayerIndex && m_completedPointCount == 0 && m_deliveredEnergyJ == 0.0) {
        configureLayerSelector(&plan);
        return;
    }

    m_selectedLayerIndex = safeIndex;
    ensureLayerProgressStorage(plan);
    m_completedPointCount = completedPointCountForLayer(safeIndex, plan);
    m_deliveredEnergyJ = 0.0;
    updatePlanSummary(&plan);
    updateLayerPreview();
    updateLayerNavigationButtons();
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && canTreatSelectedLayer(), false, false, false);
}

void TreatmentPage::generateThreeDimensionalImage()
{
    if (!m_context->hasActivePlan() || m_deferStartupPlanSelection) {
        appendLog(QStringLiteral("\u5f53\u524d\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848\uff0c\u65e0\u6cd5\u751f\u6210\u4e09\u7ef4\u56fe\u5f62"));
        return;
    }

    const TherapyPlan& plan = m_context->activePlan();
    ensureLayerProgressStorage(plan);
    const int layers = static_cast<int>(plan.segments.size());
    if (layers <= 0) {
        appendLog(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u6ca1\u6709\u53ef\u7528\u5207\u7247\u6570\u636e"));
        return;
    }

    const double sliceSpacingMm = std::max(1.0, plan.spacingMm > 0.0 ? plan.spacingMm : 1.0);
    QVector<VolumeContourSlice> contourSlices;
    QVector<TreatmentVolumeTarget> targets;
    contourSlices.reserve(layers);

    int treatedTargetCount = 0;
    int pendingTargetCount = 0;
    for (int layerIndex = 0; layerIndex < layers; ++layerIndex) {
        const TherapySegment& segment = plan.segments.at(layerIndex);

        VolumeContourSlice contourSlice;
        contourSlice.sliceIndex = layerIndex;
        contourSlice.derivedFromAnnotation = !segment.points.isEmpty();
        contourSlice.contourMm = buildTreatmentVolumeContour(segment, layerIndex, layers, sliceSpacingMm);
        if (contourSlice.contourMm.size() >= 3) {
            contourSlices.push_back(contourSlice);
        }

        const int completedPoints = completedPointCountForLayer(layerIndex, plan);
        for (int pointIndex = 0; pointIndex < segment.points.size(); ++pointIndex) {
            const TherapyPoint& point = segment.points.at(pointIndex);
            const bool isTreated = pointIndex < completedPoints;
            TreatmentVolumeTarget target;
            target.layerIndex = layerIndex;
            target.pointIndex = pointIndex;
            target.positionMm = point.positionMm;
            target.zMm = layerIndex * sliceSpacingMm;
            target.treated = isTreated;
            targets.push_back(target);

            if (isTreated) {
                ++treatedTargetCount;
            } else {
                ++pendingTargetCount;
            }
        }
    }

    if (contourSlices.isEmpty() || targets.isEmpty()) {
        appendLog(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u6ca1\u6709\u53ef\u7528\u9776\u70b9\u6570\u636e"));
        return;
    }

    const VolumeReconstructionResult reconstruction = buildVolumeReconstructionResult(
        contourSlices,
        sliceSpacingMm,
        QSize(980, 620));
    if (!reconstruction.valid) {
        appendLog(QStringLiteral("\u5f53\u524d\u6cbb\u7597\u5207\u7247\u65e0\u6cd5\u751f\u6210\u6709\u6548\u7684\u4e09\u7ef4\u4f53\u6570\u636e"));
        return;
    }

    const QPixmap preview = renderTreatmentVolumeProgressPreview(
        reconstruction,
        contourSlices,
        targets,
        sliceSpacingMm,
        normalizedLayerIndex(plan));
    const QString summary =
        QStringLiteral(
            "\u5f53\u524d\u65b9\u6848\uff1a%1\n"
            "\u91cd\u5efa\u5207\u7247\uff1a%2 \u5f20\n"
            "\u4f30\u7b97\u4f53\u79ef\uff1a%3 cm3\n"
            "\u4f53\u79ef\u4e2d\u5fc3\uff1aX %4  Y %5  Z %6 mm\n"
            "\u5f53\u524d\u6cbb\u7597\u5c42\uff1a%7 / %8\n"
            "\u5df2\u6cbb\u7597\u7ec6\u80de\uff1a%9 \u4e2a\n"
            "\u5f85\u6cbb\u7597\u7ec6\u80de\uff1a%10 \u4e2a\n"
            "\u989c\u8272\u8bf4\u660e\uff1a\u7eff\u8272\u4e3a\u5df2\u6cbb\u7597\u7ec6\u80de\u533a\u57df\uff0c\u84dd\u8272\u4e3a\u672a\u6cbb\u7597/\u7b49\u5f85\u6cbb\u7597\u7ec6\u80de\u533a\u57df\u3002")
            .arg(plan.name)
            .arg(reconstruction.sliceCount)
            .arg(reconstruction.estimatedVolumeCm3, 0, 'f', 2)
            .arg(reconstruction.weightedCentroidMm.x(), 0, 'f', 2)
            .arg(reconstruction.weightedCentroidMm.y(), 0, 'f', 2)
            .arg(reconstruction.weightedCentroidMm.z(), 0, 'f', 2)
            .arg(normalizedLayerIndex(plan) + 1)
            .arg(layers)
            .arg(treatedTargetCount)
            .arg(pendingTargetCount);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("treatmentVolumePreviewDialog"));
    dialog.setWindowTitle(QStringLiteral("\u6cbb\u7597\u4e09\u7ef4\u56fe\u5f62\u9884\u89c8"));
    dialog.resize(1080, 760);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* headerLabel = new QLabel(
        QStringLiteral("\u5df2\u57fa\u4e8e\u5f53\u524d\u6cbb\u7597\u65b9\u6848\u7684\u5168\u90e8\u5207\u7247\u751f\u6210\u4e09\u7ef4\u89c6\u56fe\u3002\u7eff\u8272\u8868\u793a\u5df2\u6cbb\u7597\u7ec6\u80de\u533a\u57df\uff0c\u84dd\u8272\u8868\u793a\u672a\u6cbb\u7597/\u7b49\u5f85\u6cbb\u7597\u7ec6\u80de\u533a\u57df\u3002"));
    headerLabel->setWordWrap(true);

    auto* previewLabel = new QLabel();
    previewLabel->setObjectName(QStringLiteral("treatmentVolumePreviewLabel"));
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setPixmap(preview);

    auto* previewScrollArea = new QScrollArea();
    previewScrollArea->setObjectName(QStringLiteral("treatmentVolumeScrollArea"));
    previewScrollArea->setWidgetResizable(true);
    previewScrollArea->setWidget(previewLabel);

    auto* summaryEdit = new QPlainTextEdit();
    summaryEdit->setObjectName(QStringLiteral("treatmentVolumeSummaryEdit"));
    summaryEdit->setReadOnly(true);
    summaryEdit->setMaximumHeight(150);
    summaryEdit->setPlainText(summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(headerLabel);
    layout->addWidget(previewScrollArea, 1);
    layout->addWidget(summaryEdit);
    layout->addWidget(buttons);

    dialog.exec();
    appendLog(
        QStringLiteral("\u5df2\u751f\u6210\u6cbb\u7597\u4e09\u7ef4\u56fe\u5f62\u9884\u89c8\uff1a\u5207\u7247 %1 \u5f20\uff0c\u5df2\u6cbb\u7597 %2 \u4e2a\uff0c\u5f85\u6cbb\u7597 %3 \u4e2a")
            .arg(reconstruction.sliceCount)
            .arg(treatedTargetCount)
            .arg(pendingTargetCount));
}

void TreatmentPage::setButtonState(bool canStart, bool canPause, bool canResume, bool canStop)
{
    m_startButton->setEnabled(canStart);
    m_pauseButton->setEnabled(canPause || canResume);
    m_pauseButton->setText(canResume ? QStringLiteral("\u7ee7\u7eed") : QStringLiteral("\u6682\u505c"));
    m_pauseButton->setProperty("pauseResumeState", canResume ? QStringLiteral("resume") : QStringLiteral("pause"));
    m_pauseButton->style()->unpolish(m_pauseButton);
    m_pauseButton->style()->polish(m_pauseButton);
    m_stopButton->setEnabled(canStop);
}

bool TreatmentPage::isPlanTreatable(const TherapyPlan& plan) const
{
    if (isSuppressedSystemPlan(plan)) {
        return false;
    }

    return plan.approvalState == ApprovalState::Approved || plan.approvalState == ApprovalState::Locked;
}

bool TreatmentPage::canTreatSelectedLayer() const
{
    return m_context->hasActivePlan()
        && !m_deferStartupPlanSelection
        && isPlanTreatable(m_context->activePlan())
        && totalPointCount() > 0;
}

int TreatmentPage::completedPointCountForLayer(int layerIndex, const TherapyPlan& plan) const
{
    if (layerIndex < 0 || layerIndex >= plan.segments.size()) {
        return 0;
    }

    const int completedPoints = layerIndex < m_layerCompletedPointCounts.size()
        ? m_layerCompletedPointCounts.at(layerIndex)
        : 0;

    return std::clamp(completedPoints, 0, static_cast<int>(plan.segments.at(layerIndex).points.size()));
}

void TreatmentPage::ensureLayerProgressStorage(const TherapyPlan& plan)
{
    const int layers = static_cast<int>(plan.segments.size());
    if (layers <= 0) {
        m_layerCompletedPointCounts.clear();
        m_completedPointCount = 0;
        return;
    }

    if (m_layerCompletedPointCounts.size() != layers) {
        QVector<int> resizedProgress(layers, 0);
        const int copyCount = std::min(layers, static_cast<int>(m_layerCompletedPointCounts.size()));
        for (int index = 0; index < copyCount; ++index) {
            resizedProgress[index] = m_layerCompletedPointCounts.at(index);
        }
        m_layerCompletedPointCounts = resizedProgress;
    }

    for (int index = 0; index < layers; ++index) {
        m_layerCompletedPointCounts[index] = std::clamp(
            m_layerCompletedPointCounts.at(index),
            0,
            static_cast<int>(plan.segments.at(index).points.size()));
    }
}

void TreatmentPage::updateVolumeButtonState()
{
    if (m_generate3dButton == nullptr) {
        return;
    }

    bool hasTargets = false;
    if (m_context->hasActivePlan() && !m_deferStartupPlanSelection) {
        const TherapyPlan& plan = m_context->activePlan();
        for (const TherapySegment& segment : plan.segments) {
            if (!segment.points.isEmpty()) {
                hasTargets = true;
                break;
            }
        }
    }
    m_generate3dButton->setEnabled(hasTargets);
}

int TreatmentPage::layerCount(const TherapyPlan* plan) const
{
    if (plan != nullptr) {
        return static_cast<int>(plan->segments.size());
    }

    return m_context->hasActivePlan() ? static_cast<int>(m_context->activePlan().segments.size()) : 0;
}

int TreatmentPage::normalizedLayerIndex(const TherapyPlan& plan) const
{
    if (plan.segments.isEmpty()) {
        return 0;
    }

    return std::clamp(m_selectedLayerIndex, 0, static_cast<int>(plan.segments.size()) - 1);
}

const TherapySegment* TreatmentPage::selectedLayerSegment() const
{
    if (!m_context->hasActivePlan() || m_context->activePlan().segments.isEmpty()) {
        return nullptr;
    }

    const TherapyPlan& plan = m_context->activePlan();
    return &plan.segments.at(normalizedLayerIndex(plan));
}

TherapyPlan TreatmentPage::selectedLayerPlan(const TherapyPlan& plan) const
{
    TherapyPlan layerPlan = plan;
    layerPlan.segments.clear();
    if (plan.segments.isEmpty()) {
        return layerPlan;
    }

    TherapySegment layerSegment = plan.segments.at(normalizedLayerIndex(plan));
    layerSegment.orderIndex = 0;
    layerPlan.segments.push_back(layerSegment);
    return layerPlan;
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
    const TherapySegment* segment = selectedLayerSegment();
    if (segment == nullptr) {
        return 0;
    }

    return static_cast<int>(segment->points.size());
}

double TreatmentPage::pointDwellSeconds(const TherapyPoint& point, const TherapyPlan& plan) const
{
    if (point.dwellSeconds > 0.0) {
        return point.dwellSeconds;
    }

    return plan.dwellSeconds > 0.0 ? plan.dwellSeconds : 0.3;
}

double TreatmentPage::layerPlannedDurationSeconds(const TherapySegment& segment, const TherapyPlan& plan) const
{
    if (segment.plannedDurationSeconds > 0.0) {
        return segment.plannedDurationSeconds;
    }

    double durationSeconds = 0.0;
    for (const TherapyPoint& point : segment.points) {
        durationSeconds += pointDwellSeconds(point, plan);
    }
    return durationSeconds;
}

double TreatmentPage::layerElapsedDurationSeconds(const TherapySegment& segment, const TherapyPlan& plan) const
{
    double elapsedSeconds = 0.0;
    const int completedPointCount = std::clamp(m_completedPointCount, 0, static_cast<int>(segment.points.size()));
    for (int index = 0; index < completedPointCount; ++index) {
        elapsedSeconds += pointDwellSeconds(segment.points.at(index), plan);
    }
    return std::min(elapsedSeconds, layerPlannedDurationSeconds(segment, plan));
}

double TreatmentPage::planElapsedDurationSeconds(const TherapyPlan& plan) const
{
    double elapsedSeconds = 0.0;
    for (int layerIndex = 0; layerIndex < plan.segments.size(); ++layerIndex) {
        const TherapySegment& segment = plan.segments.at(layerIndex);
        const int completedPointCount = completedPointCountForLayer(layerIndex, plan);
        for (int pointIndex = 0; pointIndex < completedPointCount; ++pointIndex) {
            elapsedSeconds += pointDwellSeconds(segment.points.at(pointIndex), plan);
        }
    }
    return std::min(elapsedSeconds, planPlannedDurationSeconds(plan));
}

double TreatmentPage::planPlannedDurationSeconds(const TherapyPlan& plan) const
{
    double durationSeconds = 0.0;
    for (const TherapySegment& segment : plan.segments) {
        durationSeconds += layerPlannedDurationSeconds(segment, plan);
    }
    return durationSeconds;
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
            QStringLiteral(
                "\u6267\u884c\u4fe1\u606f\n"
                "\u672a\u9009\u62e9\u6cbb\u7597\u65b9\u6848\n"
                "\u8bf7\u5148\u5728\u4e0a\u65b9\u9009\u62e9\u4e00\u4e2a\u5df2\u5ba1\u6838\u65b9\u6848\u3002"));
        return;
    }

    int pointCount = 0;
    double durationSeconds = 0.0;
    for (const TherapySegment& segment : plan->segments) {
        pointCount += segment.points.size();
        durationSeconds += segment.plannedDurationSeconds;
    }

    const int layers = static_cast<int>(plan->segments.size());
    const int selectedIndex = layers > 0 ? normalizedLayerIndex(*plan) : 0;
    const TherapySegment* selectedSegment = layers > 0 ? &plan->segments.at(selectedIndex) : nullptr;
    const int selectedPointCount = selectedSegment != nullptr ? static_cast<int>(selectedSegment->points.size()) : 0;
    double selectedDurationSeconds = selectedSegment != nullptr ? selectedSegment->plannedDurationSeconds : 0.0;
    if (selectedSegment != nullptr && selectedDurationSeconds <= 0.0) {
        for (const TherapyPoint& point : selectedSegment->points) {
            selectedDurationSeconds += point.dwellSeconds > 0.0 ? point.dwellSeconds : plan->dwellSeconds;
        }
    }
    const QString selectedLayerText = selectedSegment == nullptr
        ? QStringLiteral("\u672a\u751f\u6210")
        : (selectedSegment->label.trimmed().isEmpty()
                ? QStringLiteral("\u7b2c%1\u5c42").arg(selectedIndex + 1)
                : selectedSegment->label.trimmed());

    const QString deliveryText = plan->deliveryMode.trimmed().isEmpty() ? QStringLiteral("\u672a\u8bbe\u7f6e") : plan->deliveryMode;
    m_planSummaryLabel->setText(
        QStringLiteral(
            "\u6267\u884c\u4fe1\u606f\n"
            "%1 | %2 | %3 / %4\n"
            "%5 W | \u884c\u8ddd %6 mm | \u70b9\u7597 %7 s | \u603b\u9776\u70b9 %8\n"
            "\u5f53\u524d\u5c42\uff1a%9 / %10  %11 | %12 \u70b9 | \u9884\u8ba1 %13 min")
            .arg(plan->name.trimmed().isEmpty() ? QStringLiteral("\u672a\u547d\u540d\u65b9\u6848") : plan->name)
            .arg(toDisplayString(plan->approvalState))
            .arg(deliveryText)
            .arg(toDisplayString(plan->pattern))
            .arg(plan->plannedPowerWatts, 0, 'f', 0)
            .arg(plan->spacingMm, 0, 'f', 1)
            .arg(plan->dwellSeconds, 0, 'f', 1)
            .arg(pointCount)
            .arg(layers > 0 ? selectedIndex + 1 : 0)
            .arg(layers)
            .arg(selectedLayerText)
            .arg(selectedPointCount)
            .arg(selectedDurationSeconds / 60.0, 0, 'f', 2));
}

void TreatmentPage::configureLayerSelector(const TherapyPlan* plan)
{
    if (m_layerLabel == nullptr || m_layerSlider == nullptr) {
        return;
    }

    if (plan == nullptr || plan->segments.isEmpty()) {
        m_selectedLayerIndex = 0;
        const QSignalBlocker blocker(m_layerSlider);
        m_layerSlider->setRange(0, 0);
        m_layerSlider->setValue(0);
        m_layerSlider->setTickPosition(QSlider::NoTicks);
        m_layerSlider->setEnabled(false);
        m_layerLabel->setText(QStringLiteral("\u6cbb\u7597\u5c42\uff1a\u672a\u9009\u62e9"));
        updateLayerNavigationButtons();
        return;
    }

    const int layers = static_cast<int>(plan->segments.size());
    m_selectedLayerIndex = normalizedLayerIndex(*plan);
    const TherapySegment& segment = plan->segments.at(m_selectedLayerIndex);
    const QString layerText = segment.label.trimmed().isEmpty()
        ? QStringLiteral("\u7b2c%1\u5c42").arg(m_selectedLayerIndex + 1)
        : segment.label.trimmed();

    const QSignalBlocker blocker(m_layerSlider);
    m_layerSlider->setRange(0, layers - 1);
    m_layerSlider->setSingleStep(1);
    m_layerSlider->setPageStep(1);
    m_layerSlider->setTickInterval(1);
    m_layerSlider->setTickPosition(layers > 1 ? QSlider::TicksBelow : QSlider::NoTicks);
    m_layerSlider->setValue(m_selectedLayerIndex);
    m_layerSlider->setEnabled(layers > 1 && !m_progressTimer.isActive() && m_safetyKernel->mode() != SystemMode::Paused);
    m_layerLabel->setText(
        QStringLiteral("\u6cbb\u7597\u5c42\uff1a%1 / %2\u3000%3\u3000%4\u4e2a\u9776\u70b9")
            .arg(m_selectedLayerIndex + 1)
            .arg(layers)
            .arg(layerText)
            .arg(static_cast<int>(segment.points.size())));
    updateLayerNavigationButtons();
}

void TreatmentPage::updateLayerNavigationButtons()
{
    if (m_layerSlider == nullptr || m_previousLayerButton == nullptr || m_nextLayerButton == nullptr) {
        return;
    }

    const bool canNavigate = m_layerSlider->isEnabled() && m_layerSlider->minimum() < m_layerSlider->maximum();
    m_previousLayerButton->setEnabled(canNavigate && m_layerSlider->value() > m_layerSlider->minimum());
    m_nextLayerButton->setEnabled(canNavigate && m_layerSlider->value() < m_layerSlider->maximum());
}

void TreatmentPage::updateLayerPreview()
{
    if (m_preview == nullptr) {
        return;
    }

    if (!m_context->hasActivePlan() || m_deferStartupPlanSelection) {
        configureLayerSelector(nullptr);
        m_preview->clearPlan();
        m_preview->setCompletedPointCount(0);
        m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
        updateProgressText();
        updateVolumeButtonState();
        return;
    }

    const TherapyPlan& plan = m_context->activePlan();
    ensureLayerProgressStorage(plan);
    configureLayerSelector(&plan);
    m_preview->setSliceContext(normalizedLayerIndex(plan), layerCount(&plan));
    m_preview->setPlan(selectedLayerPlan(plan));
    m_preview->setCompletedPointCount(m_completedPointCount);
    m_preview->setCaption(
        QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7b2c%1/%2\u5c42\u7126\u70b9\u8986\u76d6")
            .arg(layerCount(&plan) > 0 ? normalizedLayerIndex(plan) + 1 : 0)
            .arg(layerCount(&plan)));
    updateProgressText();
    updateVolumeButtonState();
}

void TreatmentPage::updateProgressText()
{
    if (m_progressLabel == nullptr || m_progressBar == nullptr || m_timeSummaryLabel == nullptr) {
        return;
    }

    if (!m_context->hasActivePlan() || m_deferStartupPlanSelection) {
        m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
        m_timeSummaryLabel->setText(QStringLiteral("\u65f6\u95f4\uff1a\u672c\u5c42\u5269\u4f59 -- | \u672c\u5c42\u603b\u65f6\u957f -- | \u6cbb\u7597\u603b\u65f6\u957f --"));
        if (m_layerRemainingValueLabel != nullptr) {
            m_layerRemainingValueLabel->setText(QStringLiteral("--"));
        }
        if (m_layerTotalValueLabel != nullptr) {
            m_layerTotalValueLabel->setText(QStringLiteral("--"));
        }
        if (m_planRemainingValueLabel != nullptr) {
            m_planRemainingValueLabel->setText(QStringLiteral("--"));
        }
        if (m_planTotalValueLabel != nullptr) {
            m_planTotalValueLabel->setText(QStringLiteral("--"));
        }
        m_progressBar->setValue(0);
        return;
    }

    const TherapyPlan& plan = m_context->activePlan();
    const int layers = layerCount(&plan);
    const int totalPoints = totalPointCount();
    const int percentage = totalPoints > 0
        ? static_cast<int>((static_cast<double>(m_completedPointCount) / totalPoints) * 100.0)
        : 0;
    m_progressBar->setValue(std::min(percentage, 100));

    if (layers <= 0) {
        m_progressLabel->setText(QStringLiteral("\u6cbb\u7597\u8fdb\u5ea6\uff1a0 / 0"));
        m_timeSummaryLabel->setText(QStringLiteral("\u65f6\u95f4\uff1a\u672c\u5c42\u5269\u4f59 -- | \u672c\u5c42\u603b\u65f6\u957f -- | \u6cbb\u7597\u603b\u65f6\u957f --"));
        if (m_layerRemainingValueLabel != nullptr) {
            m_layerRemainingValueLabel->setText(QStringLiteral("--"));
        }
        if (m_layerTotalValueLabel != nullptr) {
            m_layerTotalValueLabel->setText(QStringLiteral("--"));
        }
        if (m_planRemainingValueLabel != nullptr) {
            m_planRemainingValueLabel->setText(QStringLiteral("--"));
        }
        if (m_planTotalValueLabel != nullptr) {
            m_planTotalValueLabel->setText(QStringLiteral("--"));
        }
        return;
    }

    const TherapySegment* segment = selectedLayerSegment();
    const double layerTotalSeconds = segment != nullptr ? layerPlannedDurationSeconds(*segment, plan) : 0.0;
    const double layerElapsedSeconds = segment != nullptr ? layerElapsedDurationSeconds(*segment, plan) : 0.0;
    const double layerRemainingSeconds = std::max(0.0, layerTotalSeconds - layerElapsedSeconds);
    const double totalTreatmentSeconds = planPlannedDurationSeconds(plan);
    const double totalElapsedSeconds = planElapsedDurationSeconds(plan);
    const double totalRemainingSeconds = std::max(0.0, totalTreatmentSeconds - totalElapsedSeconds);

    m_progressLabel->setText(
        QStringLiteral("\u7b2c%1/%2\u5c42\u6cbb\u7597\u8fdb\u5ea6\uff1a%3 / %4\uff0c\u7d2f\u8ba1\u80fd\u91cf %5 J")
            .arg(normalizedLayerIndex(plan) + 1)
            .arg(layers)
            .arg(m_completedPointCount)
            .arg(totalPoints)
            .arg(m_deliveredEnergyJ, 0, 'f', 0));
    m_timeSummaryLabel->setText(
        QStringLiteral("\u65f6\u95f4\uff1a\u672c\u5c42\u5269\u4f59 %1 | \u672c\u5c42\u603b\u65f6\u957f %2 | \u6cbb\u7597\u603b\u65f6\u957f %3 | \u672c\u5c42\u5df2\u7528 %4")
            .arg(formatTreatmentDuration(layerRemainingSeconds),
                formatTreatmentDuration(layerTotalSeconds),
                formatTreatmentDuration(totalTreatmentSeconds),
                formatTreatmentDuration(layerElapsedSeconds)));
    if (m_layerRemainingValueLabel != nullptr) {
        m_layerRemainingValueLabel->setText(formatTreatmentDuration(layerRemainingSeconds));
    }
    if (m_layerTotalValueLabel != nullptr) {
        m_layerTotalValueLabel->setText(formatTreatmentDuration(layerTotalSeconds));
    }
    if (m_planRemainingValueLabel != nullptr) {
        m_planRemainingValueLabel->setText(formatTreatmentDuration(totalRemainingSeconds));
    }
    if (m_planTotalValueLabel != nullptr) {
        m_planTotalValueLabel->setText(formatTreatmentDuration(totalTreatmentSeconds));
    }
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
    if (m_context->hasActivePlan() && !m_deferStartupPlanSelection) {
        configureLayerSelector(&m_context->activePlan());
    } else {
        configureLayerSelector(nullptr);
    }
    updateProgressText();
    setButtonState(m_safetyKernel->snapshot().canStartTreatment && canTreatSelectedLayer(), false, false, false);
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
