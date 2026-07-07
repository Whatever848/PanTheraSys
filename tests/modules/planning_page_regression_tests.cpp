#include <QtTest/QtTest>

#include <cmath>

#include <QCoreApplication>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include "core/application/application_context.h"
#include "core/application/event_bus.h"
#include "core/safety/safety_kernel.h"
#include "core/services/audit_service.h"
#include "adapters/seed/seed_clinical_data_repository.h"
#include "adapters/sim/simulation_device_facade.h"
#include "modules/shell/main_window.h"
#include "modules/planning/planning_page.h"
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

QVector<AnnotationStroke> buildSeparatedAnnotations()
{
    AnnotationStroke leftStroke;
    leftStroke.color = QColor(163, 239, 76);
    leftStroke.normalizedPoints = {
        QPointF(0.14, 0.30),
        QPointF(0.28, 0.28),
        QPointF(0.34, 0.46),
        QPointF(0.26, 0.62),
        QPointF(0.12, 0.54),
        QPointF(0.14, 0.30)
    };

    AnnotationStroke rightStroke;
    rightStroke.color = QColor(255, 177, 75);
    rightStroke.normalizedPoints = {
        QPointF(0.62, 0.34),
        QPointF(0.78, 0.32),
        QPointF(0.84, 0.50),
        QPointF(0.74, 0.68),
        QPointF(0.60, 0.56),
        QPointF(0.62, 0.34)
    };

    return {leftStroke, rightStroke};
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

TherapyPlan buildLayeredPlan()
{
    TherapyPlan plan;
    plan.id = QStringLiteral("TEST-LAYER-PLAN");
    plan.patientId = QStringLiteral("P-LAYER-TEST");
    plan.name = QStringLiteral("Layered Regression Plan");
    plan.pattern = TreatmentPattern::Point;
    plan.approvalState = ApprovalState::Approved;
    plan.plannedPowerWatts = 400.0;
    plan.spacingMm = 3.0;
    plan.dwellSeconds = 0.3;
    plan.createdAt = QDateTime::currentDateTime();
    plan.approvedAt = plan.createdAt;
    plan.approvedBy = QStringLiteral("Regression");

    const auto makePoint = [](int index, double x, double y) {
        TherapyPoint point;
        point.index = index;
        point.positionMm = QPointF(x, y);
        point.dwellSeconds = 0.3;
        point.powerWatts = 400.0;
        return point;
    };

    TherapySegment firstLayer;
    firstLayer.id = QStringLiteral("TEST-LAYER-01");
    firstLayer.orderIndex = 0;
    firstLayer.label = QStringLiteral("Slice 01");
    firstLayer.points = {
        makePoint(0, -6.0, -4.0),
        makePoint(1, -2.0, -4.0),
    };
    firstLayer.plannedDurationSeconds = 0.6;

    TherapySegment secondLayer;
    secondLayer.id = QStringLiteral("TEST-LAYER-02");
    secondLayer.orderIndex = 16;
    secondLayer.label = QStringLiteral("Slice 02");
    secondLayer.points = {
        makePoint(0, -6.0, 0.0),
        makePoint(1, -2.0, 0.0),
        makePoint(2, 2.0, 0.0),
    };
    secondLayer.plannedDurationSeconds = 0.9;

    plan.segments = {firstLayer, secondLayer};
    return plan;
}

}  // namespace

class PlanningPageRegressionTests final : public QObject {
    Q_OBJECT

private slots:
    void linePreviewRenderDoesNotCrash();
    void pointPreviewRenderDoesNotCrash();
    void approvalButtonApprovesDraftPlan();
    void planningPageSectionsAreCollapsible();
    void segmentedTreatmentOptionIsHiddenOnPlanningPage();
    void activePlanChangesDoNotAutoLoadSeedHistory();
    void generateTargetsDoesNotAutoLoadSeedHistory();
    void historyPreviewWheelZoomStaysInlineAndResettable();
    void comparisonSyncSliderLinksSlicePositions();
    void treatmentLayerVisualizationFocusesPlanningComparison();
    void annotationPreviewSupportsZoomWithoutBackgroundImage();
    void annotationPreviewSupportsZoomAndPanWithBackgroundImage();
    void annotationPreviewScaleRulerRendersAcrossZoomStates();
    void currentPreviewMaximizeDialogKeepsAnnotationsVisible();
    void currentPreviewMaximizeDialogSyncsAnnotationsOnWindowClose();
    void personalizationProfilesRestoreCollapseStates();
    void generateTargetsAppliesToAllAnnotatedSlices();
    void approvingPlanKeepsCurrentSlicePreviewStable();
    void multipleAnnotationsGenerateTargetsForAllRegions();
    void treatmentPageAcceptsGeneratedLinePlan();
    void treatmentPageSelectsSingleLayer();
    void addPathFallsBackWhenRobotPoseUnavailable();
    void mainWindowSwitchesFromDashboardToPlanningWithoutCrash();
    void mainWindowSwitchesFromTreatmentToPlanningWithoutCrash();
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

void PlanningPageRegressionTests::approvalButtonApprovesDraftPlan()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    planningPage.applyPersonalizationProfile(QStringLiteral("\u5168\u5c55\u5f00"));
    QCoreApplication::processEvents();

    const PatientRecord patient {
        QStringLiteral("P-TEST"),
        QStringLiteral("Test Patient"),
        42,
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
        QStringLiteral("13800000000"),
    };
    TherapyPlan plan = buildPreviewPlan(TreatmentPattern::Point);
    plan.patientId = patient.id;
    plan.approvalState = ApprovalState::Draft;

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    context.setActivePlan(plan);
    safetyKernel.setPlanApprovalState(plan.approvalState);
    QCoreApplication::processEvents();

    auto* approvalButton = planningPage.findChild<QToolButton*>(QStringLiteral("planningApprovalButton"));
    QVERIFY(approvalButton != nullptr);
    QCOMPARE(approvalButton->property("approvalState").toString(), QStringLiteral("pending"));

    approvalButton->click();
    QCoreApplication::processEvents();

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().approvalState, ApprovalState::Approved);
    QVERIFY(context.activePlan().approvedAt.isValid());
    QCOMPARE(approvalButton->property("approvalState").toString(), QStringLiteral("approved"));

    TreatmentPage treatmentPage(&context, &safetyKernel, &auditService, nullptr, nullptr);
    treatmentPage.resize(1200, 900);
    treatmentPage.show();
    QCoreApplication::processEvents();

    auto* planCombo = treatmentPage.findChild<QComboBox*>();
    QVERIFY(planCombo != nullptr);
    QCOMPARE(planCombo->currentData().toString(), context.activePlan().id);
}

void PlanningPageRegressionTests::planningPageSectionsAreCollapsible()
{
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.remove(QStringLiteral("ui/planningPersonalization/activeProfileName"));

    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    const auto previewWidgets = planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
    QVERIFY(previewWidgets.size() >= 2);
    QVERIFY(!previewWidgets.at(0)->isSyntheticImageEnabled());
    QVERIFY(!previewWidgets.at(1)->isSyntheticImageEnabled());

    const auto collapseButtons = planningPage.findChildren<QToolButton*>(QStringLiteral("planningCollapseButton"));
    QVERIFY(collapseButtons.size() >= 9);

    const auto headerMarkers = planningPage.findChildren<QLabel*>(QStringLiteral("planningHeaderIcon"));
    QVERIFY(headerMarkers.size() >= 9);
    for (const QLabel* marker : headerMarkers) {
        QCOMPARE(marker->text(), QStringLiteral("="));
    }

    const auto formLabels = planningPage.findChildren<QLabel*>(QStringLiteral("planningFormLabel"));
    QVERIFY(formLabels.size() >= 5);
    for (const QLabel* label : formLabels) {
        QVERIFY(label->minimumHeight() >= 32);
    }

    auto* totalDurationLabel = planningPage.findChild<QLabel*>(QStringLiteral("planningMetricValueLabel"));
    QVERIFY(totalDurationLabel != nullptr);
    QVERIFY(totalDurationLabel->minimumHeight() >= 32);

    const auto unitLabels = planningPage.findChildren<QLabel*>(QStringLiteral("planningMetricUnitLabel"));
    QVERIFY(unitLabels.size() >= 6);
    const QStringList expectedUnits {
        QStringLiteral("mm"),
        QStringLiteral("s"),
        QStringLiteral("min"),
        QStringLiteral("W")
    };
    for (const QString& expectedUnit : expectedUnits) {
        QVERIFY(std::any_of(unitLabels.cbegin(), unitLabels.cend(), [&expectedUnit](const QLabel* label) {
            return label->text() == expectedUnit;
        }));
    }

    QSpinBox* layerCountSpin = nullptr;
    QSpinBox* stepSpin = nullptr;
    for (QSpinBox* spin : planningPage.findChildren<QSpinBox*>(QStringLiteral("planningMetricSpin"))) {
        if (spin->maximum() == 60) {
            layerCountSpin = spin;
        } else if (spin->maximum() == 20) {
            stepSpin = spin;
        }
    }
    QVERIFY(layerCountSpin != nullptr);
    QVERIFY(stepSpin != nullptr);
    layerCountSpin->setValue(12);
    stepSpin->setValue(3);
    QCoreApplication::processEvents();
    const auto metricValueLabels = planningPage.findChildren<QLabel*>(QStringLiteral("planningMetricValueLabel"));
    QVERIFY(std::any_of(metricValueLabels.cbegin(), metricValueLabels.cend(), [](const QLabel* label) {
        return label->text() == QStringLiteral("36");
    }));

    QDoubleSpinBox* powerSpin = nullptr;
    for (QDoubleSpinBox* spin : planningPage.findChildren<QDoubleSpinBox*>(QStringLiteral("planningMetricSpin"))) {
        if (qFuzzyCompare(spin->maximum(), 800.0)) {
            powerSpin = spin;
            break;
        }
    }
    QVERIFY(powerSpin != nullptr);
    QVERIFY(!powerSpin->isHidden());
    powerSpin->setValue(520.0);
    QCoreApplication::processEvents();
    auto* powerSummaryLabel = planningPage.findChild<QLabel*>(QStringLiteral("planningPowerValueLabel"));
    QVERIFY(powerSummaryLabel != nullptr);
    QCOMPARE(powerSummaryLabel->text(), QStringLiteral("520W"));

    auto* powerSlider = planningPage.findChild<QSlider*>(QStringLiteral("planningPowerSlider"));
    QVERIFY(powerSlider != nullptr);
    QVERIFY(powerSlider->isHidden());

    auto* respiratoryTitle = planningPage.findChild<QLabel*>(QStringLiteral("planningRespiratoryTitleLabel"));
    QVERIFY(respiratoryTitle != nullptr);
    QVERIFY(respiratoryTitle->minimumHeight() >= 36);

    auto* respiratoryToggle = planningPage.findChild<QCheckBox*>(QStringLiteral("planningModeOption"));
    QVERIFY(respiratoryToggle != nullptr);
    QCOMPARE(respiratoryToggle->text(), QStringLiteral("\u547c\u5438\u8ddf\u968f"));
    QVERIFY(respiratoryToggle->isCheckable());
    QVERIFY(respiratoryToggle->minimumHeight() >= 32);

    const auto annotationColorButtons = planningPage.findChildren<QToolButton*>(QStringLiteral("planningAnnotationColorButton"));
    QVERIFY(annotationColorButtons.size() >= 4);
    QToolButton* blueAnnotationButton = nullptr;
    int checkedAnnotationButtons = 0;
    for (QToolButton* button : annotationColorButtons) {
        if (button->isChecked()) {
            ++checkedAnnotationButtons;
        }
        if (button->property("swatchColor").toString() == QStringLiteral("blue")) {
            blueAnnotationButton = button;
        }
    }
    QCOMPARE(checkedAnnotationButtons, 1);
    QVERIFY(blueAnnotationButton != nullptr);
    blueAnnotationButton->click();
    QCoreApplication::processEvents();
    QVERIFY(blueAnnotationButton->isChecked());

    const auto sliceNavButtons = planningPage.findChildren<QToolButton*>(QStringLiteral("planningSliceNavButton"));
    QCOMPARE(sliceNavButtons.size(), 4);
    for (const QToolButton* button : sliceNavButtons) {
        QVERIFY(!button->isEnabled());
    }

    QToolButton* collapsedButton = nullptr;
    for (QToolButton* button : collapseButtons) {
        if (!button->isChecked()) {
            collapsedButton = button;
            break;
        }
    }

    QVERIFY(collapsedButton != nullptr);
    collapsedButton->click();
    QCoreApplication::processEvents();
    QVERIFY(collapsedButton->isChecked());
    collapsedButton->click();
    QCoreApplication::processEvents();
    QVERIFY(!collapsedButton->isChecked());

    const auto actionButtons = planningPage.findChildren<QPushButton*>();
    for (const QPushButton* button : actionButtons) {
        QVERIFY(button->text() != QStringLiteral("+ \u6dfb\u52a0"));
        QVERIFY(button->text() != QStringLiteral("\u00d7 \u5220\u9664"));
    }

    auto* imageOpsBody = planningPage.findChild<QWidget*>(QStringLiteral("planningImageOpsBody"));
    QVERIFY(imageOpsBody != nullptr);
    QVERIFY(!imageOpsBody->isVisible());

    QWidget* imageOpsCard = imageOpsBody->parentWidget();
    QVERIFY(imageOpsCard != nullptr);
    const auto imageOpsCollapseButtons = imageOpsCard->findChildren<QToolButton*>(QStringLiteral("planningCollapseButton"));
    QCOMPARE(imageOpsCollapseButtons.size(), 1);
    imageOpsCollapseButtons.constFirst()->click();
    QCoreApplication::processEvents();
    QVERIFY(imageOpsBody->isVisible());

    bool storeButtonVisible = false;
    bool loadButtonVisible = false;
    for (const QPushButton* button : imageOpsCard->findChildren<QPushButton*>(QStringLiteral("planningActionButton"))) {
        if (button->text() == QStringLiteral("\u672c\u5730\u5b58\u50a8")) {
            storeButtonVisible = button->isVisible();
        } else if (button->text() == QStringLiteral("\u8bfb\u53d6\u56fe\u50cf")) {
            loadButtonVisible = button->isVisible();
        }
    }
    QVERIFY(storeButtonVisible);
    QVERIFY(loadButtonVisible);

    imageOpsCollapseButtons.constFirst()->click();
    QCoreApplication::processEvents();
    QVERIFY(!imageOpsBody->isVisible());

    for (const QPushButton* button : imageOpsCard->findChildren<QPushButton*>(QStringLiteral("planningActionButton"))) {
        if (button->text() == QStringLiteral("\u672c\u5730\u5b58\u50a8")
            || button->text() == QStringLiteral("\u8bfb\u53d6\u56fe\u50cf")) {
            QVERIFY(!button->isVisible());
        }
    }
}

void PlanningPageRegressionTests::segmentedTreatmentOptionIsHiddenOnPlanningPage()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QRadioButton* segmentedRadio = nullptr;
    QRadioButton* directRadio = nullptr;
    for (QRadioButton* button : planningPage.findChildren<QRadioButton*>()) {
        if (button->text() == QStringLiteral("\u5206\u6bb5\u6267\u884c")) {
            segmentedRadio = button;
        } else if (button->text() == QStringLiteral("\u76f4\u63a5\u6cbb\u7597")) {
            directRadio = button;
        }
    }

    QVERIFY(segmentedRadio != nullptr);
    QVERIFY(segmentedRadio->isHidden());
    QVERIFY(segmentedRadio->parentWidget() != nullptr);
    QVERIFY(directRadio != nullptr);
}

void PlanningPageRegressionTests::personalizationProfilesRestoreCollapseStates()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    const QString profileName = QStringLiteral("回归测试个性化方案");
    const QString storageKey = QString::fromLatin1(profileName.toUtf8().toHex());
    QSettings settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"));
    settings.remove(QStringLiteral("ui/planningPersonalization/profiles/%1").arg(storageKey));
    settings.remove(QStringLiteral("ui/planningPersonalization/activeProfileName"));

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    const auto collapseButtons = planningPage.findChildren<QToolButton*>(QStringLiteral("planningCollapseButton"));
    QVERIFY(collapseButtons.size() >= 3);

    planningPage.applyPersonalizationProfile(QStringLiteral("全折叠"));
    QCoreApplication::processEvents();
    for (QToolButton* button : collapseButtons) {
        QVERIFY(!button->isChecked());
    }

    collapseButtons.at(0)->click();
    collapseButtons.at(1)->click();
    QCoreApplication::processEvents();
    QVERIFY(collapseButtons.at(0)->isChecked());
    QVERIFY(collapseButtons.at(1)->isChecked());
    QVERIFY(!collapseButtons.at(2)->isChecked());

    QVERIFY(planningPage.saveCurrentPersonalizationProfile(profileName));
    QVERIFY(planningPage.personalizationProfileNames().contains(profileName));
    QCOMPARE(planningPage.activePersonalizationProfileName(), profileName);

    planningPage.applyPersonalizationProfile(QStringLiteral("全展开"));
    QCoreApplication::processEvents();
    for (QToolButton* button : collapseButtons) {
        QVERIFY(button->isChecked());
    }

    planningPage.applyPersonalizationProfile(profileName);
    QCoreApplication::processEvents();
    QVERIFY(collapseButtons.at(0)->isChecked());
    QVERIFY(collapseButtons.at(1)->isChecked());
    QVERIFY(!collapseButtons.at(2)->isChecked());

    collapseButtons.at(0)->click();
    QCoreApplication::processEvents();
    QCOMPARE(planningPage.activePersonalizationProfileName(), profileName);

    planningPage.applyPersonalizationProfile(QStringLiteral("\u5168\u5c55\u5f00"));
    QCoreApplication::processEvents();
    for (QToolButton* button : collapseButtons) {
        QVERIFY(button->isChecked());
    }

    planningPage.applyPersonalizationProfile(profileName);
    QCoreApplication::processEvents();
    QVERIFY(!collapseButtons.at(0)->isChecked());
    QVERIFY(collapseButtons.at(1)->isChecked());
    QVERIFY(!collapseButtons.at(2)->isChecked());

    QVERIFY(planningPage.deletePersonalizationProfile(profileName));
    QVERIFY(!planningPage.personalizationProfileNames().contains(profileName));
    QVERIFY(planningPage.activePersonalizationProfileName().isEmpty());
    QVERIFY(!planningPage.deletePersonalizationProfile(profileName));
    QVERIFY(!planningPage.deletePersonalizationProfile(QStringLiteral("鍏ㄥ睍寮€")));

    settings.remove(QStringLiteral("ui/planningPersonalization/profiles/%1").arg(storageKey));
    settings.remove(QStringLiteral("ui/planningPersonalization/activeProfileName"));
}

void PlanningPageRegressionTests::activePlanChangesDoNotAutoLoadSeedHistory()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PatientRecord patient;
    QVERIFY(repository.findPatientById(QStringLiteral("P2026001"), &patient));

    PlanningPage planningPage(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    QCoreApplication::processEvents();

    QLabel* historySummaryLabel = nullptr;
    const auto summaryLabels = planningPage.findChildren<QLabel*>(QStringLiteral("planningSliceInfoLabel"));
    for (QLabel* label : summaryLabels) {
        if (label->text().startsWith(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"))) {
            historySummaryLabel = label;
            break;
        }
    }
    QVERIFY(historySummaryLabel != nullptr);
    QVERIFY(historySummaryLabel->text().contains(QStringLiteral("\u6682\u65e0\u6570\u636e")));

    TherapyPlan plan = buildPreviewPlan(TreatmentPattern::Point);
    plan.id = QStringLiteral("TEST-SEED-ACTIVE-PLAN");
    plan.patientId = patient.id;
    context.setActivePlan(plan);
    QCoreApplication::processEvents();

    QVERIFY(historySummaryLabel->text().contains(QStringLiteral("\u6682\u65e0\u6570\u636e")));

    auto* maximizeButton = planningPage.findChild<QToolButton*>(QStringLiteral("planningMaximizeButton"));
    QVERIFY(maximizeButton != nullptr);
    QVERIFY(!maximizeButton->isEnabled());
}

void PlanningPageRegressionTests::generateTargetsDoesNotAutoLoadSeedHistory()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QPushButton* generateTargetsButton = nullptr;
    for (QPushButton* button : planningPage.findChildren<QPushButton*>(QStringLiteral("planningActionButton"))) {
        if (button->text().contains(QStringLiteral("\u751f\u6210\u9776\u70b9"))) {
            generateTargetsButton = button;
            break;
        }
    }
    QVERIFY(generateTargetsButton != nullptr);

    generateTargetsButton->click();
    QCoreApplication::processEvents();

    QVERIFY(context.hasSelectedPatient());

    QLabel* historySummaryLabel = nullptr;
    const auto summaryLabels = planningPage.findChildren<QLabel*>(QStringLiteral("planningSliceInfoLabel"));
    for (QLabel* label : summaryLabels) {
        if (label->text().startsWith(QStringLiteral("\u65e2\u5f80\u6cbb\u7597\u5f71\u50cf"))) {
            historySummaryLabel = label;
            break;
        }
    }
    QVERIFY(historySummaryLabel != nullptr);
    QVERIFY(historySummaryLabel->text().contains(QStringLiteral("\u6682\u65e0\u6570\u636e")));

    auto* maximizeButton = planningPage.findChild<QToolButton*>(QStringLiteral("planningMaximizeButton"));
    QVERIFY(maximizeButton != nullptr);
    QVERIFY(!maximizeButton->isEnabled());
}

void PlanningPageRegressionTests::historyPreviewWheelZoomStaysInlineAndResettable()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    const PatientRecord patient {
        QStringLiteral("P-HISTORY-TEST"),
        QStringLiteral("History Test Patient"),
        42,
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
        QStringLiteral("13800000000"),
    };
    QVERIFY(repository.createPatient(patient));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString imagePath = tempDir.filePath(QStringLiteral("history.png"));
    const QString nextImagePath = tempDir.filePath(QStringLiteral("history-next.png"));
    QImage image(96, 72, QImage::Format_RGB32);
    image.fill(QColor(24, 68, 104));
    QVERIFY(image.save(imagePath));
    image.fill(QColor(36, 92, 126));
    QVERIFY(image.save(nextImagePath));

    ImageSeriesRecord historyImage;
    historyImage.id = QStringLiteral("HISTORY-IMAGE-01");
    historyImage.patientId = patient.id;
    historyImage.type = QStringLiteral("History ultrasound");
    historyImage.storagePath = imagePath;
    historyImage.acquisitionDate = QDate::currentDate();
    historyImage.createdAt = QDateTime::currentDateTime();
    QVERIFY(repository.createImageSeries(historyImage));

    ImageSeriesRecord nextHistoryImage = historyImage;
    nextHistoryImage.id = QStringLiteral("HISTORY-IMAGE-02");
    nextHistoryImage.storagePath = nextImagePath;
    nextHistoryImage.createdAt = historyImage.createdAt.addSecs(60);
    QVERIFY(repository.createImageSeries(nextHistoryImage));

    PlanningPage planningPage(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    QCoreApplication::processEvents();

    auto* maximizeButton = planningPage.findChild<QToolButton*>(QStringLiteral("planningMaximizeButton"));
    QVERIFY(maximizeButton != nullptr);
    QVERIFY(!maximizeButton->isEnabled());

    QLabel* historySummaryLabel = nullptr;
    for (QLabel* label : planningPage.findChildren<QLabel*>(QStringLiteral("planningSliceInfoLabel"))) {
        if (label->text().startsWith(QStringLiteral("\u7b2c 2/2"))) {
            historySummaryLabel = label;
            break;
        }
    }
    QVERIFY(historySummaryLabel != nullptr);
    QVERIFY(!historySummaryLabel->text().contains(tempDir.path()));
    QVERIFY(historySummaryLabel->toolTip().isEmpty());

    QToolButton* historyPreviousButton = nullptr;
    QToolButton* historyNextButton = nullptr;
    for (QToolButton* button : planningPage.findChildren<QToolButton*>(QStringLiteral("planningSliceNavButton"))) {
        if (button->toolTip().contains(QStringLiteral("\u4e0a\u4e00\u5f20\u65e2\u5f80"))) {
            historyPreviousButton = button;
        } else if (button->toolTip().contains(QStringLiteral("\u4e0b\u4e00\u5f20\u65e2\u5f80"))) {
            historyNextButton = button;
        }
    }
    QVERIFY(historyPreviousButton != nullptr);
    QVERIFY(historyNextButton != nullptr);
    QVERIFY(historyPreviousButton->isEnabled());
    QVERIFY(!historyNextButton->isEnabled());
    historyPreviousButton->click();
    QCoreApplication::processEvents();
    QVERIFY(historySummaryLabel->text().startsWith(QStringLiteral("\u7b2c 1/2")));
    QVERIFY(!historyPreviousButton->isEnabled());
    QVERIFY(historyNextButton->isEnabled());

    MockUltrasoundView* historyPreview = nullptr;
    for (MockUltrasoundView* preview : planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"))) {
        if (preview->isImageZoomEnabled()) {
            historyPreview = preview;
            break;
        }
    }
    QVERIFY(historyPreview != nullptr);
    QVERIFY(qFuzzyCompare(historyPreview->imageZoomFactor(), 1.0));

    const QPointF localPosition = historyPreview->rect().center();
    const QPointF globalPosition = historyPreview->mapToGlobal(localPosition.toPoint());
    QWheelEvent wheelEvent(
        localPosition,
        globalPosition,
        QPoint(0, 0),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::ScrollUpdate,
        false);
    QApplication::sendEvent(historyPreview, &wheelEvent);
    QCoreApplication::processEvents();

    QVERIFY(historyPreview->imageZoomFactor() > 1.0);
    QVERIFY(maximizeButton->isEnabled());

    maximizeButton->click();
    QCoreApplication::processEvents();

    QVERIFY(std::abs(historyPreview->imageZoomFactor() - 1.0) < 0.001);
    QVERIFY(!maximizeButton->isEnabled());

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr && dialog->objectName() == QStringLiteral("planningHistoryPreviewDialog")) {
            QFAIL("history preview should no longer open a separate maximized dialog");
        }
    }
}

void PlanningPageRegressionTests::comparisonSyncSliderLinksSlicePositions()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    const PatientRecord patient {
        QStringLiteral("P-SYNC-SLICE-TEST"),
        QStringLiteral("Sync Slice Patient"),
        41,
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
        QStringLiteral("13800000002"),
    };
    QVERIFY(repository.createPatient(patient));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    for (int index = 0; index < 10; ++index) {
        const QString imagePath = tempDir.filePath(QStringLiteral("history-%1.png").arg(index + 1, 2, 10, QChar('0')));
        QImage image(96, 72, QImage::Format_RGB32);
        image.fill(QColor(24 + index, 68, 104));
        QVERIFY(image.save(imagePath));

        ImageSeriesRecord historyImage;
        historyImage.id = QStringLiteral("HISTORY-SYNC-%1").arg(index + 1, 2, 10, QChar('0'));
        historyImage.patientId = patient.id;
        historyImage.type = QStringLiteral("History ultrasound");
        historyImage.storagePath = imagePath;
        historyImage.acquisitionDate = QDate::currentDate();
        historyImage.createdAt = QDateTime::currentDateTime().addSecs(index);
        QVERIFY(repository.createImageSeries(historyImage));
    }

    PlanningPage planningPage(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    QCoreApplication::processEvents();

    QSpinBox* layerCountSpin = nullptr;
    for (QSpinBox* spin : planningPage.findChildren<QSpinBox*>(QStringLiteral("planningMetricSpin"))) {
        if (spin->maximum() == 60) {
            layerCountSpin = spin;
            break;
        }
    }
    QVERIFY(layerCountSpin != nullptr);
    layerCountSpin->setValue(10);
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    const auto previews = planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
    QVERIFY(previews.size() >= 2);
    QVERIFY(!previews.at(0)->isSyntheticImageEnabled());
    QVERIFY(previews.at(1)->isSyntheticImageEnabled());

    for (QLabel* label : planningPage.findChildren<QLabel*>(QStringLiteral("planningSliceInfoLabel"))) {
        QVERIFY(!label->text().contains(tempDir.path()));
        QVERIFY(!label->text().contains(QStringLiteral("staging/")));
        QVERIFY(label->toolTip().isEmpty());
    }

    const auto sliceSliders = planningPage.findChildren<QSlider*>(QStringLiteral("planningSliceSlider"));
    QVERIFY(sliceSliders.size() >= 2);
    QSlider* historySlider = sliceSliders.at(0);
    QSlider* currentSlider = sliceSliders.at(1);
    QVERIFY(historySlider != nullptr);
    QVERIFY(currentSlider != nullptr);
    QCOMPARE(historySlider->maximum(), 9);
    QCOMPARE(currentSlider->maximum(), 9);

    auto findSyncButton = [&planningPage](const QString& text) -> QPushButton* {
        for (QPushButton* button : planningPage.findChildren<QPushButton*>(QStringLiteral("planningComparisonSyncButton"))) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    };

    auto* leftStartButton = findSyncButton(QStringLiteral("左起点"));
    auto* leftEndButton = findSyncButton(QStringLiteral("左终点"));
    auto* rightStartButton = findSyncButton(QStringLiteral("右起点"));
    auto* rightEndButton = findSyncButton(QStringLiteral("右终点"));
    auto* syncCheck = planningPage.findChild<QCheckBox*>(QStringLiteral("planningComparisonSyncToggle"));
    auto* syncSlider = planningPage.findChild<QSlider*>(QStringLiteral("planningComparisonSyncSlider"));
    const auto syncNavButtons = planningPage.findChildren<QToolButton*>(QStringLiteral("planningComparisonSyncNavButton"));
    QVERIFY(leftStartButton != nullptr);
    QVERIFY(leftEndButton != nullptr);
    QVERIFY(rightStartButton != nullptr);
    QVERIFY(rightEndButton != nullptr);
    QVERIFY(syncCheck != nullptr);
    QVERIFY(syncSlider != nullptr);
    QCOMPARE(syncNavButtons.size(), 2);
    auto* syncPrevButton = syncNavButtons.at(0);
    auto* syncNextButton = syncNavButtons.at(1);
    QVERIFY(!syncCheck->isEnabled());
    QVERIFY(!syncSlider->isEnabled());
    QVERIFY(!syncPrevButton->isEnabled());
    QVERIFY(!syncNextButton->isEnabled());

    historySlider->setValue(1);
    leftStartButton->click();
    historySlider->setValue(7);
    leftEndButton->click();
    currentSlider->setValue(2);
    rightStartButton->click();
    currentSlider->setValue(8);
    rightEndButton->click();

    QVERIFY(syncCheck->isEnabled());
    QVERIFY(!syncSlider->isEnabled());
    QVERIFY(!syncPrevButton->isEnabled());
    QVERIFY(!syncNextButton->isEnabled());
    QVERIFY(syncSlider->property("syncInactive").toBool());
    QVERIFY(!historySlider->property("syncLocked").toBool());
    QVERIFY(!currentSlider->property("syncLocked").toBool());
    QVERIFY(historySlider->isEnabled());
    QVERIFY(currentSlider->isEnabled());
    syncCheck->setChecked(true);
    QCoreApplication::processEvents();
    QVERIFY(syncSlider->isEnabled());
    QVERIFY(!historySlider->isEnabled());
    QVERIFY(!currentSlider->isEnabled());
    QVERIFY(!syncSlider->property("syncInactive").toBool());
    QVERIFY(historySlider->property("syncLocked").toBool());
    QVERIFY(currentSlider->property("syncLocked").toBool());
    QCOMPARE(syncSlider->maximum(), 6);
    QCOMPARE(historySlider->value(), 1);
    QCOMPARE(currentSlider->value(), 2);

    currentSlider->setValue(3);
    QCoreApplication::processEvents();
    QCOMPARE(currentSlider->value(), 2);

    QVERIFY(!syncPrevButton->isEnabled());
    QVERIFY(syncNextButton->isEnabled());
    syncNextButton->click();
    QCoreApplication::processEvents();
    QCOMPARE(syncSlider->value(), 1);
    QCOMPARE(historySlider->value(), 2);
    QCOMPARE(currentSlider->value(), 3);
    QVERIFY(syncPrevButton->isEnabled());
    syncPrevButton->click();
    QCoreApplication::processEvents();
    QCOMPARE(syncSlider->value(), 0);
    QCOMPARE(historySlider->value(), 1);
    QCOMPARE(currentSlider->value(), 2);

    syncSlider->setValue(1);
    QCoreApplication::processEvents();
    QCOMPARE(historySlider->value(), 2);
    QCOMPARE(currentSlider->value(), 3);

    syncSlider->setValue(syncSlider->maximum());
    QCoreApplication::processEvents();
    QCOMPARE(historySlider->value(), 7);
    QCOMPARE(currentSlider->value(), 8);

    syncCheck->setChecked(false);
    QCoreApplication::processEvents();
    QVERIFY(!syncSlider->isEnabled());
    QVERIFY(!syncPrevButton->isEnabled());
    QVERIFY(!syncNextButton->isEnabled());
    QVERIFY(syncSlider->property("syncInactive").toBool());
    QVERIFY(!historySlider->property("syncLocked").toBool());
    QVERIFY(!currentSlider->property("syncLocked").toBool());
    QVERIFY(historySlider->isEnabled());
    QVERIFY(currentSlider->isEnabled());
}

void PlanningPageRegressionTests::treatmentLayerVisualizationFocusesPlanningComparison()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    const PatientRecord patient {
        QStringLiteral("P-TREATMENT-FOCUS"),
        QStringLiteral("Treatment Focus Patient"),
        45,
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
        QStringLiteral("13800000003"),
    };
    QVERIFY(repository.createPatient(patient));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    for (int index = 0; index < 4; ++index) {
        const QString imagePath = tempDir.filePath(QStringLiteral("history-focus-%1.png").arg(index + 1));
        QImage image(96, 72, QImage::Format_RGB32);
        image.fill(QColor(30 + index * 8, 72, 108));
        QVERIFY(image.save(imagePath));

        ImageSeriesRecord historyImage;
        historyImage.id = QStringLiteral("HISTORY-FOCUS-%1").arg(index + 1);
        historyImage.patientId = patient.id;
        historyImage.type = QStringLiteral("History ultrasound");
        historyImage.storagePath = imagePath;
        historyImage.acquisitionDate = QDate::currentDate();
        historyImage.createdAt = QDateTime::currentDateTime().addSecs(index);
        QVERIFY(repository.createImageSeries(historyImage));
    }

    PlanningPage planningPage(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    QCoreApplication::processEvents();

    QSpinBox* layerCountSpin = nullptr;
    for (QSpinBox* spin : planningPage.findChildren<QSpinBox*>(QStringLiteral("planningMetricSpin"))) {
        if (spin->maximum() == 60) {
            layerCountSpin = spin;
            break;
        }
    }
    QVERIFY(layerCountSpin != nullptr);
    layerCountSpin->setValue(4);
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    context.requestTreatmentLayerVisualization(QStringLiteral("FOCUS-PLAN"), 2, true);
    QCoreApplication::processEvents();

    for (QFrame* frame : planningPage.findChildren<QFrame*>(QStringLiteral("planningSidebarCard"))) {
        QVERIFY(!frame->isVisible());
    }
    for (QFrame* frame : planningPage.findChildren<QFrame*>(QStringLiteral("planningBottomCard"))) {
        QVERIFY(!frame->isVisible());
    }
    auto* leftColumnHost = planningPage.findChild<QWidget*>(QStringLiteral("planningLeftColumnHost"));
    auto* centerColumnHost = planningPage.findChild<QWidget*>(QStringLiteral("planningCenterColumnHost"));
    auto* rightColumnHost = planningPage.findChild<QWidget*>(QStringLiteral("planningRightColumnHost"));
    auto* previewFrame = planningPage.findChild<QFrame*>(QStringLiteral("planningPreviewFrame"));
    QVERIFY(leftColumnHost != nullptr);
    QVERIFY(centerColumnHost != nullptr);
    QVERIFY(rightColumnHost != nullptr);
    QVERIFY(previewFrame != nullptr);
    QVERIFY(!leftColumnHost->isVisible());
    QVERIFY(rightColumnHost->isHidden());
    QVERIFY(centerColumnHost->width() > planningPage.width() * 0.9);
    QVERIFY(previewFrame->width() > planningPage.width() * 0.85);
    auto* controlFrame = planningPage.findChild<QFrame*>(QStringLiteral("planningControlFrame"));
    QVERIFY(controlFrame != nullptr);
    QVERIFY(!controlFrame->isVisible());
    auto* annotationPanel = planningPage.findChild<QFrame*>(QStringLiteral("planningAnnotationPanel"));
    QVERIFY(annotationPanel != nullptr);
    QVERIFY(!annotationPanel->isVisible());
    const auto previewWidgets = planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
    QVERIFY(previewWidgets.size() >= 2);
    QVERIFY(previewWidgets.at(0)->isBackgroundImageStretchToFill());
    QVERIFY(previewWidgets.at(1)->isBackgroundImageStretchToFill());

    const auto sliceSliders = planningPage.findChildren<QSlider*>(QStringLiteral("planningSliceSlider"));
    QVERIFY(sliceSliders.size() >= 2);
    QCOMPARE(sliceSliders.at(0)->value(), 2);
    QCOMPARE(sliceSliders.at(1)->value(), 2);

    auto* syncCheck = planningPage.findChild<QCheckBox*>(QStringLiteral("planningComparisonSyncToggle"));
    auto* syncSlider = planningPage.findChild<QSlider*>(QStringLiteral("planningComparisonSyncSlider"));
    QVERIFY(syncCheck != nullptr);
    QVERIFY(syncSlider != nullptr);
    QVERIFY(syncCheck->isChecked());
    QVERIFY(syncSlider->isEnabled());
    QCOMPARE(syncSlider->value(), 2);

    context.requestTreatmentLayerVisualization(QStringLiteral("FOCUS-PLAN"), 2, false);
    QCoreApplication::processEvents();

    QVERIFY(controlFrame->isVisible());
    QVERIFY(annotationPanel->isVisible());
    QVERIFY(leftColumnHost->isVisible());
    QVERIFY(rightColumnHost->isVisible());
    QVERIFY(!previewWidgets.at(0)->isBackgroundImageStretchToFill());
    QVERIFY(!previewWidgets.at(1)->isBackgroundImageStretchToFill());
    for (QFrame* frame : planningPage.findChildren<QFrame*>(QStringLiteral("planningSidebarCard"))) {
        QVERIFY(frame->isVisible());
    }
    for (QFrame* frame : planningPage.findChildren<QFrame*>(QStringLiteral("planningBottomCard"))) {
        QVERIFY(frame->isVisible());
    }
}

void PlanningPageRegressionTests::annotationPreviewSupportsZoomWithoutBackgroundImage()
{
    MockUltrasoundView preview;
    preview.resize(960, 720);
    preview.setImageZoomEnabled(true);
    preview.setAnnotationEnabled(true);
    preview.setAnnotationStrokes(buildSampleAnnotations());
    preview.show();
    QCoreApplication::processEvents();

    QVERIFY(qFuzzyCompare(preview.imageZoomFactor(), 1.0));

    const QPointF localPosition = preview.rect().center();
    const QPointF globalPosition = preview.mapToGlobal(localPosition.toPoint());
    QWheelEvent wheelEvent(
        localPosition,
        globalPosition,
        QPoint(0, 0),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::ScrollUpdate,
        false);
    QApplication::sendEvent(&preview, &wheelEvent);
    QCoreApplication::processEvents();

    QVERIFY(preview.imageZoomFactor() > 1.0);
}

void PlanningPageRegressionTests::annotationPreviewSupportsZoomAndPanWithBackgroundImage()
{
    MockUltrasoundView preview;
    preview.resize(960, 720);
    preview.setImageZoomEnabled(true);
    preview.setAnnotationEnabled(true);
    preview.setAnnotationStrokes(buildSampleAnnotations());

    QImage background(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < background.height(); ++y) {
        for (int x = 0; x < background.width(); ++x) {
            background.setPixelColor(x, y, QColor((x * 255) / background.width(), 40, (y * 255) / background.height()));
        }
    }
    preview.setBackgroundImage(QPixmap::fromImage(background));
    preview.show();
    QCoreApplication::processEvents();

    QPixmap beforeZoom(preview.size());
    beforeZoom.fill(Qt::transparent);
    preview.render(&beforeZoom);

    const QPointF localPosition = preview.rect().center();
    const QPointF globalPosition = preview.mapToGlobal(localPosition.toPoint());
    QWheelEvent wheelEvent(
        localPosition,
        globalPosition,
        QPoint(0, 0),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::ScrollUpdate,
        false);
    QApplication::sendEvent(&preview, &wheelEvent);
    QCoreApplication::processEvents();

    QVERIFY(preview.imageZoomFactor() > 1.0);

    const QPointF centerBeforePan = preview.imageZoomCenterNormalized();
    QMouseEvent pressEvent(
        QEvent::MouseButtonPress,
        localPosition,
        globalPosition,
        Qt::RightButton,
        Qt::RightButton,
        Qt::NoModifier);
    QApplication::sendEvent(&preview, &pressEvent);

    const QPointF movedPosition = localPosition + QPointF(80.0, 0.0);
    const QPointF movedGlobalPosition = preview.mapToGlobal(movedPosition.toPoint());
    QMouseEvent moveEvent(
        QEvent::MouseMove,
        movedPosition,
        movedGlobalPosition,
        Qt::NoButton,
        Qt::RightButton,
        Qt::NoModifier);
    QApplication::sendEvent(&preview, &moveEvent);

    QMouseEvent releaseEvent(
        QEvent::MouseButtonRelease,
        movedPosition,
        movedGlobalPosition,
        Qt::RightButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&preview, &releaseEvent);
    QCoreApplication::processEvents();

    const QPointF centerAfterPan = preview.imageZoomCenterNormalized();
    QVERIFY(std::abs(centerAfterPan.x() - centerBeforePan.x()) > 0.0001);

    QPixmap afterPan(preview.size());
    afterPan.fill(Qt::transparent);
    preview.render(&afterPan);

    QVERIFY(beforeZoom.toImage() != afterPan.toImage());
    QVERIFY(!preview.annotationStrokes().isEmpty());
}

void PlanningPageRegressionTests::annotationPreviewScaleRulerRendersAcrossZoomStates()
{
    MockUltrasoundView preview;
    preview.resize(960, 720);
    preview.setImageZoomEnabled(true);
    preview.setScaleRulerEnabled(true);
    preview.setScaleRulerExpanded(true);
    preview.show();
    QCoreApplication::processEvents();

    QVERIFY(preview.isScaleRulerEnabled());
    QVERIFY(preview.isScaleRulerExpanded());

    QPixmap expandedPixmap(preview.size());
    expandedPixmap.fill(Qt::transparent);
    preview.render(&expandedPixmap);

    preview.setImageZoom(2.0, QPointF(0.5, 0.5));
    QCoreApplication::processEvents();
    QPixmap zoomedPixmap(preview.size());
    zoomedPixmap.fill(Qt::transparent);
    preview.render(&zoomedPixmap);

    preview.setScaleRulerExpanded(false);
    QCoreApplication::processEvents();
    QVERIFY(!preview.isScaleRulerExpanded());

    QPixmap collapsedPixmap(preview.size());
    collapsedPixmap.fill(Qt::transparent);
    preview.render(&collapsedPixmap);

    QVERIFY(expandedPixmap.toImage() != zoomedPixmap.toImage());
    QVERIFY(zoomedPixmap.toImage() != collapsedPixmap.toImage());
}

void PlanningPageRegressionTests::currentPreviewMaximizeDialogKeepsAnnotationsVisible()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    MockUltrasoundView* editablePreview = nullptr;
    const auto previews = planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
    for (MockUltrasoundView* preview : previews) {
        if (preview->isImageZoomEnabled() && preview->isVisible()) {
            editablePreview = preview;
        }
    }
    QVERIFY(editablePreview != nullptr);
    editablePreview->setAnnotationStrokes(buildSampleAnnotations());

    QToolButton* currentMaximizeButton = nullptr;
    for (QToolButton* button : planningPage.findChildren<QToolButton*>(QStringLiteral("planningMaximizeButton"))) {
        if (button->toolTip().contains(QStringLiteral("\u53f3\u4fa7"))) {
            currentMaximizeButton = button;
            break;
        }
    }
    QVERIFY(currentMaximizeButton != nullptr);

    QTimer::singleShot(100, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || dialog->objectName() != QStringLiteral("planningCurrentPreviewDialog")) {
                continue;
            }

            auto dialogPreviews = dialog->findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
            QVERIFY(!dialogPreviews.isEmpty());
            auto* dialogPreview = dialogPreviews.constFirst();
            QVERIFY(!dialogPreview->annotationStrokes().isEmpty());

            auto* dialogNavigationPanel = dialog->findChild<QFrame*>(QStringLiteral("planningDialogSliceNavigationPanel"));
            QVERIFY(dialogNavigationPanel != nullptr);
            QVERIFY(dialogNavigationPanel->isVisible());

            auto* dialogSliceSlider = dialog->findChild<QSlider*>(QStringLiteral("planningSliceSlider"));
            QVERIFY(dialogSliceSlider != nullptr);
            QVERIFY(dialogSliceSlider->isEnabled());
            QVERIFY(dialogSliceSlider->maximum() > dialogSliceSlider->minimum());
            QVERIFY(dialogSliceSlider->isVisible());
            const QRect visibleDialogRect = dialog->rect();
            const QRect navigationRect(dialogNavigationPanel->mapTo(dialog, QPoint(0, 0)), dialogNavigationPanel->size());
            QVERIFY(visibleDialogRect.contains(navigationRect.topLeft()));
            QVERIFY(visibleDialogRect.contains(navigationRect.bottomRight()));
            QVERIFY(navigationRect.top() >= dialogPreview->mapTo(dialog, QPoint(0, dialogPreview->height())).y());
            QVERIFY(visibleDialogRect.contains(dialogSliceSlider->mapTo(dialog, QPoint(0, 0))));
            QVERIFY(visibleDialogRect.contains(dialogSliceSlider->mapTo(dialog, QPoint(dialogSliceSlider->width() - 1, dialogSliceSlider->height() - 1))));

            QToolButton* dialogPreviousButton = nullptr;
            QToolButton* dialogNextButton = nullptr;
            for (QToolButton* button : dialog->findChildren<QToolButton*>(QStringLiteral("planningSliceNavButton"))) {
                if (button->toolTip().contains(QStringLiteral("\u4e0a\u4e00\u5f20\u5f53\u524d"))) {
                    dialogPreviousButton = button;
                } else if (button->toolTip().contains(QStringLiteral("\u4e0b\u4e00\u5f20\u5f53\u524d"))) {
                    dialogNextButton = button;
                }
            }
            QVERIFY(dialogPreviousButton != nullptr);
            QVERIFY(dialogNextButton != nullptr);
            QVERIFY(dialogPreviousButton->isVisible());
            QVERIFY(dialogNextButton->isVisible());
            const QRect previousButtonRect(dialogPreviousButton->mapTo(dialog, QPoint(0, 0)), dialogPreviousButton->size());
            const QRect nextButtonRect(dialogNextButton->mapTo(dialog, QPoint(0, 0)), dialogNextButton->size());
            QVERIFY(visibleDialogRect.contains(previousButtonRect.topLeft()));
            QVERIFY(visibleDialogRect.contains(previousButtonRect.bottomRight()));
            QVERIFY(visibleDialogRect.contains(nextButtonRect.topLeft()));
            QVERIFY(visibleDialogRect.contains(nextButtonRect.bottomRight()));
            const int originalValue = dialogSliceSlider->value();
            if (originalValue < dialogSliceSlider->maximum()) {
                dialogNextButton->click();
                QCOMPARE(dialogSliceSlider->value(), originalValue + 1);
            } else {
                dialogPreviousButton->click();
                QCOMPARE(dialogSliceSlider->value(), originalValue - 1);
            }
            dialog->accept();
        }
    });

    currentMaximizeButton->click();
    QCoreApplication::processEvents();
}

void PlanningPageRegressionTests::currentPreviewMaximizeDialogSyncsAnnotationsOnWindowClose()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    MockUltrasoundView* editablePreview = nullptr;
    const auto previews = planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
    for (MockUltrasoundView* preview : previews) {
        if (preview->isImageZoomEnabled() && preview->isVisible()) {
            editablePreview = preview;
        }
    }
    QVERIFY(editablePreview != nullptr);
    editablePreview->setAnnotationStrokes(buildSampleAnnotations());

    QToolButton* currentMaximizeButton = nullptr;
    for (QToolButton* button : planningPage.findChildren<QToolButton*>(QStringLiteral("planningMaximizeButton"))) {
        if (button->toolTip().contains(QStringLiteral("\u53f3\u4fa7"))) {
            currentMaximizeButton = button;
            break;
        }
    }
    QVERIFY(currentMaximizeButton != nullptr);

    QTimer::singleShot(100, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || dialog->objectName() != QStringLiteral("planningCurrentPreviewDialog")) {
                continue;
            }

            auto dialogPreviews = dialog->findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"));
            QVERIFY(!dialogPreviews.isEmpty());
            QVector<AnnotationStroke> expandedAnnotations = buildSampleAnnotations();
            AnnotationStroke extraStroke;
            extraStroke.color = QColor(255, 177, 75);
            extraStroke.normalizedPoints = {
                QPointF(0.20, 0.20),
                QPointF(0.24, 0.26),
                QPointF(0.28, 0.32)
            };
            expandedAnnotations.push_back(extraStroke);
            dialogPreviews.constFirst()->setAnnotationStrokes(expandedAnnotations);
            dialog->close();
        }
    });

    currentMaximizeButton->click();
    QCoreApplication::processEvents();

    QVERIFY(editablePreview->annotationStrokes().size() >= 2);
}

void PlanningPageRegressionTests::generateTargetsAppliesToAllAnnotatedSlices()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    MockUltrasoundView* preview = nullptr;
    for (MockUltrasoundView* candidate : planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"))) {
        if (candidate->isImageZoomEnabled() && candidate->isVisible()) {
            preview = candidate;
        }
    }
    QVERIFY(preview != nullptr);
    QListWidget* modelList = nullptr;
    for (QListWidget* list : planningPage.findChildren<QListWidget*>()) {
        if (list->count() >= 2) {
            modelList = list;
            break;
        }
    }
    QVERIFY(modelList != nullptr);

    modelList->setCurrentRow(0);
    QCoreApplication::processEvents();
    preview->setAnnotationStrokes(buildSampleAnnotations());
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "onPreviewAnnotationsChanged", Qt::DirectConnection));

    QVector<AnnotationStroke> secondSliceAnnotations = buildSampleAnnotations();
    secondSliceAnnotations.first().normalizedPoints = {
        QPointF(0.18, 0.24),
        QPointF(0.52, 0.22),
        QPointF(0.70, 0.48),
        QPointF(0.50, 0.76),
        QPointF(0.24, 0.62),
        QPointF(0.18, 0.24)
    };
    modelList->setCurrentRow(1);
    QCoreApplication::processEvents();
    preview->setAnnotationStrokes(secondSliceAnnotations);
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "onPreviewAnnotationsChanged", Qt::DirectConnection));

    auto radioButtons = planningPage.findChildren<QRadioButton*>();
    QRadioButton* lineRadio = nullptr;
    for (QRadioButton* button : radioButtons) {
        if (button->text() == QStringLiteral("\u7ebf\u6cbb\u7597")) {
            lineRadio = button;
            break;
        }
    }
    QVERIFY(lineRadio != nullptr);
    lineRadio->setChecked(true);

    QPushButton* generateTargetsButton = nullptr;
    for (QPushButton* button : planningPage.findChildren<QPushButton*>(QStringLiteral("planningActionButton"))) {
        if (button->text().contains(QStringLiteral("\u751f\u6210\u9776\u70b9"))) {
            generateTargetsButton = button;
            break;
        }
    }
    QVERIFY(generateTargetsButton != nullptr);
    generateTargetsButton->click();
    QCoreApplication::processEvents();

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().segments.size(), 2);
    QCOMPARE(context.activePlan().pattern, TreatmentPattern::Line);
    QVERIFY(!context.activePlan().segments.at(0).points.isEmpty());
    QVERIFY(!context.activePlan().segments.at(1).points.isEmpty());
}

void PlanningPageRegressionTests::approvingPlanKeepsCurrentSlicePreviewStable()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "simulateImageAcquisition", Qt::DirectConnection));
    QCoreApplication::processEvents();

    MockUltrasoundView* preview = nullptr;
    for (MockUltrasoundView* candidate : planningPage.findChildren<MockUltrasoundView*>(QStringLiteral("planningPreviewWidget"))) {
        if (candidate->isImageZoomEnabled() && candidate->isVisible()) {
            preview = candidate;
        }
    }
    QVERIFY(preview != nullptr);

    preview->setAnnotationStrokes(buildSampleAnnotations());
    QVERIFY(QMetaObject::invokeMethod(&planningPage, "onPreviewAnnotationsChanged", Qt::DirectConnection));

    QPushButton* generateTargetsButton = nullptr;
    for (QPushButton* button : planningPage.findChildren<QPushButton*>(QStringLiteral("planningActionButton"))) {
        if (button->text().contains(QStringLiteral("\u751f\u6210\u9776\u70b9"))) {
            generateTargetsButton = button;
            break;
        }
    }
    QVERIFY(generateTargetsButton != nullptr);
    generateTargetsButton->click();
    QCoreApplication::processEvents();

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().segments.size(), 1);
    const int targetCountBeforeApprove = context.activePlan().segments.constFirst().points.size();
    QVERIFY(targetCountBeforeApprove > 0);

    auto* approvalButton = planningPage.findChild<QToolButton*>(QStringLiteral("planningApprovalButton"));
    QVERIFY(approvalButton != nullptr);
    approvalButton->click();
    QCoreApplication::processEvents();

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().approvalState, ApprovalState::Approved);
    QCOMPARE(context.activePlan().segments.size(), 1);
    QCOMPARE(context.activePlan().segments.constFirst().points.size(), targetCountBeforeApprove);
}

void PlanningPageRegressionTests::multipleAnnotationsGenerateTargetsForAllRegions()
{
    const QVector<AnnotationStroke> annotations = buildSeparatedAnnotations();
    const QVector<TherapyPoint> targets = generateTherapyTargetsFromAnnotations(
        annotations,
        TreatmentPattern::Point,
        3.0,
        0.3,
        400.0);

    QVERIFY(!targets.isEmpty());

    bool hasLeftRegionTarget = false;
    bool hasRightRegionTarget = false;
    for (const TherapyPoint& point : targets) {
        if (point.positionMm.x() < 0.0) {
            hasLeftRegionTarget = true;
        }
        if (point.positionMm.x() > 0.0) {
            hasRightRegionTarget = true;
        }
    }

    QVERIFY(hasLeftRegionTarget);
    QVERIFY(hasRightRegionTarget);
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
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
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

void PlanningPageRegressionTests::treatmentPageSelectsSingleLayer()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;

    const PatientRecord patient {
        QStringLiteral("P-LAYER-TEST"),
        QStringLiteral("Layer Test Patient"),
        42,
        QStringLiteral("F"),
        QStringLiteral("Regression diagnosis"),
        QStringLiteral("13800000000"),
    };
    QVERIFY(repository.createPatient(patient));

    const TherapyPlan plan = buildLayeredPlan();
    QVERIFY(repository.createTherapyPlan(plan));

    TreatmentPage treatmentPage(&context, &safetyKernel, &auditService, &repository, nullptr);
    treatmentPage.resize(1200, 900);
    treatmentPage.show();

    context.selectPatient(patient);
    safetyKernel.setPatientSelected(true);
    QCoreApplication::processEvents();

    auto* planCombo = treatmentPage.findChild<QComboBox*>();
    QVERIFY(planCombo != nullptr);
    const int planIndex = planCombo->findData(plan.id);
    QVERIFY(planIndex > 0);
    planCombo->setCurrentIndex(planIndex);
    QCoreApplication::processEvents();

    auto* layerSlider = treatmentPage.findChild<QSlider*>(QStringLiteral("treatmentLayerSlider"));
    QVERIFY(layerSlider != nullptr);
    QCOMPARE(layerSlider->minimum(), 0);
    QCOMPARE(layerSlider->maximum(), 1);
    QCOMPARE(layerSlider->value(), 0);

    QToolButton* previousLayerButton = nullptr;
    QToolButton* nextLayerButton = nullptr;
    for (QToolButton* button : treatmentPage.findChildren<QToolButton*>(QStringLiteral("treatmentLayerNavButton"))) {
        if (button->toolTip().contains(QStringLiteral("\u4e0a\u4e00\u5c42"))) {
            previousLayerButton = button;
        } else if (button->toolTip().contains(QStringLiteral("\u4e0b\u4e00\u5c42"))) {
            nextLayerButton = button;
        }
    }
    QVERIFY(previousLayerButton != nullptr);
    QVERIFY(nextLayerButton != nullptr);
    QVERIFY(!previousLayerButton->isEnabled());
    QVERIFY(nextLayerButton->isEnabled());
    nextLayerButton->click();
    QCoreApplication::processEvents();
    QCOMPARE(layerSlider->value(), 1);
    QVERIFY(previousLayerButton->isEnabled());
    QVERIFY(!nextLayerButton->isEnabled());
    previousLayerButton->click();
    QCoreApplication::processEvents();
    QCOMPARE(layerSlider->value(), 0);
    QVERIFY(!previousLayerButton->isEnabled());
    QVERIFY(nextLayerButton->isEnabled());

    QStringList treatmentControlTexts;
    for (QPushButton* button : treatmentPage.findChildren<QPushButton*>(QStringLiteral("treatmentControlButton"))) {
        treatmentControlTexts.append(button->text());
    }
    QCOMPARE(treatmentControlTexts.size(), 3);
    QVERIFY(treatmentControlTexts.contains(QStringLiteral("\u5f00\u59cb")));
    QVERIFY(treatmentControlTexts.contains(QStringLiteral("\u6682\u505c")));
    QVERIFY(treatmentControlTexts.contains(QStringLiteral("\u7ec8\u6b62")));
    QVERIFY(!treatmentControlTexts.contains(QStringLiteral("\u5f00\u59cb\u6cbb\u7597")));

    auto* progressLabel = treatmentPage.findChild<QLabel*>(QStringLiteral("treatmentProgressLabel"));
    QVERIFY(progressLabel != nullptr);
    QVERIFY(progressLabel->text().contains(QStringLiteral("0 / 2")));
    auto* timeSummaryLabel = treatmentPage.findChild<QLabel*>(QStringLiteral("treatmentTimeSummaryLabel"));
    QVERIFY(timeSummaryLabel != nullptr);
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("本层剩余")));
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("0.6 s")));
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("1.5 s")));
    QVERIFY(QMetaObject::invokeMethod(&treatmentPage, "advanceProgress", Qt::DirectConnection));
    QVERIFY(progressLabel->text().contains(QStringLiteral("1 / 2")));
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("0.3 s")));

    layerSlider->setValue(1);
    QCoreApplication::processEvents();
    QCOMPARE(layerSlider->value(), 1);
    QVERIFY(progressLabel->text().contains(QStringLiteral("0 / 3")));
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("0.9 s")));
    QVERIFY(timeSummaryLabel->text().contains(QStringLiteral("1.5 s")));

    auto* volumeButton = treatmentPage.findChild<QPushButton*>(QStringLiteral("treatmentVolumeButton"));
    QVERIFY(volumeButton != nullptr);
    QVERIFY(volumeButton->isEnabled());

    bool sawVolumeDialog = false;
    bool sawProgressSummary = false;
    QTimer::singleShot(100, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || dialog->objectName() != QStringLiteral("treatmentVolumePreviewDialog")) {
                continue;
            }

            sawVolumeDialog = true;
            auto* summaryEdit = dialog->findChild<QPlainTextEdit*>(QStringLiteral("treatmentVolumeSummaryEdit"));
            sawProgressSummary = summaryEdit != nullptr
                && summaryEdit->toPlainText().contains(QStringLiteral("\u5df2\u6cbb\u7597\u7ec6\u80de"))
                && summaryEdit->toPlainText().contains(QStringLiteral("\u5f85\u6cbb\u7597\u7ec6\u80de"));
            dialog->accept();
        }
    });
    volumeButton->click();
    QVERIFY(sawVolumeDialog);
    QVERIFY(sawProgressSummary);

    QSignalSpy visualizationSpy(&context, &ApplicationContext::treatmentLayerVisualizationRequested);
    QVERIFY(QMetaObject::invokeMethod(&treatmentPage, "startTreatment", Qt::DirectConnection));
    QCOMPARE(visualizationSpy.count(), 1);
    const QList<QVariant> visualizationArgs = visualizationSpy.takeFirst();
    QCOMPARE(visualizationArgs.at(0).toString(), plan.id);
    QCOMPARE(visualizationArgs.at(1).toInt(), 16);
    QVERIFY(visualizationArgs.at(2).toBool());
    QVERIFY(QMetaObject::invokeMethod(&treatmentPage, "stopTreatment", Qt::DirectConnection));
}

void PlanningPageRegressionTests::mainWindowSwitchesFromTreatmentToPlanningWithoutCrash()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    MainWindow mainWindow(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    mainWindow.resize(1600, 900);
    mainWindow.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&mainWindow, "showTreatment", Qt::DirectConnection));
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&mainWindow, "showPlanning", Qt::DirectConnection));
    QCoreApplication::processEvents();

    QVERIFY(mainWindow.findChild<PlanningPage*>() != nullptr);
}

void PlanningPageRegressionTests::addPathFallsBackWhenRobotPoseUnavailable()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    PlanningPage planningPage(&context, &safetyKernel, &auditService, nullptr, &simulationDevice);
    planningPage.resize(1600, 900);
    planningPage.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&planningPage, "addPathItem", Qt::DirectConnection));

    auto* pathList = planningPage.findChild<QListWidget*>(QStringLiteral("planningPathList"));
    QVERIFY(pathList != nullptr);
    QCOMPARE(pathList->count(), 1);
    QVERIFY(pathList->item(0)->text().contains(QStringLiteral("未获取机械臂坐标")));
    QVERIFY(!pathList->item(0)->text().contains(QStringLiteral("-9.53")));
}

void PlanningPageRegressionTests::mainWindowSwitchesFromDashboardToPlanningWithoutCrash()
{
    EventBus eventBus;
    AuditService auditService;
    ApplicationContext context(&eventBus, &auditService);
    SafetyKernel safetyKernel;
    panthera::adapters::SeedClinicalDataRepository repository;
    panthera::adapters::SimulationDeviceFacade simulationDevice;

    MainWindow mainWindow(&context, &safetyKernel, &auditService, &repository, &simulationDevice);
    mainWindow.resize(1600, 900);
    mainWindow.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&mainWindow, "showDashboard", Qt::DirectConnection));
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&mainWindow, "showPlanning", Qt::DirectConnection));
    QCoreApplication::processEvents();

    QVERIFY(mainWindow.findChild<PlanningPage*>() != nullptr);
}

QTEST_MAIN(PlanningPageRegressionTests)

#include "planning_page_regression_tests.moc"
