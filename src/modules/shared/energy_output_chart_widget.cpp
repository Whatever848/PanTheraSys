#include "modules/shared/energy_output_chart_widget.h"

#include <algorithm>
#include <cmath>

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace panthera::modules {

namespace {

QPainterPath smoothCurvePath(const QVector<QPointF>& points)
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

EnergyOutputChartWidget::EnergyOutputChartWidget(QWidget* parent)
    : QFrame(parent)
    , m_placeholderText(QStringLiteral("\u65b0\u589e\u8def\u5f84\u540e\u663e\u793a\u8d85\u58f0\u5934\u529f\u7387\u66f2\u7ebf"))
{
    setObjectName(QStringLiteral("planningChartCanvas"));
    setMinimumHeight(150);
}

void EnergyOutputChartWidget::setRealtimePowerWatts(double powerWatts)
{
    m_powerWatts = std::max(0.0, powerWatts);
    m_curveSamples = buildCurveSamples(m_powerWatts);
    update();
}

void EnergyOutputChartWidget::clearPowerCurve(const QString& placeholderText)
{
    m_powerWatts = 0.0;
    m_curveSamples.clear();
    if (!placeholderText.trimmed().isEmpty()) {
        m_placeholderText = placeholderText;
    }
    update();
}

QVector<QPointF> EnergyOutputChartWidget::buildCurveSamples(double powerWatts) const
{
    if (powerWatts <= 0.0) {
        return {};
    }

    static const QVector<QPointF> sampleTemplate {
        {0.0, 0.00},
        {8.0, 0.12},
        {16.0, 0.24},
        {22.0, 0.36},
        {28.0, 0.58},
        {34.0, 0.60},
        {40.0, 0.56},
        {44.0, 0.57},
        {50.0, 0.75},
        {60.0, 1.00}
    };

    QVector<QPointF> samples;
    samples.reserve(sampleTemplate.size());
    for (const QPointF& point : sampleTemplate) {
        samples.push_back(QPointF(point.x(), point.y() * powerWatts));
    }
    return samples;
}

void EnergyOutputChartWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QRectF plotRect = rect().adjusted(40, 14, -16, -26);
    if (plotRect.width() <= 40.0 || plotRect.height() <= 40.0) {
        return;
    }

    const QColor gridColor(122, 142, 170, 36);
    const QColor axisLabelColor(107, 125, 154);
    const QColor curveColor(109, 103, 255);
    const QColor fillTopColor(110, 100, 255, 92);
    const QColor fillBottomColor(70, 103, 214, 10);

    painter.setPen(QPen(gridColor, 1.0));
    constexpr int ySteps = 4;
    for (int step = 0; step <= ySteps; ++step) {
        const qreal y = plotRect.bottom() - ((plotRect.height() / ySteps) * step);
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    const double maxPower = std::max(400.0, std::ceil(std::max(m_powerWatts, 1.0) / 100.0) * 100.0);
    painter.setPen(axisLabelColor);
    for (int step = 0; step <= ySteps; ++step) {
        const int value = static_cast<int>(std::round((maxPower / ySteps) * step));
        const qreal y = plotRect.bottom() - ((plotRect.height() / ySteps) * step);
        painter.drawText(QRectF(4.0, y - 10.0, 30.0, 20.0), Qt::AlignRight | Qt::AlignVCenter, QString::number(value));
    }

    constexpr int xSteps = 6;
    for (int step = 0; step <= xSteps; ++step) {
        const qreal x = plotRect.left() + ((plotRect.width() / xSteps) * step);
        painter.drawText(
            QRectF(x - 14.0, plotRect.bottom() + 6.0, 28.0, 16.0),
            Qt::AlignHCenter | Qt::AlignTop,
            QStringLiteral("%1s").arg(step * 10));
    }

    if (m_curveSamples.isEmpty()) {
        painter.setPen(QColor(112, 131, 162));
        painter.drawText(plotRect, Qt::AlignCenter, m_placeholderText);
        return;
    }

    QVector<QPointF> widgetPoints;
    widgetPoints.reserve(m_curveSamples.size());
    for (const QPointF& sample : m_curveSamples) {
        const qreal normalizedX = sample.x() / 60.0;
        const qreal normalizedY = sample.y() / maxPower;
        widgetPoints.push_back(
            QPointF(
                plotRect.left() + (plotRect.width() * normalizedX),
                plotRect.bottom() - (plotRect.height() * normalizedY)));
    }

    const QPainterPath curvePath = smoothCurvePath(widgetPoints);
    QPainterPath fillPath(curvePath);
    fillPath.lineTo(plotRect.right(), plotRect.bottom());
    fillPath.lineTo(plotRect.left(), plotRect.bottom());
    fillPath.closeSubpath();

    QLinearGradient fillGradient(plotRect.topLeft(), plotRect.bottomLeft());
    fillGradient.setColorAt(0.0, fillTopColor);
    fillGradient.setColorAt(1.0, fillBottomColor);
    painter.fillPath(fillPath, fillGradient);

    painter.setPen(QPen(curveColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curvePath);
}

}  // namespace panthera::modules
