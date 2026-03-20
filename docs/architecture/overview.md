# Architecture Overview

PanTheraSys is organized into five layers:

1. `UI Shell`
   Presents the workstation experience and routes user actions without directly owning treatment hardware logic.
2. `Application Services`
   Coordinates patient context, plan context, audit flow, and cross-screen state.
3. `Domain + Safety Kernel`
   Owns workflow state, treatment interlocks, approval rules, and execution permissions.
4. `Device Abstraction Layer`
   Defines stable contracts for motion, ultrasound, power, water loop, health, and emergency stop channels.
5. `Infrastructure`
   Provides configuration, persistence, report generation, schema management, and deployment-facing services.

The current bootstrap implements a simulation-backed slice of this architecture to validate UI flow and safety gating before introducing real hardware or regulated data flows.
