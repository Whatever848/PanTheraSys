# Requirements Baseline

## Product Goal

Provide a Windows-based workstation for breast ultrasound ablation workflow orchestration covering monitoring, planning, execution, and audited data management.

## Baseline Functional Areas

- Device monitoring with electrical, water loop, motion, transducer, and image-quality indicators
- Treatment planning with image channel selection, plan drafting, parameterization, review, and approval lock
- Treatment execution with start, pause, resume, stop, progress tracking, and interlock enforcement
- Data management for patient, imaging, treatment, and report records
- Audit logging and role-sensitive access to engineering functions

## Safety-Critical Software Expectations

- No treatment start without selected patient
- No treatment start without approved plan
- No treatment start during emergency-stop or subsystem red interlock
- Treatment execution must stop or reject continuation when water, motion, or power health becomes unsafe
- Critical configuration must be versioned and auditable
