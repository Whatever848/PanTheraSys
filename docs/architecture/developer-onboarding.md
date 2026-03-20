# Developer Onboarding

## First Read

If you are opening PanTheraSys for the first time, read the code in this order:

1. `apps/console/main.cpp`
   Understand process startup, shared service creation, and the top-level window bootstrap.
2. `src/modules/shell/main_window.*`
   See how the workstation shell hosts the four major workspaces.
3. `src/core/application/application_context.*`
   Learn how selected patient, active plan, and current role are shared across pages.
4. `src/core/safety/safety_kernel.*`
   Learn the interlock model and why treatment may or may not start.
5. `src/adapters/sim/simulation_device_facade.*`
   See the current simulation backend that feeds the dashboard and treatment flow.
6. `src/modules/planning`, `src/modules/treatment`, `src/modules/data`
   Follow the user workflow from plan generation to execution and data review.

## Layer Responsibilities

- `UI Shell`
  The window shell and page widgets render data and emit user intent. UI code must not talk to hardware drivers directly.
- `Application Services`
  `ApplicationContext` and `EventBus` share cross-screen state and domain events.
- `Domain + Safety Kernel`
  `system_types.*` defines shared contracts; `SafetyKernel` owns interlock aggregation and mode transitions.
- `Device Abstraction Layer`
  Interfaces in `src/core/device` define what motion, ultrasound, power, water-loop, and emergency channels must provide.
- `Infrastructure`
  MySQL bootstrap, local settings, logging, and future report/export plumbing live here.

## Current Workflow

The current prototype follows this closed loop:

`患者上下文 -> 方案草案 -> 审批/锁定 -> 联锁评估 -> 治疗执行 -> 审计/报告预览`

Important details:

- A treatment plan is generated in the planning page and stored in `ApplicationContext`.
- The treatment page never starts execution by itself; it must ask `SafetyKernel`.
- `SafetyKernel` decides whether the system is `Yellow`, `Green`, or `Red`.
- The simulated device adapter produces the telemetry snapshot consumed by both the dashboard and the safety kernel.
- The data page is still a prototype shell. It shows sample rows plus the current in-memory patient/plan context.

## Key Objects

- `ApplicationContext`
  Shared in-memory session state for selected patient, active plan, and current role.
- `SafetyKernel`
  Central safety policy engine. This is the first place to inspect when treatment enablement behaves unexpectedly.
- `SimulationDeviceFacade`
  Simulation-only hardware adapter. Replace this with real adapters behind the same interfaces in later milestones.
- `MockUltrasoundView`
  Non-clinical preview renderer for plan points and execution progress overlays.

## Implementation Rules

- Keep shared workflow objects in `core/domain`; avoid widget dependencies there.
- Do not add hardware calls directly inside page widgets.
- Route workflow gating through `SafetyKernel` instead of duplicating checks in UI code.
- Keep user-facing text in UTF-8 and prefer `QStringLiteral(...)`.
- Add comments only where they explain system intent, safety assumptions, or non-obvious coupling.

## Next Integration Points

- Replace static rows in `src/modules/data` with repository-backed queries.
- Replace synthetic plan generation in `src/modules/planning` with contour-driven planning logic.
- Replace timer-based execution in `src/modules/treatment` with checkpoint callbacks from the real control loop.
- Replace `SimulationDeviceFacade` with vendor-backed device adapters while preserving the existing interfaces.
