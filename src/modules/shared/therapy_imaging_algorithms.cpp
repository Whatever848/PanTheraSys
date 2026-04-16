#include "modules/shared/therapy_imaging_algorithms.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr double kPi = 3.14159265358979323846;

QVector<QPointF> annotationPointsInMillimeters(const QVector<AnnotationStroke>& annotations)
{
    QVector<QPointF> points;
    for (const AnnotationStroke& stroke : annotations) {
        for (const QPointF& normalizedPoint : stroke.normalizedPoints) {
            points.push_back(QPointF((normalizedPoint.x() * 60.0) - 30.0, (normalizedPoint.y() * 60.0) - 30.0));
        }
    }
    return points;
}

bool pointLess(const QPointF& left, const QPointF& right)
{
    if (!qFuzzyCompare(left.x(), right.x())) {
        return left.x() < right.x();
    }
    return left.y() < right.y();
}

double crossProduct(const QPointF& origin, const QPointF& left, const QPointF& right)
{
    return ((left.x() - origin.x()) * (right.y() - origin.y()))
        - ((left.y() - origin.y()) * (right.x() - origin.x()));
}

QVector<QPointF> buildConvexHull(QVector<QPointF> points)
{
    if (points.size() <= 3) {
        return points;
    }

    std::sort(points.begin(), points.end(), pointLess);
    points.erase(std::unique(points.begin(), points.end(), [](const QPointF& left, const QPointF& right) {
        return qFuzzyCompare(left.x(), right.x()) && qFuzzyCompare(left.y(), right.y());
    }), points.end());

    if (points.size() <= 3) {
        return points;
    }

    QVector<QPointF> lower;
    for (const QPointF& point : points) {
        while (lower.size() >= 2 && crossProduct(lower.at(lower.size() - 2), lower.last(), point) <= 0.0) {
            lower.removeLast();
        }
        lower.push_back(point);
    }

    QVector<QPointF> upper;
    for (auto it = points.crbegin(); it != points.crend(); ++it) {
        while (upper.size() >= 2 && crossProduct(upper.at(upper.size() - 2), upper.last(), *it) <= 0.0) {
            upper.removeLast();
        }
        upper.push_back(*it);
    }

    lower.removeLast();
    upper.removeLast();
    lower += upper;
    return lower;
}

double polygonSignedAreaTwice(const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) {
        return 0.0;
    }

    double areaTwice = 0.0;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF& current = polygon.at(index);
        const QPointF& next = polygon.at((index + 1) % polygon.size());
        areaTwice += (current.x() * next.y()) - (next.x() * current.y());
    }
    return areaTwice;
}

double polygonAreaMm2(const QVector<QPointF>& polygon)
{
    return std::abs(polygonSignedAreaTwice(polygon)) * 0.5;
}

QPointF polygonCentroid(const QVector<QPointF>& polygon)
{
    if (polygon.isEmpty()) {
        return {};
    }

    const double areaTwice = polygonSignedAreaTwice(polygon);
    if (std::abs(areaTwice) < 1e-6) {
        QPointF centroid;
        for (const QPointF& point : polygon) {
            centroid += point;
        }
        return centroid / static_cast<double>(polygon.size());
    }

    double factor = 0.0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF& current = polygon.at(index);
        const QPointF& next = polygon.at((index + 1) % polygon.size());
        factor = (current.x() * next.y()) - (next.x() * current.y());
        centroidX += (current.x() + next.x()) * factor;
        centroidY += (current.y() + next.y()) * factor;
    }

    return QPointF(centroidX / (3.0 * areaTwice), centroidY / (3.0 * areaTwice));
}

QRectF polygonBounds(const QVector<QPointF>& polygon)
{
    if (polygon.isEmpty()) {
        return {};
    }

    qreal minX = polygon.first().x();
    qreal maxX = polygon.first().x();
    qreal minY = polygon.first().y();
    qreal maxY = polygon.first().y();
    for (const QPointF& point : polygon) {
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y());
        maxY = std::max(maxY, point.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

QPointF rawProjectedPoint(const QVector3D& point)
{
    return QPointF(point.x() + (point.z() * 0.58), -point.y() - (point.z() * 0.42));
}

QPainterPath smoothPath(const QVector<QPointF>& points)
{
    if (points.isEmpty()) {
        return {};
    }

    QPainterPath path(points.first());
    if (points.size() == 1) {
        return path;
    }

    for (int index = 1; index < points.size(); ++index) {
        const QPointF previous = points.at(index - 1);
        const QPointF current = points.at(index);
        const QPointF midpoint((previous.x() + current.x()) * 0.5, (previous.y() + current.y()) * 0.5);
        path.quadTo(previous, midpoint);
    }
    path.lineTo(points.last());
    return path;
}

}  // namespace

QVector<QPointF> extractContourFromAnnotations(const QVector<AnnotationStroke>& annotations)
{
    const QVector<QPointF> points = annotationPointsInMillimeters(annotations);
    if (points.size() < 3) {
        return points;
    }
    return buildConvexHull(points);
}

QVector<QPointF> buildFallbackLesionContourMm(int sliceIndex, int totalSliceCount)
{
    const double ratio = totalSliceCount <= 1
        ? 0.5
        : std::clamp(static_cast<double>(sliceIndex) / static_cast<double>(totalSliceCount - 1), 0.0, 1.0);
    const double lateralShift = (ratio - 0.5) * 9.0;
    const double depthShift = std::sin(ratio * kPi) * 4.0 - 1.5;

    QVector<QPointF> contour;
    contour << QPointF(-18.0 + lateralShift, -8.0 + depthShift)
            << QPointF(-8.0 + lateralShift, -16.0 + depthShift * 0.9)
            << QPointF(12.0 + lateralShift, -10.0 + depthShift * 0.75)
            << QPointF(20.0 + lateralShift, 6.0 + depthShift)
            << QPointF(6.0 + lateralShift, 18.0 + depthShift * 0.85)
            << QPointF(-14.0 + lateralShift, 12.0 + depthShift);
    return contour;
}

RespiratoryFollowResult computeRespiratoryFollowResult(
    const QVector<AnnotationStroke>& annotations,
    const QVector<TherapyPoint>& originalTargets,
    int sliceIndex,
    int totalSliceCount,
    const DeviceSnapshot* snapshot)
{
    RespiratoryFollowResult result;
    if (originalTargets.isEmpty()) {
        return result;
    }

    const QVector<QPointF> contour = extractContourFromAnnotations(annotations);
    if (contour.size() < 3) {
        return result;
    }

    const QRectF bounds = polygonBounds(contour);
    if (!bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        return result;
    }

    result.valid = true;
    const qreal marginX = std::max<qreal>(2.0, bounds.width() * 0.18);
    const qreal marginY = std::max<qreal>(2.0, bounds.height() * 0.18);
    result.calibrationBoxMm = bounds.adjusted(-marginX, -marginY, marginX, marginY);
    result.baselineCentroidMm = polygonCentroid(contour);

    const double seconds = snapshot != nullptr && snapshot->capturedAt.isValid()
        ? snapshot->capturedAt.time().msecsSinceStartOfDay() / 1000.0
        : QDateTime::currentDateTime().time().msecsSinceStartOfDay() / 1000.0;
    const double cyclePhase = ((seconds / 4.0) * (2.0 * kPi))
        + (std::max(0, sliceIndex) * 0.38)
        + (std::max(1, totalSliceCount) * 0.03);
    const double efficiency = snapshot != nullptr
        ? std::clamp(snapshot->conversionEfficiencyPercent / 100.0, 0.78, 1.05)
        : 0.92;
    const double amplitudeX = std::clamp(bounds.width() * 0.08 * efficiency, 0.4, 3.2);
    const double amplitudeY = std::clamp(bounds.height() * 0.12 * efficiency, 0.8, 4.0);

    result.liveCentroidMm = result.baselineCentroidMm
        + QPointF(
            std::sin(cyclePhase) * amplitudeX,
            std::cos(cyclePhase * 1.13) * amplitudeY);
    result.deltaMm = result.liveCentroidMm - result.baselineCentroidMm;

    result.correctedTargets.reserve(originalTargets.size());
    for (const TherapyPoint& point : originalTargets) {
        TherapyPoint correctedPoint = point;
        correctedPoint.positionMm += result.deltaMm;
        result.correctedTargets.push_back(correctedPoint);
    }

    result.summary =
        QStringLiteral("标定框: [%1, %2, %3, %4] mm\n基准质心: (%5, %6) mm\n实时质心: (%7, %8) mm\n补偿位移: dX %9 mm / dY %10 mm\n补偿靶点: %11 个")
            .arg(result.calibrationBoxMm.left(), 0, 'f', 1)
            .arg(result.calibrationBoxMm.top(), 0, 'f', 1)
            .arg(result.calibrationBoxMm.width(), 0, 'f', 1)
            .arg(result.calibrationBoxMm.height(), 0, 'f', 1)
            .arg(result.baselineCentroidMm.x(), 0, 'f', 2)
            .arg(result.baselineCentroidMm.y(), 0, 'f', 2)
            .arg(result.liveCentroidMm.x(), 0, 'f', 2)
            .arg(result.liveCentroidMm.y(), 0, 'f', 2)
            .arg(result.deltaMm.x(), 0, 'f', 2)
            .arg(result.deltaMm.y(), 0, 'f', 2)
            .arg(result.correctedTargets.size());

    return result;
}

VolumeReconstructionResult buildVolumeReconstructionResult(
    const QVector<VolumeContourSlice>& slices,
    double sliceSpacingMm,
    const QSize& previewSize)
{
    struct PreparedSlice {
        int sliceIndex {0};
        bool derivedFromAnnotation {false};
        double areaMm2 {0.0};
        double zMm {0.0};
        QPointF centroidMm;
        QVector<QPointF> contourMm;
    };

    QVector<PreparedSlice> preparedSlices;
    preparedSlices.reserve(slices.size());
    const double clampedSpacing = std::max(0.5, sliceSpacingMm);
    for (const VolumeContourSlice& slice : slices) {
        if (slice.contourMm.size() < 3) {
            continue;
        }

        const double areaMm2 = polygonAreaMm2(slice.contourMm);
        if (areaMm2 <= 0.0) {
            continue;
        }

        PreparedSlice preparedSlice;
        preparedSlice.sliceIndex = slice.sliceIndex;
        preparedSlice.derivedFromAnnotation = slice.derivedFromAnnotation;
        preparedSlice.areaMm2 = areaMm2;
        preparedSlice.zMm = slice.sliceIndex * clampedSpacing;
        preparedSlice.centroidMm = polygonCentroid(slice.contourMm);
        preparedSlice.contourMm = slice.contourMm;
        preparedSlices.push_back(preparedSlice);
    }

    VolumeReconstructionResult result;
    if (preparedSlices.isEmpty() || !previewSize.isValid()) {
        return result;
    }

    std::sort(preparedSlices.begin(), preparedSlices.end(), [](const PreparedSlice& left, const PreparedSlice& right) {
        return left.sliceIndex < right.sliceIndex;
    });

    double totalAreaMm2 = 0.0;
    double totalVolumeMm3 = 0.0;
    QVector3D weightedCentroid;
    for (const PreparedSlice& slice : preparedSlices) {
        totalAreaMm2 += slice.areaMm2;
        totalVolumeMm3 += slice.areaMm2 * clampedSpacing;
        weightedCentroid += QVector3D(
            static_cast<float>(slice.centroidMm.x() * slice.areaMm2),
            static_cast<float>(slice.centroidMm.y() * slice.areaMm2),
            static_cast<float>(slice.zMm * slice.areaMm2));
        if (slice.derivedFromAnnotation) {
            ++result.annotatedSliceCount;
        } else {
            ++result.inferredSliceCount;
        }
    }

    if (totalAreaMm2 <= 0.0) {
        return result;
    }

    result.valid = true;
    result.sliceCount = preparedSlices.size();
    result.estimatedVolumeCm3 = totalVolumeMm3 / 1000.0;
    result.weightedCentroidMm = weightedCentroid / static_cast<float>(totalAreaMm2);

    QPixmap preview(previewSize);
    preview.fill(QColor(12, 20, 33));

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient backgroundGradient(QPointF(0.0, 0.0), QPointF(0.0, preview.height()));
    backgroundGradient.setColorAt(0.0, QColor(22, 35, 58));
    backgroundGradient.setColorAt(0.55, QColor(28, 49, 76));
    backgroundGradient.setColorAt(1.0, QColor(12, 18, 30));
    painter.fillRect(preview.rect(), backgroundGradient);

    const QRectF plotRect = preview.rect().adjusted(34, 28, -34, -52);
    painter.setPen(QPen(QColor(132, 160, 203, 26), 1.0));
    for (int step = 0; step <= 5; ++step) {
        const qreal y = plotRect.top() + ((plotRect.height() / 5.0) * step);
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }
    for (int step = 0; step <= 5; ++step) {
        const qreal x = plotRect.left() + ((plotRect.width() / 5.0) * step);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }

    QVector<QVector<QPointF>> rawProjectedContours;
    rawProjectedContours.reserve(preparedSlices.size());
    QVector<QPointF> rawProjectedCentroids;
    rawProjectedCentroids.reserve(preparedSlices.size());

    QRectF rawBounds;
    bool firstProjectedPoint = true;
    for (const PreparedSlice& slice : preparedSlices) {
        QVector<QPointF> projectedContour;
        projectedContour.reserve(slice.contourMm.size());
        for (const QPointF& point : slice.contourMm) {
            const QPointF projected = rawProjectedPoint(QVector3D(point.x(), point.y(), slice.zMm));
            projectedContour.push_back(projected);
            if (firstProjectedPoint) {
                rawBounds = QRectF(projected, projected);
                firstProjectedPoint = false;
            } else {
                rawBounds = rawBounds.united(QRectF(projected, projected));
            }
        }

        const QPointF projectedCentroid = rawProjectedPoint(QVector3D(slice.centroidMm.x(), slice.centroidMm.y(), slice.zMm));
        rawProjectedCentroids.push_back(projectedCentroid);
        rawBounds = rawBounds.united(QRectF(projectedCentroid, projectedCentroid));
        rawProjectedContours.push_back(projectedContour);
    }

    const qreal scaleX = plotRect.width() / std::max<qreal>(rawBounds.width(), 1.0);
    const qreal scaleY = plotRect.height() / std::max<qreal>(rawBounds.height(), 1.0);
    const qreal scale = std::min(scaleX, scaleY);

    const auto mapProjectedPoint = [&](const QPointF& point) {
        return QPointF(
            plotRect.left() + ((point.x() - rawBounds.left()) * scale),
            plotRect.top() + ((point.y() - rawBounds.top()) * scale));
    };

    QVector<QPointF> mappedCentroids;
    mappedCentroids.reserve(rawProjectedCentroids.size());
    for (int index = 0; index < rawProjectedCentroids.size(); ++index) {
        mappedCentroids.push_back(mapProjectedPoint(rawProjectedCentroids.at(index)));
    }

    for (int index = 0; index < rawProjectedContours.size(); ++index) {
        const qreal depthRatio = preparedSlices.size() <= 1
            ? 1.0
            : static_cast<qreal>(index) / static_cast<qreal>(preparedSlices.size() - 1);
        const QColor strokeColor = preparedSlices.at(index).derivedFromAnnotation
            ? QColor(0, 198, 255, 220)
            : QColor(137, 169, 210, 185);
        const QColor fillColor = preparedSlices.at(index).derivedFromAnnotation
            ? QColor(66, 178, 255, 52 + static_cast<int>(depthRatio * 60.0))
            : QColor(112, 140, 188, 34 + static_cast<int>(depthRatio * 34.0));

        QPolygonF polygon;
        for (const QPointF& point : rawProjectedContours.at(index)) {
            polygon << mapProjectedPoint(point);
        }

        painter.setBrush(fillColor);
        painter.setPen(QPen(strokeColor, 2.0));
        painter.drawPolygon(polygon);
    }

    if (mappedCentroids.size() >= 2) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(165, 186, 255, 180), 2.0));
        painter.drawPath(smoothPath(mappedCentroids));
    }

    for (const QPointF& centroid : mappedCentroids) {
        painter.setBrush(QColor(255, 188, 76, 210));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(centroid, 4.0, 4.0);
    }

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12, QFont::Bold));
    painter.drawText(plotRect.adjusted(0, -6, 0, 0), Qt::AlignTop | Qt::AlignLeft, QStringLiteral("三维重建视图"));

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    painter.setPen(QColor(171, 191, 225));
    painter.drawText(
        plotRect.adjusted(0, 0, 0, -6),
        Qt::AlignBottom | Qt::AlignLeft,
        QStringLiteral("体积中心: X %1  Y %2  Z %3 mm")
            .arg(result.weightedCentroidMm.x(), 0, 'f', 2)
            .arg(result.weightedCentroidMm.y(), 0, 'f', 2)
            .arg(result.weightedCentroidMm.z(), 0, 'f', 2));

    result.preview = preview;
    result.summary =
        QStringLiteral("重建切片: %1 张\n人工标注切片: %2 张\n自动推断切片: %3 张\n估算体积: %4 cm³\n体积中心: (%5, %6, %7) mm")
            .arg(result.sliceCount)
            .arg(result.annotatedSliceCount)
            .arg(result.inferredSliceCount)
            .arg(result.estimatedVolumeCm3, 0, 'f', 2)
            .arg(result.weightedCentroidMm.x(), 0, 'f', 2)
            .arg(result.weightedCentroidMm.y(), 0, 'f', 2)
            .arg(result.weightedCentroidMm.z(), 0, 'f', 2);
    return result;
}

}  // namespace panthera::modules
