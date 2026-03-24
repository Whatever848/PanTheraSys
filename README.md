# PanTheraSys

PanTheraSys is a Qt 6.2.0 and C++ desktop prototype platform for a breast ultrasound ablation treatment workstation. This repository implements the M0-M1 bootstrap of the project plan: engineering baseline, layered architecture, simulation-backed workflow shell, data schema, and quality-system starter documents.

## Implemented In This Bootstrap

- CMake-based `Qt Widgets` workspace aligned to `apps/console`, `src/core`, `src/adapters`, `src/modules`, `tests`, and `docs/qms`
- Safety kernel with treatment interlock evaluation and operational state transitions
- Device abstraction interfaces plus a simulation device facade for monitor and treatment workflow demos
- Four primary UI workspaces:
  - `设备监控`
  - `治疗方案`
  - `治疗`
  - `数据管理` (role-gated demo entry)
- MySQL 5.7 schema bootstrap SQL and local configuration templates
- GitHub engineering baseline: ignore rules, issue templates, PR template, CI workflow, CodeQL workflow
- QMS starter documents: requirements baseline, software design spec, risk register, traceability matrix, verification plan

## Current Scope

This repository is a non-clinical prototype foundation. It is suitable for UI walkthroughs, simulation-driven integration, workflow review, and further real-hardware integration. It is not a validated medical treatment system and must not be used for human treatment.

## Build Prerequisites

- Windows x64
- Visual Studio 2019 with MSVC x64 tools
- Qt 6.2.0 `msvc2019_64`
- CMake 3.21+

## Local Configure

Open a developer shell with MSVC active, then configure:

```powershell
.\scripts\configure-msvc2019.ps1
cmake --build build\msvc2019 --config Debug
ctest --test-dir build\msvc2019 --output-on-failure -C Debug
```

## Repository Conventions

- `main` is protected and release-oriented
- feature work goes through `feature/*`, `release/*`, `hotfix/*`
- sample imaging assets and DICOM files must be anonymized and stored via Git LFS
- critical configuration and treatment parameters must be audited and versioned

## Developer Docs

- Architecture overview: [docs/architecture/overview.md](docs/architecture/overview.md)
- Developer onboarding: [docs/architecture/developer-onboarding.md](docs/architecture/developer-onboarding.md)
- Enterprise development plan: [docs/architecture/enterprise-development-plan-cn.md](docs/architecture/enterprise-development-plan-cn.md)
- 中文项目说明: [docs/architecture/project-overview-cn.md](docs/architecture/project-overview-cn.md)

## Encoding Policy

- All repository text files must be UTF-8 without BOM
- MSVC builds use `/utf-8` so Chinese UI text is compiled consistently
- Run `.\scripts\validate-utf8.ps1` to verify encoding
- Run `.\scripts\normalize-utf8.ps1` to normalize supported text files to UTF-8

## Known Environment Gaps On This Machine

- `D:\PanTheraSys` was initially empty and not under Git
- Visual Studio developer environment was not pre-activated in the shell
- GitHub CLI was not installed
- Local MySQL 5.7 runtime was not present

The bootstrap scripts and docs account for these gaps, but external installation and GitHub remote creation still need to be completed on a provisioned workstation.
