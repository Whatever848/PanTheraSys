# Risk Register

| ID | Hazard / Risk | Software Control |
| --- | --- | --- |
| R-001 | Treatment starts without patient context | Safety kernel interlock `NoPatientSelected` |
| R-002 | Unreviewed plan executed | Safety kernel interlock `PlanNotApproved` |
| R-003 | Water loop fault during treatment | Simulation and hardware health inputs drive red interlock and abort request |
| R-004 | Emergency stop ignored | Dedicated interlock path forces red state and treatment abort |
| R-005 | Untracked configuration drift | Versioned config profiles and audit entries |
| R-006 | Sensitive data leakage in repository | Git LFS rules, anonymization policy, non-checked-in runtime data |
