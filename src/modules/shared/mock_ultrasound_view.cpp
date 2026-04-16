#include "modules/shared/mock_ultrasound_view.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

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

void drawStroke(QPainter* painter, const AnnotationStroke& stroke, const std::function<QPointF(const QPointF&)>& toWidget)
{
    if (painter == nullptr || stroke.normalizedPoints.isEmpty()) {
        return;
    }

    QPen pen(stroke.color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (stroke.normalizedPoints.size() == 1) {
        const QPointF point = toWidget(stroke.normalizedPoints.first());
        painter->setBrush(stroke.color);
        painter->drawEllipse(point, 2.0, 2.0);
        return;
    }

    QPainterPath path(toWidget(stroke.normalizedPoints.first()));
    for (int index = 1; index < stroke.normalizedPoints.size(); ++index) {
        path.lineTo(toWidget(stroke.normalizedPoints.at(index)));
    }
    painter->drawPath(path);
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
    update();
}

void MockUltrasoundView::clearBackgroundImage()
{
    if (m_backgroundImage.isNull()) {
        return;
    }

    m_backgroundImage = QPixmap {};
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
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
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
        const QPixmap scaled = m_backgroundImage.scaled(canvas.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRectF imageRect(
            canvas.center().x() - scaled.width() / 2.0,
            canvas.center().y() - scaled.height() / 2.0,
            scaled.width(),
            scaled.height());
        painter.drawPixmap(imageRect.topLeft(), scaled);
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
            const qreal ratio = static_cast<qreal>(index) / (lineCount - 1);
            const qreal y = canvas.top() + canvas.height() * ratio;
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
        for (const TherapySegment& segment : m_plan.segments) {
            QPolygonF linePath;
            for (const TherapyPoint& point : segment.points) {
                const QPointF mapped = mapPointToWidget(point.positionMm);
                linePath << mapped;
                const bool done = currentIndex < m_completedPointCount;
                painter.setBrush(done ? QColor(255, 226, 80) : QColor(0, 164, 255, 160));
                painter.setPen(QPen(done ? QColor(255, 226, 80) : QColor(0, 164, 255), 1.5));
                painter.drawEllipse(mapped, done ? 7.0 : 5.0, done ? 7.0 : 5.0);
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
        drawStroke(&painter, m_activeStroke, toWidget);
    }
    painter.restore();

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
    painter.drawText(canvas.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft,
        m_caption.isEmpty() ? QStringLiteral("B \u8d85\u6a21\u62df\u89c6\u56fe") : m_caption);
}

void MockUltrasoundView::mousePressEvent(QMouseEvent* event)
{
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
    if (!m_isDrawing || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (annotationCanvasRect().contains(event->position())) {
        m_activeStroke.normalizedPoints.push_back(normalizePoint(event->position()));
    }

    m_isDrawing = false;
    if (!m_activeStroke.normalizedPoints.isEmpty()) {
        m_annotationStrokes.push_back(m_activeStroke);
        emit annotationStrokesChanged();
    }
    m_activeStroke = AnnotationStroke {};
    update();
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
    const QRectF canvas = annotationCanvasRect();
    const qreal normalizedX = (logicalPoint.x() + 30.0) / 60.0;
    const qreal normalizedY = (logicalPoint.y() + 30.0) / 60.0;
    return QPointF(canvas.left() + canvas.width() * normalizedX, canvas.top() + canvas.height() * normalizedY);
}

QRectF MockUltrasoundView::annotationCanvasRect() const
{
    return rect().adjusted(50, 42, -50, -52);
}

QPainterPath MockUltrasoundView::drawingPath() const
{
    return buildFanPath(rect().adjusted(16, 8, -16, -24));
}

QPointF MockUltrasoundView::normalizePoint(const QPointF& widgetPoint) const
{
    const QRectF canvas = annotationCanvasRect();
    const qreal x = qBound(0.0, (widgetPoint.x() - canvas.left()) / canvas.width(), 1.0);
    const qreal y = qBound(0.0, (widgetPoint.y() - canvas.top()) / canvas.height(), 1.0);
    return QPointF(x, y);
}

QPointF MockUltrasoundView::denormalizePoint(const QPointF& normalizedPoint) const
{
    const QRectF canvas = annotationCanvasRect();
    return QPointF(canvas.left() + canvas.width() * normalizedPoint.x(), canvas.top() + canvas.height() * normalizedPoint.y());
}

bool MockUltrasoundView::isDrawablePoint(const QPointF& widgetPoint) const
{
    return annotationCanvasRect().contains(widgetPoint) && drawingPath().contains(widgetPoint);
}

qreal MockUltrasoundView::sliceRatio() const
{
    if (m_totalSliceCount <= 1) {
        return 0.5;
    }
    return static_cast<qreal>(m_sliceIndex) / static_cast<qreal>(m_totalSliceCount - 1);
}

}  // namespace panthera::modules
