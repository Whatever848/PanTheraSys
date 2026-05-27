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

AnnotationStroke normalizeClosedAnnotationStroke(const AnnotationStroke& annotation);
QVector<AnnotationStroke> normalizeClosedAnnotations(const QVector<AnnotationStroke>& annotations);
double annotationRegionAreaMm2(const QVector<AnnotationStroke>& annotations);
QRectF annotationRegionBoundsMm(const QVector<AnnotationStroke>& annotations);
int therapyLineGroupCount(const QVector<panthera::core::TherapyPoint>& points);
QVector<panthera::core::TherapyPoint> orderPointTargetsSerpentine(
    const QVector<panthera::core::TherapyPoint>& targets,
    double spacingMm);
double contourAreaMm2(const QVector<QPointF>& contour);
QRectF contourBoundsMm(const QVector<QPointF>& contour);
bool contourContainsPointMm(const QVector<QPointF>& contour, const QPointF& pointMm, double toleranceMm = 0.0);
QVector<panthera::core::TherapyPoint> generateTherapyTargetsWithinContour(
    const QVector<QPointF>& contourMm,
    panthera::core::TreatmentPattern pattern,
    double spacingMm,
    double dwellSeconds,
    double powerWatts);
QVector<panthera::core::TherapyPoint> generateTherapyTargetsFromAnnotations(
    const QVector<AnnotationStroke>& annotations,
    panthera::core::TreatmentPattern pattern,
    double spacingMm,
    double dwellSeconds,
    double powerWatts);

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
