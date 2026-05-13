#include <QtTest/QtTest>

#include <QCoreApplication>

#include "core/application/application_context.h"
#include "core/application/event_bus.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "modules/shared/mock_ultrasound_view.h"
#include "modules/shared/therapy_imaging_algorithms.h"
#include "modules/treatment/treatment_page.h"

using namespace panthera::core;
using namespace panthera::modules;

namespace {

QVector<AnnotationStroke> buildSampleAnnotations()
{
    AnnotationStroke stroke;
    stroke.color = QColor(163, 239, 76);
    stroke.normalizedPoints = {
        QPointF(0.30, 0.30),
        QPointF(0.60, 0.28),
        QPointF(0.72, 0.52),
        QPointF(0.56, 0.72),
        QPointF(0.34, 0.64),
        QPointF(0.30, 0.30)
    };
    return {stroke};
}

TherapyPlan buildPreviewPlan(TreatmentPattern pattern)
{
    const QVector<AnnotationStroke> annotations = buildSampleAnnotations();
    const QVector<TherapyPoint> targets = generateTherapyTargetsFromAnnotations(
        annotations,
        pattern,
        3.0,
        0.3,
        400.0);

    TherapyPlan plan;
    plan.id = QStringLiteral("TEST-PLAN");
    plan.patientId = QStringLiteral("P-TEST");
    plan.name = QStringLiteral("Regression Preview Plan");
    plan.pattern = pattern;
    plan.plannedPowerWatts = 400.0;
    plan.spacingMm = 3.0;
    plan.dwellSeconds = 0.3;

    TherapySegment segment;
    segment.id = QStringLiteral("TEST-SEGMENT");
    segment.orderIndex = 0;
    segment.label = QStringLiteral("Preview Segment");
    segment.points = targets;
    for (int index = 0; index < targets.size(); ++index) {
        segment.plannedDurationSeconds += targets.at(index).dwellSeconds;
    }

    plan.segments.push_back(segment);
    return plan;
}

}  // namespace

class PlanningPageRegressionTests final : public QObject {
    Q_OBJECT

private slots:
    void linePreviewRenderDoesNotCrash();
    void pointPreviewRenderDoesNotCrash();
    void treatmentPageAcceptsGeneratedLinePlan();
};

void PlanningPageRegressionTests::linePreviewRenderDoesNotCrash()
{
    const QVector<AnnotationStroke> annotations = buildSampleAnnotations();
    const TherapyPlan plan = buildPreviewPlan(TreatmentPattern::Line);

    QVERIFY(!plan.segments.isEmpty());
    QVERIFY(!plan.segments.constFirst().points.isEmpty());

    MockUltrasoundView preview;
    preview.resize(960, 720);
    preview.setAnnotationStrokes(annotations);
    preview.setPlan(plan);
    preview.show();
    QCoreApplication::processEvents();

    QPixmap pixmap(preview.size());
    pixmap.fill(Qt::transparent);
    preview.render(&pixmap);

    QVERIFY(!pixmap.isNull());
}

void PlanningPageRegressionTests::pointPreviewRenderDoesNotCrash()
{
    const QVector<AnnotationStroke> annotations = buildSampleAnnotations();
    const TherapyPlan plan = buildPreviewPlan(TreatmentPattern::Point);

    QVERIFY(!plan.segments.isEmpty());
    QVERIFY(!plan.segments.constFirst().points.isEmpty());

    MockUltrasoundView preview;
    preview.resize(960, 720);
    preview.setAnnotationStrokes(annotations);
    preview.setPlan(plan);
    preview.show();
    QCoreApplication::processEvents();

    QPixmap pixmap(preview.size());
    pixmap.fill(Qt::transparent);
    preview.render(&pixmap);

    QVERIFY(!pixmap.isNull());
}

void PlanningPageRegressionTests::treatmentPageAcceptsGeneratedLinePlan()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    TreatmentPage treatmentPage(&context, &safetyKernel, &auditService, nullptr, nullptr);
    treatmentPage.resize(1200, 900);
    treatmentPage.show();

    const PatientRecord patient {
        QStringLiteral("P-TEST"),
        QStringLiteral("Test Patient"),
        42,
        QStringLiteral("女"),
        QStringLiteral("回归测试患者"),
        QStringLiteral("13800000000"),
    };

    context.selectPatient(patient);
    context.setActivePlan(buildPreviewPlan(TreatmentPattern::Line));
    QCoreApplication::processEvents();

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().pattern, TreatmentPattern::Line);
    QVERIFY(!context.activePlan().segments.isEmpty());
    QVERIFY(!context.activePlan().segments.constFirst().points.isEmpty());
}

QTEST_MAIN(PlanningPageRegressionTests)

#include "planning_page_regression_tests.moc"
