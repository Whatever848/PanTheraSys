#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

#include "core/domain/system_types.h"

namespace panthera::modules {

struct AnnotationStroke {
    QColor color;
    QVector<QPointF> normalizedPoints;
};

class MockUltrasoundView final : public QWidget {
    Q_OBJECT

public:
    explicit MockUltrasoundView(QWidget* parent = nullptr);

    void setPlan(const panthera::core::TherapyPlan& plan);
    void clearPlan();
    void setCompletedPointCount(int completedPointCount);
    void setCaption(const QString& caption);
    void setAnnotationEnabled(bool enabled);
    void setCurrentAnnotationColor(const QColor& color);
    void setAnnotationStrokes(const QVector<AnnotationStroke>& strokes);
    QVector<AnnotationStroke> annotationStrokes() const;
    bool undoLastAnnotation();
    void clearAnnotations();

signals:
    void annotationStrokesChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int totalPointCount() const;
    QPointF mapPointToWidget(const QPointF& logicalPoint) const;
    QRectF annotationCanvasRect() const;
    QPainterPath drawingPath() const;
    QPointF normalizePoint(const QPointF& widgetPoint) const;
    QPointF denormalizePoint(const QPointF& normalizedPoint) const;
    bool isDrawablePoint(const QPointF& widgetPoint) const;

    panthera::core::TherapyPlan m_plan;
    bool m_hasPlan {false};
    int m_completedPointCount {0};
    QString m_caption;
    bool m_annotationEnabled {false};
    bool m_isDrawing {false};
    QColor m_currentAnnotationColor {QColor(201, 71, 51)};
    QVector<AnnotationStroke> m_annotationStrokes;
    AnnotationStroke m_activeStroke;
};

}  // namespace panthera::modules
