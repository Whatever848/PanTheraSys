#include <QtTest/QtTest>

#include "core/application/application_context.h"

using namespace panthera::core;

class ApplicationContextTests final : public QObject {
    Q_OBJECT

private slots:
    void switchingPatientClearsMismatchedActivePlan();
    void selectingSamePatientKeepsActivePlan();
};

void ApplicationContextTests::switchingPatientClearsMismatchedActivePlan()
{
    ApplicationContext context(nullptr, nullptr);

    context.selectPatient(PatientRecord {QStringLiteral("P-001"), QStringLiteral("张三"), 24, QStringLiteral("女"), QStringLiteral("诊断A"), QStringLiteral("13800000001")});
    TherapyPlan plan;
    plan.id = QStringLiteral("PLAN-001");
    plan.patientId = QStringLiteral("P-001");
    context.setActivePlan(plan);

    QVERIFY(context.hasActivePlan());

    context.selectPatient(PatientRecord {QStringLiteral("P-002"), QStringLiteral("李四"), 57, QStringLiteral("女"), QStringLiteral("诊断B"), QStringLiteral("13800000002")});

    QVERIFY(!context.hasActivePlan());
    QCOMPARE(context.selectedPatient().id, QStringLiteral("P-002"));
}

void ApplicationContextTests::selectingSamePatientKeepsActivePlan()
{
    ApplicationContext context(nullptr, nullptr);

    context.selectPatient(PatientRecord {QStringLiteral("P-001"), QStringLiteral("张三"), 24, QStringLiteral("女"), QStringLiteral("诊断A"), QStringLiteral("13800000001")});
    TherapyPlan plan;
    plan.id = QStringLiteral("PLAN-001");
    plan.patientId = QStringLiteral("P-001");
    context.setActivePlan(plan);

    context.selectPatient(PatientRecord {QStringLiteral("P-001"), QStringLiteral("张三"), 24, QStringLiteral("女"), QStringLiteral("诊断A"), QStringLiteral("13800000001")});

    QVERIFY(context.hasActivePlan());
    QCOMPARE(context.activePlan().id, QStringLiteral("PLAN-001"));
}

QTEST_GUILESS_MAIN(ApplicationContextTests)

#include "application_context_tests.moc"
