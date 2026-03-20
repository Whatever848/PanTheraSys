#include <QtTest/QtTest>

#include "core/safety/safety_kernel.h"

using namespace panthera::core;

class SafetyKernelTests final : public QObject {
    Q_OBJECT

private slots:
    void rejectsTreatmentWithoutPatient();
    void requiresApprovedPlanBeforeStart();
    void emergencyStopForcesRedInterlock();
};

void SafetyKernelTests::rejectsTreatmentWithoutPatient()
{
    SafetyKernel kernel;
    kernel.setPlanApprovalState(ApprovalState::Locked);

    QString reason;
    QVERIFY(!kernel.requestTreatmentStart(&reason));
    QCOMPARE(reason, QStringLiteral("未选择患者"));
}

void SafetyKernelTests::requiresApprovedPlanBeforeStart()
{
    SafetyKernel kernel;
    kernel.setPatientSelected(true);

    QString reason;
    QVERIFY(!kernel.requestTreatmentStart(&reason));
    QCOMPARE(reason, QStringLiteral("方案未审批"));

    kernel.setPlanApprovalState(ApprovalState::Locked);
    QVERIFY(kernel.requestTreatmentStart(&reason));
    QCOMPARE(kernel.mode(), SystemMode::Treating);
}

void SafetyKernelTests::emergencyStopForcesRedInterlock()
{
    SafetyKernel kernel;
    kernel.setPatientSelected(true);
    kernel.setPlanApprovalState(ApprovalState::Locked);

    QString reason;
    QVERIFY(kernel.requestTreatmentStart(&reason));
    kernel.setEmergencyStopReleased(false);

    QCOMPARE(kernel.snapshot().state, SafetyState::Red);
    QCOMPARE(kernel.mode(), SystemMode::Alarm);
}

QTEST_GUILESS_MAIN(SafetyKernelTests)

#include "safety_kernel_tests.moc"
