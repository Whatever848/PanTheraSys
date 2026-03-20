# PanTheraSys 项目说明文档

## 1. 文档目的

本文档用于帮助项目负责人、开发人员和后续接手同事快速了解 `PanTheraSys` 当前代码仓库的实际状态，包括：

- 项目定位与当前范围
- 当前已实现功能
- 主要页面与工作流
- 目录结构与文件夹职责
- 配置、构建、启动与测试方式
- 当前未完成项与后续接入点

## 2. 项目概况

`PanTheraSys` 是一个基于 `Qt 6.2.0 + C++ + CMake + MSVC2019 + MySQL 5.7` 规划的乳腺超声消融治疗工作站原型项目。

当前仓库实现的是 **M0-M1 阶段的可演示原型底座**，核心目标是先打通以下闭环：

`患者上下文 -> 治疗方案 -> 审批/锁定 -> 安全联锁 -> 治疗执行 -> 审计/数据展示`

需要特别强调：

- 当前版本属于 **工程原型 / 演示原型**
- 当前版本 **不能用于人体临床治疗**
- 当前版本已具备“界面、流程、联锁、仿真设备、审计、数据库脚本、测试骨架”等企业级项目底座
- 当前版本尚未接入真实硬件、真实数据库业务仓储、真实影像链路和注册级验证体系

## 3. 当前已实现功能

### 3.1 工程与构建底座

- 已建立 `Qt Widgets` 桌面工程，入口位于 `apps/console/main.cpp`
- 已建立顶层 `CMakeLists.txt`，支持模块化构建
- 已启用 `MSVC /utf-8` 编译选项，统一源码编码为 `UTF-8`
- 已提供 Windows 本地构建脚本和运行脚本
- 已提供 `CTest` 测试入口
- 已提供 GitHub Actions 构建与 CodeQL 工作流

### 3.2 主工作站界面

当前主界面已经具备 4 个主工作区：

1. `设备监控`
   展示设备实时状态、安全状态与故障注入入口
2. `治疗方案`
   负责加载示例患者、生成方案草案、审批并锁定方案、方案预览
3. `治疗`
   负责方案执行演示，包括开始、暂停、继续、终止、进度与日志
4. `数据管理`
   负责展示患者信息、影像数据、治疗数据、治疗报告与审计信息

其中：

- `数据管理` 当前默认只对工程师和管理员角色可见
- 主页面导航与角色可见性控制由 `src/modules/shell/main_window.*` 统一负责

### 3.3 核心业务与安全能力

- 已实现 `ApplicationContext`
  用于共享当前患者、当前方案、当前角色等运行时上下文
- 已实现 `EventBus`
  用于跨模块事件广播
- 已实现 `AuditService`
  用于记录治疗流程中的审计事件
- 已实现 `SafetyKernel`
  用于统一进行联锁判定、系统模式切换和治疗启动许可控制
- 已实现 `SystemMode / SafetyState / InterlockReason / ApprovalState` 等核心领域枚举
- 已实现“未选患者、方案未审批、水循环故障、功率故障、运动故障、急停、超声源不可用”等联锁逻辑

### 3.4 仿真设备能力

- 已实现 `SimulationDeviceFacade`
- 已提供运动、功率、水循环、急停、设备健康等统一接口
- 已实现实时快照更新
- 已实现故障注入
- 已实现治疗输出功率启停的模拟
- 已实现监控页与联锁内核共享同一份设备快照

### 3.5 页面演示能力

#### 设备监控页

- 展示安全状态和联锁信息
- 展示电源、水循环、运动/换能器、影像质量与功率输出等模拟指标
- 支持手动注入故障并触发联锁

#### 治疗方案页

- 加载固定示例患者
- 配置治疗模式、采集层数、点疗时长、功率、呼吸跟随等参数
- 生成草案方案
- 审批并锁定方案
- 撤销审批并退回草案
- 使用模拟超声视图展示病灶草图和治疗点位

#### 治疗页

- 根据安全内核结果决定是否允许开始治疗
- 支持开始、暂停、继续、终止
- 模拟按点位推进治疗进度
- 显示累计能量和治疗日志
- 红色联锁触发时自动中止治疗

#### 数据管理页

- 显示患者信息、影像数据、治疗数据、治疗报告 4 个页签
- 同步当前患者与当前方案上下文
- 显示审计日志流
- 当前仍属于“原型壳层 + 样例数据”

### 3.6 数据与配置底座

- 已提供 `MySQL 5.7` 初始化脚本 `db/schema/mysql_5_7_init.sql`
- 已建立数据库连接外观类 `MySqlRepositoryFacade`
- 已支持本地配置存储 `LocalSettingsStore`
- 已提供默认配置文件与部署示例配置
- 已提供统一界面样式 `src/resources/styles/panthera.qss`

### 3.7 测试与质量体系骨架

- 已有 `SafetyKernel` 单元测试
- 已有需求基线、风险表、设计规格、追溯矩阵、验证计划等 QMS 起步文档
- 已加入 UTF-8 编码校验脚本

## 4. 当前页面与业务流说明

### 4.1 页面关系

系统目前围绕以下页面工作：

| 页面 | 代码位置 | 当前作用 |
|---|---|---|
| 主窗口导航壳 | `src/modules/shell` | 顶部导航、角色切换、状态汇总、页面切换 |
| 设备监控页 | `src/modules/dashboard` | 设备状态查看与故障注入 |
| 治疗方案页 | `src/modules/planning` | 患者加载、方案生成、审批锁定、预览 |
| 治疗页 | `src/modules/treatment` | 执行方案、推进进度、显示日志 |
| 数据管理页 | `src/modules/data` | 查看患者/影像/治疗/报告与审计 |
| 公共示意视图 | `src/modules/shared` | 模拟超声视图与点位覆盖显示 |

### 4.2 当前主流程

当前可演示的主流程如下：

1. 启动主程序
2. 进入 `治疗方案` 页
3. 加载示例患者
4. 生成草案方案
5. 审批并锁定方案
6. 进入 `治疗` 页
7. 安全内核判定通过后开始治疗
8. 查看治疗进度、累计能量和执行日志
9. 在 `设备监控` 页可手动注入故障，验证联锁中止逻辑
10. 在 `数据管理` 页查看当前上下文和审计记录

## 5. 项目架构说明

当前仓库遵循 5 层结构：

| 层级 | 职责 |
|---|---|
| UI Shell | 页面展示、导航、角色可见性控制，不直接操作硬件 |
| Application Services | 维护当前患者、方案、角色和事件广播 |
| Domain + Safety Kernel | 定义领域对象、联锁规则、系统模式切换 |
| Device Abstraction Layer | 统一设备接口，屏蔽真实硬件与仿真后端差异 |
| Infrastructure | 配置、数据库、脚本、样式、测试、CI、QMS 文档 |

### 5.1 核心对象

| 对象 | 位置 | 作用 |
|---|---|---|
| `ApplicationContext` | `src/core/application` | 维护选中患者、当前方案、当前角色 |
| `EventBus` | `src/core/application` | 跨模块事件广播 |
| `AuditService` | `src/core/services` | 记录审计信息 |
| `SafetyKernel` | `src/core/safety` | 联锁判定、模式切换、治疗许可 |
| `SimulationDeviceFacade` | `src/adapters/sim` | 仿真设备后端 |
| `MockUltrasoundView` | `src/modules/shared` | 模拟超声视图与方案点位示意 |

## 6. 目录结构与文件夹职责

### 6.1 仓库根目录

| 目录 / 文件 | 作用 |
|---|---|
| `.github/` | GitHub 协作模板与 CI 工作流 |
| `apps/` | 应用程序入口 |
| `build/` | 本地构建输出目录，属于生成产物 |
| `cmake/` | 预留给公共 CMake 模块，当前基本为空 |
| `config/` | 默认配置和部署示例配置 |
| `db/` | 数据库初始化脚本 |
| `docs/` | 架构文档和 QMS 文档 |
| `scripts/` | 构建、运行、编码校验脚本 |
| `src/` | 主要源代码 |
| `tests/` | 自动化测试 |
| `CMakeLists.txt` | 项目顶层构建入口 |
| `README.md` | 仓库总览说明 |

### 6.2 `.github/`

| 目录 / 文件 | 作用 |
|---|---|
| `.github/workflows/windows-ci.yml` | Windows 构建、测试与 UTF-8 校验 |
| `.github/workflows/codeql.yml` | 静态安全分析 |
| `.github/ISSUE_TEMPLATE/` | 缺陷 / 需求模板 |
| `.github/PULL_REQUEST_TEMPLATE.md` | Pull Request 模板 |

### 6.3 `apps/`

| 目录 / 文件 | 作用 |
|---|---|
| `apps/console/` | 当前桌面控制台程序 |
| `apps/console/main.cpp` | 程序入口，创建核心对象并启动主窗口 |
| `apps/console/CMakeLists.txt` | 控制台应用构建配置 |

### 6.4 `config/`

| 文件 | 作用 |
|---|---|
| `config/defaults.ini` | 默认运行参数，如主题、数据库连接、存储路径、仿真开关 |
| `config/deployment.example.ini` | 部署环境示例配置，如仿真/真机模式、硬件适配器类型等 |

### 6.5 `db/`

| 目录 / 文件 | 作用 |
|---|---|
| `db/schema/mysql_5_7_init.sql` | MySQL 5.7 初始化脚本，包含患者、方案、治疗、报告、审计等表 |

### 6.6 `docs/`

#### `docs/architecture/`

| 文件 | 作用 |
|---|---|
| `overview.md` | 架构总览 |
| `github-bootstrap.md` | GitHub 仓库初始化说明 |
| `developer-onboarding.md` | 开发者导读 |
| `project-overview-cn.md` | 当前这份中文项目说明文档 |

#### `docs/qms/`

| 文件 | 作用 |
|---|---|
| `requirements-baseline.md` | 需求基线 |
| `software-design-specification.md` | 软件设计规格 |
| `risk-register.md` | 风险登记表 |
| `traceability-matrix.md` | 需求与验证追溯 |
| `verification-plan.md` | 验证计划 |

### 6.7 `scripts/`

| 文件 | 作用 |
|---|---|
| `configure-msvc2019.ps1` | 使用 Qt 6.2.0 + VS2019 配置工程 |
| `run-console.ps1` | 启动桌面程序 |
| `validate-utf8.ps1` | 校验仓库文本编码是否为 UTF-8 |
| `normalize-utf8.ps1` | 规范化文本文件编码为 UTF-8 |

### 6.8 `src/`

#### `src/core/`

核心业务与安全内核。

| 子目录 | 作用 |
|---|---|
| `application/` | 运行时上下文和事件总线 |
| `device/` | 设备抽象接口定义 |
| `domain/` | 领域对象、状态枚举、共享数据结构 |
| `safety/` | 联锁判定和系统模式切换 |
| `services/` | 审计等共享服务 |

#### `src/adapters/`

适配器层，用于把上层逻辑与具体实现隔离。

| 子目录 | 作用 |
|---|---|
| `config/` | `QSettings` 本地配置存储 |
| `mysql/` | MySQL 连接与 schema 初始化外观类 |
| `sim/` | 仿真设备后端 |

#### `src/modules/`

界面模块层。

| 子目录 | 作用 |
|---|---|
| `dashboard/` | 设备监控页面 |
| `planning/` | 治疗方案页面 |
| `treatment/` | 治疗执行页面 |
| `data/` | 数据管理页面 |
| `shared/` | 页面共用控件 |
| `shell/` | 顶层主窗口和导航壳 |

#### `src/resources/`

| 子目录 / 文件 | 作用 |
|---|---|
| `styles/panthera.qss` | 全局深色临床风格样式表 |
| `resources.qrc` | Qt 资源清单 |

### 6.9 `tests/`

| 目录 / 文件 | 作用 |
|---|---|
| `tests/core/` | 核心模块测试 |
| `tests/core/safety_kernel_tests.cpp` | 安全内核单元测试 |
| `tests/CMakeLists.txt` | 测试构建入口 |

## 7. 配置、构建与启动

### 7.1 环境要求

- Windows x64
- Visual Studio 2019
- Qt 6.2.0 `msvc2019_64`
- CMake 3.21+

### 7.2 配置工程

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\configure-msvc2019.ps1
```

### 7.3 编译

```powershell
cmd /c "\"D:\VS2019\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cmake --build build\msvc2019 --config Debug"
```

### 7.4 运行

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-console.ps1
```

### 7.5 测试

```powershell
cmd /c "\"D:\VS2019\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && ctest --test-dir build\msvc2019 --output-on-failure -C Debug"
```

## 8. 当前配置文件说明

### 8.1 `config/defaults.ini`

当前主要包含：

- 应用组织名、应用名、主题名
- MySQL 驱动、主机、端口、库名、账户
- 影像和报告的默认存储目录
- 仿真模式开关和仿真刷新周期

### 8.2 `config/deployment.example.ini`

当前主要包含：

- 部署环境类型
- 设备模式：仿真 / 真机
- 隐藏数据管理入口的默认开关
- 审计保留天数
- 运动、超声、功率、水循环等硬件适配器预留配置

## 9. 当前未完成项与预留扩展点

以下内容目前还没有真正完成，或只做了预留骨架：

- 真实电机、超声、功率放大器、水循环、急停等硬件接入
- MySQL 真实 CRUD 仓储层
- 患者管理、影像管理、治疗记录、报告管理的完整业务流
- DICOM / DCMTK 接入
- OpenCV、三维重建、真实病灶勾画与路径规划
- 用户登录、持久化权限、操作审批留痕
- 治疗报告导出、打印与签名
- 更完整的单元测试、集成测试、HIL 测试

## 10. 建议的阅读顺序

如果你希望最快理解整个项目，建议按以下顺序阅读：

1. `docs/architecture/project-overview-cn.md`
2. `apps/console/main.cpp`
3. `src/modules/shell/main_window.*`
4. `src/core/application/application_context.*`
5. `src/core/safety/safety_kernel.*`
6. `src/adapters/sim/simulation_device_facade.*`
7. `src/modules/planning/*`
8. `src/modules/treatment/*`
9. `src/modules/data/*`
10. `db/schema/mysql_5_7_init.sql`

## 11. 结论

当前 `PanTheraSys` 已经具备一个企业级项目早期原型应有的基础框架：

- 有工程化底座
- 有主页面骨架
- 有核心状态与联锁逻辑
- 有仿真设备适配器
- 有数据库脚本
- 有配置体系
- 有测试与质量文档起点

它现在更适合作为：

- 产品界面与工作流演示平台
- 硬件联调前的软件底座
- 后续真实业务模块和真机适配器的承载骨架

但它还不是一个完整的临床治疗系统，后续仍需补齐真实设备接入、真实数据仓储、影像链路和验证体系。
