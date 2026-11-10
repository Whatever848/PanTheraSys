#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>

namespace panthera::modules {

struct UltrasoundScaleCalibration {
    double pixelsPerMillimeter {0.0};
    double rulerZeroYNormalized {0.0};
    int detectedTickCount {0};
    double confidence {0.0};
    bool labelsEveryCentimeter {false};

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QRectF drawingZoneNormalized(
        const QSize& imageSize,
        const QSizeF& physicalSizeMm = QSizeF(40.0, 100.0)) const;
};

// This follows the legacy D:/PanSoftware/bilichishibie detector: inspect the
// right-side depth ruler and its short marks. In normal depth mode the numeric
// labels are 0/5/10/15..., while at maximum zoom they become 0/1/2/3/4 with a
// half-centimetre dot between labels. Periodicity and mark-length validation
// distinguish the two layouts before converting pixels to millimetres.
[[nodiscard]] UltrasoundScaleCalibration detectUltrasoundScale(const QImage& frame);

}  // namespace panthera::modules
