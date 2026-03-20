# Traceability Matrix

| Requirement | Design Element | Verification |
| --- | --- | --- |
| Selected patient required before treatment | `SafetyKernel::setPatientSelected` | `SafetyKernelTests::rejectsTreatmentWithoutPatient` |
| Approved plan required before treatment | `SafetyKernel::setPlanApprovalState` | `SafetyKernelTests::requiresApprovedPlanBeforeStart` |
| Emergency stop blocks execution | `SimulationDeviceFacade` + `SafetyKernel` | `SafetyKernelTests::emergencyStopForcesRedInterlock` |
| Role-gated data management | `MainWindow::applyRoleVisibility` | Manual UI verification |
| Simulated device data updates monitor UI | `SimulationDeviceFacade::snapshotUpdated` | Manual UI verification |
