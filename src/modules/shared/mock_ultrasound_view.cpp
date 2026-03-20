#include "modules/shared/mock_ultrasound_view.h"

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

}  // 匿名命名空间

MockUltrasoundView::MockUltrasoundView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 360);
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

void MockUltrasoundView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    // 这是一个偏教学和演示用途的可视化控件，不是临床影像渲染器。
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(9, 16, 26));

    const QRectF canvas = rect().adjusted(16, 16, -16, -16);
    const QPainterPath fanPath = buildFanPath(canvas);

    QLinearGradient gradient(canvas.topLeft(), canvas.bottomLeft());
    gradient.setColorAt(0.0, QColor(20, 45, 72));
    gradient.setColorAt(0.55, QColor(50, 92, 128));
    gradient.setColorAt(1.0, QColor(175, 196, 210));

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
    painter.restore();

    painter.setPen(QPen(QColor(0, 187, 255), 2.0));
    painter.drawPath(fanPath);

    QPolygonF lesion;
    lesion << mapPointToWidget(QPointF(-18.0, -8.0))
           << mapPointToWidget(QPointF(-8.0, -16.0))
           << mapPointToWidget(QPointF(12.0, -10.0))
           << mapPointToWidget(QPointF(20.0, 6.0))
           << mapPointToWidget(QPointF(6.0, 18.0))
           << mapPointToWidget(QPointF(-14.0, 12.0));

    painter.setBrush(QColor(255, 144, 0, 80));
    painter.setPen(QPen(QColor(255, 190, 70), 2.0));
    painter.drawPolygon(lesion);

    if (m_hasPlan) {
        // 通过高亮已完成点位，让同一个控件同时服务于方案预览和执行进度展示。
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

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
    painter.drawText(canvas.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft, m_caption.isEmpty() ? QStringLiteral("B 超模拟视图") : m_caption);
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
    const QRectF canvas = rect().adjusted(50, 55, -50, -40);
    const qreal normalizedX = (logicalPoint.x() + 30.0) / 60.0;
    const qreal normalizedY = (logicalPoint.y() + 30.0) / 60.0;
    return QPointF(canvas.left() + canvas.width() * normalizedX, canvas.top() + canvas.height() * normalizedY);
}

}  // panthera::modules 命名空间
