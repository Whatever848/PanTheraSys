#pragma once

#include <QColor>
#include <QPointF>
#include <QPixmap>
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
    void setBackgroundImage(const QPixmap& image);
    void clearBackgroundImage();
    void setImageZoomEnabled(bool enabled);
    bool isImageZoomEnabled() const;
    void resetImageZoom();
    qreal imageZoomFactor() const;
    QPointF imageZoomCenterNormalized() const;
    void setSliceContext(int sliceIndex, int totalSliceCount);
    void setAnnotationEnabled(bool enabled);
    void setCurrentAnnotationColor(const QColor& color);
    void setAnnotationStrokes(const QVector<AnnotationStroke>& strokes);
    QVector<AnnotationStroke> annotationStrokes() const;
    bool undoLastAnnotation();
    void clearAnnotations();

signals:
    void annotationStrokesChanged();
    void imageZoomChanged(qreal zoomFactor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    int totalPointCount() const;
    QPointF mapPointToWidget(const QPointF& logicalPoint) const;
    QRectF annotationCanvasRect() const;
    QPainterPath drawingPath() const;
    QRectF backgroundImageDisplayRect() const;
    QRectF backgroundImageSourceRect() const;
    QRectF contentViewportRect() const;
    QRectF zoomViewportNormalizedRect() const;
    void setImageZoomState(qreal zoomFactor, const QPointF& zoomCenterNormalized);
    void panImageBy(const QPointF& widgetDelta);
    QPointF normalizePoint(const QPointF& widgetPoint) const;
    QPointF denormalizePoint(const QPointF& normalizedPoint) const;
    bool canStartImagePan(Qt::MouseButton button, const QPointF& widgetPoint) const;
    bool isDrawablePoint(const QPointF& widgetPoint) const;
    qreal sliceRatio() const;

    panthera::core::TherapyPlan m_plan;
    bool m_hasPlan {false};
    int m_completedPointCount {0};
    QString m_caption;
    QPixmap m_backgroundImage;
    bool m_imageZoomEnabled {false};
    qreal m_imageZoomFactor {1.0};
    QPointF m_imageZoomCenterNormalized {0.5, 0.5};
    int m_sliceIndex {0};
    int m_totalSliceCount {0};
    bool m_annotationEnabled {false};
    bool m_isDrawing {false};
    bool m_isPanningImage {false};
    QPointF m_lastPanPosition;
    QColor m_currentAnnotationColor {QColor(201, 71, 51)};
    QVector<AnnotationStroke> m_annotationStrokes;
    AnnotationStroke m_activeStroke;
};

}  // namespace panthera::modules
