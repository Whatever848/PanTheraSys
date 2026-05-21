#include "modules/shared/mock_ultrasound_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>

#include "modules/shared/therapy_imaging_algorithms.h"

namespace panthera::modules {

using namespace panthera::core;

namespace {

QPainterPath buildFanPath(const QRectF& bounds)
{
    const QPointF center(bounds.center().x(), bounds.top() - bounds.height() * 0.15);
    const qreal radius = bounds.height() * 1.15;
    QPainterPath path(center);
    path.arcTo(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0), 212.0, 116.0);
    path.closeSubpath();
    return path;
}

bool pointsNearlyEqual(const QPointF& left, const QPointF& right, qreal epsilon = 0.0005)
{
    return std::abs(left.x() - right.x()) <= epsilon && std::abs(left.y() - right.y()) <= epsilon;
}

QVector<QPointF> uniqueStrokePoints(const QVector<QPointF>& points)
{
    if (points.size() >= 2 && pointsNearlyEqual(points.first(), points.last())) {
        return points.first(points.size() - 1);
    }
    return points;
}

QRectF normalizedBounds(const QVector<QPointF>& points)
{
    if (points.isEmpty()) {
        return {};
    }

    qreal minimumX = points.first().x();
    qreal maximumX = points.first().x();
    qreal minimumY = points.first().y();
    qreal maximumY = points.first().y();
    for (const QPointF& point : points) {
        minimumX = std::min(minimumX, point.x());
        maximumX = std::max(maximumX, point.x());
        minimumY = std::min(minimumY, point.y());
        maximumY = std::max(maximumY, point.y());
    }
    return QRectF(QPointF(minimumX, minimumY), QPointF(maximumX, maximumY)).normalized();
}

bool isClosedStroke(const QVector<QPointF>& points)
{
    return points.size() >= 4 && pointsNearlyEqual(points.first(), points.last());
}

qreal closureAssistThreshold(const AnnotationStroke& stroke)
{
    const QVector<QPointF> uniquePoints = uniqueStrokePoints(stroke.normalizedPoints);
    if (uniquePoints.size() < 3) {
        return 0.0;
    }

    const QRectF bounds = normalizedBounds(uniquePoints);
    const qreal maximumSpan = std::max(bounds.width(), bounds.height());
    return std::clamp(maximumSpan * 0.18, 0.028, 0.085);
}

bool shouldPreviewClosure(const AnnotationStroke& stroke)
{
    const QVector<QPointF> uniquePoints = uniqueStrokePoints(stroke.normalizedPoints);
    if (uniquePoints.size() < 4) {
        return false;
    }

    return QLineF(uniquePoints.first(), uniquePoints.last()).length() <= closureAssistThreshold(stroke);
}

QPointF midpoint(const QPointF& left, const QPointF& right)
{
    return QPointF((left.x() + right.x()) * 0.5, (left.y() + right.y()) * 0.5);
}

QPainterPath buildSmoothedStrokePath(
    const QVector<QPointF>& normalizedPoints,
    bool closed,
    const std::function<QPointF(const QPointF&)>& toWidget)
{
    if (normalizedPoints.isEmpty()) {
        return {};
    }

    const QVector<QPointF> uniquePoints = closed ? uniqueStrokePoints(normalizedPoints) : normalizedPoints;
    if (uniquePoints.isEmpty()) {
        return {};
    }

    if (uniquePoints.size() == 1) {
        return QPainterPath(toWidget(uniquePoints.first()));
    }

    if (!closed || uniquePoints.size() < 3) {
        QPainterPath path(toWidget(uniquePoints.first()));
        for (int index = 1; index < uniquePoints.size(); ++index) {
            const QPointF previous = toWidget(uniquePoints.at(index - 1));
            const QPointF current = toWidget(uniquePoints.at(index));
            path.quadTo(previous, midpoint(previous, current));
        }
        path.lineTo(toWidget(uniquePoints.last()));
        return path;
    }

    const QPointF startMidpoint = midpoint(
        toWidget(uniquePoints.constLast()),
        toWidget(uniquePoints.first()));
    QPainterPath path(startMidpoint);
    for (int index = 0; index < uniquePoints.size(); ++index) {
        const QPointF current = toWidget(uniquePoints.at(index));
        const QPointF next = toWidget(uniquePoints.at((index + 1) % uniquePoints.size()));
        path.quadTo(current, midpoint(current, next));
    }
    path.closeSubpath();
    return path;
}

void drawStroke(
    QPainter* painter,
    const AnnotationStroke& stroke,
    const std::function<QPointF(const QPointF&)>& toWidget,
    bool closePreview = false,
    bool highlightClosure = false)
{
    if (painter == nullptr || stroke.normalizedPoints.isEmpty()) {
        return;
    }

    QVector<QPointF> displayPoints = stroke.normalizedPoints;
    if (closePreview && displayPoints.size() >= 3 && !pointsNearlyEqual(displayPoints.first(), displayPoints.last())) {
        displayPoints.push_back(displayPoints.first());
    }

    const bool closed = isClosedStroke(displayPoints);

    QPen pen(stroke.color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (displayPoints.size() == 1) {
        const QPointF point = toWidget(displayPoints.first());
        painter->setBrush(stroke.color);
        painter->drawEllipse(point, 2.0, 2.0);
        return;
    }

    const QPainterPath path = buildSmoothedStrokePath(displayPoints, closed, toWidget);
    if (closed) {
        QColor fillColor = stroke.color;
        fillColor.setAlpha(highlightClosure ? 64 : 40);
        painter->fillPath(path, fillColor);
    }
    painter->drawPath(path);

    if (highlightClosure && !displayPoints.isEmpty()) {
        const QPointF startPoint = toWidget(displayPoints.first());
        painter->setBrush(QColor(255, 255, 255, 220));
        painter->setPen(QPen(stroke.color, 1.4));
        painter->drawEllipse(startPoint, 4.2, 4.2);
    }

    if (highlightClosure && !closed && displayPoints.size() >= 2) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(stroke.color.red(), stroke.color.green(), stroke.color.blue(), 180), 1.5, Qt::DashLine));
        painter->drawLine(toWidget(displayPoints.constLast()), toWidget(displayPoints.first()));
    }
}

struct LineRenderTrack {
    QVector<QPointF> widgetPoints;
    int completedSampleCount {0};
};

bool hasExplicitLineTreatmentMetadata(const TherapySegment& segment)
{
    for (const TherapyPoint& point : segment.points) {
        if (point.lineGroupIndex >= 0 || point.lineStart || point.lineEnd) {
            return true;
        }
    }
    return false;
}

bool hasLineTreatmentTracks(const TherapySegment& segment, TreatmentPattern pattern)
{
    if (pattern == TreatmentPattern::Point) {
        return false;
    }

    return hasExplicitLineTreatmentMetadata(segment) || pattern == TreatmentPattern::Line;
}

QVector<LineRenderTrack> buildLineRenderTracks(
    const TherapySegment& segment,
    int completedPointCount,
    int* currentIndex,
    const std::function<QPointF(const QPointF&)>& mapPointToWidget)
{
    QVector<LineRenderTrack> tracks;
    if (currentIndex == nullptr) {
        return tracks;
    }

    LineRenderTrack activeTrack;
    int activeLineGroupIndex = std::numeric_limits<int>::min();
    qreal previousY = std::numeric_limits<qreal>::quiet_NaN();

    const auto flushTrack = [&]() {
        if (!activeTrack.widgetPoints.isEmpty()) {
            tracks.push_back(activeTrack);
            activeTrack = LineRenderTrack {};
        }
    };

    for (const TherapyPoint& point : segment.points) {
        const bool done = *currentIndex < completedPointCount;
        ++(*currentIndex);

        const int resolvedLineGroupIndex = point.lineGroupIndex >= 0 ? point.lineGroupIndex : activeLineGroupIndex;
        const bool explicitGroupBoundary = !activeTrack.widgetPoints.isEmpty()
            && point.lineGroupIndex >= 0
            && point.lineGroupIndex != activeLineGroupIndex;
        const bool fallbackRowBoundary = !activeTrack.widgetPoints.isEmpty()
            && point.lineGroupIndex < 0
            && !std::isnan(previousY)
            && std::abs(point.positionMm.y() - previousY) > 0.01;
        if (point.lineStart || explicitGroupBoundary || fallbackRowBoundary) {
            flushTrack();
        }

        if (activeTrack.widgetPoints.isEmpty()) {
            activeLineGroupIndex = resolvedLineGroupIndex;
        }

        activeTrack.widgetPoints.push_back(mapPointToWidget(point.positionMm));
        if (done) {
            ++activeTrack.completedSampleCount;
        }
        previousY = point.positionMm.y();

        if (point.lineEnd) {
            flushTrack();
            activeLineGroupIndex = std::numeric_limits<int>::min();
            previousY = std::numeric_limits<qreal>::quiet_NaN();
        }
    }

    flushTrack();
    return tracks;
}

void drawLineTreatmentTrack(QPainter* painter, const LineRenderTrack& track)
{
    if (painter == nullptr || track.widgetPoints.isEmpty()) {
        return;
    }

    QPointF lineStartPoint = track.widgetPoints.first();
    QPointF lineEndPoint = track.widgetPoints.last();
    if (track.widgetPoints.size() == 1) {
        lineStartPoint.rx() -= 4.0;
        lineEndPoint.rx() += 4.0;
    }

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(0, 183, 225, 215), 2.4, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(lineStartPoint, lineEndPoint);

    if (track.completedSampleCount <= 0) {
        return;
    }

    const int lastPointIndex = static_cast<int>(track.widgetPoints.size()) - 1;
    const int completedIndex = std::clamp(track.completedSampleCount - 1, 0, lastPointIndex);
    QPointF completedEndPoint = track.widgetPoints.at(completedIndex);
    if (track.widgetPoints.size() == 1) {
        completedEndPoint = lineEndPoint;
    }

    painter->setPen(QPen(QColor(255, 226, 80, 235), 3.2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(lineStartPoint, completedEndPoint);
}

qreal pointTreatmentRadiusPx(double spacingMm, const QRectF& canvas)
{
    const qreal mmToPx = std::min(canvas.width(), canvas.height()) / 60.0;
    return std::clamp<qreal>(std::max(0.8, spacingMm) * mmToPx * 0.68, 5.5, 16.0);
}

void drawPointTreatmentMarker(QPainter* painter, const QPointF& mappedPoint, qreal radiusPx, bool completed)
{
    if (painter == nullptr) {
        return;
    }

    const QColor strokeColor = completed ? QColor(255, 226, 80, 230) : QColor(0, 201, 215, 210);
    const QColor fillColor = completed ? QColor(255, 226, 80, 55) : QColor(0, 201, 215, 22);

    painter->setPen(QPen(strokeColor, completed ? 2.0 : 1.6));
    painter->setBrush(fillColor);
    painter->drawEllipse(mappedPoint, radiusPx, radiusPx);
}

}  // namespace

MockUltrasoundView::MockUltrasoundView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 360);
    setMouseTracking(true);
}

void MockUltrasoundView::setPlan(const TherapyPlan& plan)
{
    m_plan = plan;
    m_hasPlan = true;
    update();
}

void MockUltrasoundView::clearPlan()
{
    m_hasPlan = false;
    m_completedPointCount = 0;
    update();
}

void MockUltrasoundView::setCompletedPointCount(int completedPointCount)
{
    m_completedPointCount = completedPointCount;
    update();
}

void MockUltrasoundView::setCaption(const QString& caption)
{
    m_caption = caption;
    update();
}

void MockUltrasoundView::setBackgroundImage(const QPixmap& image)
{
    m_backgroundImage = image;
    resetImageZoom();
    update();
}

void MockUltrasoundView::clearBackgroundImage()
{
    if (m_backgroundImage.isNull()) {
        return;
    }

    m_backgroundImage = QPixmap {};
    resetImageZoom();
    update();
}

void MockUltrasoundView::setImageZoomEnabled(bool enabled)
{
    if (m_imageZoomEnabled == enabled) {
        return;
    }

    m_imageZoomEnabled = enabled;
    if (!m_imageZoomEnabled) {
        resetImageZoom();
    }
}

bool MockUltrasoundView::isImageZoomEnabled() const
{
    return m_imageZoomEnabled;
}

void MockUltrasoundView::resetImageZoom()
{
    setImageZoomState(1.0, QPointF(0.5, 0.5));
}

void MockUltrasoundView::setImageZoom(qreal zoomFactor, const QPointF& zoomCenterNormalized)
{
    setImageZoomState(zoomFactor, zoomCenterNormalized);
}

qreal MockUltrasoundView::imageZoomFactor() const
{
    return m_imageZoomFactor;
}

QPointF MockUltrasoundView::imageZoomCenterNormalized() const
{
    return m_imageZoomCenterNormalized;
}

void MockUltrasoundView::beginComparisonCalibrationPointCapture(int pointIndex)
{
    if (pointIndex != 0 && pointIndex != 1) {
        m_pendingComparisonPointIndex = -1;
        updateInteractionCursor();
        return;
    }

    m_pendingComparisonPointIndex = pointIndex;
    setCursor(Qt::CrossCursor);
}

void MockUltrasoundView::setComparisonCalibrationPoint(int pointIndex, const QPointF& normalizedPoint)
{
    const QPointF clampedPoint(
        qBound(0.0, normalizedPoint.x(), 1.0),
        qBound(0.0, normalizedPoint.y(), 1.0));

    if (pointIndex == 0) {
        m_comparisonStartPointNormalized = clampedPoint;
        m_hasComparisonStartPoint = true;
    } else if (pointIndex == 1) {
        m_comparisonEndPointNormalized = clampedPoint;
        m_hasComparisonEndPoint = true;
    } else {
        return;
    }
    update();
}

void MockUltrasoundView::clearComparisonCalibrationPoints()
{
    m_pendingComparisonPointIndex = -1;
    m_hasComparisonStartPoint = false;
    m_hasComparisonEndPoint = false;
    updateInteractionCursor();
    update();
}

void MockUltrasoundView::setSliceContext(int sliceIndex, int totalSliceCount)
{
    const int normalizedTotal = std::max(0, totalSliceCount);
    const int normalizedIndex = normalizedTotal <= 1 ? 0 : std::max(0, std::min(sliceIndex, normalizedTotal - 1));
    if (m_sliceIndex == normalizedIndex && m_totalSliceCount == normalizedTotal) {
        return;
    }

    m_sliceIndex = normalizedIndex;
    m_totalSliceCount = normalizedTotal;
    update();
}

void MockUltrasoundView::setAnnotationEnabled(bool enabled)
{
    m_annotationEnabled = enabled;
    if (!enabled) {
        m_isDrawing = false;
        m_activeStroke = AnnotationStroke {};
    }
    if (!m_isPanningImage) {
        updateInteractionCursor();
    }
    update();
}

void MockUltrasoundView::setCurrentAnnotationColor(const QColor& color)
{
    m_currentAnnotationColor = color;
}

void MockUltrasoundView::setAnnotationStrokes(const QVector<AnnotationStroke>& strokes)
{
    m_annotationStrokes = strokes;
    m_activeStroke = AnnotationStroke {};
    m_isDrawing = false;
    update();
}

QVector<AnnotationStroke> MockUltrasoundView::annotationStrokes() const
{
    return m_annotationStrokes;
}

bool MockUltrasoundView::undoLastAnnotation()
{
    if (m_annotationStrokes.isEmpty()) {
        return false;
    }

    m_annotationStrokes.removeLast();
    emit annotationStrokesChanged();
    update();
    return true;
}

void MockUltrasoundView::clearAnnotations()
{
    if (m_annotationStrokes.isEmpty() && m_activeStroke.normalizedPoints.isEmpty()) {
        return;
    }

    m_annotationStrokes.clear();
    m_activeStroke = AnnotationStroke {};
    m_isDrawing = false;
    emit annotationStrokesChanged();
    update();
}

void MockUltrasoundView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(9, 16, 26));

    const QRectF canvas = rect().adjusted(16, 8, -16, -24);
    const QPainterPath fanPath = buildFanPath(canvas);
    const qreal ratio = sliceRatio();

    if (!m_backgroundImage.isNull()) {
        const QRectF imageRect = backgroundImageDisplayRect();
        const QRectF sourceRect = backgroundImageSourceRect();
        painter.drawPixmap(imageRect, m_backgroundImage, sourceRect);
        painter.setPen(QPen(QColor(42, 58, 82), 1.0));
        painter.drawRect(canvas.adjusted(0, 0, -1, -1));
    } else {
        QLinearGradient gradient(canvas.topLeft(), canvas.bottomLeft());
        gradient.setColorAt(0.0, QColor(18, 39 + static_cast<int>(ratio * 10.0), 66 + static_cast<int>(ratio * 8.0)));
        gradient.setColorAt(0.55, QColor(44 + static_cast<int>(ratio * 12.0), 83 + static_cast<int>(ratio * 14.0), 118 + static_cast<int>(ratio * 16.0)));
        gradient.setColorAt(1.0, QColor(162 + static_cast<int>(ratio * 18.0), 184 + static_cast<int>(ratio * 16.0), 202 + static_cast<int>(ratio * 10.0)));

        painter.save();
        painter.setClipPath(fanPath);
        painter.fillPath(fanPath, gradient);

        painter.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
        const int lineCount = 12;
        for (int index = 0; index < lineCount; ++index) {
            const qreal lineRatio = static_cast<qreal>(index) / (lineCount - 1);
            const qreal y = canvas.top() + canvas.height() * lineRatio;
            painter.drawLine(QPointF(canvas.left(), y), QPointF(canvas.right(), y));
        }

        painter.setPen(QPen(QColor(255, 255, 255, 20), 1.0));
        for (int index = 0; index < 8; ++index) {
            const qreal x = canvas.left() + 30.0 + index * 55.0;
            painter.drawLine(QPointF(x, canvas.top()), QPointF(canvas.center().x(), canvas.bottom()));
        }

        const qreal echoCenterY = canvas.top() + canvas.height() * (0.20 + ratio * 0.48);
        QLinearGradient echoGradient(QPointF(canvas.left(), echoCenterY - 22.0), QPointF(canvas.left(), echoCenterY + 22.0));
        echoGradient.setColorAt(0.0, QColor(255, 255, 255, 0));
        echoGradient.setColorAt(0.5, QColor(255, 255, 255, 34));
        echoGradient.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(QRectF(canvas.left(), echoCenterY - 22.0, canvas.width(), 44.0), echoGradient);
        painter.restore();

        painter.setPen(QPen(QColor(0, 187, 255), 2.0));
        painter.drawPath(fanPath);

        const qreal lateralShift = (ratio - 0.5) * 9.0;
        const qreal depthShift = std::sin(ratio * 3.14159265358979323846) * 4.0 - 1.5;
        QPolygonF lesion;
        lesion << mapPointToWidget(QPointF(-18.0 + lateralShift, -8.0 + depthShift))
               << mapPointToWidget(QPointF(-8.0 + lateralShift, -16.0 + depthShift * 0.9))
               << mapPointToWidget(QPointF(12.0 + lateralShift, -10.0 + depthShift * 0.75))
               << mapPointToWidget(QPointF(20.0 + lateralShift, 6.0 + depthShift))
               << mapPointToWidget(QPointF(6.0 + lateralShift, 18.0 + depthShift * 0.85))
               << mapPointToWidget(QPointF(-14.0 + lateralShift, 12.0 + depthShift));

        painter.setBrush(QColor(255, 144, 0, 80));
        painter.setPen(QPen(QColor(255, 190, 70), 2.0));
        painter.drawPolygon(lesion);

        const QPointF focusCenter = mapPointToWidget(QPointF(-4.0 + lateralShift * 0.55, 1.5 + depthShift * 0.8));
        QRadialGradient focusGradient(focusCenter, 28.0);
        focusGradient.setColorAt(0.0, QColor(255, 86, 0, 120));
        focusGradient.setColorAt(0.4, QColor(255, 166, 76, 84));
        focusGradient.setColorAt(1.0, QColor(255, 166, 76, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(focusGradient);
        painter.drawEllipse(focusCenter, 28.0, 28.0);
    }

    if (m_hasPlan) {
        int currentIndex = 0;
        const qreal pointRadiusPx = pointTreatmentRadiusPx(m_plan.spacingMm, annotationCanvasRect());
        for (const TherapySegment& segment : m_plan.segments) {
            if (hasLineTreatmentTracks(segment, m_plan.pattern)) {
                const QVector<LineRenderTrack> tracks = buildLineRenderTracks(
                    segment,
                    m_completedPointCount,
                    &currentIndex,
                    [this](const QPointF& point) { return mapPointToWidget(point); });
                for (const LineRenderTrack& track : tracks) {
                    drawLineTreatmentTrack(&painter, track);
                }
                continue;
            }

            QPolygonF linePath;
            for (const TherapyPoint& point : segment.points) {
                const QPointF mapped = mapPointToWidget(point.positionMm);
                linePath << mapped;
                const bool done = currentIndex < m_completedPointCount;
                drawPointTreatmentMarker(&painter, mapped, pointRadiusPx, done);
                ++currentIndex;
            }

            if (m_plan.pattern != TreatmentPattern::Point) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(91, 152, 255, 160), 2.0));
                painter.drawPolyline(linePath);
            }
        }
    }

    painter.save();
    painter.setClipPath(drawingPath());
    const auto toWidget = [this](const QPointF& point) {
        return denormalizePoint(point);
    };
    for (const AnnotationStroke& stroke : m_annotationStrokes) {
        drawStroke(&painter, stroke, toWidget);
    }
    if (!m_activeStroke.normalizedPoints.isEmpty()) {
        const bool previewClosure = shouldPreviewClosure(m_activeStroke);
        drawStroke(&painter, m_activeStroke, toWidget, previewClosure, previewClosure);
    }
    painter.restore();

    drawComparisonCalibration(&painter);

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
    painter.drawText(canvas.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft,
        m_caption.isEmpty() ? QStringLiteral("B \u8d85\u6a21\u62df\u89c6\u56fe") : m_caption);
}

void MockUltrasoundView::mousePressEvent(QMouseEvent* event)
{
    if (m_pendingComparisonPointIndex >= 0 && event->button() == Qt::LeftButton) {
        if (contentViewportRect().contains(event->position())) {
            const int capturedIndex = m_pendingComparisonPointIndex;
            const QPointF normalizedPoint = normalizePoint(event->position());
            setComparisonCalibrationPoint(capturedIndex, normalizedPoint);
            m_pendingComparisonPointIndex = -1;
            updateInteractionCursor();
            emit comparisonCalibrationPointCaptured(capturedIndex, normalizedPoint);
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    if (canStartImagePan(event->button(), event->position())) {
        m_isPanningImage = true;
        m_lastPanPosition = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || !m_annotationEnabled || !isDrawablePoint(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_isDrawing = true;
    m_activeStroke = AnnotationStroke {};
    m_activeStroke.color = m_currentAnnotationColor;
    m_activeStroke.normalizedPoints.push_back(normalizePoint(event->position()));
    update();
    event->accept();
}

void MockUltrasoundView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanningImage) {
        panImageBy(event->position() - m_lastPanPosition);
        m_lastPanPosition = event->position();
        event->accept();
        return;
    }

    if (!m_isDrawing || !m_annotationEnabled) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!annotationCanvasRect().contains(event->position())) {
        event->accept();
        return;
    }

    const QPointF normalizedPoint = normalizePoint(event->position());
    if (!m_activeStroke.normalizedPoints.isEmpty()) {
        const QPointF last = m_activeStroke.normalizedPoints.constLast();
        if (QLineF(last, normalizedPoint).length() < 0.002) {
            event->accept();
            return;
        }
    }

    m_activeStroke.normalizedPoints.push_back(normalizedPoint);
    update();
    event->accept();
}

void MockUltrasoundView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isPanningImage
        && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)) {
        m_isPanningImage = false;
        updateInteractionCursor();
        event->accept();
        return;
    }

    if (!m_isDrawing || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (annotationCanvasRect().contains(event->position())) {
        m_activeStroke.normalizedPoints.push_back(normalizePoint(event->position()));
    }

    m_isDrawing = false;
    if (!m_activeStroke.normalizedPoints.isEmpty()) {
        const AnnotationStroke finalizedStroke = normalizeClosedAnnotationStroke(m_activeStroke);
        if (!finalizedStroke.normalizedPoints.isEmpty()) {
            m_annotationStrokes.push_back(finalizedStroke);
            emit annotationStrokesChanged();
        }
    }
    m_activeStroke = AnnotationStroke {};
    update();
    event->accept();
}

void MockUltrasoundView::wheelEvent(QWheelEvent* event)
{
    if (!m_imageZoomEnabled) {
        QWidget::wheelEvent(event);
        return;
    }

    const QRectF viewportRect = contentViewportRect();
    if (!viewportRect.contains(event->position())) {
        QWidget::wheelEvent(event);
        return;
    }

    const QPoint numDegrees = event->angleDelta();
    if (numDegrees.y() == 0) {
        event->accept();
        return;
    }

    const qreal previousZoom = m_imageZoomFactor;
    const qreal step = numDegrees.y() > 0 ? 1.15 : (1.0 / 1.15);
    const qreal nextZoom = std::clamp(previousZoom * step, 1.0, 8.0);
    if (qFuzzyCompare(previousZoom, nextZoom)) {
        event->accept();
        return;
    }

    const QRectF previousViewport = zoomViewportNormalizedRect();
    const qreal cursorRatioX = qBound(0.0, (event->position().x() - viewportRect.left()) / viewportRect.width(), 1.0);
    const qreal cursorRatioY = qBound(0.0, (event->position().y() - viewportRect.top()) / viewportRect.height(), 1.0);
    const qreal focusX = previousViewport.left() + previousViewport.width() * cursorRatioX;
    const qreal focusY = previousViewport.top() + previousViewport.height() * cursorRatioY;

    const qreal nextViewportWidth = 1.0 / nextZoom;
    const qreal nextViewportHeight = 1.0 / nextZoom;
    const qreal nextLeft = qBound(0.0, focusX - nextViewportWidth * cursorRatioX, 1.0 - nextViewportWidth);
    const qreal nextTop = qBound(0.0, focusY - nextViewportHeight * cursorRatioY, 1.0 - nextViewportHeight);
    const QPointF nextCenterNormalized(
        nextLeft + nextViewportWidth * 0.5,
        nextTop + nextViewportHeight * 0.5);

    setImageZoomState(nextZoom, nextCenterNormalized);
    event->accept();
}

int MockUltrasoundView::totalPointCount() const
{
    int total = 0;
    for (const TherapySegment& segment : m_plan.segments) {
        total += segment.points.size();
    }
    return total;
}

QPointF MockUltrasoundView::mapPointToWidget(const QPointF& logicalPoint) const
{
    const qreal normalizedX = (logicalPoint.x() + 30.0) / 60.0;
    const qreal normalizedY = (logicalPoint.y() + 30.0) / 60.0;
    return denormalizePoint(QPointF(normalizedX, normalizedY));
}

QRectF MockUltrasoundView::annotationCanvasRect() const
{
    return rect().adjusted(50, 42, -50, -52);
}

QPainterPath MockUltrasoundView::drawingPath() const
{
    return buildFanPath(rect().adjusted(16, 8, -16, -24));
}

QRectF MockUltrasoundView::backgroundImageDisplayRect() const
{
    const QRectF canvas = rect().adjusted(16, 8, -16, -24);
    if (m_backgroundImage.isNull()) {
        return canvas;
    }

    const QSize scaledSize = m_backgroundImage.size().scaled(canvas.size().toSize(), Qt::KeepAspectRatio);
    return QRectF(
        canvas.center().x() - scaledSize.width() / 2.0,
        canvas.center().y() - scaledSize.height() / 2.0,
        scaledSize.width(),
        scaledSize.height());
}

QRectF MockUltrasoundView::backgroundImageSourceRect() const
{
    if (m_backgroundImage.isNull()) {
        return {};
    }

    const qreal imageWidth = m_backgroundImage.width();
    const qreal imageHeight = m_backgroundImage.height();
    const QRectF viewport = zoomViewportNormalizedRect();
    const qreal sourceWidth = imageWidth * viewport.width();
    const qreal sourceHeight = imageHeight * viewport.height();
    const qreal centerX = viewport.center().x() * imageWidth;
    const qreal centerY = viewport.center().y() * imageHeight;
    const qreal left = qBound(0.0, centerX - sourceWidth * 0.5, imageWidth - sourceWidth);
    const qreal top = qBound(0.0, centerY - sourceHeight * 0.5, imageHeight - sourceHeight);
    return QRectF(left, top, sourceWidth, sourceHeight);
}

QRectF MockUltrasoundView::contentViewportRect() const
{
    if (!m_backgroundImage.isNull()) {
        return backgroundImageDisplayRect();
    }
    return annotationCanvasRect();
}

QRectF MockUltrasoundView::zoomViewportNormalizedRect() const
{
    const qreal viewportWidth = 1.0 / m_imageZoomFactor;
    const qreal viewportHeight = 1.0 / m_imageZoomFactor;
    const qreal left = qBound(0.0, m_imageZoomCenterNormalized.x() - viewportWidth * 0.5, 1.0 - viewportWidth);
    const qreal top = qBound(0.0, m_imageZoomCenterNormalized.y() - viewportHeight * 0.5, 1.0 - viewportHeight);
    return QRectF(left, top, viewportWidth, viewportHeight);
}

void MockUltrasoundView::setImageZoomState(qreal zoomFactor, const QPointF& zoomCenterNormalized)
{
    const qreal normalizedZoom = std::clamp(zoomFactor, 1.0, 8.0);
    const QPointF normalizedCenter(
        qBound(0.0, zoomCenterNormalized.x(), 1.0),
        qBound(0.0, zoomCenterNormalized.y(), 1.0));
    if (qFuzzyCompare(m_imageZoomFactor, normalizedZoom)
        && qFuzzyCompare(m_imageZoomCenterNormalized.x(), normalizedCenter.x())
        && qFuzzyCompare(m_imageZoomCenterNormalized.y(), normalizedCenter.y())) {
        return;
    }

    m_imageZoomFactor = normalizedZoom;
    m_imageZoomCenterNormalized = normalizedCenter;
    emit imageZoomChanged(m_imageZoomFactor);
    emit imageViewportChanged(m_imageZoomFactor, m_imageZoomCenterNormalized);
    update();
}

void MockUltrasoundView::panImageBy(const QPointF& widgetDelta)
{
    if (m_imageZoomFactor <= 1.0) {
        return;
    }

    const QRectF viewportRect = contentViewportRect();
    if (viewportRect.width() <= 0.0 || viewportRect.height() <= 0.0) {
        return;
    }

    const QRectF visibleViewport = zoomViewportNormalizedRect();
    const QPointF center(
        m_imageZoomCenterNormalized.x() - (widgetDelta.x() / viewportRect.width()) * visibleViewport.width(),
        m_imageZoomCenterNormalized.y() - (widgetDelta.y() / viewportRect.height()) * visibleViewport.height());
    setImageZoomState(m_imageZoomFactor, center);
}

QPointF MockUltrasoundView::normalizePoint(const QPointF& widgetPoint) const
{
    const QRectF viewportRect = contentViewportRect();
    const QRectF visibleViewport = zoomViewportNormalizedRect();
    const qreal viewportX = qBound(0.0, (widgetPoint.x() - viewportRect.left()) / viewportRect.width(), 1.0);
    const qreal viewportY = qBound(0.0, (widgetPoint.y() - viewportRect.top()) / viewportRect.height(), 1.0);
    const qreal x = qBound(0.0, visibleViewport.left() + viewportX * visibleViewport.width(), 1.0);
    const qreal y = qBound(0.0, visibleViewport.top() + viewportY * visibleViewport.height(), 1.0);
    return QPointF(x, y);
}

QPointF MockUltrasoundView::denormalizePoint(const QPointF& normalizedPoint) const
{
    const QRectF viewportRect = contentViewportRect();
    const QRectF visibleViewport = zoomViewportNormalizedRect();
    const qreal xRatio = visibleViewport.width() > 0.0
        ? (normalizedPoint.x() - visibleViewport.left()) / visibleViewport.width()
        : 0.0;
    const qreal yRatio = visibleViewport.height() > 0.0
        ? (normalizedPoint.y() - visibleViewport.top()) / visibleViewport.height()
        : 0.0;
    return QPointF(
        viewportRect.left() + viewportRect.width() * xRatio,
        viewportRect.top() + viewportRect.height() * yRatio);
}

void MockUltrasoundView::drawComparisonCalibration(QPainter* painter)
{
    if (painter == nullptr || (!m_hasComparisonStartPoint && !m_hasComparisonEndPoint)) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    auto drawPoint = [this, painter](const QPointF& normalizedPoint, const QString& label, const QColor& color) {
        const QPointF widgetPoint = denormalizePoint(normalizedPoint);
        painter->setPen(QPen(color, 2.0));
        painter->setBrush(QColor(color.red(), color.green(), color.blue(), 70));
        painter->drawEllipse(widgetPoint, 8.0, 8.0);

        QRectF labelRect(widgetPoint.x() + 10.0, widgetPoint.y() - 18.0, 32.0, 18.0);
        if (labelRect.right() > rect().right() - 4.0) {
            labelRect.moveRight(widgetPoint.x() - 10.0);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(14, 31, 52, 220));
        painter->drawRoundedRect(labelRect, 6.0, 6.0);
        painter->setPen(QPen(QColor(238, 247, 255), 1.0));
        painter->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8, QFont::Bold));
        painter->drawText(labelRect, Qt::AlignCenter, label);
    };

    if (m_hasComparisonStartPoint && m_hasComparisonEndPoint) {
        painter->setPen(QPen(QColor(75, 210, 255, 210), 2.0, Qt::DashLine, Qt::RoundCap));
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(denormalizePoint(m_comparisonStartPointNormalized), denormalizePoint(m_comparisonEndPointNormalized));
    }

    if (m_hasComparisonStartPoint) {
        drawPoint(m_comparisonStartPointNormalized, QStringLiteral("起"), QColor(50, 211, 127));
    }
    if (m_hasComparisonEndPoint) {
        drawPoint(m_comparisonEndPointNormalized, QStringLiteral("止"), QColor(255, 177, 75));
    }

    painter->restore();
}

void MockUltrasoundView::updateInteractionCursor()
{
    if (m_pendingComparisonPointIndex >= 0) {
        setCursor(Qt::CrossCursor);
        return;
    }
    setCursor(m_annotationEnabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

bool MockUltrasoundView::canStartImagePan(Qt::MouseButton button, const QPointF& widgetPoint) const
{
    if (!m_imageZoomEnabled || m_imageZoomFactor <= 1.0) {
        return false;
    }
    if (!contentViewportRect().contains(widgetPoint)) {
        return false;
    }

    if (button == Qt::MiddleButton) {
        return true;
    }
    if (!m_annotationEnabled && button == Qt::LeftButton) {
        return true;
    }
    return m_annotationEnabled && button == Qt::RightButton;
}

bool MockUltrasoundView::isDrawablePoint(const QPointF& widgetPoint) const
{
    return contentViewportRect().contains(widgetPoint) && drawingPath().contains(widgetPoint);
}

qreal MockUltrasoundView::sliceRatio() const
{
    if (m_totalSliceCount <= 1) {
        return 0.5;
    }
    return static_cast<qreal>(m_sliceIndex) / static_cast<qreal>(m_totalSliceCount - 1);
}

}  // namespace panthera::modules
