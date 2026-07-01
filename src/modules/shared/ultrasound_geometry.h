#pragma once

#include <algorithm>

#include <QPointF>

namespace panthera::modules {

constexpr double kUltrasoundLateralSpanMm = 60.0;
constexpr double kUltrasoundLateralHalfSpanMm = kUltrasoundLateralSpanMm * 0.5;
constexpr double kUltrasoundDepthRangeMm = 50.0;
constexpr double kUltrasoundSwingMaximumDegrees = 10.0;

inline QPointF normalizedUltrasoundPointToMillimeters(const QPointF& normalizedPoint)
{
    return QPointF(
        (normalizedPoint.x() * kUltrasoundLateralSpanMm) - kUltrasoundLateralHalfSpanMm,
        normalizedPoint.y() * kUltrasoundDepthRangeMm);
}

inline QPointF ultrasoundMillimeterPointToNormalized(const QPointF& millimeterPoint)
{
    return QPointF(
        (millimeterPoint.x() + kUltrasoundLateralHalfSpanMm) / kUltrasoundLateralSpanMm,
        millimeterPoint.y() / kUltrasoundDepthRangeMm);
}

inline double ultrasoundLateralMillimetersToSwingDegrees(double lateralMillimeters)
{
    return std::clamp(
        (lateralMillimeters / kUltrasoundLateralHalfSpanMm) * kUltrasoundSwingMaximumDegrees,
        -kUltrasoundSwingMaximumDegrees,
        kUltrasoundSwingMaximumDegrees);
}

}  // namespace panthera::modules
