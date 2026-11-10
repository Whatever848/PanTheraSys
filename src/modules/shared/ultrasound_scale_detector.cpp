#include "modules/shared/ultrasound_scale_detector.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QHash>
#include <QVector>

namespace panthera::modules {
namespace {

constexpr int kScaleMarkMillimeters = 10;

struct HorizontalRun {
    int centerX {0};
    int y {0};
    int width {0};
};

struct PeriodicRunMatch {
    bool valid {false};
    double spacingPixels {0.0};
    double firstTickY {0.0};
    int tickCount {0};
    double density {0.0};
    double meanResidual {0.0};
    double score {-std::numeric_limits<double>::infinity()};
    int centerX {0};
};

QVector<int> uniqueRows(QVector<int> rows, int mergeDistance)
{
    if (rows.isEmpty()) {
        return {};
    }

    std::sort(rows.begin(), rows.end());
    QVector<int> result;
    int clusterSum = rows.first();
    int clusterCount = 1;
    int clusterLast = rows.first();
    for (int index = 1; index < rows.size(); ++index) {
        const int row = rows.at(index);
        if (row - clusterLast <= mergeDistance) {
            clusterSum += row;
            ++clusterCount;
            clusterLast = row;
            continue;
        }
        result.push_back(qRound(static_cast<double>(clusterSum) / clusterCount));
        clusterSum = row;
        clusterCount = 1;
        clusterLast = row;
    }
    result.push_back(qRound(static_cast<double>(clusterSum) / clusterCount));
    return result;
}

PeriodicRunMatch findPeriodicRows(
    const QVector<int>& rows,
    int imageHeight,
    int minimumMatchCount,
    double maximumSpacingRatio)
{
    PeriodicRunMatch best;
    if (rows.size() < minimumMatchCount || imageHeight <= 0 || minimumMatchCount < 2) {
        return best;
    }

    const int minimumSpacing = std::max(6, qRound(imageHeight * 0.009));
    const int maximumSpacing = std::max(minimumSpacing, qRound(imageHeight * maximumSpacingRatio));
    for (int spacing = minimumSpacing; spacing <= maximumSpacing; ++spacing) {
        const double tolerance = std::max(2.0, spacing * 0.16);
        for (const int anchor : rows) {
            QHash<int, double> residualByStep;
            QHash<int, int> rowByStep;
            for (const int row : rows) {
                const double exactStep = static_cast<double>(row - anchor) / spacing;
                const int step = qRound(exactStep);
                const double residual = std::abs((row - anchor) - step * spacing);
                if (residual > tolerance) {
                    continue;
                }
                const auto existing = residualByStep.constFind(step);
                if (existing == residualByStep.cend() || residual < existing.value()) {
                    residualByStep.insert(step, residual);
                    rowByStep.insert(step, row);
                }
            }

            if (residualByStep.size() < minimumMatchCount) {
                continue;
            }
            const QList<int> steps = residualByStep.keys();
            const auto minimumMaximum = std::minmax_element(steps.cbegin(), steps.cend());
            const int minimumStep = *minimumMaximum.first;
            const int maximumStep = *minimumMaximum.second;
            const int expectedCount = maximumStep - minimumStep + 1;
            if (expectedCount <= 0) {
                continue;
            }
            const double density = static_cast<double>(residualByStep.size()) / expectedCount;
            if (density < 0.68) {
                continue;
            }

            double residualTotal = 0.0;
            for (auto iterator = residualByStep.cbegin(); iterator != residualByStep.cend(); ++iterator) {
                residualTotal += iterator.value();
            }
            const double meanResidual = residualTotal / residualByStep.size();
            const double score = residualByStep.size() * 20.0
                + density * 12.0
                - meanResidual * 3.0;
            if (score <= best.score) {
                continue;
            }

            best.valid = true;
            best.spacingPixels = spacing;
            best.firstTickY = rowByStep.value(minimumStep, anchor + minimumStep * spacing);
            best.tickCount = residualByStep.size();
            best.density = density;
            best.meanResidual = meanResidual;
            best.score = score;
        }
    }
    return best;
}

}  // namespace

bool UltrasoundScaleCalibration::isValid() const
{
    return std::isfinite(pixelsPerMillimeter)
        && pixelsPerMillimeter > 0.1
        && std::isfinite(rulerZeroYNormalized)
        && rulerZeroYNormalized >= 0.0
        && rulerZeroYNormalized <= 1.0
        && detectedTickCount >= (labelsEveryCentimeter ? 4 : 8)
        && confidence >= 0.65;
}

QRectF UltrasoundScaleCalibration::drawingZoneNormalized(
    const QSize& imageSize,
    const QSizeF& physicalSizeMm) const
{
    if (!isValid() || imageSize.width() <= 0 || imageSize.height() <= 0
        || physicalSizeMm.width() <= 0.0 || physicalSizeMm.height() <= 0.0) {
        return {};
    }

    const double normalizedWidth = std::min(
        1.0,
        physicalSizeMm.width() * pixelsPerMillimeter / imageSize.width());
    const double normalizedHeight = std::min(
        1.0,
        physicalSizeMm.height() * pixelsPerMillimeter / imageSize.height());
    const double left = std::clamp(0.5 - normalizedWidth * 0.5, 0.0, 1.0 - normalizedWidth);
    const double top = std::clamp(rulerZeroYNormalized, 0.0, 1.0 - normalizedHeight);
    return QRectF(left, top, normalizedWidth, normalizedHeight);
}

UltrasoundScaleCalibration detectUltrasoundScale(const QImage& frame)
{
    UltrasoundScaleCalibration calibration;
    if (frame.width() < 320 || frame.height() < 240) {
        return calibration;
    }

    const QImage grayscale = frame.convertToFormat(QImage::Format_Grayscale8);
    const int width = grayscale.width();
    const int height = grayscale.height();
    const int searchLeft = qRound(width * 0.60);
    const int searchRight = qRound(width * 0.94);
    const int searchTop = qRound(height * 0.08);
    const int searchBottom = qRound(height * 0.92);
    const int minimumRunWidth = std::max(3, width / 1200);
    const int maximumRunWidth = std::max(18, width / 55);
    constexpr int brightnessThreshold = 128;

    QVector<HorizontalRun> runs;
    for (int y = searchTop; y < searchBottom; ++y) {
        const uchar* row = grayscale.constScanLine(y);
        int x = searchLeft;
        while (x < searchRight) {
            while (x < searchRight && row[x] < brightnessThreshold) {
                ++x;
            }
            const int start = x;
            while (x < searchRight && row[x] >= brightnessThreshold) {
                ++x;
            }
            const int runWidth = x - start;
            if (runWidth >= minimumRunWidth && runWidth <= maximumRunWidth) {
                runs.push_back(HorizontalRun {start + runWidth / 2, y, runWidth});
            }
        }
    }

    if (runs.isEmpty()) {
        return calibration;
    }

    const int xBinWidth = std::max(3, width / 900);
    QHash<int, QVector<int>> rowsByXBin;
    for (const HorizontalRun& run : runs) {
        rowsByXBin[run.centerX / xBinWidth].push_back(run.y);
    }

    const auto findBestInBins = [height, xBinWidth](
                                    const QHash<int, QVector<int>>& rowsByBin,
                                    int minimumMatchCount,
                                    double maximumSpacingRatio) {
        PeriodicRunMatch best;
        for (auto iterator = rowsByBin.cbegin(); iterator != rowsByBin.cend(); ++iterator) {
            QVector<int> candidateRows;
            for (int neighboringBin = iterator.key() - 1; neighboringBin <= iterator.key() + 1; ++neighboringBin) {
                candidateRows += rowsByBin.value(neighboringBin);
            }
            candidateRows = uniqueRows(candidateRows, std::max(2, height / 600));
            const PeriodicRunMatch candidate = findPeriodicRows(
                candidateRows,
                height,
                minimumMatchCount,
                maximumSpacingRatio);
            if (candidate.valid && candidate.score > best.score) {
                best = candidate;
                best.centerX = iterator.key() * xBinWidth + xBinWidth / 2;
            }
        }
        return best;
    };

    // Normal layouts expose at least eight one-centimetre marks. At maximum
    // zoom there are only 0/1/2/3/4 major labels and optional half-centimetre
    // dots. Capturing/resizing changes their apparent widths, so do not rely
    // on one fixed width threshold: try several thresholds and accept a major
    // sequence only when its interval is twice the dense dot interval.
    const PeriodicRunMatch denseTickMatch = findBestInBins(rowsByXBin, 8, 0.10);
    PeriodicRunMatch majorTickMatch;
    const int maximumMajorThreshold = std::min(12, maximumRunWidth);
    for (int minimumMajorWidth = 4;
         minimumMajorWidth <= maximumMajorThreshold;
         ++minimumMajorWidth) {
        QHash<int, QVector<int>> candidateRowsByXBin;
        for (const HorizontalRun& run : runs) {
            if (run.width >= minimumMajorWidth) {
                candidateRowsByXBin[run.centerX / xBinWidth].push_back(run.y);
            }
        }

        const PeriodicRunMatch candidate = findBestInBins(candidateRowsByXBin, 4, 0.22);
        const bool geometryMatchesRuler = candidate.valid
            && candidate.firstTickY >= height * 0.10
            && candidate.firstTickY <= height * 0.45
            && candidate.spacingPixels >= height * 0.05
            && candidate.centerX >= width * 0.70
            && candidate.centerX <= width * 0.90;
        if (!geometryMatchesRuler) {
            continue;
        }

        bool intervalMatchesMaximumZoom = !denseTickMatch.valid;
        if (denseTickMatch.valid) {
            const double spacingRatio = candidate.spacingPixels / denseTickMatch.spacingPixels;
            intervalMatchesMaximumZoom = spacingRatio >= 1.70 && spacingRatio <= 2.30;
        }
        if (intervalMatchesMaximumZoom && candidate.score > majorTickMatch.score) {
            majorTickMatch = candidate;
        }
    }
    const bool labelsEveryCentimeter = majorTickMatch.valid;
    const PeriodicRunMatch best = labelsEveryCentimeter ? majorTickMatch : denseTickMatch;

    if (!best.valid) {
        return calibration;
    }

    calibration.pixelsPerMillimeter = best.spacingPixels / kScaleMarkMillimeters;
    calibration.rulerZeroYNormalized = std::clamp(best.firstTickY / height, 0.0, 1.0);
    calibration.detectedTickCount = best.tickCount;
    calibration.labelsEveryCentimeter = labelsEveryCentimeter;
    const double countConfidence = labelsEveryCentimeter
        ? std::min(1.0, (best.tickCount - 2) / 2.0)
        : std::min(1.0, (best.tickCount - 4) / 6.0);
    calibration.confidence = std::clamp(
        best.density * countConfidence
            * (1.0 - std::min(0.5, best.meanResidual / std::max(1.0, best.spacingPixels))),
        0.0,
        1.0);
    return calibration;
}

}  // namespace panthera::modules
