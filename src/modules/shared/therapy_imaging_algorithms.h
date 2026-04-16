#pragma once

#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>

#include "core/domain/system_types.h"
#include "modules/shared/mock_ultrasound_view.h"

namespace panthera::modules {

struct RespiratoryFollowResult {
    bool valid {false};
    QRectF calibrationBoxMm;
    QPointF baselineCentroidMm;
    QPointF liveCentroidMm;
    QPointF deltaMm;
    QVector<panthera::core::TherapyPoint> correctedTargets;
    QString summary;
};

struct VolumeContourSlice {
    int sliceIndex {0};
    bool derivedFromAnnotation {false};
    QVector<QPointF> contourMm;
};

struct VolumeReconstructionResult {
    bool valid {false};
    int sliceCount {0};
    int annotatedSliceCount {0};
    int inferredSliceCount {0};
    double estimatedVolumeCm3 {0.0};
    QVector3D weightedCentroidMm;
    QPixmap preview;
    QString summary;
};

QVector<QPointF> extractContourFromAnnotations(const QVector<AnnotationStroke>& annotations);
QVector<QPointF> buildFallbackLesionContourMm(int sliceIndex, int totalSliceCount);

RespiratoryFollowResult computeRespiratoryFollowResult(
    const QVector<AnnotationStroke>& annotations,
    const QVector<panthera::core::TherapyPoint>& originalTargets,
    int sliceIndex,
    int totalSliceCount,
    const panthera::core::DeviceSnapshot* snapshot = nullptr);

VolumeReconstructionResult buildVolumeReconstructionResult(
    const QVector<VolumeContourSlice>& slices,
    double sliceSpacingMm,
    const QSize& previewSize);

}  // namespace panthera::modules
