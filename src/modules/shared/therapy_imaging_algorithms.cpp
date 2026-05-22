#include "modules/shared/therapy_imaging_algorithms.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDateTime>
#include <QFont>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

namespace panthera::modules {

using namespace panthera::core;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLogicalCanvasSpanMm = 60.0;
constexpr double kLogicalCanvasHalfSpanMm = kLogicalCanvasSpanMm * 0.5;
constexpr double kMinimumAnnotationPointDistanceNormalized = 0.0015;
constexpr double kSmoothingPreviousWeight = 0.18;
constexpr double kSmoothingCurrentWeight = 0.64;
constexpr double kSmoothingNextWeight = 0.18;

QPointF normalizedPointToMillimeters(const QPointF& normalizedPoint)
{
    return QPointF(
        (normalizedPoint.x() * kLogicalCanvasSpanMm) - kLogicalCanvasHalfSpanMm,
        (normalizedPoint.y() * kLogicalCanvasSpanMm) - kLogicalCanvasHalfSpanMm);
}

bool pointsNearlyEqual(const QPointF& left, const QPointF& right, double epsilon = 1e-4)
{
    return std::abs(left.x() - right.x()) <= epsilon && std::abs(left.y() - right.y()) <= epsilon;
}

QVector<QPointF> deduplicateSequentialPoints(const QVector<QPointF>& points, double minimumDistance)
{
    QVector<QPointF> deduplicated;
    deduplicated.reserve(points.size());
    for (const QPointF& point : points) {
        if (!deduplicated.isEmpty() && QLineF(deduplicated.constLast(), point).length() < minimumDistance) {
            continue;
        }
        deduplicated.push_back(point);
    }
    return deduplicated;
}

AnnotationStroke normalizeClosedStroke(const AnnotationStroke& stroke)
{
    AnnotationStroke normalized = stroke;
    QVector<QPointF> uniquePoints = deduplicateSequentialPoints(
        normalized.normalizedPoints,
        kMinimumAnnotationPointDistanceNormalized);

    if (uniquePoints.size() >= 2 && pointsNearlyEqual(uniquePoints.first(), uniquePoints.last())) {
        uniquePoints.removeLast();
    }

    if (uniquePoints.size() >= 6) {
        QVector<QPointF> smoothedPoints;
        smoothedPoints.reserve(uniquePoints.size());
        for (int index = 0; index < uniquePoints.size(); ++index) {
            const QPointF& previous = uniquePoints.at((index - 1 + uniquePoints.size()) % uniquePoints.size());
            const QPointF& current = uniquePoints.at(index);
            const QPointF& next = uniquePoints.at((index + 1) % uniquePoints.size());
            smoothedPoints.push_back(QPointF(
                (previous.x() * kSmoothingPreviousWeight)
                    + (current.x() * kSmoothingCurrentWeight)
                    + (next.x() * kSmoothingNextWeight),
                (previous.y() * kSmoothingPreviousWeight)
                    + (current.y() * kSmoothingCurrentWeight)
                    + (next.y() * kSmoothingNextWeight)));
        }

        smoothedPoints = deduplicateSequentialPoints(
            smoothedPoints,
            kMinimumAnnotationPointDistanceNormalized * 0.55);
        if (smoothedPoints.size() >= 3) {
            uniquePoints = smoothedPoints;
        }
    }

    normalized.normalizedPoints = uniquePoints;
    if (normalized.normalizedPoints.size() >= 3) {
        normalized.normalizedPoints.push_back(normalized.normalizedPoints.first());
    }

    return normalized;
}

QVector<QPointF> annotationPointsInMillimeters(const AnnotationStroke& annotation)
{
    QVector<QPointF> points;
    points.reserve(annotation.normalizedPoints.size());
    for (const QPointF& normalizedPoint : annotation.normalizedPoints) {
        points.push_back(normalizedPointToMillimeters(normalizedPoint));
    }
    return points;
}

QVector<QPointF> annotationPointsInMillimeters(const QVector<AnnotationStroke>& annotations)
{
    QVector<QPointF> points;
    for (const AnnotationStroke& stroke : annotations) {
        points += annotationPointsInMillimeters(stroke);
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

double pointToSegmentDistanceMm(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const double segmentLengthSquared =
        std::pow(end.x() - start.x(), 2.0) + std::pow(end.y() - start.y(), 2.0);
    if (segmentLengthSquared <= 1e-9) {
        return QLineF(point, start).length();
    }

    const QPointF offset = point - start;
    const QPointF direction = end - start;
    const double projection = std::clamp(
        ((offset.x() * direction.x()) + (offset.y() * direction.y())) / segmentLengthSquared,
        0.0,
        1.0);
    const QPointF closestPoint(start.x() + (direction.x() * projection), start.y() + (direction.y() * projection));
    return QLineF(point, closestPoint).length();
}

int contourVertexCount(const QVector<QPointF>& contour)
{
    if (contour.isEmpty()) {
        return 0;
    }

    const int rawCount = contour.size();
    if (rawCount >= 2 && pointsNearlyEqual(contour.first(), contour.last())) {
        return rawCount - 1;
    }
    return rawCount;
}

QVector<qreal> distributedPositions(qreal minimum, qreal maximum, double spacingMm)
{
    QVector<qreal> positions;
    if (maximum < minimum) {
        return positions;
    }

    const qreal span = std::max<qreal>(0.0, maximum - minimum);
    const double clampedSpacing = std::max(0.5, spacingMm);
    const int count = std::max(1, static_cast<int>(std::floor(span / clampedSpacing)) + 1);
    const qreal occupiedSpan = static_cast<qreal>(clampedSpacing * std::max(0, count - 1));
    const qreal offset = (span - occupiedSpan) * 0.5;

    positions.reserve(count);
    for (int index = 0; index < count; ++index) {
        positions.push_back(minimum + offset + static_cast<qreal>(index * clampedSpacing));
    }
    return positions;
}

struct HorizontalSpan {
    qreal startX {0.0};
    qreal endX {0.0};
};

QPainterPath closedStrokePathMm(const AnnotationStroke& annotation)
{
    const QVector<QPointF> contour = annotationPointsInMillimeters(annotation);
    if (contourVertexCount(contour) < 3) {
        return {};
    }

    QPainterPath strokePath(contour.first());
    for (int index = 1; index < contourVertexCount(contour); ++index) {
        strokePath.lineTo(contour.at(index));
    }
    strokePath.closeSubpath();
    strokePath.setFillRule(Qt::WindingFill);
    return strokePath.simplified();
}

QPainterPath annotationRegionPathMm(const QVector<AnnotationStroke>& annotations)
{
    QPainterPath mergedPath;
    mergedPath.setFillRule(Qt::WindingFill);

    const QVector<AnnotationStroke> normalizedAnnotations = normalizeClosedAnnotations(annotations);
    for (const AnnotationStroke& stroke : normalizedAnnotations) {
        const QPainterPath strokePath = closedStrokePathMm(stroke);
        if (strokePath.isEmpty()) {
            continue;
        }
        mergedPath = mergedPath.isEmpty() ? strokePath : mergedPath.united(strokePath);
    }

    return mergedPath.simplified();
}

QPainterPath contourRegionPathMm(const QVector<QPointF>& contour)
{
    if (contourVertexCount(contour) < 3) {
        return {};
    }

    QPainterPath contourPath(contour.first());
    for (int index = 1; index < contourVertexCount(contour); ++index) {
        contourPath.lineTo(contour.at(index));
    }
    contourPath.closeSubpath();
    contourPath.setFillRule(Qt::WindingFill);
    return contourPath.simplified();
}

QVector<QVector<QPointF>> fillContoursFromPathMm(const QPainterPath& path)
{
    QVector<QVector<QPointF>> contours;
    const QList<QPolygonF> polygons = path.toFillPolygons();
    contours.reserve(polygons.size());
    for (const QPolygonF& polygon : polygons) {
        QVector<QPointF> contour;
        contour.reserve(polygon.size() + 1);
        for (const QPointF& point : polygon) {
            contour.push_back(point);
        }
        if (contourVertexCount(contour) >= 3 && !pointsNearlyEqual(contour.first(), contour.last())) {
            contour.push_back(contour.first());
        }
        if (contourVertexCount(contour) >= 3) {
            contours.push_back(contour);
        }
    }
    return contours;
}

double contourCollectionAreaMm2(const QVector<QVector<QPointF>>& contours)
{
    double totalAreaMm2 = 0.0;
    for (const QVector<QPointF>& contour : contours) {
        totalAreaMm2 += polygonAreaMm2(contour);
    }
    return totalAreaMm2;
}

QRectF contourCollectionBoundsMm(const QVector<QVector<QPointF>>& contours)
{
    QRectF bounds;
    bool firstContour = true;
    for (const QVector<QPointF>& contour : contours) {
        const QRectF contourBounds = polygonBounds(contour);
        if (!contourBounds.isValid()) {
            continue;
        }
        if (firstContour) {
            bounds = contourBounds;
            firstContour = false;
        } else {
            bounds = bounds.united(contourBounds);
        }
    }
    return bounds;
}

bool pathContainsPointMm(const QPainterPath& path, const QPointF& pointMm, double toleranceMm)
{
    if (path.contains(pointMm)) {
        return true;
    }
    if (toleranceMm <= 0.0) {
        return false;
    }

    const QVector<QVector<QPointF>> contours = fillContoursFromPathMm(path);
    for (const QVector<QPointF>& contour : contours) {
        if (contourContainsPointMm(contour, pointMm, toleranceMm)) {
            return true;
        }
    }
    return false;
}

QVector<HorizontalSpan> mergeHorizontalSpans(QVector<HorizontalSpan> spans, qreal mergeToleranceMm)
{
    if (spans.isEmpty()) {
        return spans;
    }

    std::sort(spans.begin(), spans.end(), [](const HorizontalSpan& left, const HorizontalSpan& right) {
        return left.startX < right.startX;
    });

    QVector<HorizontalSpan> mergedSpans;
    mergedSpans.push_back(spans.first());
    for (int index = 1; index < spans.size(); ++index) {
        HorizontalSpan& current = mergedSpans.last();
        const HorizontalSpan& candidate = spans.at(index);
        if (candidate.startX <= current.endX + mergeToleranceMm) {
            current.endX = std::max(current.endX, candidate.endX);
            continue;
        }
        mergedSpans.push_back(candidate);
    }
    return mergedSpans;
}

QVector<HorizontalSpan> horizontalSpansForRow(
    const QVector<QVector<QPointF>>& contours,
    qreal y,
    qreal minimumSpanMm)
{
    QVector<HorizontalSpan> spans;
    for (const QVector<QPointF>& contour : contours) {
        const int vertexCount = contourVertexCount(contour);
        if (vertexCount < 3) {
            continue;
        }

        QVector<qreal> intersections;
        intersections.reserve(vertexCount);
        for (int index = 0; index < vertexCount; ++index) {
            const QPointF& start = contour.at(index);
            const QPointF& end = contour.at((index + 1) % vertexCount);
            if (std::abs(start.y() - end.y()) <= 1e-6) {
                continue;
            }

            const qreal minimumY = std::min(start.y(), end.y());
            const qreal maximumY = std::max(start.y(), end.y());
            if (y < minimumY || y >= maximumY) {
                continue;
            }

            const qreal ratio = (y - start.y()) / (end.y() - start.y());
            intersections.push_back(start.x() + ((end.x() - start.x()) * ratio));
        }

        std::sort(intersections.begin(), intersections.end());
        for (int index = 0; index + 1 < intersections.size(); index += 2) {
            const qreal startX = intersections.at(index);
            const qreal endX = intersections.at(index + 1);
            if ((endX - startX) >= minimumSpanMm) {
                spans.push_back(HorizontalSpan {startX, endX});
            }
        }
    }

    return mergeHorizontalSpans(spans, 0.3);
}

QVector<qreal> samplePositionsForContinuousLineSpan(qreal startX, qreal endX, double spacingMm)
{
    if (endX <= startX) {
        return {};
    }

    const qreal span = endX - startX;
    const double targetStepMm = std::max(0.8, spacingMm * 1.15);
    const int segmentCount = std::max(1, static_cast<int>(std::round(span / targetStepMm)));
    QVector<qreal> positions;
    positions.reserve(segmentCount + 1);
    for (int index = 0; index <= segmentCount; ++index) {
        const qreal ratio = static_cast<qreal>(index) / static_cast<qreal>(segmentCount);
        positions.push_back(startX + span * ratio);
    }
    return positions;
}

QVector<qreal> pointCircleCenterPositionsForSpan(
    qreal startX,
    qreal endX,
    qreal globalStartX,
    double spacingMm,
    bool offsetRow)
{
    QVector<qreal> positions;
    if (endX < startX) {
        return positions;
    }

    const double clampedSpacing = std::max(0.5, spacingMm);
    qreal firstX = globalStartX + (offsetRow ? clampedSpacing * 0.5 : 0.0);
    if (firstX < startX) {
        const double stepCount = std::ceil((startX - firstX) / clampedSpacing);
        firstX += static_cast<qreal>(stepCount * clampedSpacing);
    }

    for (qreal x = firstX; x <= endX + 1e-6; x += clampedSpacing) {
        positions.push_back(x);
    }

    if (positions.isEmpty() && (endX - startX) >= clampedSpacing * 0.35) {
        positions.push_back((startX + endX) * 0.5);
    }
    return positions;
}

bool circleFitsInsidePath(const QPainterPath& path, const QPointF& center, double radiusMm)
{
    if (!pathContainsPointMm(path, center, 0.05)) {
        return false;
    }

    const double checkedRadius = std::max(0.1, radiusMm);
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        const double angle = (static_cast<double>(sampleIndex) / 8.0) * 2.0 * kPi;
        const QPointF sample(
            center.x() + std::cos(angle) * checkedRadius,
            center.y() + std::sin(angle) * checkedRadius);
        if (!pathContainsPointMm(path, sample, 0.05)) {
            return false;
        }
    }
    return true;
}

QPointF fallbackPointInsidePath(const QPainterPath& path, const QRectF& bounds, double spacingMm)
{
    const QPointF preferredPoint = bounds.center();
    if (pathContainsPointMm(path, preferredPoint, std::max(0.2, spacingMm * 0.15))) {
        return preferredPoint;
    }

    QPointF bestPoint = preferredPoint;
    double bestDistance = std::numeric_limits<double>::max();
    const QVector<qreal> rows = distributedPositions(bounds.top(), bounds.bottom(), std::max(0.5, spacingMm * 0.5));
    const QVector<qreal> columns = distributedPositions(bounds.left(), bounds.right(), std::max(0.5, spacingMm * 0.5));
    for (const qreal y : rows) {
        for (const qreal x : columns) {
            const QPointF candidate(x, y);
            if (!pathContainsPointMm(path, candidate, std::max(0.2, spacingMm * 0.15))) {
                continue;
            }

            const double distance = QLineF(candidate, preferredPoint).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = candidate;
            }
        }
    }

    return bestPoint;
}

void appendContourBoundaryPointTargets(
    QVector<TherapyPoint>* targets,
    const QVector<QVector<QPointF>>& contours,
    double spacingMm,
    double dwellSeconds,
    double powerWatts)
{
    if (targets == nullptr || contours.isEmpty()) {
        return;
    }

    const double clampedSpacing = std::max(0.5, spacingMm);
    for (const QVector<QPointF>& contour : contours) {
        const int vertexCount = contourVertexCount(contour);
        if (vertexCount < 3) {
            continue;
        }

        QVector<double> segmentLengths;
        segmentLengths.reserve(vertexCount);
        double perimeterMm = 0.0;
        for (int index = 0; index < vertexCount; ++index) {
            const QPointF& start = contour.at(index);
            const QPointF& end = contour.at((index + 1) % vertexCount);
            const double edgeLengthMm = QLineF(start, end).length();
            segmentLengths.push_back(edgeLengthMm);
            perimeterMm += edgeLengthMm;
        }
        if (perimeterMm <= 1e-6) {
            continue;
        }

        const auto pointAtBoundaryDistance = [&contour, &segmentLengths, vertexCount](double targetDistanceMm) {
            double traversedMm = 0.0;
            for (int index = 0; index < vertexCount; ++index) {
                const double edgeLengthMm = segmentLengths.at(index);
                if (edgeLengthMm <= 1e-6) {
                    continue;
                }
                if (targetDistanceMm <= traversedMm + edgeLengthMm || index == vertexCount - 1) {
                    const QPointF& start = contour.at(index);
                    const QPointF& end = contour.at((index + 1) % vertexCount);
                    const double ratio = std::clamp((targetDistanceMm - traversedMm) / edgeLengthMm, 0.0, 1.0);
                    return QPointF(
                        start.x() + ((end.x() - start.x()) * ratio),
                        start.y() + ((end.y() - start.y()) * ratio));
                }
                traversedMm += edgeLengthMm;
            }

            return contour.first();
        };

        // Sample the whole contour by arc length. Sampling each small hand-drawn edge would
        // put a target on nearly every vertex and make the boundary look overpacked.
        const double boundarySpacingMm = std::max(0.5, clampedSpacing * 1.3);
        for (double distanceMm = 0.0; distanceMm < perimeterMm - 1e-6; distanceMm += boundarySpacingMm) {
            TherapyPoint point;
            point.positionMm = pointAtBoundaryDistance(distanceMm);
            point.dwellSeconds = dwellSeconds;
            point.powerWatts = powerWatts;
            targets->push_back(point);
        }
    }
}

QVector<TherapyPoint> generateTherapyTargetsInRegionPath(
    const QPainterPath& regionPath,
    TreatmentPattern pattern,
    double spacingMm,
    double dwellSeconds,
    double powerWatts)
{
    QVector<TherapyPoint> targets;
    const QVector<QVector<QPointF>> contours = fillContoursFromPathMm(regionPath);
    const QRectF bounds = regionPath.boundingRect();
    if (contours.isEmpty() || !bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        return targets;
    }

    const double clampedSpacing = std::max(0.5, spacingMm);
    if (pattern == TreatmentPattern::Line) {
        const double rowSpacingMm = clampedSpacing;
        const double minimumLineSpanMm = std::max(1.6, clampedSpacing * 1.4);
        const double endpointInsetMm = std::min(0.8, clampedSpacing * 0.22);
        const QVector<qreal> rows = distributedPositions(bounds.top(), bounds.bottom(), rowSpacingMm);

        int lineGroupIndex = 0;
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const qreal y = rows.at(rowIndex);
            const bool reverseRowDirection = rowIndex % 2 == 1;
            QVector<HorizontalSpan> spans = horizontalSpansForRow(contours, y, minimumLineSpanMm);
            if (reverseRowDirection) {
                std::reverse(spans.begin(), spans.end());
            }

            for (const HorizontalSpan& span : spans) {
                qreal startX = span.startX + endpointInsetMm;
                qreal endX = span.endX - endpointInsetMm;
                if (endX <= startX && span.endX > span.startX) {
                    startX = span.startX;
                    endX = span.endX;
                }

                QVector<qreal> lineSamplePositions = samplePositionsForContinuousLineSpan(startX, endX, clampedSpacing);
                if (lineSamplePositions.size() < 2) {
                    continue;
                }
                if (reverseRowDirection) {
                    std::reverse(lineSamplePositions.begin(), lineSamplePositions.end());
                }

                for (int sampleIndex = 0; sampleIndex < lineSamplePositions.size(); ++sampleIndex) {
                    TherapyPoint point;
                    point.positionMm = QPointF(lineSamplePositions.at(sampleIndex), y);
                    point.dwellSeconds = dwellSeconds;
                    point.powerWatts = powerWatts;
                    point.lineGroupIndex = lineGroupIndex;
                    point.lineSampleIndex = sampleIndex;
                    point.lineStart = sampleIndex == 0;
                    point.lineEnd = sampleIndex == lineSamplePositions.size() - 1;
                    targets.push_back(point);
                }
                ++lineGroupIndex;
            }
        }
    } else {
        const double pointInsetMm = std::clamp(clampedSpacing * 0.35, 0.25, 3.0);
        const double fitRadiusMm = std::max(0.15, pointInsetMm * 0.9);
        const double rowSpacingMm = std::max(0.5, clampedSpacing * 0.8660254037844386);
        const QRectF centerBounds = bounds.adjusted(pointInsetMm, pointInsetMm, -pointInsetMm, -pointInsetMm);
        const QVector<qreal> rows = centerBounds.isValid()
            ? distributedPositions(centerBounds.top(), centerBounds.bottom(), rowSpacingMm)
            : QVector<qreal> {bounds.center().y()};

        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const qreal y = rows.at(rowIndex);
            const QVector<HorizontalSpan> spans = horizontalSpansForRow(contours, y, fitRadiusMm * 1.2);
            for (const HorizontalSpan& span : spans) {
                const qreal startX = span.startX + pointInsetMm;
                const qreal endX = span.endX - pointInsetMm;
                const QVector<qreal> columns = pointCircleCenterPositionsForSpan(
                    startX,
                    endX,
                    bounds.left() + pointInsetMm,
                    clampedSpacing,
                    rowIndex % 2 == 1);
                for (const qreal x : columns) {
                    const QPointF center(x, y);
                    if (!circleFitsInsidePath(regionPath, center, fitRadiusMm)) {
                        continue;
                    }

                    TherapyPoint point;
                    point.positionMm = center;
                    point.dwellSeconds = dwellSeconds;
                    point.powerWatts = powerWatts;
                    targets.push_back(point);
                }
            }
        }

        appendContourBoundaryPointTargets(&targets, contours, clampedSpacing, dwellSeconds, powerWatts);
    }

    if (targets.isEmpty()) {
        TherapyPoint fallbackPoint;
        fallbackPoint.positionMm = fallbackPointInsidePath(regionPath, bounds, spacingMm);
        fallbackPoint.dwellSeconds = dwellSeconds;
        fallbackPoint.powerWatts = powerWatts;
        targets.push_back(fallbackPoint);
    }

    for (int index = 0; index < targets.size(); ++index) {
        targets[index].index = index;
    }
    return targets;
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

AnnotationStroke normalizeClosedAnnotationStroke(const AnnotationStroke& annotation)
{
    return normalizeClosedStroke(annotation);
}

QVector<AnnotationStroke> normalizeClosedAnnotations(const QVector<AnnotationStroke>& annotations)
{
    QVector<AnnotationStroke> normalizedAnnotations;
    normalizedAnnotations.reserve(annotations.size());
    for (const AnnotationStroke& stroke : annotations) {
        normalizedAnnotations.push_back(normalizeClosedAnnotationStroke(stroke));
    }
    return normalizedAnnotations;
}

double annotationRegionAreaMm2(const QVector<AnnotationStroke>& annotations)
{
    const QPainterPath regionPath = annotationRegionPathMm(annotations);
    return contourCollectionAreaMm2(fillContoursFromPathMm(regionPath));
}

QRectF annotationRegionBoundsMm(const QVector<AnnotationStroke>& annotations)
{
    return annotationRegionPathMm(annotations).boundingRect();
}

int therapyLineGroupCount(const QVector<TherapyPoint>& points)
{
    QVector<int> groupIndices;
    for (const TherapyPoint& point : points) {
        if (point.lineGroupIndex < 0 || groupIndices.contains(point.lineGroupIndex)) {
            continue;
        }
        groupIndices.push_back(point.lineGroupIndex);
    }
    return groupIndices.size();
}

double contourAreaMm2(const QVector<QPointF>& contour)
{
    return polygonAreaMm2(contour);
}

QRectF contourBoundsMm(const QVector<QPointF>& contour)
{
    return polygonBounds(contour);
}

bool contourContainsPointMm(const QVector<QPointF>& contour, const QPointF& pointMm, double toleranceMm)
{
    const int vertexCount = contourVertexCount(contour);
    if (vertexCount < 3) {
        return false;
    }

    QPolygonF polygon;
    polygon.reserve(vertexCount);
    for (int index = 0; index < vertexCount; ++index) {
        polygon << contour.at(index);
    }
    if (polygon.containsPoint(pointMm, Qt::OddEvenFill)) {
        return true;
    }

    if (toleranceMm <= 0.0) {
        return false;
    }

    for (int index = 0; index < vertexCount; ++index) {
        const QPointF& current = contour.at(index);
        const QPointF& next = contour.at((index + 1) % vertexCount);
        if (pointToSegmentDistanceMm(pointMm, current, next) <= toleranceMm) {
            return true;
        }
    }
    return false;
}

QVector<TherapyPoint> generateTherapyTargetsWithinContour(
    const QVector<QPointF>& contourMm,
    TreatmentPattern pattern,
    double spacingMm,
    double dwellSeconds,
    double powerWatts)
{
    return generateTherapyTargetsInRegionPath(
        contourRegionPathMm(contourMm),
        pattern,
        spacingMm,
        dwellSeconds,
        powerWatts);
}

QVector<TherapyPoint> generateTherapyTargetsFromAnnotations(
    const QVector<AnnotationStroke>& annotations,
    TreatmentPattern pattern,
    double spacingMm,
    double dwellSeconds,
    double powerWatts)
{
    return generateTherapyTargetsInRegionPath(
        annotationRegionPathMm(annotations),
        pattern,
        spacingMm,
        dwellSeconds,
        powerWatts);
}

QVector<QPointF> extractContourFromAnnotations(const QVector<AnnotationStroke>& annotations)
{
    const QVector<QVector<QPointF>> contours = fillContoursFromPathMm(annotationRegionPathMm(annotations));
    double largestAreaMm2 = 0.0;
    QVector<QPointF> largestContour;
    for (const QVector<QPointF>& contour : contours) {
        const double areaMm2 = polygonAreaMm2(contour);
        if (contourVertexCount(contour) >= 3 && areaMm2 > largestAreaMm2) {
            largestAreaMm2 = areaMm2;
            largestContour = contour;
        }
    }
    if (largestAreaMm2 > 0.0) {
        return largestContour;
    }

    const QVector<AnnotationStroke> normalizedAnnotations = normalizeClosedAnnotations(annotations);
    const QVector<QPointF> points = annotationPointsInMillimeters(normalizedAnnotations);
    if (points.size() < 3) {
        return points;
    }

    QVector<QPointF> hull = buildConvexHull(points);
    if (!hull.isEmpty() && !pointsNearlyEqual(hull.first(), hull.last())) {
        hull.push_back(hull.first());
    }
    return hull;
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
