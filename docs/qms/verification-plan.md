# Verification Plan

## Automated

- Unit tests for safety-kernel interlocks and operational transitions
- Build verification on Windows via GitHub Actions
- Static analysis via CodeQL

## Manual

- Monitor screen reflects simulation updates
- Planning screen can load demo patient, draft plan, and approve plan
- Treatment screen refuses execution until all interlocks are green
- Data management becomes visible only for engineer or administrator roles

## Deferred

- Hardware-in-the-loop validation
- DICOM ingress verification
- Real MySQL 5.7 integration and migration tests
