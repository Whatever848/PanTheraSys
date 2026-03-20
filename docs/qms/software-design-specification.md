# Software Design Specification

## Primary Modules

- `core/domain`
  Shared enums, records, and workflow data contracts
- `core/safety`
  Safety kernel, interlock aggregation, operational mode transitions
- `core/application`
  Application context and event bus
- `core/services`
  Audit service and shared service contracts
- `adapters/sim`
  Simulation device facade implementing hardware abstraction interfaces
- `adapters/config`
  QSettings-based local settings store
- `adapters/mysql`
  MySQL connection bootstrap and schema initialization helper
- `modules/dashboard`
  Device monitoring UI
- `modules/planning`
  Treatment planning UI and workflow orchestration
- `modules/treatment`
  Treatment execution UI and progress engine
- `modules/data`
  Data-management UI shell with sample tables and audit stream

## Design Constraints

- Qt Widgets only for first phase
- UI thread never blocks on simulated long-running work
- Hardware dependencies remain behind interfaces
- The execution screen consumes approved plans only
- Data-management visibility is role-gated for engineering access
