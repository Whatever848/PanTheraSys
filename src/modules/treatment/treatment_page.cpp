#include "modules/treatment/treatment_page.h"

#include "adapters/anthone/lu926_temperature_protocol.h"
#include "modules/shared/system_sound_guard.h"
#include "modules/shared/therapy_imaging_algorithms.h"
#include "modules/shared/ultrasound_geometry.h"

#include <algorithm>
#include <cmath>

#include <QAbstractSpinBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr int kTreatmentSwingAxisNodeId = 6;
constexpr int kTreatmentLayerAxisNodeId = 7;
constexpr int kTreatmentVerticalAxisNodeId = 8;
constexpr int kTreatmentLinearStepsPerTurn = 3200;
constexpr double kTreatmentLinearMillimetersPerTurn = 2.0;
constexpr double kTreatmentLinearStepsPerMillimeter =
    kTreatmentLinearStepsPerTurn / kTreatmentLinearMillimetersPerTurn;
constexpr int kTreatmentMotorSpeed = 2000;
constexpr int kTreatmentLayerAxisMinimumSteps = 0;
constexpr int kTreatmentLayerAxisMaximumSteps = 76119;
constexpr int kTreatmentVerticalAxisMinimumSteps = 0;
constexpr int kTreatmentVerticalAxisMaximumSteps = 145743;
constexpr int kTreatmentSwingStepsPerDegree = 1778;
constexpr double kTreatmentSwingMaximumDegrees = 10.0;
constexpr double kTreatmentVerticalAxisMaximumMillimeters =
    kTreatmentVerticalAxisMaximumSteps / kTreatmentLinearStepsPerMillimeter;
constexpr int kTreatmentSwingMaximumSteps =
    static_cast<int>(kTreatmentSwingStepsPerDegree * kTreatmentSwingMaximumDegrees);
constexpr int kTreatmentMotorPositionToleranceSteps = 20;
constexpr int kTreatmentMotorBoundaryToleranceSteps = 160;
constexpr int kTreatmentMotorPollMs = 160;
constexpr int kTreatmentMotorMoveTimeoutMs = 120000;
constexpr int kTreatmentMotorMoveStartTimeoutMs = 3000;
constexpr int kTank1UpperLimitNodeId = 6;
constexpr int kTank1UpperLimitSensorIndex = 2;
constexpr bool kTank1UpperLimitHighActive = true;
constexpr int kTank1UpperLimitDebounceMs = 1000;
constexpr int kFluidControlPollMs = 300;
constexpr int kTemperatureRefreshMs = 3000;
constexpr int kRobotPumpForwardDo = 13;
constexpr int kRobotPumpReverseDo = 14;
constexpr double kTankTransferFlowMlPerMin = 450.0;
constexpr double kDefaultLoopFlowMlPerMin = 600.0;
constexpr double kTank2DefaultTargetLevelMillimeters = 300.0;
constexpr double kTank2MaximumTargetLevelMillimeters = 430.0;
constexpr double kTank2CycleToleranceMillimeters = 10.0;
constexpr double kTemperatureMinimumCelsius = 0.0;
constexpr double kTemperatureMaximumCelsius = 45.0;

QString treatmentOptionalIntText(bool hasValue, int value)
{
    return hasValue ? QString::number(value) : QStringLiteral("-");
}

QString treatmentBoolText(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

bool treatmentSnapshotPosition(
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
        *errorMessage = QStringLiteral("POS/QEC 均无反馈");
    }
    return false;
}

bool treatmentSnapshotMoved(
    const diji::adapters::uim::UimMotorSnapshot& startSnapshot,
    const diji::adapters::uim::UimMotorSnapshot& currentSnapshot)
{
    if (startSnapshot.hasPosition && currentSnapshot.hasPosition
        && std::abs(currentSnapshot.position - startSnapshot.position) > kTreatmentMotorPositionToleranceSteps) {
        return true;
    }
    if (startSnapshot.hasEncoderPosition && currentSnapshot.hasEncoderPosition
        && std::abs(currentSnapshot.encoderPosition - startSnapshot.encoderPosition) > kTreatmentMotorPositionToleranceSteps) {
        return true;
    }
    return false;
}

int treatmentMotionPosition(
    const diji::adapters::uim::UimMotorSnapshot& startSnapshot,
    const diji::adapters::uim::UimMotorSnapshot& currentSnapshot,
    int fallbackPositionSteps)
{
    if (startSnapshot.hasPosition && currentSnapshot.hasPosition
        && std::abs(currentSnapshot.position - startSnapshot.position) > kTreatmentMotorPositionToleranceSteps) {
        return currentSnapshot.position;
    }
    if (startSnapshot.hasEncoderPosition && currentSnapshot.hasEncoderPosition
        && std::abs(currentSnapshot.encoderPosition - startSnapshot.encoderPosition) > kTreatmentMotorPositionToleranceSteps) {
        return currentSnapshot.encoderPosition;
    }
    int positionSteps = fallbackPositionSteps;
    treatmentSnapshotPosition(currentSnapshot, &positionSteps, nullptr);
    return positionSteps;
}

QString treatmentMotorStatusText(const diji::adapters::uim::UimMotorSnapshot& snapshot)
{
    QString sensorText = QStringLiteral("S1/S2/S3=-");
    if (snapshot.hasSensorFeedback) {
        sensorText = QStringLiteral("S1/S2/S3=%1/%2/%3")
            .arg(treatmentBoolText(snapshot.sensor1))
            .arg(treatmentBoolText(snapshot.sensor2))
            .arg(treatmentBoolText(snapshot.sensor3));
    }

    return QStringLiteral("ENA=%1, SPD=%2, STEP=%3, POS=%4, QEC=%5, %6")
        .arg(treatmentBoolText(snapshot.enabled))
        .arg(snapshot.speed)
        .arg(snapshot.step)
        .arg(treatmentOptionalIntText(snapshot.hasPosition, snapshot.position))
        .arg(treatmentOptionalIntText(snapshot.hasEncoderPosition, snapshot.encoderPosition))
        .arg(sensorText);
}

bool treatmentPositionOutsideHardRange(int positionSteps, int minimumPositionSteps, int maximumPositionSteps)
{
    return positionSteps < minimumPositionSteps - kTreatmentMotorBoundaryToleranceSteps
        || positionSteps > maximumPositionSteps + kTreatmentMotorBoundaryToleranceSteps;
}

int treatmentClampPositionToSafetyRange(int positionSteps, int minimumPositionSteps, int maximumPositionSteps)
{
    if (positionSteps < minimumPositionSteps && positionSteps >= minimumPositionSteps - kTreatmentMotorBoundaryToleranceSteps) {
        return minimumPositionSteps;
    }
    if (positionSteps > maximumPositionSteps && positionSteps <= maximumPositionSteps + kTreatmentMotorBoundaryToleranceSteps) {
        return maximumPositionSteps;
    }
    return positionSteps;
}

QString defaultTreatmentMotorSdkPath()
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

bool segmentHasLineTreatmentMetadata(const TherapySegment& segment)
{
    for (const TherapyPoint& point : segment.points) {
        if (point.lineGroupIndex >= 0 || point.lineStart || point.lineEnd) {
            return true;
        }
    }
    return false;
}

bool shouldUseSerpentinePointOrder(const TherapyPlan& plan, const TherapySegment& segment)
{
    if (segment.points.size() <= 1 || plan.pattern == TreatmentPattern::Line) {
        return false;
    }

    return !segmentHasLineTreatmentMetadata(segment);
}

bool hasSameTargetPositionOrder(const QVector<TherapyPoint>& left, const QVector<TherapyPoint>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (int index = 0; index < left.size(); ++index) {
        const QPointF& leftPosition = left.at(index).positionMm;
        const QPointF& rightPosition = right.at(index).positionMm;
        if (std::abs(leftPosition.x() - rightPosition.x()) > 0.001
            || std::abs(leftPosition.y() - rightPosition.y()) > 0.001) {
            return false;
        }
    }
    return true;
}

bool applySerpentinePointExecutionOrder(TherapyPlan* plan)
{
    if (plan == nullptr) {
        return false;
    }

    bool changed = false;
    for (TherapySegment& segment : plan->segments) {
        if (!shouldUseSerpentinePointOrder(*plan, segment)) {
            continue;
        }

        const QVector<TherapyPoint> orderedPoints = orderPointTargetsSerpentine(segment.points, plan->spacingMm);
        if (!hasSameTargetPositionOrder(segment.points, orderedPoints)) {
            segment.points = orderedPoints;
            changed = true;
        }
    }
    return changed;
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

QString treatmentResolveRuntimePath(const QString& relativePath)
{
    const QString projectDefaultsPath = QStringLiteral("D:/PanSoftware/PanTheraSys/config/defaults.ini");
    if (relativePath == QStringLiteral("config/defaults.ini") && QFileInfo::exists(projectDefaultsPath)) {
        return QDir::cleanPath(projectDefaultsPath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QList<QDir> baseDirectories {
        QDir::current(),
        QDir(appDir)
    };
    const QStringList relativeCandidates {
        relativePath,
        QStringLiteral("../%1").arg(relativePath),
        QStringLiteral("../../%1").arg(relativePath),
        QStringLiteral("../../../%1").arg(relativePath),
        QStringLiteral("../../../../%1").arg(relativePath),
        QStringLiteral("../../../../../%1").arg(relativePath)
    };

    for (const QDir& baseDirectory : baseDirectories) {
        for (const QString& relativeCandidate : relativeCandidates) {
            const QString candidate = baseDirectory.absoluteFilePath(relativeCandidate);
            if (QFileInfo::exists(candidate)) {
                return QDir::cleanPath(candidate);
            }
        }
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath(relativePath));
}

int treatmentSafePort(int value, int fallback)
{
    return value >= 1 && value <= 65535 ? value : fallback;
}

QString selectedSerialPortName(QComboBox* combo)
{
    if (combo == nullptr) {
        return {};
    }

    QVariant selectedPort = combo->currentData();
    QString portName = selectedPort.isValid() ? selectedPort.toString() : combo->currentText();
    if (portName.contains(QStringLiteral(" - "))) {
        portName = portName.section(QStringLiteral(" - "), 0, 0);
    }
    return portName.trimmed();
}

void refreshSerialPortCombo(QComboBox* combo)
{
    if (combo == nullptr) {
        return;
    }

    const QString previousPort = combo->currentText().trimmed();
    QSignalBlocker blocker(combo);
    combo->clear();

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& port : ports) {
        const QString label = port.description().trimmed().isEmpty()
            ? port.portName()
            : QStringLiteral("%1 - %2").arg(port.portName(), port.description().trimmed());
        combo->addItem(label, port.portName());
    }

    if (!previousPort.isEmpty()) {
        const int index = combo->findData(previousPort);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        } else {
            combo->setEditText(previousPort);
        }
    } else if (combo->count() == 0) {
        combo->setEditText(QString());
    }
}

void addBaudRates(QComboBox* combo, const QList<int>& baudRates, int defaultBaudRate)
{
    if (combo == nullptr) {
        return;
    }
    for (int baudRate : baudRates) {
        combo->addItem(QString::number(baudRate), baudRate);
    }
    const int defaultIndex = combo->findData(defaultBaudRate);
    if (defaultIndex >= 0) {
        combo->setCurrentIndex(defaultIndex);
    }
}

int selectedBaudRate(QComboBox* combo, int fallback)
{
    if (combo == nullptr) {
        return fallback;
    }
    int baudRate = combo->currentData().toInt();
    if (baudRate <= 0) {
        baudRate = combo->currentText().toInt();
    }
    return baudRate > 0 ? baudRate : fallback;
}

QString compactStatusStyle(bool ok)
{
    return ok
        ? QStringLiteral(
              "QLabel { padding: 10px 12px; border: 1px solid #1e5d91; border-radius: 6px; "
              "background: #0e2943; color: #ffffff; font-weight: 600; }")
        : QStringLiteral(
              "QLabel { padding: 10px 12px; border: 1px solid #d94a4a; border-radius: 6px; "
              "background: #3a1014; color: #ffd7d7; font-weight: 700; }");
}

bool tankLimitSensorActive(const diji::adapters::uim::UimMotorSnapshot& snapshot, int sensorIndex)
{
    bool rawHigh = false;
    if (sensorIndex == 1) {
        rawHigh = snapshot.sensor1;
    } else if (sensorIndex == 2) {
        rawHigh = snapshot.sensor2;
    } else if (sensorIndex == 3) {
        rawHigh = snapshot.sensor3;
    }
    return kTank1UpperLimitHighActive ? rawHigh : !rawHigh;
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
    loadTreatmentRobotPumpSettings();
    applyTreatmentRobotPumpSettings();

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* imageCard = new QGroupBox(QStringLiteral("\u6cbb\u7597\u6267\u884c\u89c6\u56fe"));
    auto* imageLayout = new QVBoxLayout(imageCard);
    m_preview = new MockUltrasoundView();
    m_preview->setCaption(QStringLiteral("\u6cbb\u7597\u6267\u884c\u76d1\u89c6 / \u7126\u70b9\u8986\u76d6\u793a\u610f"));
    m_preview->setBackgroundImageStretchToFill(false);
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
    m_fluidControlTimer.setInterval(kFluidControlPollMs);
    m_temperatureRefreshTimer.setInterval(kTemperatureRefreshMs);

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
    connect(&m_fluidControlTimer, &QTimer::timeout, this, &TreatmentPage::onFluidControlTick);
    connect(&m_temperatureRefreshTimer, &QTimer::timeout, this, &TreatmentPage::onTemperatureRefreshTick);
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

    connect(m_context, &ApplicationContext::treatmentCameraFrameUpdated, this, [this](const QImage& image, const QString&) {
        if (image.isNull()) {
            return;
        }

        if (m_preview == nullptr) {
            return;
        }
        m_preview->setSyntheticImageEnabled(false);
        m_preview->setBackgroundImageStretchToFill(false);
        m_preview->setBackgroundImage(QPixmap::fromImage(image));
    });
    if (m_context != nullptr && m_context->hasLatestTreatmentCameraFrame()) {
        m_preview->setSyntheticImageEnabled(false);
        m_preview->setBackgroundImageStretchToFill(false);
        m_preview->setBackgroundImage(QPixmap::fromImage(m_context->latestTreatmentCameraFrame()));
    }

    setButtonState(false, false, false, false);
    refreshAvailablePlans(!m_context->hasActivePlan());
}

TreatmentPage::~TreatmentPage()
{
    QString ignoredSummary;
    stopAllFluidDevices(true, true, &ignoredSummary);
}

QWidget* TreatmentPage::createFluidControlCard()
{
    auto* groupBox = new QGroupBox(QStringLiteral("水路与温控"));
    groupBox->setMinimumWidth(380);
    groupBox->setMaximumWidth(520);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(groupBox);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_fluidStatusLabel = new QLabel(QStringLiteral("水路待命"), groupBox);
    m_fluidStatusLabel->setWordWrap(true);
    m_fluidStatusLabel->setMinimumHeight(58);
    m_fluidStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_fluidStatusLabel);

    auto* refreshPortsButton = new QPushButton(QStringLiteral("刷新串口"), groupBox);

    auto configurePortCombo = [](QComboBox* combo) {
        combo->setEditable(true);
        combo->setMinimumWidth(110);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    };

    auto* connectionGrid = new QGridLayout();
    connectionGrid->setHorizontalSpacing(8);
    connectionGrid->setVerticalSpacing(8);
    connectionGrid->setColumnStretch(1, 1);
    connectionGrid->setColumnStretch(2, 0);
    connectionGrid->setColumnStretch(3, 0);

    m_waterPumpPortCombo = new QComboBox(groupBox);
    configurePortCombo(m_waterPumpPortCombo);
    m_waterPumpBaudCombo = new QComboBox(groupBox);
    addBaudRates(m_waterPumpBaudCombo, {9600, 19200, 38400, 57600, 115200}, 9600);
    m_waterPumpConnectionButton = new QPushButton(QStringLiteral("连接水泵"), groupBox);

    m_liquidLevelPortCombo = new QComboBox(groupBox);
    configurePortCombo(m_liquidLevelPortCombo);
    m_liquidLevelBaudCombo = new QComboBox(groupBox);
    addBaudRates(m_liquidLevelBaudCombo, {9600, 19200, 38400, 57600, 115200}, 9600);
    m_liquidLevelConnectionButton = new QPushButton(QStringLiteral("连接液位"), groupBox);

    m_temperaturePortCombo = new QComboBox(groupBox);
    configurePortCombo(m_temperaturePortCombo);
    m_temperatureBaudCombo = new QComboBox(groupBox);
    addBaudRates(
        m_temperatureBaudCombo,
        {1200, 2400, 4800, 9600, 19200, 38400},
        panthera::adapters::anthone::Lu926TemperatureProtocol::kDefaultBaudRate);
    m_temperatureConnectionButton = new QPushButton(QStringLiteral("连接温控"), groupBox);

    connectionGrid->addWidget(new QLabel(QStringLiteral("水泵485"), groupBox), 0, 0);
    connectionGrid->addWidget(m_waterPumpPortCombo, 0, 1);
    connectionGrid->addWidget(m_waterPumpBaudCombo, 0, 2);
    connectionGrid->addWidget(m_waterPumpConnectionButton, 0, 3);
    connectionGrid->addWidget(new QLabel(QStringLiteral("液位485"), groupBox), 1, 0);
    connectionGrid->addWidget(m_liquidLevelPortCombo, 1, 1);
    connectionGrid->addWidget(m_liquidLevelBaudCombo, 1, 2);
    connectionGrid->addWidget(m_liquidLevelConnectionButton, 1, 3);
    connectionGrid->addWidget(new QLabel(QStringLiteral("温控485"), groupBox), 2, 0);
    connectionGrid->addWidget(m_temperaturePortCombo, 2, 1);
    connectionGrid->addWidget(m_temperatureBaudCombo, 2, 2);
    connectionGrid->addWidget(m_temperatureConnectionButton, 2, 3);
    connectionGrid->addWidget(refreshPortsButton, 3, 3);
    layout->addLayout(connectionGrid);

    auto* levelGrid = new QGridLayout();
    levelGrid->setHorizontalSpacing(8);
    levelGrid->setVerticalSpacing(8);
    levelGrid->setColumnStretch(1, 1);
    m_liquidLevelAddressEdit = new QLineEdit(QStringLiteral("01"), groupBox);
    m_liquidLevelAddressEdit->setReadOnly(true);
    m_tank2TargetLevelSpin = new QDoubleSpinBox(groupBox);
    m_tank2TargetLevelSpin->setRange(0.0, kTank2MaximumTargetLevelMillimeters);
    m_tank2TargetLevelSpin->setDecimals(1);
    m_tank2TargetLevelSpin->setSingleStep(5.0);
    m_tank2TargetLevelSpin->setValue(kTank2DefaultTargetLevelMillimeters);
    m_tank2TargetLevelSpin->setSuffix(QStringLiteral(" mm"));
    m_tank2TargetLevelSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tank2LevelLabel = new QLabel(QStringLiteral("-- mm"), groupBox);
    m_tank2LevelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    levelGrid->addWidget(new QLabel(QStringLiteral("液位地址"), groupBox), 0, 0);
    levelGrid->addWidget(m_liquidLevelAddressEdit, 0, 1);
    levelGrid->addWidget(new QLabel(QStringLiteral("当前液位"), groupBox), 1, 0);
    levelGrid->addWidget(m_tank2LevelLabel, 1, 1);
    levelGrid->addWidget(new QLabel(QStringLiteral("目标液位"), groupBox), 2, 0);
    levelGrid->addWidget(m_tank2TargetLevelSpin, 2, 1);
    layout->addLayout(levelGrid);

    auto* robotPumpGrid = new QGridLayout();
    robotPumpGrid->setHorizontalSpacing(8);
    robotPumpGrid->setVerticalSpacing(8);
    m_robotPumpFillButton = new QPushButton(QStringLiteral("RO注水"), groupBox);
    m_robotPumpDrainButton = new QPushButton(QStringLiteral("RO出水"), groupBox);
    m_robotPumpStopButton = new QPushButton(QStringLiteral("停止RO"), groupBox);
    m_confirmTank2FillButton = new QPushButton(QStringLiteral("确认03加水"), groupBox);
    robotPumpGrid->addWidget(m_robotPumpFillButton, 0, 0);
    robotPumpGrid->addWidget(m_robotPumpDrainButton, 0, 1);
    robotPumpGrid->addWidget(m_robotPumpStopButton, 0, 2);
    robotPumpGrid->addWidget(m_confirmTank2FillButton, 1, 0, 1, 3);
    layout->addLayout(robotPumpGrid);

    auto* cycleGrid = new QGridLayout();
    cycleGrid->setHorizontalSpacing(8);
    cycleGrid->setVerticalSpacing(8);
    m_startCycleButton = new QPushButton(QStringLiteral("启动循环"), groupBox);
    m_stopCycleButton = new QPushButton(QStringLiteral("停止循环"), groupBox);
    m_stopFluidDevicesButton = new QPushButton(QStringLiteral("停止水路"), groupBox);
    cycleGrid->addWidget(m_startCycleButton, 0, 0);
    cycleGrid->addWidget(m_stopCycleButton, 0, 1);
    cycleGrid->addWidget(m_stopFluidDevicesButton, 0, 2);
    layout->addLayout(cycleGrid);

    auto* temperatureGrid = new QGridLayout();
    temperatureGrid->setHorizontalSpacing(8);
    temperatureGrid->setVerticalSpacing(8);
    temperatureGrid->setColumnStretch(1, 1);
    m_temperatureChannelCombo = new QComboBox(groupBox);
    for (int channelIndex = 1; channelIndex <= panthera::adapters::anthone::Lu926TemperatureProtocol::kChannelCount; ++channelIndex) {
        m_temperatureChannelCombo->addItem(QStringLiteral("CH%1").arg(channelIndex), channelIndex);
    }
    m_temperatureSetpointSpin = new QDoubleSpinBox(groupBox);
    m_temperatureSetpointSpin->setRange(kTemperatureMinimumCelsius, kTemperatureMaximumCelsius);
    m_temperatureSetpointSpin->setDecimals(1);
    m_temperatureSetpointSpin->setSingleStep(0.5);
    m_temperatureSetpointSpin->setValue(37.0);
    m_temperatureSetpointSpin->setSuffix(QStringLiteral(" °C"));
    m_temperatureSetpointSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_temperatureStartButton = new QPushButton(QStringLiteral("开始加热"), groupBox);
    m_temperatureStopButton = new QPushButton(QStringLiteral("停止加热"), groupBox);

    temperatureGrid->addWidget(new QLabel(QStringLiteral("温控通道"), groupBox), 0, 0);
    temperatureGrid->addWidget(m_temperatureChannelCombo, 0, 1);
    temperatureGrid->addWidget(new QLabel(QStringLiteral("目标温度"), groupBox), 1, 0);
    temperatureGrid->addWidget(m_temperatureSetpointSpin, 1, 1);
    temperatureGrid->addWidget(m_temperatureStartButton, 1, 2);
    temperatureGrid->addWidget(m_temperatureStopButton, 2, 2);
    layout->addLayout(temperatureGrid);

    m_temperatureStatusLabel = new QLabel(QStringLiteral("温控待连接"), groupBox);
    m_temperatureStatusLabel->setWordWrap(true);
    m_temperatureStatusLabel->setMinimumHeight(58);
    m_temperatureStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_temperatureStatusLabel);
    layout->addStretch();

    connect(refreshPortsButton, &QPushButton::clicked, this, &TreatmentPage::refreshFluidSerialPorts);
    connect(m_waterPumpConnectionButton, &QPushButton::clicked, this, &TreatmentPage::toggleWaterPumpConnection);
    connect(m_liquidLevelConnectionButton, &QPushButton::clicked, this, &TreatmentPage::toggleLiquidLevelConnection);
    connect(m_temperatureConnectionButton, &QPushButton::clicked, this, &TreatmentPage::toggleTemperatureConnection);
    connect(m_robotPumpFillButton, &QPushButton::clicked, this, &TreatmentPage::startRobotPumpFill);
    connect(m_robotPumpDrainButton, &QPushButton::clicked, this, &TreatmentPage::startRobotPumpDrain);
    connect(m_robotPumpStopButton, &QPushButton::clicked, this, &TreatmentPage::stopRobotPumpFromUi);
    connect(m_confirmTank2FillButton, &QPushButton::clicked, this, &TreatmentPage::confirmTank2Fill);
    connect(m_startCycleButton, &QPushButton::clicked, this, &TreatmentPage::startWaterCycle);
    connect(m_stopCycleButton, &QPushButton::clicked, this, &TreatmentPage::stopWaterCycle);
    connect(m_stopFluidDevicesButton, &QPushButton::clicked, this, &TreatmentPage::stopFluidDevicesFromUi);
    connect(m_temperatureStartButton, &QPushButton::clicked, this, &TreatmentPage::startHeating);
    connect(m_temperatureStopButton, &QPushButton::clicked, this, &TreatmentPage::stopHeating);

    return groupBox;
}

bool TreatmentPage::prepareTreatmentMotorGateway(QString* errorMessage)
{
    if (!m_treatmentMotorGateway.isSdkLoaded()) {
        const QString sdkPath = defaultTreatmentMotorSdkPath();
        if (!m_treatmentMotorGateway.loadSdk(sdkPath, errorMessage)) {
            return false;
        }
    }

    if (!m_treatmentMotorGateway.isGatewayOpen()) {
        m_treatmentMotorDevices = m_treatmentMotorGateway.searchGateways(errorMessage);
        if (m_treatmentMotorDevices.isEmpty()) {
            if (errorMessage != nullptr && errorMessage->trimmed().isEmpty()) {
                *errorMessage = QStringLiteral("未搜索到 USB-CAN 网关");
            }
            return false;
        }

        QString openError;
        if (!m_treatmentMotorGateway.openGateway(m_treatmentMotorDevices.first().deviceIndex, &openError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1。请确认设备监控页没有打开同一个 USB-CAN 网关。").arg(openError);
            }
            return false;
        }
    }

    m_treatmentMotorNodes = m_treatmentMotorGateway.nodes();
    const QVector<int> requiredNodeIds {
        kTreatmentSwingAxisNodeId,
        kTreatmentLayerAxisNodeId,
        kTreatmentVerticalAxisNodeId
    };
    for (int requiredNodeId : requiredNodeIds) {
        const bool nodeExists = std::any_of(
            m_treatmentMotorNodes.cbegin(),
            m_treatmentMotorNodes.cend(),
            [requiredNodeId](const diji::adapters::uim::UimNodeInfo& node) {
                return static_cast<int>(node.nodeId) == requiredNodeId;
            });
        if (!nodeExists) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("未发现 %1 号治疗电机节点").arg(requiredNodeId);
            }
            return false;
        }
    }

    return true;
}

bool TreatmentPage::selectTreatmentMotor(quint32 nodeId, QString* errorMessage)
{
    if (!m_treatmentMotorGateway.selectNode(nodeId, errorMessage)) {
        return false;
    }
    if (!m_treatmentMotorGateway.enableMotor(errorMessage)) {
        return false;
    }
    return m_treatmentMotorGateway.setSpeed(kTreatmentMotorSpeed, errorMessage);
}

bool TreatmentPage::readTreatmentMotorSnapshot(
    quint32 nodeId,
    diji::adapters::uim::UimMotorSnapshot* snapshot,
    QString* errorMessage)
{
    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 号电机状态输出为空").arg(nodeId);
        }
        return false;
    }
    if (!prepareTreatmentMotorGateway(errorMessage)) {
        return false;
    }
    if (!m_treatmentMotorGateway.selectNode(nodeId, errorMessage)) {
        return false;
    }
    if (!m_treatmentMotorGateway.refreshSnapshot(errorMessage)) {
        return false;
    }

    *snapshot = m_treatmentMotorGateway.latestSnapshot();
    return true;
}

bool TreatmentPage::readTreatmentMotorPosition(quint32 nodeId, int* positionSteps, QString* errorMessage)
{
    if (positionSteps == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 号电机位置输出为空").arg(nodeId);
        }
        return false;
    }

    diji::adapters::uim::UimMotorSnapshot snapshot;
    if (!readTreatmentMotorSnapshot(nodeId, &snapshot, errorMessage)) {
        return false;
    }

    QString positionError;
    if (!treatmentSnapshotPosition(snapshot, positionSteps, &positionError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取 %1 号电机绝对位置：%2。当前状态：%3")
                .arg(nodeId)
                .arg(positionError, treatmentMotorStatusText(snapshot));
        }
        return false;
    }
    return true;
}

bool TreatmentPage::moveTreatmentMotorToAbsolute(
    quint32 nodeId,
    int targetPositionSteps,
    int minimumPositionSteps,
    int maximumPositionSteps,
    const QString& axisLabel,
    QString* errorMessage)
{
    if (treatmentPositionOutsideHardRange(targetPositionSteps, minimumPositionSteps, maximumPositionSteps)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 目标位置 %2 超出安全范围 %3-%4（边界允许误差 ±%5 步）")
                .arg(axisLabel)
                .arg(targetPositionSteps)
                .arg(minimumPositionSteps)
                .arg(maximumPositionSteps)
                .arg(kTreatmentMotorBoundaryToleranceSteps);
        }
        return false;
    }
    const int effectiveTargetPositionSteps = treatmentClampPositionToSafetyRange(
        targetPositionSteps,
        minimumPositionSteps,
        maximumPositionSteps);
    if (effectiveTargetPositionSteps != targetPositionSteps) {
        appendLog(QStringLiteral("%1 目标边界误差允许：目标 %2 步，按 %3 步执行")
            .arg(axisLabel)
            .arg(targetPositionSteps)
            .arg(effectiveTargetPositionSteps));
    }

    diji::adapters::uim::UimMotorSnapshot startSnapshot;
    if (!readTreatmentMotorSnapshot(nodeId, &startSnapshot, errorMessage)) {
        return false;
    }
    int rawCurrentPositionSteps = 0;
    QString startPositionError;
    if (!treatmentSnapshotPosition(startSnapshot, &rawCurrentPositionSteps, &startPositionError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 无法读取当前位置：%2。当前状态：%3")
                .arg(axisLabel, startPositionError, treatmentMotorStatusText(startSnapshot));
        }
        return false;
    }
    if (treatmentPositionOutsideHardRange(rawCurrentPositionSteps, minimumPositionSteps, maximumPositionSteps)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 当前位置 %2 超出安全范围 %3-%4（边界允许误差 ±%5 步），拒绝运动。当前状态：%6")
                .arg(axisLabel)
                .arg(rawCurrentPositionSteps)
                .arg(minimumPositionSteps)
                .arg(maximumPositionSteps)
                .arg(kTreatmentMotorBoundaryToleranceSteps)
                .arg(treatmentMotorStatusText(startSnapshot));
        }
        return false;
    }
    const int currentPositionSteps = treatmentClampPositionToSafetyRange(
        rawCurrentPositionSteps,
        minimumPositionSteps,
        maximumPositionSteps);
    if (currentPositionSteps != rawCurrentPositionSteps) {
        appendLog(QStringLiteral("%1 边界误差允许：读取 %2 步，按 %3 步参与治疗运动")
            .arg(axisLabel)
            .arg(rawCurrentPositionSteps)
            .arg(currentPositionSteps));
    }
    if (std::abs(currentPositionSteps - effectiveTargetPositionSteps) <= kTreatmentMotorPositionToleranceSteps) {
        appendLog(QStringLiteral("%1 已在目标附近：当前 %2 步，目标 %3 步，未重复运动")
            .arg(axisLabel)
            .arg(currentPositionSteps)
            .arg(effectiveTargetPositionSteps));
        return true;
    }

    if (!selectTreatmentMotor(nodeId, errorMessage)) {
        return false;
    }

    const int relativeSteps = effectiveTargetPositionSteps - currentPositionSteps;
    appendLog(QStringLiteral("%1 下发运动：当前 %2 步，目标 %3 步，相对 %4 步")
        .arg(axisLabel)
        .arg(currentPositionSteps)
        .arg(effectiveTargetPositionSteps)
        .arg(relativeSteps));
    if (!m_treatmentMotorGateway.setStep(relativeSteps, errorMessage)) {
        return false;
    }

    int latestPositionSteps = currentPositionSteps;
    bool hasObservedMovement = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kTreatmentMotorMoveTimeoutMs) {
        waitForTreatmentMotor(kTreatmentMotorPollMs);
        QCoreApplication::processEvents();

        if (m_safetyKernel != nullptr && m_safetyKernel->mode() == SystemMode::Alarm) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 运动过程中安全联锁进入报警态").arg(axisLabel);
            }
            return false;
        }

        QString readError;
        diji::adapters::uim::UimMotorSnapshot polledSnapshot;
        if (!readTreatmentMotorSnapshot(nodeId, &polledSnapshot, &readError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 运动过程中无法读取绝对位置：%2").arg(axisLabel, readError);
            }
            return false;
        }
        const int rawPolledPositionSteps = treatmentMotionPosition(startSnapshot, polledSnapshot, latestPositionSteps);

        if (treatmentPositionOutsideHardRange(rawPolledPositionSteps, minimumPositionSteps, maximumPositionSteps)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 运动过程中位置 %2 超出安全范围 %3-%4（边界允许误差 ±%5 步）。当前状态：%6")
                    .arg(axisLabel)
                    .arg(rawPolledPositionSteps)
                    .arg(minimumPositionSteps)
                    .arg(maximumPositionSteps)
                    .arg(kTreatmentMotorBoundaryToleranceSteps)
                    .arg(treatmentMotorStatusText(polledSnapshot));
            }
            return false;
        }
        latestPositionSteps = treatmentClampPositionToSafetyRange(
            rawPolledPositionSteps,
            minimumPositionSteps,
            maximumPositionSteps);

        if (treatmentSnapshotMoved(startSnapshot, polledSnapshot)) {
            hasObservedMovement = true;
        }
        if (!hasObservedMovement && timer.elapsed() >= kTreatmentMotorMoveStartTimeoutMs) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1 运动命令已下发，但 %2 ms 内 POS/QEC 均未变化。当前状态：%3")
                    .arg(axisLabel)
                    .arg(kTreatmentMotorMoveStartTimeoutMs)
                    .arg(treatmentMotorStatusText(polledSnapshot));
            }
            return false;
        }

        if (std::abs(latestPositionSteps - effectiveTargetPositionSteps) <= kTreatmentMotorPositionToleranceSteps) {
            appendLog(QStringLiteral("%1 到位：%2 步").arg(axisLabel).arg(latestPositionSteps));
            return true;
        }
    }

    if (errorMessage != nullptr) {
        diji::adapters::uim::UimMotorSnapshot timeoutSnapshot;
        const QString timeoutStatus = readTreatmentMotorSnapshot(nodeId, &timeoutSnapshot, nullptr)
            ? treatmentMotorStatusText(timeoutSnapshot)
            : QStringLiteral("无法读取");
        *errorMessage = QStringLiteral("%1 运动超时，目标 %2，当前位置 %3。当前状态：%4")
            .arg(axisLabel)
            .arg(effectiveTargetPositionSteps)
            .arg(latestPositionSteps)
            .arg(timeoutStatus);
    }
    return false;
}

bool TreatmentPage::prepareSelectedLayerTreatmentMotors(
    const TherapyPlan& plan,
    const TherapySegment& segment,
    QString* errorMessage)
{
    Q_UNUSED(plan)
    if (!prepareTreatmentMotorGateway(errorMessage)) {
        return false;
    }
    if (treatmentPositionOutsideHardRange(
            segment.axis7PositionSteps,
            kTreatmentLayerAxisMinimumSteps,
            kTreatmentLayerAxisMaximumSteps)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前治疗层缺少有效的 7 号采集绝对位置，请重新执行图像采集并生成治疗方案");
        }
        return false;
    }

    int beforePositionSteps = 0;
    if (!readTreatmentMotorPosition(kTreatmentLayerAxisNodeId, &beforePositionSteps, errorMessage)) {
        return false;
    }
    appendLog(
        QStringLiteral("7号层位定位：当前 %1 步，目标 %2 步，层 %3")
            .arg(beforePositionSteps)
            .arg(segment.axis7PositionSteps)
            .arg(segment.sourceSliceIndex >= 0 ? segment.sourceSliceIndex + 1 : segment.orderIndex + 1));

    if (!moveTreatmentMotorToAbsolute(
            kTreatmentLayerAxisNodeId,
            segment.axis7PositionSteps,
            kTreatmentLayerAxisMinimumSteps,
            kTreatmentLayerAxisMaximumSteps,
            QStringLiteral("7号左右层位电机"),
            errorMessage)) {
        return false;
    }

    int afterPositionSteps = segment.axis7PositionSteps;
    QString readError;
    if (readTreatmentMotorPosition(kTreatmentLayerAxisNodeId, &afterPositionSteps, &readError)) {
        appendLog(QStringLiteral("7号层位到位：%1 -> %2 步").arg(beforePositionSteps).arg(afterPositionSteps));
    }

    if (!readTreatmentMotorPosition(kTreatmentSwingAxisNodeId, &m_treatmentSwingCenterSteps, errorMessage)) {
        m_hasTreatmentSwingCenter = false;
        return false;
    }
    m_hasTreatmentSwingCenter = true;
    appendLog(
        QStringLiteral("6号摆动中心：当前 %1 步，治疗过程限制在左右 %2°（±%3 步）")
            .arg(m_treatmentSwingCenterSteps)
            .arg(kTreatmentSwingMaximumDegrees, 0, 'f', 0)
            .arg(kTreatmentSwingMaximumSteps));
    return true;
}

bool TreatmentPage::moveTreatmentPointMotors(const TherapySegment& segment, int pointIndex, QString* errorMessage)
{
    if (pointIndex < 0 || pointIndex >= segment.points.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("治疗靶点序号无效：%1").arg(pointIndex + 1);
        }
        return false;
    }

    const TherapyPoint& point = segment.points.at(pointIndex);
    const double verticalMillimeters = std::clamp(
        point.positionMm.y(),
        0.0,
        kTreatmentVerticalAxisMaximumMillimeters);
    const qint64 verticalTargetSteps64 = std::llround(verticalMillimeters * kTreatmentLinearStepsPerMillimeter);
    if (verticalTargetSteps64 > kTreatmentVerticalAxisMaximumSteps) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("8号上下电机目标 %1 步超过 S2=%2，请校准像素到毫米比例")
                .arg(verticalTargetSteps64)
                .arg(kTreatmentVerticalAxisMaximumSteps);
        }
        return false;
    }
    const int verticalTargetSteps = static_cast<int>(verticalTargetSteps64);

    const double swingDegrees = ultrasoundLateralMillimetersToSwingDegrees(point.positionMm.x());
    const int swingOffsetSteps = static_cast<int>(std::llround(swingDegrees * kTreatmentSwingStepsPerDegree));
    if (!m_hasTreatmentSwingCenter) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("6号摆动中心尚未记录，请重新开始当前层治疗");
        }
        return false;
    }
    const int swingTargetSteps = m_treatmentSwingCenterSteps + swingOffsetSteps;

    int beforeVerticalSteps = 0;
    int beforeSwingSteps = 0;
    if (!readTreatmentMotorPosition(kTreatmentVerticalAxisNodeId, &beforeVerticalSteps, errorMessage)
        || !readTreatmentMotorPosition(kTreatmentSwingAxisNodeId, &beforeSwingSteps, errorMessage)) {
        return false;
    }

    appendLog(
        QStringLiteral("靶点 %1 定位：8号 %2 mm/%3 步，6号 %4°/%5 步，目标 %6 步")
            .arg(pointIndex + 1)
            .arg(verticalMillimeters, 0, 'f', 2)
            .arg(verticalTargetSteps)
            .arg(swingDegrees, 0, 'f', 2)
            .arg(swingOffsetSteps)
            .arg(swingTargetSteps));

    if (!moveTreatmentMotorToAbsolute(
            kTreatmentVerticalAxisNodeId,
            verticalTargetSteps,
            kTreatmentVerticalAxisMinimumSteps,
            kTreatmentVerticalAxisMaximumSteps,
            QStringLiteral("8号上下电机"),
            errorMessage)) {
        return false;
    }
    if (!moveTreatmentMotorToAbsolute(
            kTreatmentSwingAxisNodeId,
            swingTargetSteps,
            m_treatmentSwingCenterSteps - kTreatmentSwingMaximumSteps,
            m_treatmentSwingCenterSteps + kTreatmentSwingMaximumSteps,
            QStringLiteral("6号摆动电机"),
            errorMessage)) {
        return false;
    }

    int afterVerticalSteps = verticalTargetSteps;
    int afterSwingSteps = swingTargetSteps;
    QString readError;
    readTreatmentMotorPosition(kTreatmentVerticalAxisNodeId, &afterVerticalSteps, &readError);
    readTreatmentMotorPosition(kTreatmentSwingAxisNodeId, &afterSwingSteps, &readError);
    appendLog(
        QStringLiteral("靶点 %1 到位：8号 %2 -> %3 步，6号 %4 -> %5 步")
            .arg(pointIndex + 1)
            .arg(beforeVerticalSteps)
            .arg(afterVerticalSteps)
            .arg(beforeSwingSteps)
            .arg(afterSwingSteps));
    return true;
}

void TreatmentPage::waitForTreatmentMotor(int milliseconds)
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

void TreatmentPage::loadTreatmentRobotPumpSettings()
{
    m_robotPumpSettings = panthera::adapters::dobot::DobotConnectionSettings {};
    m_robotPumpSettings.host = QStringLiteral("192.168.5.1");
    m_robotPumpSettings.commandPort = 29999;
    m_robotPumpSettings.motionPort = 29999;
    m_robotPumpSettings.timeoutMs = 3000;

    const QString defaultsIniPath = treatmentResolveRuntimePath(QStringLiteral("config/defaults.ini"));
    if (!QFileInfo::exists(defaultsIniPath)) {
        return;
    }

    QSettings settings(defaultsIniPath, QSettings::IniFormat);
    const QString configuredHost = settings.value(QStringLiteral("dobot/host"), m_robotPumpSettings.host).toString().trimmed();
    if (!configuredHost.isEmpty()) {
        m_robotPumpSettings.host = configuredHost;
    }
    m_robotPumpSettings.commandPort = static_cast<quint16>(
        treatmentSafePort(
            settings.value(QStringLiteral("dobot/dashboard_port"), static_cast<int>(m_robotPumpSettings.commandPort)).toInt(),
            m_robotPumpSettings.commandPort));
    m_robotPumpSettings.motionPort = static_cast<quint16>(
        treatmentSafePort(
            settings.value(QStringLiteral("dobot/motion_port"), static_cast<int>(m_robotPumpSettings.motionPort)).toInt(),
            m_robotPumpSettings.motionPort));
    m_robotPumpSettings.timeoutMs =
        qBound(1000, settings.value(QStringLiteral("dobot/timeout_ms"), m_robotPumpSettings.timeoutMs).toInt(), 10000);
}

void TreatmentPage::applyTreatmentRobotPumpSettings()
{
    m_robotPumpClient.setSettings(m_robotPumpSettings);
}

bool TreatmentPage::ensureRobotPumpControl(QString* errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (!m_robotPumpClient.isConnected()) {
        QString connectError;
        if (!m_robotPumpClient.connectToController(&connectError)) {
            if (errorMessage != nullptr) {
                *errorMessage = connectError;
            }
            return false;
        }
        appendLog(QStringLiteral("RO 第三泵已连接机械臂 %1:%2").arg(m_robotPumpSettings.host).arg(m_robotPumpSettings.commandPort));
    }

    QString controlError;
    const panthera::adapters::dobot::DobotCommandResult controlResult = m_robotPumpClient.requestControl(&controlError);
    if (!controlResult.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = controlError.isEmpty() ? controlResult.protocolError : controlError;
        }
        return false;
    }
    return true;
}

bool TreatmentPage::sendRobotPumpDo(int index, bool on, const QString& action, QString* errorMessage)
{
    QString commandError;
    const panthera::adapters::dobot::DobotCommandResult result =
        m_robotPumpClient.setDigitalOutputInstant(index, on, &commandError);
    appendLog(QStringLiteral("%1：DOInstant(%2,%3) -> ErrorID=%4")
                  .arg(action)
                  .arg(index)
                  .arg(on ? 1 : 0)
                  .arg(result.errorId));
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = commandError.isEmpty() ? result.protocolError : commandError;
        }
        return false;
    }
    return true;
}

bool TreatmentPage::setRobotPumpMode(bool do13On, bool do14On, const QString& action, QString* errorMessage)
{
    if (!ensureRobotPumpControl(errorMessage)) {
        return false;
    }

    if (do13On && do14On) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("禁止同时打开 DO13 和 DO14");
        }
        return false;
    }

    if (do13On) {
        if (!sendRobotPumpDo(kRobotPumpReverseDo, false, action, errorMessage)) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(80);
        return sendRobotPumpDo(kRobotPumpForwardDo, true, action, errorMessage);
    }

    if (do14On) {
        if (!sendRobotPumpDo(kRobotPumpForwardDo, false, action, errorMessage)) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(80);
        return sendRobotPumpDo(kRobotPumpReverseDo, true, action, errorMessage);
    }

    return sendRobotPumpDo(kRobotPumpForwardDo, false, action, errorMessage)
        && sendRobotPumpDo(kRobotPumpReverseDo, false, action, errorMessage);
}

void TreatmentPage::refreshFluidSerialPorts()
{
    if (!m_waterPumpClient.isOpen()) {
        refreshSerialPortCombo(m_waterPumpPortCombo);
    }
    if (!m_liquidLevelClient.isOpen()) {
        refreshSerialPortCombo(m_liquidLevelPortCombo);
    }
    if (!m_temperatureClient.isOpen()) {
        refreshSerialPortCombo(m_temperaturePortCombo);
    }
}

void TreatmentPage::toggleWaterPumpConnection()
{
    if (m_waterPumpClient.isOpen()) {
        const QString port = m_waterPumpClient.portName();
        m_waterPumpClient.close();
        setFluidStatus(QStringLiteral("水泵 485 已断开：%1").arg(port), false);
        refreshFluidUi();
        return;
    }

    panthera::adapters::waterpump::WaterPumpSerialSettings settings;
    settings.portName = selectedSerialPortName(m_waterPumpPortCombo);
    settings.baudRate = selectedBaudRate(m_waterPumpBaudCombo, 9600);

    QString errorMessage;
    if (!m_waterPumpClient.open(settings, &errorMessage)) {
        setFluidStatus(errorMessage, false);
        refreshFluidUi();
        return;
    }

    setFluidStatus(QStringLiteral("水泵 485 连接成功：%1 @ %2 bps")
                       .arg(m_waterPumpClient.portName())
                       .arg(m_waterPumpClient.baudRate()),
                   true);
    refreshFluidUi();
}

void TreatmentPage::toggleTemperatureConnection()
{
    if (m_temperatureClient.isOpen()) {
        const QString port = m_temperatureClient.portName();
        m_temperatureClient.close();
        m_temperatureRefreshTimer.stop();
        setTemperatureStatus(QStringLiteral("温控 485 已断开：%1").arg(port), false);
        refreshTemperatureUi();
        return;
    }

    panthera::adapters::anthone::Lu926TemperatureSerialSettings settings;
    settings.portName = selectedSerialPortName(m_temperaturePortCombo);
    settings.baudRate = selectedBaudRate(
        m_temperatureBaudCombo,
        panthera::adapters::anthone::Lu926TemperatureProtocol::kDefaultBaudRate);

    QString errorMessage;
    if (!m_temperatureClient.open(settings, &errorMessage)) {
        setTemperatureStatus(errorMessage, false);
        refreshTemperatureUi();
        return;
    }

    setTemperatureStatus(QStringLiteral("温控 485 连接成功：%1 @ %2 bps，地址 04")
                             .arg(m_temperatureClient.portName())
                             .arg(m_temperatureClient.baudRate()),
                         true);
    refreshTemperatureUi();
    m_temperatureRefreshTimer.start();
    onTemperatureRefreshTick();
}

void TreatmentPage::toggleLiquidLevelConnection()
{
    if (m_liquidLevelClient.isOpen()) {
        const QString port = m_liquidLevelClient.portName();
        m_liquidLevelClient.close();
        setFluidStatus(QStringLiteral("液位传感器 485 已断开：%1").arg(port), false);
        refreshFluidUi();
        return;
    }

    panthera::adapters::liquidlevel::LiquidLevelSerialSettings settings;
    settings.portName = selectedSerialPortName(m_liquidLevelPortCombo);
    settings.baudRate = selectedBaudRate(m_liquidLevelBaudCombo, 9600);

    QString errorMessage;
    if (!m_liquidLevelClient.open(settings, &errorMessage)) {
        setFluidStatus(errorMessage, false);
        refreshFluidUi();
        return;
    }

    setFluidStatus(QStringLiteral("液位传感器 485 连接成功：%1 @ %2 bps，地址 01")
                       .arg(m_liquidLevelClient.portName())
                       .arg(m_liquidLevelClient.baudRate()),
                   true);
    refreshFluidUi();
}

bool TreatmentPage::ensureWaterPumpConnection()
{
    if (m_waterPumpClient.isOpen()) {
        return true;
    }
    setFluidStatus(QStringLiteral("请先连接 02/03 水泵 485 串口"), false);
    refreshFluidUi();
    return false;
}

bool TreatmentPage::ensureTemperatureConnection()
{
    if (m_temperatureClient.isOpen()) {
        return true;
    }
    setTemperatureStatus(QStringLiteral("请先连接温控 485 串口"), false);
    refreshTemperatureUi();
    return false;
}

bool TreatmentPage::ensureLiquidLevelConnection()
{
    if (m_liquidLevelClient.isOpen()) {
        return true;
    }
    setFluidStatus(QStringLiteral("请先连接液位传感器 485 串口"), false);
    refreshFluidUi();
    return false;
}

bool TreatmentPage::setWaterPumpFlow(quint8 address, double flowMlPerMin, QString* errorMessage, QByteArray* response)
{
    return m_waterPumpClient.setFlowMlPerMin(address, flowMlPerMin, errorMessage, response);
}

bool TreatmentPage::startWaterPump(quint8 address, QString* errorMessage, QByteArray* response)
{
    return m_waterPumpClient.startPump(address, errorMessage, response);
}

bool TreatmentPage::stopWaterPump(quint8 address, QString* errorMessage, QByteArray* response)
{
    return m_waterPumpClient.stopPump(address, errorMessage, response);
}

void TreatmentPage::startRobotPumpFill()
{
    resetTank1UpperLimitDebounce();
    m_robotPumpFillingTank1 = false;
    setFluidWorkflowState(FluidWorkflowState::FillingTank1);
    setFluidStatus(QStringLiteral("RO 注水流程启动：检测水箱1上限位"), true);
    if (!m_fluidControlTimer.isActive()) {
        m_fluidControlTimer.start();
    }
    onFluidControlTick();
}

void TreatmentPage::startRobotPumpDrain()
{
    QString errorMessage;
    if (!setRobotPumpMode(false, true, QStringLiteral("RO 第三泵出水"), &errorMessage)) {
        setFluidStatus(QStringLiteral("RO 出水启动失败：%1").arg(errorMessage), false);
        return;
    }
    m_robotPumpFillingTank1 = false;
    setFluidStatus(QStringLiteral("RO 出水已启动：DO13=OFF / DO14=ON"), true);
}

void TreatmentPage::stopRobotPumpFromUi()
{
    QString errorMessage;
    if (!setRobotPumpMode(false, false, QStringLiteral("RO 第三泵停止"), &errorMessage)) {
        setFluidStatus(QStringLiteral("RO 停止失败：%1").arg(errorMessage), false);
        return;
    }
    m_robotPumpFillingTank1 = false;
    setFluidStatus(QStringLiteral("RO 第三泵已停止"), true);
}

void TreatmentPage::confirmTank2Fill()
{
    if (m_fluidWorkflowState != FluidWorkflowState::WaitingTank2Confirm
        && m_fluidWorkflowState != FluidWorkflowState::ReadyToCycle) {
        setFluidStatus(QStringLiteral("当前阶段不需要确认 03 加水"), false);
        return;
    }
    startTank2FillInternal();
}

void TreatmentPage::startWaterCycle()
{
    if (!ensureWaterPumpConnection() || !ensureLiquidLevelConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!setWaterPumpFlow(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            kDefaultLoopFlowMlPerMin,
            &errorMessage,
            &response)) {
        setFluidStatus(QStringLiteral("启动循环失败：03 流速设置失败：%1").arg(errorMessage), false);
        return;
    }
    if (!setWaterPumpFlow(
            panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress,
            kDefaultLoopFlowMlPerMin,
            &errorMessage,
            &response)) {
        setFluidStatus(QStringLiteral("启动循环失败：02 流速设置失败：%1").arg(errorMessage), false);
        return;
    }

    m_cycleBalanceMode = CycleBalanceMode::Unknown;
    setFluidWorkflowState(FluidWorkflowState::Cycling);
    setCycleBalanceMode(CycleBalanceMode::BothPumps);
    if (!m_fluidControlTimer.isActive()) {
        m_fluidControlTimer.start();
    }
    setFluidStatus(QStringLiteral("循环已启动：03 与 02 同步运行，目标液位 %1 mm")
                       .arg(m_tank2TargetLevelSpin != nullptr ? m_tank2TargetLevelSpin->value() : 0.0, 0, 'f', 1),
                   true);
}

void TreatmentPage::stopWaterCycle()
{
    if (!m_waterPumpClient.isOpen()) {
        setFluidStatus(QStringLiteral("水泵 485 未连接，循环停止命令未发送"), false);
        return;
    }

    QStringList failures;
    QString errorMessage;
    QByteArray response;
    if (!stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &response)) {
        failures.push_back(QStringLiteral("03：%1").arg(errorMessage));
    }
    if (!stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &response)) {
        failures.push_back(QStringLiteral("02：%1").arg(errorMessage));
    }

    m_cycleBalanceMode = CycleBalanceMode::Unknown;
    if (m_fluidWorkflowState == FluidWorkflowState::Cycling) {
        setFluidWorkflowState(FluidWorkflowState::ReadyToCycle);
    }
    if (m_fluidWorkflowState != FluidWorkflowState::FillingTank1
        && m_fluidWorkflowState != FluidWorkflowState::FillingTank2) {
        m_fluidControlTimer.stop();
    }

    if (!failures.isEmpty()) {
        setFluidStatus(QStringLiteral("停止循环未全部成功：%1").arg(failures.join(QStringLiteral("；"))), false);
        return;
    }
    setFluidStatus(QStringLiteral("循环已停止，流程阶段保持为：%1").arg(fluidWorkflowStateText()), true);
}

void TreatmentPage::stopFluidDevicesFromUi()
{
    QString summary;
    stopAllFluidDevices(false, true, &summary);
    setFluidStatus(QStringLiteral("水路已手动停止，流程阶段保持为：%1\n%2")
                       .arg(fluidWorkflowStateText(), summary),
                   true);
}

bool TreatmentPage::stopAllFluidDevices(bool resetWorkflow, bool stopHeatingDevice, QString* summary)
{
    QStringList messages;
    QStringList failures;
    QString errorMessage;
    QByteArray response;

    m_fluidControlTimer.stop();
    m_robotPumpFillingTank1 = false;
    m_cycleBalanceMode = CycleBalanceMode::Unknown;

    if (m_waterPumpClient.isOpen()) {
        if (stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &response)) {
            messages.push_back(QStringLiteral("03 已停止"));
        } else {
            failures.push_back(QStringLiteral("03：%1").arg(errorMessage));
        }
        if (stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &response)) {
            messages.push_back(QStringLiteral("02 已停止"));
        } else {
            failures.push_back(QStringLiteral("02：%1").arg(errorMessage));
        }
    }

    if (m_robotPumpClient.isConnected() || m_robotPumpFillingTank1) {
        errorMessage.clear();
        if (setRobotPumpMode(false, false, QStringLiteral("RO 第三泵安全停止"), &errorMessage)) {
            messages.push_back(QStringLiteral("RO 已停止"));
        } else if (!errorMessage.trimmed().isEmpty()) {
            failures.push_back(QStringLiteral("RO：%1").arg(errorMessage));
        }
    }

    if (stopHeatingDevice) {
        m_temperatureRefreshTimer.stop();
        if (m_temperatureClient.isOpen()) {
            errorMessage.clear();
            if (setTemperatureSetpoint(0.0, &errorMessage, &response)) {
                m_activeTemperatureTargetCelsius = 0.0;
                messages.push_back(QStringLiteral("加热目标已置 0°C"));
            } else {
                failures.push_back(QStringLiteral("温控：%1").arg(errorMessage));
            }
        }
    }

    if (resetWorkflow) {
        setFluidWorkflowState(FluidWorkflowState::Idle);
    }
    refreshFluidUi();
    refreshTemperatureUi();

    if (summary != nullptr) {
        QStringList parts = messages;
        for (const QString& failure : failures) {
            parts.push_back(QStringLiteral("失败 %1").arg(failure));
        }
        *summary = parts.isEmpty() ? QStringLiteral("没有已连接设备需要停止") : parts.join(QStringLiteral("；"));
    }
    return failures.isEmpty();
}

bool TreatmentPage::readTank1UpperLimit(bool* active, QString* errorMessage)
{
    if (active != nullptr) {
        *active = false;
    }
    if (!prepareTreatmentMotorGateway(errorMessage)) {
        return false;
    }
    if (!selectTreatmentMotor(static_cast<quint32>(kTank1UpperLimitNodeId), errorMessage)) {
        return false;
    }
    if (!m_treatmentMotorGateway.refreshSensorFeedback(errorMessage)) {
        return false;
    }

    const diji::adapters::uim::UimMotorSnapshot snapshot = m_treatmentMotorGateway.latestSnapshot();
    if (!snapshot.hasSensorFeedback) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("水箱1上限位未返回 S1/S2/S3 反馈");
        }
        return false;
    }

    if (active != nullptr) {
        *active = tankLimitSensorActive(snapshot, kTank1UpperLimitSensorIndex);
    }
    return true;
}

bool TreatmentPage::tank1UpperLimitDebounced(bool active)
{
    if (!active) {
        resetTank1UpperLimitDebounce();
        return false;
    }

    if (!m_tank1UpperLimitRawActive) {
        m_tank1UpperLimitRawActive = true;
        m_tank1UpperLimitDebounceTimer.start();
        return false;
    }

    return m_tank1UpperLimitDebounceTimer.isValid()
        && m_tank1UpperLimitDebounceTimer.elapsed() >= kTank1UpperLimitDebounceMs;
}

void TreatmentPage::resetTank1UpperLimitDebounce()
{
    m_tank1UpperLimitRawActive = false;
    m_tank1UpperLimitDebounceTimer.invalidate();
}

bool TreatmentPage::readTank2Level(double* millimeters, QString* errorMessage, QByteArray* response)
{
    if (!m_liquidLevelClient.readLevelMillimeters(
            panthera::adapters::liquidlevel::LiquidLevelModbusClient::kDefaultAddress,
            millimeters,
            errorMessage,
            response)) {
        return false;
    }
    if (millimeters != nullptr) {
        updateTank2LevelDisplay(*millimeters);
    }
    return true;
}

int TreatmentPage::selectedTemperatureChannel() const
{
    if (m_temperatureChannelCombo == nullptr) {
        return 1;
    }
    const QVariant value = m_temperatureChannelCombo->currentData();
    return value.isValid() ? value.toInt() : m_temperatureChannelCombo->currentIndex() + 1;
}

bool TreatmentPage::setTemperatureSetpoint(double celsius, QString* errorMessage, QByteArray* response)
{
    return m_temperatureClient.setChannelSetpoint(
        panthera::adapters::anthone::Lu926TemperatureProtocol::kDefaultAddress,
        selectedTemperatureChannel(),
        celsius,
        errorMessage,
        response);
}

void TreatmentPage::startHeating()
{
    if (!ensureTemperatureConnection() || m_temperatureSetpointSpin == nullptr) {
        return;
    }

    const double celsius = m_temperatureSetpointSpin->value();
    if (celsius < kTemperatureMinimumCelsius || celsius > kTemperatureMaximumCelsius) {
        QMessageBox::warning(
            this,
            QStringLiteral("温控设定异常"),
            QStringLiteral("目标温度需在 %1 - %2 °C。")
                .arg(kTemperatureMinimumCelsius, 0, 'f', 1)
                .arg(kTemperatureMaximumCelsius, 0, 'f', 1));
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!setTemperatureSetpoint(celsius, &errorMessage, &response)) {
        setTemperatureStatus(QStringLiteral("开始加热失败：%1").arg(errorMessage), false);
        QMessageBox::warning(this, QStringLiteral("温控通信异常"), QStringLiteral("开始加热失败：%1").arg(errorMessage));
        return;
    }

    m_activeTemperatureTargetCelsius = celsius;
    m_temperatureAlarmDisplayed = false;
    setTemperatureStatus(QStringLiteral("已设定目标温度 %1 °C，温控模块将自动加热\n响应：%2")
                             .arg(celsius, 0, 'f', 1)
                             .arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(response)),
                         true);
    refreshTemperatureUi();
    if (!m_temperatureRefreshTimer.isActive()) {
        m_temperatureRefreshTimer.start();
    }
    onTemperatureRefreshTick();
}

void TreatmentPage::stopHeating()
{
    if (!ensureTemperatureConnection()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!setTemperatureSetpoint(0.0, &errorMessage, &response)) {
        setTemperatureStatus(QStringLiteral("停止加热失败：%1").arg(errorMessage), false);
        QMessageBox::warning(this, QStringLiteral("温控通信异常"), QStringLiteral("停止加热失败：%1").arg(errorMessage));
        return;
    }

    m_activeTemperatureTargetCelsius = 0.0;
    setTemperatureStatus(QStringLiteral("已停止加热：目标温度置 0 °C\n响应：%1")
                             .arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(response)),
                         true);
    refreshTemperatureUi();
}

void TreatmentPage::stopHeatingForAlarm(const QString& reason)
{
    QString errorMessage;
    QByteArray response;
    if (m_temperatureClient.isOpen()) {
        setTemperatureSetpoint(0.0, &errorMessage, &response);
    }
    m_activeTemperatureTargetCelsius = 0.0;
    m_temperatureRefreshTimer.stop();
    setTemperatureStatus(QStringLiteral("温控异常，已尝试将目标温度置 0 °C：%1").arg(reason), false);
    if (!m_temperatureAlarmDisplayed) {
        m_temperatureAlarmDisplayed = true;
        QMessageBox::warning(this, QStringLiteral("温控异常"), QStringLiteral("温控异常，已尝试停止加热：%1").arg(reason));
    }
}

void TreatmentPage::onTemperatureRefreshTick()
{
    if (!m_temperatureClient.isOpen()) {
        m_temperatureRefreshTimer.stop();
        refreshTemperatureUi();
        return;
    }

    const int channelIndex = selectedTemperatureChannel();
    QString errorMessage;
    QByteArray response;
    double celsius = 0.0;
    if (!m_temperatureClient.readChannelTemperature(
            panthera::adapters::anthone::Lu926TemperatureProtocol::kDefaultAddress,
            channelIndex,
            &celsius,
            &errorMessage,
            &response)) {
        stopHeatingForAlarm(QStringLiteral("CH%1 读取温度失败：%2").arg(channelIndex).arg(errorMessage));
        return;
    }

    const QString heatState = m_activeTemperatureTargetCelsius <= 0.0
        ? QStringLiteral("已停止")
        : (celsius < m_activeTemperatureTargetCelsius ? QStringLiteral("加热中") : QStringLiteral("目标附近"));
    setTemperatureStatus(QStringLiteral("CH%1 当前温度：%2 °C\n目标：%3 °C；状态：%4\n响应：%5")
                             .arg(channelIndex)
                             .arg(celsius, 0, 'f', 1)
                             .arg(m_activeTemperatureTargetCelsius, 0, 'f', 1)
                             .arg(heatState)
                             .arg(panthera::adapters::anthone::Lu926TemperatureModbusClient::frameToHex(response)),
                         true);
}

void TreatmentPage::onFluidControlTick()
{
    if (m_fluidWorkflowState == FluidWorkflowState::FillingTank1) {
        bool upperLimitActive = false;
        QString errorMessage;
        if (!readTank1UpperLimit(&upperLimitActive, &errorMessage)) {
            if (m_robotPumpFillingTank1) {
                QString stopError;
                setRobotPumpMode(false, false, QStringLiteral("水箱1上限位读取失败，停止 RO"), &stopError);
                m_robotPumpFillingTank1 = false;
            }
            setFluidStatus(QStringLiteral("水箱1上限位读取失败，已停止 RO：%1").arg(errorMessage), false);
            return;
        }

        if (upperLimitActive) {
            if (m_robotPumpFillingTank1) {
                QString stopError;
                setRobotPumpMode(false, false, QStringLiteral("水箱1上限位触发，停止 RO"), &stopError);
                m_robotPumpFillingTank1 = false;
            }
            if (tank1UpperLimitDebounced(true)) {
                handleTank1UpperLimitReached();
                return;
            }
            setFluidStatus(QStringLiteral("水箱1上限位已触发，正在防抖确认"), true);
            return;
        }

        resetTank1UpperLimitDebounce();
        if (!m_robotPumpFillingTank1) {
            QString pumpError;
            if (!setRobotPumpMode(true, false, QStringLiteral("RO 第三泵注水"), &pumpError)) {
                setFluidStatus(QStringLiteral("RO 注水启动失败：%1").arg(pumpError), false);
                return;
            }
            m_robotPumpFillingTank1 = true;
        }
        setFluidStatus(QStringLiteral("RO 注水中：等待水箱1上限位"), true);
        return;
    }

    if (m_fluidWorkflowState == FluidWorkflowState::FillingTank2) {
        if (!ensureLiquidLevelConnection()) {
            return;
        }
        double level = 0.0;
        QString errorMessage;
        QByteArray response;
        if (!readTank2Level(&level, &errorMessage, &response)) {
            stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, nullptr, nullptr);
            setFluidStatus(QStringLiteral("读取水箱2液位失败，已停止03水泵：%1").arg(errorMessage), false);
            return;
        }

        const double target = m_tank2TargetLevelSpin != nullptr ? m_tank2TargetLevelSpin->value() : 0.0;
        if (level >= target) {
            QString stopError;
            QByteArray stopResponse;
            stopWaterPump(
                panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
                &stopError,
                &stopResponse);
            setFluidWorkflowState(FluidWorkflowState::ReadyToCycle);
            m_fluidControlTimer.stop();
            setFluidStatus(QStringLiteral("水箱2已达到目标液位：%1 / %2 mm，03水泵已停止")
                               .arg(level, 0, 'f', 1)
                               .arg(target, 0, 'f', 1),
                           true);
            return;
        }

        setFluidStatus(QStringLiteral("03 向水箱2加水中：%1 / %2 mm")
                           .arg(level, 0, 'f', 1)
                           .arg(target, 0, 'f', 1),
                       true);
        return;
    }

    if (m_fluidWorkflowState == FluidWorkflowState::Cycling) {
        if (!ensureLiquidLevelConnection()) {
            return;
        }

        double level = 0.0;
        QString errorMessage;
        if (!readTank2Level(&level, &errorMessage)) {
            stopWaterCycle();
            setFluidStatus(QStringLiteral("循环中读取液位失败，已停止循环：%1").arg(errorMessage), false);
            return;
        }

        const double target = m_tank2TargetLevelSpin != nullptr ? m_tank2TargetLevelSpin->value() : 0.0;
        if (level > target + kTank2CycleToleranceMillimeters) {
            setCycleBalanceMode(CycleBalanceMode::DrainingTank2);
            setFluidStatus(QStringLiteral("循环中：水箱2偏高 %1 mm，02 开 / 03 关")
                               .arg(level, 0, 'f', 1),
                           true);
        } else if (level < target - kTank2CycleToleranceMillimeters) {
            setCycleBalanceMode(CycleBalanceMode::FillingTank2);
            setFluidStatus(QStringLiteral("循环中：水箱2偏低 %1 mm，03 开 / 02 关")
                               .arg(level, 0, 'f', 1),
                           true);
        } else {
            setCycleBalanceMode(CycleBalanceMode::BothPumps);
            setFluidStatus(QStringLiteral("循环中：水箱2液位 %1 mm，03/02 同步运行")
                               .arg(level, 0, 'f', 1),
                           true);
        }
    }
}

void TreatmentPage::handleTank1UpperLimitReached()
{
    QString stopError;
    setRobotPumpMode(false, false, QStringLiteral("水箱1上限位确认，停止 RO"), &stopError);
    m_robotPumpFillingTank1 = false;
    setFluidWorkflowState(FluidWorkflowState::WaitingTank2Confirm);
    m_fluidControlTimer.stop();
    setFluidStatus(QStringLiteral("水箱1上限位已确认，等待确认启动03向水箱2加水"), true);

    const QMessageBox::StandardButton button = QMessageBox::question(
        this,
        QStringLiteral("确认03加水"),
        QStringLiteral("水箱1已达到上限位，是否启动03水泵向水箱2加水？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (button == QMessageBox::Yes) {
        startTank2FillInternal();
    }
}

void TreatmentPage::startTank2FillInternal()
{
    if (!ensureWaterPumpConnection() || !ensureLiquidLevelConnection()) {
        return;
    }
    const double target = m_tank2TargetLevelSpin != nullptr ? m_tank2TargetLevelSpin->value() : 0.0;
    if (target <= 0.0 || target > kTank2MaximumTargetLevelMillimeters) {
        QMessageBox::warning(this, QStringLiteral("目标液位异常"), QStringLiteral("请设置 0 - %1 mm 内的水箱2目标液位。")
            .arg(kTank2MaximumTargetLevelMillimeters, 0, 'f', 1));
        return;
    }

    QString errorMessage;
    QByteArray response;
    if (!setWaterPumpFlow(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            kTankTransferFlowMlPerMin,
            &errorMessage,
            &response)) {
        setFluidStatus(QStringLiteral("03 加水启动失败：流速设置失败：%1").arg(errorMessage), false);
        return;
    }
    if (!startWaterPump(
            panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress,
            &errorMessage,
            &response)) {
        setFluidStatus(QStringLiteral("03 加水启动失败：%1").arg(errorMessage), false);
        return;
    }

    setFluidWorkflowState(FluidWorkflowState::FillingTank2);
    if (!m_fluidControlTimer.isActive()) {
        m_fluidControlTimer.start();
    }
    setFluidStatus(QStringLiteral("03 已启动，正在从水箱1向水箱2加水，目标 %1 mm").arg(target, 0, 'f', 1), true);
}

void TreatmentPage::setCycleBalanceMode(CycleBalanceMode mode)
{
    if (mode == m_cycleBalanceMode || !m_waterPumpClient.isOpen()) {
        return;
    }

    QString errorMessage;
    QByteArray response;
    bool ok = true;
    if (mode == CycleBalanceMode::BothPumps) {
        ok = startWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &response)
            && startWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &response);
    } else if (mode == CycleBalanceMode::FillingTank2) {
        ok = stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &response)
            && startWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &response);
    } else if (mode == CycleBalanceMode::DrainingTank2) {
        ok = stopWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kSupplyPumpAddress, &errorMessage, &response)
            && startWaterPump(panthera::adapters::waterpump::WaterPumpModbusClient::kReturnPumpAddress, &errorMessage, &response);
    }

    if (!ok) {
        setFluidStatus(QStringLiteral("循环液位调节失败：%1").arg(errorMessage), false);
        return;
    }
    m_cycleBalanceMode = mode;
}

void TreatmentPage::setFluidStatus(const QString& message, bool ok)
{
    if (m_fluidStatusLabel == nullptr) {
        return;
    }
    m_fluidStatusLabel->setText(QStringLiteral("%1\n阶段：%2").arg(message, fluidWorkflowStateText()));
    m_fluidStatusLabel->setStyleSheet(compactStatusStyle(ok));
    appendLog(QStringLiteral("水路：%1").arg(message));
}

void TreatmentPage::setTemperatureStatus(const QString& message, bool ok)
{
    if (m_temperatureStatusLabel == nullptr) {
        return;
    }
    m_temperatureStatusLabel->setText(message);
    m_temperatureStatusLabel->setStyleSheet(compactStatusStyle(ok));
    appendLog(QStringLiteral("温控：%1").arg(message));
}

void TreatmentPage::refreshFluidUi()
{
    const bool waterPumpConnected = m_waterPumpClient.isOpen();
    const bool liquidLevelConnected = m_liquidLevelClient.isOpen();
    if (m_waterPumpPortCombo != nullptr) {
        m_waterPumpPortCombo->setEnabled(!waterPumpConnected);
    }
    if (m_waterPumpBaudCombo != nullptr) {
        m_waterPumpBaudCombo->setEnabled(!waterPumpConnected);
    }
    if (m_waterPumpConnectionButton != nullptr) {
        m_waterPumpConnectionButton->setText(waterPumpConnected ? QStringLiteral("断开水泵") : QStringLiteral("连接水泵"));
    }
    if (m_liquidLevelPortCombo != nullptr) {
        m_liquidLevelPortCombo->setEnabled(!liquidLevelConnected);
    }
    if (m_liquidLevelBaudCombo != nullptr) {
        m_liquidLevelBaudCombo->setEnabled(!liquidLevelConnected);
    }
    if (m_liquidLevelConnectionButton != nullptr) {
        m_liquidLevelConnectionButton->setText(liquidLevelConnected ? QStringLiteral("断开液位") : QStringLiteral("连接液位"));
    }
    if (m_liquidLevelAddressEdit != nullptr) {
        m_liquidLevelAddressEdit->setText(QStringLiteral("01"));
    }
    if (m_confirmTank2FillButton != nullptr) {
        m_confirmTank2FillButton->setEnabled(m_fluidWorkflowState == FluidWorkflowState::WaitingTank2Confirm);
    }
    if (m_startCycleButton != nullptr) {
        m_startCycleButton->setEnabled(waterPumpConnected && liquidLevelConnected);
    }
    if (m_stopCycleButton != nullptr) {
        m_stopCycleButton->setEnabled(waterPumpConnected);
    }
}

void TreatmentPage::refreshTemperatureUi()
{
    const bool connected = m_temperatureClient.isOpen();
    if (m_temperaturePortCombo != nullptr) {
        m_temperaturePortCombo->setEnabled(!connected);
    }
    if (m_temperatureBaudCombo != nullptr) {
        m_temperatureBaudCombo->setEnabled(!connected);
    }
    if (m_temperatureConnectionButton != nullptr) {
        m_temperatureConnectionButton->setText(connected ? QStringLiteral("断开温控") : QStringLiteral("连接温控"));
    }
    if (m_temperatureStartButton != nullptr) {
        m_temperatureStartButton->setEnabled(connected);
    }
    if (m_temperatureStopButton != nullptr) {
        m_temperatureStopButton->setEnabled(connected);
    }
}

void TreatmentPage::updateTank2LevelDisplay(double millimeters)
{
    m_hasTank2Level = true;
    m_lastTank2LevelMillimeters = millimeters;
    if (m_tank2LevelLabel != nullptr) {
        m_tank2LevelLabel->setText(QStringLiteral("%1 mm").arg(millimeters, 0, 'f', 1));
    }
}

void TreatmentPage::setFluidWorkflowState(FluidWorkflowState state)
{
    m_fluidWorkflowState = state;
    refreshFluidUi();
}

QString TreatmentPage::fluidWorkflowStateText() const
{
    switch (m_fluidWorkflowState) {
    case FluidWorkflowState::Idle:
        return QStringLiteral("待命");
    case FluidWorkflowState::FillingTank1:
        return QStringLiteral("RO注水到水箱1");
    case FluidWorkflowState::WaitingTank2Confirm:
        return QStringLiteral("等待确认03加水");
    case FluidWorkflowState::FillingTank2:
        return QStringLiteral("03加水到水箱2");
    case FluidWorkflowState::ReadyToCycle:
        return QStringLiteral("等待启动循环");
    case FluidWorkflowState::Cycling:
        return QStringLiteral("循环中");
    }
    return QStringLiteral("未知");
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

    const int requestedLayerIndex = m_selectedLayerIndex;
    TherapyPlan executionPlan = m_context->activePlan();
    if (applySerpentinePointExecutionOrder(&executionPlan)) {
        m_context->setActivePlan(executionPlan);
        m_selectedLayerIndex = std::clamp(requestedLayerIndex, 0, std::max(0, static_cast<int>(executionPlan.segments.size()) - 1));
        m_completedPointCount = 0;
        ensureLayerProgressStorage(executionPlan);
        updateLayerPreview();
    }

    {
        ScopedSystemBeepMute muteSystemBeeps;

        QString reason;
        if (!m_safetyKernel->requestTreatmentStart(&reason)) {
            appendLog(QStringLiteral("\u62d2\u7edd\u5f00\u59cb\u6cbb\u7597\uff1a%1").arg(reason));
            return;
        }

        const TherapyPlan& selectedPlan = m_context->activePlan();
        const TherapySegment* selectedSegment = selectedLayerSegment();
        if (selectedSegment == nullptr) {
            appendLog(QStringLiteral("当前治疗层无效，无法定位 7 号电机"));
            m_safetyKernel->stopTreatment();
            return;
        }

        QString motorError;
        if (!prepareSelectedLayerTreatmentMotors(selectedPlan, *selectedSegment, &motorError)) {
            appendLog(QStringLiteral("治疗前电机定位失败：%1").arg(motorError));
            m_safetyKernel->stopTreatment();
            return;
        }
    }
    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }

    const TherapyPlan& plan = m_context->activePlan();
    ensureLayerProgressStorage(plan);
    m_completedPointCount = 0;
    m_deliveredEnergyJ = 0.0;
    if (m_selectedLayerIndex >= 0 && m_selectedLayerIndex < m_layerCompletedPointCounts.size()) {
        m_layerCompletedPointCounts[m_selectedLayerIndex] = 0;
    }
    updateLayerPreview();
    m_context->requestTreatmentLayerVisualization(plan.id, visualizationSliceIndexForSelectedLayer(plan), true);
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
    if (m_context->hasActivePlan()) {
        const TherapyPlan& plan = m_context->activePlan();
        m_context->requestTreatmentLayerVisualization(plan.id, visualizationSliceIndexForSelectedLayer(plan), true);
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

    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }

    m_progressTimer.start();
    if (m_context->hasActivePlan()) {
        const TherapyPlan& plan = m_context->activePlan();
        m_context->requestTreatmentLayerVisualization(plan.id, visualizationSliceIndexForSelectedLayer(plan), true);
    }
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

    const TherapyPlan& plan = m_context->activePlan();
    ensureLayerProgressStorage(plan);
    const TherapySegment* segment = selectedLayerSegment();
    const int maximumPointIndex = segment == nullptr || segment->points.isEmpty()
        ? -1
        : static_cast<int>(segment->points.size()) - 1;
    const int pointIndex = segment == nullptr || segment->points.isEmpty()
        ? -1
        : std::clamp(m_completedPointCount, 0, maximumPointIndex);
    const TherapyPoint* point = pointIndex >= 0 ? &segment->points.at(pointIndex) : nullptr;
    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }

    if (segment != nullptr && pointIndex >= 0) {
        QString motionError;
        if (!moveTreatmentPointMotors(*segment, pointIndex, &motionError)) {
            appendLog(QStringLiteral("靶点定位失败：%1").arg(motionError));
            finalizeTreatment(QStringLiteral("运动失败"));
            return;
        }
    }

    QString outputReason;
    if (m_simulationDevice != nullptr && !m_simulationDevice->setTreatmentOutputEnabled(true, &outputReason)) {
        appendLog(QStringLiteral("\u529f\u7387\u94fe\u8def\u672a\u5c31\u7eea\uff1a%1").arg(outputReason));
        finalizeTreatment(QStringLiteral("功率链路失败"));
        return;
    }

    ++m_completedPointCount;
    if (m_selectedLayerIndex >= 0 && m_selectedLayerIndex < m_layerCompletedPointCounts.size()) {
        m_layerCompletedPointCounts[m_selectedLayerIndex] = std::min(m_completedPointCount, totalPoints);
    }
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
    if (snapshot.state == SafetyState::Red) {
        QString summary;
        stopAllFluidDevices(true, true, &summary);
        setFluidStatus(QStringLiteral("安全联锁触发，水路与加热已停止：%1").arg(summary), false);
    }
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
    QString summary;
    stopAllFluidDevices(true, true, &summary);
    setFluidStatus(QStringLiteral("治疗中止，水路与加热已停止：%1").arg(summary), false);
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
    applySerpentinePointExecutionOrder(&therapyPlan);
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

int TreatmentPage::visualizationSliceIndexForSelectedLayer(const TherapyPlan& plan) const
{
    const int selectedIndex = normalizedLayerIndex(plan);
    if (selectedIndex < 0 || selectedIndex >= plan.segments.size()) {
        return 0;
    }

    const TherapySegment& segment = plan.segments.at(selectedIndex);
    const int sourceSliceIndex = segment.sourceSliceIndex >= 0 ? segment.sourceSliceIndex : segment.orderIndex;
    return sourceSliceIndex >= 0 ? sourceSliceIndex : selectedIndex;
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

bool TreatmentPage::selectedLayerHasSourceImage(const TherapyPlan& plan) const
{
    if (plan.segments.isEmpty()) {
        return false;
    }

    const TherapySegment& segment = plan.segments.at(normalizedLayerIndex(plan));
    const QString sourceImagePath = segment.sourceImagePath.trimmed();
    return !sourceImagePath.isEmpty() && QFileInfo::exists(sourceImagePath);
}

bool TreatmentPage::applySelectedLayerPreviewImage(const TherapyPlan& plan)
{
    if (m_preview == nullptr || plan.segments.isEmpty()) {
        return false;
    }

    const TherapySegment& segment = plan.segments.at(normalizedLayerIndex(plan));
    const QString sourceImagePath = segment.sourceImagePath.trimmed();
    if (!sourceImagePath.isEmpty()) {
        QPixmap sourcePixmap;
        if (sourcePixmap.load(sourceImagePath)) {
            m_preview->setBackgroundImageStretchToFill(false);
            m_preview->setBackgroundImage(sourcePixmap);
            m_preview->setSyntheticImageEnabled(false);
            return true;
        }
    }

    if (m_context != nullptr && m_context->hasLatestTreatmentCameraFrame()) {
        m_preview->setBackgroundImageStretchToFill(false);
        m_preview->setBackgroundImage(QPixmap::fromImage(m_context->latestTreatmentCameraFrame()));
        m_preview->setSyntheticImageEnabled(false);
    }
    return false;
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
    const QString axis7Text = segment.axis7PositionSteps >= 0
        ? QStringLiteral("\u30007\u53f7%1\u6b65").arg(segment.axis7PositionSteps)
        : QStringLiteral("\u30007\u53f7\u672a\u8bb0\u5f55");

    const QSignalBlocker blocker(m_layerSlider);
    m_layerSlider->setRange(0, layers - 1);
    m_layerSlider->setSingleStep(1);
    m_layerSlider->setPageStep(1);
    m_layerSlider->setTickInterval(1);
    m_layerSlider->setTickPosition(layers > 1 ? QSlider::TicksBelow : QSlider::NoTicks);
    m_layerSlider->setValue(m_selectedLayerIndex);
    m_layerSlider->setEnabled(layers > 1 && !m_progressTimer.isActive() && m_safetyKernel->mode() != SystemMode::Paused);
    m_layerLabel->setText(
        QStringLiteral("\u6cbb\u7597\u5c42\uff1a%1 / %2\u3000%3\u3000%4\u4e2a\u9776\u70b9%5")
            .arg(m_selectedLayerIndex + 1)
            .arg(layers)
            .arg(layerText)
            .arg(static_cast<int>(segment.points.size()))
            .arg(axis7Text));
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
    applySelectedLayerPreviewImage(plan);
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
    const QString activePlanId = m_context->hasActivePlan() ? m_context->activePlan().id : QString();
    const int activeLayerIndex = m_context->hasActivePlan() ? visualizationSliceIndexForSelectedLayer(m_context->activePlan()) : m_selectedLayerIndex;
    m_progressTimer.stop();
    if (m_simulationDevice != nullptr) {
        m_simulationDevice->setTreatmentOutputEnabled(false);
    }
    m_hasTreatmentSwingCenter = false;
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
    m_context->requestTreatmentLayerVisualization(activePlanId, activeLayerIndex, false);
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
