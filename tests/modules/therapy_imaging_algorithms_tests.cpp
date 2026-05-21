#include <cmath>
#include <QtTest/QtTest>
#include <QLineF>

#include "modules/shared/therapy_imaging_algorithms.h"

using namespace panthera::core;
using namespace panthera::modules;

class TherapyImagingAlgorithmsTests final : public QObject {
    Q_OBJECT

private slots:
    void normalizeClosedAnnotationStrokeSmoothsDenseLoop();
    void normalizeClosedAnnotationsClosesOpenStroke();
    void annotationRegionAreaMm2MergesMultipleStrokes();
    void extractContourFromAnnotationsUsesLargestStroke();
    void generateTherapyTargetsWithinContourStaysInsideContour();
    void generatePointTargetsPackAcrossContourRows();
    void generatePointTargetsIncludeContourBoundary();
    void generatePointTargetsCoverNearContourEdgeBands();
    void generatePointTargetsClearLineTreatmentMetadata();
    void generateLineTargetsCreateContinuousHorizontalSegments();
    void generateLineTargetsUseSerpentineOrder();
    void generateTherapyTargetsFromAnnotationsCreatesHorizontalLineTracks();
};

void TherapyImagingAlgorithmsTests::normalizeClosedAnnotationStrokeSmoothsDenseLoop()
{
    AnnotationStroke denseStroke;
    denseStroke.normalizedPoints = {
        QPointF(0.20, 0.22),
        QPointF(0.48, 0.18),
        QPointF(0.78, 0.24),
        QPointF(0.82, 0.50),
        QPointF(0.72, 0.78),
        QPointF(0.42, 0.82),
        QPointF(0.18, 0.66),
        QPointF(0.16, 0.40)
    };

    const AnnotationStroke normalized = normalizeClosedAnnotationStroke(denseStroke);

    QCOMPARE(normalized.normalizedPoints.size(), 9);
    QCOMPARE(normalized.normalizedPoints.first(), normalized.normalizedPoints.last());
    QVERIFY(QLineF(normalized.normalizedPoints.at(2), denseStroke.normalizedPoints.at(2)).length() > 0.005);
}

void TherapyImagingAlgorithmsTests::normalizeClosedAnnotationsClosesOpenStroke()
{
    AnnotationStroke openStroke;
    openStroke.normalizedPoints = {
        QPointF(0.20, 0.20),
        QPointF(0.80, 0.20),
        QPointF(0.78, 0.74),
        QPointF(0.24, 0.76)
    };

    const QVector<AnnotationStroke> normalized = normalizeClosedAnnotations({openStroke});

    QCOMPARE(normalized.size(), 1);
    QCOMPARE(normalized.first().normalizedPoints.size(), 5);
    QCOMPARE(normalized.first().normalizedPoints.first(), normalized.first().normalizedPoints.last());
}

void TherapyImagingAlgorithmsTests::annotationRegionAreaMm2MergesMultipleStrokes()
{
    AnnotationStroke leftStroke;
    leftStroke.normalizedPoints = {
        QPointF(0.18, 0.28),
        QPointF(0.42, 0.24),
        QPointF(0.44, 0.54),
        QPointF(0.20, 0.58)
    };

    AnnotationStroke rightStroke;
    rightStroke.normalizedPoints = {
        QPointF(0.54, 0.34),
        QPointF(0.82, 0.32),
        QPointF(0.80, 0.72),
        QPointF(0.56, 0.76)
    };

    const double mergedAreaMm2 = annotationRegionAreaMm2({leftStroke, rightStroke});
    const QVector<TherapyPoint> targets = generateTherapyTargetsFromAnnotations(
        {leftStroke, rightStroke},
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    QVERIFY(mergedAreaMm2 > 300.0);
    QVERIFY(!targets.isEmpty());

    bool hasLeftRegionTarget = false;
    bool hasRightRegionTarget = false;
    for (const TherapyPoint& point : targets) {
        hasLeftRegionTarget = hasLeftRegionTarget || point.positionMm.x() < -5.0;
        hasRightRegionTarget = hasRightRegionTarget || point.positionMm.x() > 5.0;
    }

    QVERIFY(hasLeftRegionTarget);
    QVERIFY(hasRightRegionTarget);
}

void TherapyImagingAlgorithmsTests::extractContourFromAnnotationsUsesLargestStroke()
{
    AnnotationStroke smallStroke;
    smallStroke.normalizedPoints = {
        QPointF(0.42, 0.42),
        QPointF(0.58, 0.42),
        QPointF(0.58, 0.58),
        QPointF(0.42, 0.58)
    };

    AnnotationStroke largeStroke;
    largeStroke.normalizedPoints = {
        QPointF(0.18, 0.24),
        QPointF(0.76, 0.22),
        QPointF(0.82, 0.62),
        QPointF(0.34, 0.80)
    };

    const QVector<QPointF> contour = extractContourFromAnnotations({smallStroke, largeStroke});

    QVERIFY(contour.size() >= 5);
    QCOMPARE(contour.first(), contour.last());
    QVERIFY(contourAreaMm2(contour) > 900.0);
}

void TherapyImagingAlgorithmsTests::generateTherapyTargetsWithinContourStaysInsideContour()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(12.0, 0.0),
        QPointF(12.0, 4.0),
        QPointF(4.0, 4.0),
        QPointF(4.0, 12.0),
        QPointF(0.0, 12.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> targets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Point,
        2.0,
        0.3,
        400.0);

    QVERIFY(!targets.isEmpty());

    bool hasPointOutsideContour = false;
    bool hasPointInsideRemovedCorner = false;
    for (const TherapyPoint& target : targets) {
        if (!contourContainsPointMm(contour, target.positionMm, 0.2)) {
            hasPointOutsideContour = true;
        }
        if (target.positionMm.x() > 4.1 && target.positionMm.y() > 4.1) {
            hasPointInsideRemovedCorner = true;
        }
    }

    QVERIFY(!hasPointOutsideContour);
    QVERIFY(!hasPointInsideRemovedCorner);
}

void TherapyImagingAlgorithmsTests::generatePointTargetsPackAcrossContourRows()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(14.0, 0.0),
        QPointF(14.0, 10.0),
        QPointF(0.0, 10.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> targets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    QVERIFY(targets.size() >= 6);

    QVector<int> rowKeys;
    for (const TherapyPoint& point : targets) {
        QVERIFY(contourContainsPointMm(contour, point.positionMm, 0.2));
        QCOMPARE(point.lineGroupIndex, -1);
        QVERIFY(!point.lineStart);
        QVERIFY(!point.lineEnd);

        const int rowKey = static_cast<int>(std::round(point.positionMm.y() * 10.0));
        if (!rowKeys.contains(rowKey)) {
            rowKeys.push_back(rowKey);
        }
    }

    QVERIFY(rowKeys.size() >= 3);
}

void TherapyImagingAlgorithmsTests::generatePointTargetsIncludeContourBoundary()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(14.0, 0.0),
        QPointF(14.0, 10.0),
        QPointF(0.0, 10.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> targets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    bool hasInteriorTarget = false;
    bool hasBoundaryTarget = false;
    for (const TherapyPoint& point : targets) {
        QVERIFY(contourContainsPointMm(contour, point.positionMm, 0.2));
        hasInteriorTarget = hasInteriorTarget
            || (point.positionMm.x() > 1.0 && point.positionMm.x() < 13.0
                && point.positionMm.y() > 1.0 && point.positionMm.y() < 9.0);
        hasBoundaryTarget = hasBoundaryTarget
            || std::abs(point.positionMm.x()) <= 0.2
            || std::abs(point.positionMm.x() - 14.0) <= 0.2
            || std::abs(point.positionMm.y()) <= 0.2
            || std::abs(point.positionMm.y() - 10.0) <= 0.2;
    }

    QVERIFY(hasInteriorTarget);
    QVERIFY(hasBoundaryTarget);
}

void TherapyImagingAlgorithmsTests::generatePointTargetsCoverNearContourEdgeBands()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(10.0, 0.0),
        QPointF(10.0, 6.0),
        QPointF(0.0, 6.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> targets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    bool hasInteriorTargetNearBoundary = false;
    for (const TherapyPoint& point : targets) {
        QVERIFY(contourContainsPointMm(contour, point.positionMm, 0.2));
        hasInteriorTargetNearBoundary = hasInteriorTargetNearBoundary
            || (point.positionMm.x() > 0.4 && point.positionMm.x() < 1.5
                && point.positionMm.y() > 0.4 && point.positionMm.y() < 5.6);
    }

    QVERIFY(hasInteriorTargetNearBoundary);
}

void TherapyImagingAlgorithmsTests::generatePointTargetsClearLineTreatmentMetadata()
{
    AnnotationStroke stroke;
    stroke.normalizedPoints = {
        QPointF(0.26, 0.24),
        QPointF(0.72, 0.22),
        QPointF(0.78, 0.62),
        QPointF(0.34, 0.78)
    };

    const QVector<TherapyPoint> pointTargets = generateTherapyTargetsFromAnnotations(
        {stroke},
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    QVERIFY(!pointTargets.isEmpty());
    for (const TherapyPoint& point : pointTargets) {
        QCOMPARE(point.lineGroupIndex, -1);
        QCOMPARE(point.lineSampleIndex, 0);
        QVERIFY(!point.lineStart);
        QVERIFY(!point.lineEnd);
    }
}

void TherapyImagingAlgorithmsTests::generateLineTargetsCreateContinuousHorizontalSegments()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(16.0, 0.0),
        QPointF(16.0, 9.0),
        QPointF(0.0, 9.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> lineTargets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Line,
        3.0,
        0.3,
        400.0);

    QVERIFY(!lineTargets.isEmpty());
    QVERIFY(therapyLineGroupCount(lineTargets) >= 3);

    int activeGroupIndex = -1;
    qreal activeY = 0.0;
    qreal firstX = 0.0;
    qreal lastX = 0.0;
    int groupPointCount = 0;
    bool hasWideHorizontalSegment = false;

    const auto verifyActiveGroup = [&]() {
        if (activeGroupIndex < 0) {
            return;
        }

        QVERIFY(groupPointCount >= 2);
        QVERIFY(std::abs(lastX - firstX) >= 8.0);
    };

    for (const TherapyPoint& point : lineTargets) {
        QVERIFY(point.lineGroupIndex >= 0);
        QVERIFY(contourContainsPointMm(contour, point.positionMm, 0.2));
        if (point.lineGroupIndex != activeGroupIndex) {
            verifyActiveGroup();
            activeGroupIndex = point.lineGroupIndex;
            activeY = point.positionMm.y();
            firstX = point.positionMm.x();
            groupPointCount = 0;
            QVERIFY(point.lineStart);
        } else {
            QVERIFY(std::abs(point.positionMm.y() - activeY) < 0.001);
        }

        lastX = point.positionMm.x();
        ++groupPointCount;
        hasWideHorizontalSegment = hasWideHorizontalSegment || std::abs(lastX - firstX) >= 8.0;
    }

    verifyActiveGroup();
    QVERIFY(hasWideHorizontalSegment);
}

void TherapyImagingAlgorithmsTests::generateLineTargetsUseSerpentineOrder()
{
    const QVector<QPointF> contour {
        QPointF(0.0, 0.0),
        QPointF(18.0, 0.0),
        QPointF(18.0, 12.0),
        QPointF(0.0, 12.0),
        QPointF(0.0, 0.0)
    };

    const QVector<TherapyPoint> lineTargets = generateTherapyTargetsWithinContour(
        contour,
        TreatmentPattern::Line,
        3.0,
        0.3,
        400.0);

    QVector<QVector<TherapyPoint>> groups;
    for (const TherapyPoint& point : lineTargets) {
        if (groups.isEmpty() || groups.last().first().lineGroupIndex != point.lineGroupIndex) {
            groups.push_back({});
        }
        groups.last().push_back(point);
    }

    QVERIFY(groups.size() >= 3);
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const QVector<TherapyPoint>& group = groups.at(groupIndex);
        QVERIFY(group.size() >= 2);
        if (groupIndex > 0) {
            QVERIFY(group.first().positionMm.y() >= groups.at(groupIndex - 1).first().positionMm.y() - 0.001);
        }

        const bool shouldMoveLeftToRight = groupIndex % 2 == 0;
        QVERIFY(shouldMoveLeftToRight
            ? group.first().positionMm.x() < group.last().positionMm.x()
            : group.first().positionMm.x() > group.last().positionMm.x());

        for (int sampleIndex = 0; sampleIndex < group.size(); ++sampleIndex) {
            QCOMPARE(group.at(sampleIndex).lineSampleIndex, sampleIndex);
            QCOMPARE(group.at(sampleIndex).lineStart, sampleIndex == 0);
            QCOMPARE(group.at(sampleIndex).lineEnd, sampleIndex == group.size() - 1);
        }
    }
}

void TherapyImagingAlgorithmsTests::generateTherapyTargetsFromAnnotationsCreatesHorizontalLineTracks()
{
    AnnotationStroke stroke;
    stroke.normalizedPoints = {
        QPointF(0.24, 0.20),
        QPointF(0.70, 0.18),
        QPointF(0.82, 0.46),
        QPointF(0.74, 0.78),
        QPointF(0.36, 0.82),
        QPointF(0.18, 0.56)
    };

    const QVector<TherapyPoint> lineTargets = generateTherapyTargetsFromAnnotations(
        {stroke},
        TreatmentPattern::Line,
        3.0,
        0.3,
        400.0);

    QVERIFY(!lineTargets.isEmpty());
    QVERIFY(therapyLineGroupCount(lineTargets) >= 3);

    int previousGroupIndex = -1;
    qreal activeY = 0.0;
    bool activeDirectionKnown = false;
    bool activeIncreasing = true;
    int pointCountInCurrentGroup = 0;
    for (int index = 0; index < lineTargets.size(); ++index) {
        const TherapyPoint& point = lineTargets.at(index);
        QVERIFY(point.lineGroupIndex >= 0);
        if (point.lineGroupIndex != previousGroupIndex) {
            if (previousGroupIndex >= 0) {
                QVERIFY(pointCountInCurrentGroup >= 2);
            }
            previousGroupIndex = point.lineGroupIndex;
            activeY = point.positionMm.y();
            activeDirectionKnown = false;
            pointCountInCurrentGroup = 0;
            QVERIFY(point.lineStart);
        } else {
            QVERIFY(std::abs(point.positionMm.y() - activeY) < 0.001);
        }

        if (pointCountInCurrentGroup > 0) {
            const TherapyPoint& previousPoint = lineTargets.at(index - 1);
            if (!activeDirectionKnown && std::abs(point.positionMm.x() - previousPoint.positionMm.x()) > 0.001) {
                activeIncreasing = point.positionMm.x() > previousPoint.positionMm.x();
                activeDirectionKnown = true;
            }
            if (activeDirectionKnown) {
                QVERIFY(activeIncreasing
                    ? point.positionMm.x() >= previousPoint.positionMm.x() - 0.001
                    : point.positionMm.x() <= previousPoint.positionMm.x() + 0.001);
            }
        }

        ++pointCountInCurrentGroup;
    }

    QVERIFY(pointCountInCurrentGroup >= 2);
    QVERIFY(lineTargets.constLast().lineEnd);
}

QTEST_GUILESS_MAIN(TherapyImagingAlgorithmsTests)

#include "therapy_imaging_algorithms_tests.moc"
