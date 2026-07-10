# PanTheraSys MySQL 数据库表结构说明

文档状态：维护中  
最近更新：2026-07-10  
数据库名称：`panthera_sys`  
初始化脚本：[db/schema/mysql_5_7_init.sql](../db/schema/mysql_5_7_init.sql)  
推荐字符集：`utf8mb4`  
推荐排序规则：`utf8mb4_unicode_ci`  
推荐引擎：`InnoDB`

## 维护要求

本项目会用于真实医疗操作，数据库表结构会直接影响患者档案、治疗方案、治疗执行数据、报告生成、医生责任追溯和设备安全记录。因此后续任何数据库变更必须同步更新本文档。

变更时至少记录以下内容：

1. 变更日期。
2. 变更人。
3. 变更表名。
4. 新增、删除或修改的字段。
5. 变更原因。
6. 是否涉及患者隐私、医疗责任追溯、治疗安全或审计合规。
7. 是否需要数据迁移脚本。
8. 是否需要更新代码中的读写逻辑。

禁止在未记录文档的情况下直接修改生产数据库结构。

## 当前表清单

| 表名 | 所属模块 | 作用 |
| --- | --- | --- |
| `role` | 用户权限 | 存储系统角色，例如医生、操作员、工程师、管理员。 |
| `doctor_profile` | 医生信息 | 存储医生实名身份、科室、职称、执业证号等医疗责任主体信息。 |
| `user_account` | 登录认证 | 存储登录账号、密码哈希、账号状态，并绑定医生信息。 |
| `patient` | 患者信息 | 存储患者基本档案，用于数据管理模块的患者信息页面。 |
| `image_series` | 影像数据 | 存储患者影像序列及本地/网络存储路径。 |
| `therapy_plan` | 治疗方案 | 存储治疗方案主表，包括方案状态、能量、间距、审批信息和序列化方案载荷。 |
| `therapy_segment` | 治疗方案 | 存储治疗方案中的分段、层、路径或治疗片段。 |
| `treatment_session` | 治疗执行 | 存储一次治疗会话的总体结果。 |
| `treatment_record` | 治疗执行 | 存储治疗过程中的点位级、片段级执行记录。 |
| `treatment_report` | 治疗报告 | 存储治疗报告内容和生成时间。 |
| `device_snapshot` | 设备监控 | 存储设备运行状态快照。 |
| `alarm_log` | 安全联锁 | 存储报警、联锁、安全事件。 |
| `audit_log` | 审计追溯 | 存储系统操作审计日志。 |
| `config_profile` | 配置管理 | 存储系统配置、医生习惯、方案配置等配置快照。 |

## 表关系概览

核心关系如下：

```text
role
  └── user_account.role_id

doctor_profile
  └── user_account.doctor_id

patient
  ├── image_series.patient_id
  ├── therapy_plan.patient_id
  ├── treatment_session.patient_id
  └── treatment_report.patient_id

user_account
  └── therapy_plan.approved_by

therapy_plan
  ├── therapy_segment.plan_id
  └── treatment_session.plan_id

treatment_session
  ├── treatment_record.session_id
  └── treatment_report.treatment_session_id
```

登录身份链路：

```text
医生实名信息 doctor_profile
    ↓ doctor_id
登录账号 user_account
    ↓ 当前登录用户进入 ApplicationContext
系统操作、方案审批、治疗执行、审计记录
```

患者治疗链路：

```text
patient
    ↓
therapy_plan + therapy_segment
    ↓
treatment_session
    ↓
treatment_record + treatment_report
```

## 表结构详情

### 1. `role`

用途：系统角色字典表。用于描述账号权限类别。当前代码中主要用于区分医生、操作员、工程师、管理员。

医疗意义：真实医疗环境中，不同角色应对应不同操作权限。例如医生可审批方案，工程师只能维护设备，不应执行治疗。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 角色唯一 ID。建议固定为 `operator`、`physician`、`engineer`、`administrator`。 |
| `name` | `VARCHAR(64)` | 非空 | 角色显示名称。 |
| `description` | `VARCHAR(255)` | 可空 | 角色说明。 |

默认角色：

| id | 含义 |
| --- | --- |
| `operator` | 设备操作与治疗执行人员。 |
| `physician` | 临床医生账号。 |
| `engineer` | 设备维护与工程调试人员。 |
| `administrator` | 系统管理人员。 |

维护注意：

- 不建议随意删除角色，否则可能破坏 `user_account.role_id` 外键。
- 新增角色时，需要同步更新代码中的权限判断逻辑。

### 2. `doctor_profile`

用途：医生实名信息表。用于记录医生身份，并和登录账号绑定。

医疗意义：这是医疗责任划分的关键表。系统必须能追溯“哪个医生登录系统并进行了某项操作”。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 医生唯一 ID。当前代码生成格式为 `D-<uuid>`。 |
| `full_name` | `VARCHAR(128)` | 非空 | 医生真实姓名。 |
| `department` | `VARCHAR(128)` | 非空 | 医生所属科室，例如肿瘤科、超声科。 |
| `title_name` | `VARCHAR(128)` | 非空 | 医生职称，例如主治医师、副主任医师。 |
| `license_number` | `VARCHAR(64)` | 可空，唯一索引 | 医师执业证号或院内资质编号。建议生产环境必填。 |
| `phone` | `VARCHAR(64)` | 可空 | 联系方式。 |
| `is_active` | `TINYINT(1)` | 非空，默认 `1` | 医生信息是否有效。`1` 表示有效，`0` 表示停用。 |
| `created_at` | `DATETIME` | 非空 | 创建时间。 |
| `updated_at` | `DATETIME` | 非空 | 最近更新时间。 |

索引：

| 索引 | 字段 | 含义 |
| --- | --- | --- |
| `PRIMARY KEY` | `id` | 医生主键。 |
| `uk_doctor_license` | `license_number` | 执业证号唯一，防止同一证号重复注册。 |

维护注意：

- 医生离职或停用时，建议设置 `is_active = 0`，不要物理删除。
- 真实临床环境中建议强制填写 `license_number`。
- 后续若需要医院多院区支持，可增加 `hospital_id` 或 `organization_id`。

### 3. `user_account`

用途：登录账号表。存储登录用户名、密码哈希、角色、账号状态，并通过 `doctor_id` 绑定医生实名信息。

医疗意义：该表连接“登录账号”和“医生实名身份”。账号不是医疗责任主体，医生实名信息才是责任主体，因此必须绑定 `doctor_profile`。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 登录用户唯一 ID。当前代码生成格式为 `U-<uuid>`。 |
| `username` | `VARCHAR(64)` | 非空，唯一 | 登录账号。当前注册逻辑会转为小写保存。 |
| `display_name` | `VARCHAR(128)` | 非空 | 用户显示名，默认使用医生姓名。 |
| `role_id` | `VARCHAR(64)` | 非空，外键 | 关联 `role.id`。医生注册默认使用 `physician`。 |
| `doctor_id` | `VARCHAR(64)` | 可空，外键 | 关联 `doctor_profile.id`。医生账号必须绑定该字段。 |
| `password_hash` | `VARCHAR(255)` | 非空 | 密码哈希，不存储明文密码。当前格式为 `pbkdf2-sha256$iterations$salt$hash`。 |
| `is_active` | `TINYINT(1)` | 非空，默认 `1` | 账号是否启用。`1` 表示启用，`0` 表示禁用。 |
| `created_at` | `DATETIME` | 非空 | 账号创建时间。 |
| `updated_at` | `DATETIME` | 非空 | 账号最近更新时间。 |
| `last_login_at` | `DATETIME` | 可空 | 最近一次成功登录时间。 |

外键：

| 外键 | 字段 | 关联表 | 含义 |
| --- | --- | --- | --- |
| `fk_user_role` | `role_id` | `role(id)` | 用户角色。 |
| `fk_user_doctor` | `doctor_id` | `doctor_profile(id)` | 账号绑定医生。 |

安全要求：

- 禁止存储明文密码。
- 禁止把开发密码用于生产。
- 生产环境应增加密码复杂度、登录失败次数限制、账号锁定、密码过期策略。
- 管理员重置密码应写入审计日志。

### 4. `patient`

用途：患者基本档案表。对应数据管理模块中的“患者信息”。

医疗意义：患者信息是所有影像、方案、治疗、报告的根数据。真实临床系统中必须保证患者 ID 唯一、不可混淆。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 患者唯一 ID。示例：`P2026001` 或 `P-<uuid>`。 |
| `name` | `VARCHAR(128)` | 非空 | 患者姓名。 |
| `age` | `INT` | 非空 | 患者年龄。 |
| `gender` | `VARCHAR(32)` | 非空 | 患者性别。 |
| `diagnosis` | `VARCHAR(255)` | 非空 | 诊断结果，例如病灶类型、分期。 |
| `contact` | `VARCHAR(64)` | 可空 | 联系方式。 |
| `deleted_at` | `DATETIME` | 可空 | 软删除时间。为空表示当前有效。 |
| `created_at` | `DATETIME` | 非空 | 创建时间。 |
| `updated_at` | `DATETIME` | 非空 | 最近更新时间。 |

维护注意：

- 当前系统删除患者采用软删除，即写入 `deleted_at`，不是物理删除。
- 医疗系统中建议保留软删除策略，避免责任追溯数据丢失。
- 后续建议增加患者院内编号、身份证/病历号脱敏字段、出生日期等字段。

### 5. `image_series`

用途：影像序列表。对应数据管理模块中的“影像数据”。

医疗意义：治疗方案设计依赖影像数据，必须记录影像来源和存储路径，确保治疗方案可追溯到使用的影像。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 影像序列唯一 ID。 |
| `patient_id` | `VARCHAR(64)` | 非空，外键 | 关联患者 `patient.id`。 |
| `type` | `VARCHAR(32)` | 非空 | 影像类型，例如超声、CT、MRI 或内部类型枚举。 |
| `storage_path` | `VARCHAR(255)` | 非空 | 影像文件或目录的本地/网络路径。治疗方案采集图片的本地路径应记录在此类路径字段或后续专门字段中。 |
| `acquisition_date` | `DATE` | 非空 | 影像采集日期。 |
| `notes` | `VARCHAR(255)` | 可空 | 影像说明。 |
| `created_at` | `DATETIME` | 非空 | 入库时间。 |

外键：

| 外键 | 字段 | 关联表 |
| --- | --- | --- |
| `fk_image_series_patient` | `patient_id` | `patient(id)` |

维护注意：

- 数据库只保存路径，不建议直接保存大体积影像二进制。
- 路径必须稳定，生产环境建议使用受控存储目录或 PACS/对象存储引用。
- 后续如治疗方案会采集多张图片，建议新增 `plan_image_asset` 或 `therapy_plan_image` 表，而不是把多路径拼接进一个字段。

### 6. `therapy_plan`

用途：治疗方案主表。记录某患者的一份治疗方案。

医疗意义：治疗方案决定实际治疗路径和能量，是医疗责任划分的核心数据之一。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 治疗方案唯一 ID。 |
| `patient_id` | `VARCHAR(64)` | 非空，外键 | 关联患者 `patient.id`。 |
| `name` | `VARCHAR(128)` | 非空 | 方案名称。 |
| `pattern` | `VARCHAR(32)` | 非空 | 治疗模式，例如点治疗、线治疗、分段治疗等。 |
| `approval_state` | `VARCHAR(32)` | 非空 | 审批状态，例如草稿、待审批、已审批。 |
| `planned_power_watts` | `DOUBLE` | 非空 | 计划功率，单位瓦。 |
| `spacing_mm` | `DOUBLE` | 非空 | 点位/路径间距，单位毫米。 |
| `respiratory_tracking_enabled` | `TINYINT(1)` | 非空，默认 `0` | 是否启用呼吸跟踪。 |
| `serialized_payload` | `MEDIUMTEXT` | 非空 | 方案完整序列化数据，保存复杂参数、路径、图层等。 |
| `created_at` | `DATETIME` | 非空 | 创建时间。 |
| `approved_at` | `DATETIME` | 可空 | 审批时间。 |
| `approved_by` | `VARCHAR(64)` | 可空，外键 | 审批人账号 ID，关联 `user_account.id`。 |

外键：

| 外键 | 字段 | 关联表 | 含义 |
| --- | --- | --- | --- |
| `fk_therapy_plan_patient` | `patient_id` | `patient(id)` | 方案所属患者。 |
| `fk_therapy_plan_approver` | `approved_by` | `user_account(id)` | 方案审批账号。 |

维护注意：

- `serialized_payload` 必须保持向后兼容，建议内部带版本号。
- 生产环境中已审批方案不应被静默覆盖，应生成新版本。
- 后续建议增加 `created_by`、`updated_by`、`version_no`、`locked_at` 字段，加强责任追溯。

### 7. `therapy_segment`

用途：治疗方案分段表。记录方案中的每个治疗片段、层、路径段或执行段。

医疗意义：用于还原治疗方案的细粒度执行计划。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 治疗片段唯一 ID。 |
| `plan_id` | `VARCHAR(64)` | 非空，外键 | 关联 `therapy_plan.id`。 |
| `order_index` | `INT` | 非空 | 片段顺序，从小到大执行或展示。 |
| `label_name` | `VARCHAR(128)` | 非空 | 片段名称或标签。 |
| `planned_duration_seconds` | `DOUBLE` | 非空 | 计划持续时间，单位秒。 |
| `serialized_payload` | `MEDIUMTEXT` | 非空 | 片段完整序列化数据，例如点位、路径、层信息。 |

外键：

| 外键 | 字段 | 关联表 |
| --- | --- | --- |
| `fk_therapy_segment_plan` | `plan_id` | `therapy_plan(id)` |

维护注意：

- 应保证同一 `plan_id` 下 `order_index` 顺序稳定。
- 后续建议增加唯一约束：`UNIQUE(plan_id, order_index)`。

### 8. `treatment_session`

用途：治疗会话主表。记录一次治疗执行的总体结果。

医疗意义：用于划分一次真实治疗行为，包括患者、方案、治疗时间、剂量、状态等。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 治疗会话唯一 ID。 |
| `patient_id` | `VARCHAR(64)` | 非空，外键 | 关联患者 `patient.id`。 |
| `plan_id` | `VARCHAR(64)` | 非空，外键 | 关联治疗方案 `therapy_plan.id`。 |
| `treatment_date` | `DATETIME` | 非空 | 治疗发生时间。 |
| `lesion_type` | `VARCHAR(128)` | 非空 | 病灶类型。 |
| `total_energy_j` | `DOUBLE` | 非空 | 实际总能量，单位焦耳。 |
| `total_duration_seconds` | `DOUBLE` | 非空 | 实际总时长，单位秒。 |
| `path_summary` | `VARCHAR(255)` | 非空 | 治疗路径摘要。 |
| `dose` | `DOUBLE` | 非空 | 治疗剂量或系统定义的剂量指标。 |
| `status` | `VARCHAR(32)` | 非空 | 治疗状态，例如完成、中止、异常。 |
| `created_at` | `DATETIME` | 非空 | 入库时间。 |

外键：

| 外键 | 字段 | 关联表 |
| --- | --- | --- |
| `fk_treatment_session_patient` | `patient_id` | `patient(id)` |
| `fk_treatment_session_plan` | `plan_id` | `therapy_plan(id)` |

维护注意：

- 后续建议增加 `operator_user_id` 或 `doctor_user_id`，直接记录执行治疗的登录用户。
- 若治疗中止，应记录中止原因、报警状态和操作者确认信息。

### 9. `treatment_record`

用途：治疗过程明细表。记录治疗会话中的片段和点位执行数据。

医疗意义：这是责任划分中最重要的过程数据之一，用于证明某个时间、某个点位实际输出了多少能量和剂量。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 治疗记录唯一 ID。 |
| `session_id` | `VARCHAR(64)` | 非空，外键 | 关联治疗会话 `treatment_session.id`。 |
| `segment_index` | `INT` | 非空 | 所属治疗片段序号。 |
| `point_index` | `INT` | 非空 | 片段内点位序号。 |
| `delivered_energy_j` | `DOUBLE` | 非空 | 实际释放能量，单位焦耳。 |
| `delivered_dose` | `DOUBLE` | 非空 | 实际剂量。 |
| `executed_at` | `DATETIME` | 非空 | 点位执行时间。 |

外键：

| 外键 | 字段 | 关联表 |
| --- | --- | --- |
| `fk_treatment_record_session` | `session_id` | `treatment_session(id)` |

维护注意：

- 真实治疗执行时应持续写入该表或以事务/缓冲方式可靠落库。
- 后续建议补充实际坐标、设备状态快照 ID、温度、功率、频率、水流、水压等关键字段，或通过外键关联 `device_snapshot`。
- 建议增加唯一约束：`UNIQUE(session_id, segment_index, point_index)`。

### 10. `treatment_report`

用途：治疗报告表。对应数据管理模块中的“治疗报告”。

医疗意义：报告是治疗结果归档和医患沟通的重要文档，应能追溯到患者和具体治疗会话。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 报告唯一 ID。 |
| `patient_id` | `VARCHAR(64)` | 非空，外键 | 关联患者 `patient.id`。 |
| `treatment_session_id` | `VARCHAR(64)` | 非空，外键 | 关联治疗会话 `treatment_session.id`。 |
| `generated_at` | `DATETIME` | 非空 | 报告生成时间。 |
| `title` | `VARCHAR(128)` | 非空 | 报告标题。 |
| `content_html` | `MEDIUMTEXT` | 非空 | 报告正文 HTML。 |
| `notes` | `VARCHAR(255)` | 可空 | 报告备注。 |

外键：

| 外键 | 字段 | 关联表 |
| --- | --- | --- |
| `fk_treatment_report_patient` | `patient_id` | `patient(id)` |
| `fk_treatment_report_session` | `treatment_session_id` | `treatment_session(id)` |

维护注意：

- 报告正文使用 HTML，应注意 XSS 和外部资源引用风险。
- 生产环境建议增加报告 PDF 路径、签名医生、签发时间、报告版本号。

### 11. `device_snapshot`

用途：设备状态快照表。记录设备监控页面中的实时关键参数。

医疗意义：治疗责任划分不仅需要治疗参数，还需要设备当时状态。该表可用于追溯治疗时设备是否处于安全状态。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `BIGINT` | 主键，自增 | 快照自增 ID。 |
| `captured_at` | `DATETIME` | 非空 | 快照采集时间。 |
| `voltage` | `DOUBLE` | 非空 | 电压。 |
| `current` | `DOUBLE` | 非空 | 电流。 |
| `power_watts` | `DOUBLE` | 非空 | 功率，单位瓦。 |
| `water_pressure_mpa` | `DOUBLE` | 非空 | 水压，单位 MPa。 |
| `water_flow_lpm` | `DOUBLE` | 非空 | 水流量，单位 L/min。 |
| `transducer_temp_c` | `DOUBLE` | 非空 | 换能器温度，单位摄氏度。 |
| `vibration_frequency_mhz` | `DOUBLE` | 非空 | 振动/超声频率，单位 MHz。 |
| `motor_load_percent` | `DOUBLE` | 非空 | 电机负载百分比。 |
| `position_x` | `DOUBLE` | 非空 | 运动轴 X 坐标。 |
| `position_y` | `DOUBLE` | 非空 | 运动轴 Y 坐标。 |
| `position_z` | `DOUBLE` | 非空 | 运动轴 Z 坐标。 |
| `position_a` | `DOUBLE` | 非空 | 运动轴 A 坐标或姿态参数。 |
| `position_b` | `DOUBLE` | 非空 | 运动轴 B 坐标或姿态参数。 |
| `position_c` | `DOUBLE` | 非空 | 运动轴 C 坐标或姿态参数。 |

维护注意：

- 如果用于治疗追溯，需要明确采样频率和保留策略。
- 后续建议增加 `session_id`，将快照绑定到治疗会话。
- 建议区分模拟数据和真实设备数据，可增加 `source_name` 或 `is_simulated`。

### 12. `alarm_log`

用途：报警和联锁日志表。记录设备、安全、流程异常。

医疗意义：任何治疗中止、设备异常、安全联锁都应可追溯。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `BIGINT` | 主键，自增 | 报警日志 ID。 |
| `raised_at` | `DATETIME` | 非空 | 报警发生时间。 |
| `level_name` | `VARCHAR(32)` | 非空 | 报警级别，例如 info、warning、critical。 |
| `reason_code` | `VARCHAR(64)` | 非空 | 报警原因代码。 |
| `message_text` | `VARCHAR(255)` | 非空 | 报警描述。 |
| `source_name` | `VARCHAR(64)` | 非空 | 报警来源，例如水冷、电源、运动控制、温度模块。 |

维护注意：

- 后续建议增加 `session_id` 和 `operator_user_id`，记录报警发生于哪次治疗、由谁确认。
- 报警日志不应被普通用户删除。

### 13. `audit_log`

用途：系统审计日志表。记录操作者、操作类别、操作详情。

医疗意义：用于追溯谁在什么时间做了什么操作。真实医疗系统中非常关键。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `BIGINT` | 主键，自增 | 审计日志 ID。 |
| `occurred_at` | `DATETIME` | 非空 | 操作发生时间。 |
| `actor_name` | `VARCHAR(128)` | 非空 | 操作者名称或账号。 |
| `category_name` | `VARCHAR(64)` | 非空 | 操作类别，例如 auth、patient、plan、treatment、database。 |
| `details` | `TEXT` | 非空 | 操作详情。 |

维护注意：

- 当前审计字段较基础，后续建议增加 `actor_user_id`、`doctor_id`、`patient_id`、`session_id`、`ip_address`、`workstation_id`。
- 审计日志应追加写，不应普通删除。
- 生产环境建议启用数据库备份和不可抵赖策略。

### 14. `config_profile`

用途：配置快照表。保存系统配置、医生习惯、治疗方案设计偏好或其他配置资料。

医疗意义：如果医生使用了某套配置或系统参数，应能追溯当时配置内容。

| 字段 | 类型 | 约束 | 含义 |
| --- | --- | --- | --- |
| `id` | `VARCHAR(64)` | 主键 | 配置记录唯一 ID。 |
| `profile_name` | `VARCHAR(128)` | 非空 | 配置名称。 |
| `category_name` | `VARCHAR(64)` | 非空 | 配置类别。 |
| `version_name` | `VARCHAR(64)` | 非空 | 配置版本。 |
| `checksum_value` | `VARCHAR(128)` | 非空 | 配置内容校验值，用于发现内容被修改。 |
| `approval_state` | `VARCHAR(32)` | 非空 | 审批状态。 |
| `serialized_payload` | `MEDIUMTEXT` | 非空 | 配置完整序列化内容。 |
| `created_at` | `DATETIME` | 非空 | 创建时间。 |
| `approved_at` | `DATETIME` | 可空 | 审批时间。 |

维护注意：

- `serialized_payload` 应带内部版本号。
- 如果配置影响治疗安全，应增加审批人字段和审批记录。

## 医疗安全与合规建议

当前数据库已经具备患者、影像、方案、治疗、报告、医生登录的基础结构，但真实医疗应用仍建议继续增强：

1. 所有治疗执行表应记录当前登录医生或操作员 ID。
2. 治疗方案应记录创建人、修改人、审批人、版本号。
3. 患者数据应避免物理删除，继续使用 `deleted_at` 软删除。
4. 治疗过程数据应与设备快照和报警日志建立关联。
5. 密码必须保持哈希存储，禁止明文密码。
6. 对患者隐私字段应考虑脱敏、访问控制和备份加密。
7. 关键治疗数据建议使用事务写入，避免断电或异常退出造成部分数据丢失。
8. 数据库账号应按最小权限原则配置，生产环境不应使用 `root`。
9. 应建立数据库备份、恢复演练和变更审批流程。
10. 对用于医疗责任划分的数据，应明确保留年限和不可篡改策略。

## 后续建议新增或增强的表

| 建议表名 | 目的 | 优先级 |
| --- | --- | --- |
| `therapy_plan_image` | 存储治疗方案采集图片路径，与方案、患者、医生绑定。 | 高 |
| `treatment_device_snapshot_link` | 将治疗记录与设备快照建立关联。 | 高 |
| `doctor_operation_log` | 更结构化地记录医生操作。 | 中 |
| `patient_identifier` | 存储院内病历号、就诊号等多标识。 | 中 |
| `database_migration_log` | 记录数据库迁移脚本执行历史。 | 中 |

## 变更记录

| 日期 | 变更人 | 变更内容 | 影响 |
| --- | --- | --- | --- |
| 2026-07-10 | Codex | 建立数据库说明文档，记录当前 `panthera_sys` 所有表结构和维护规范。 | 文档化数据库结构。 |
| 2026-07-10 | Codex | 增加 `doctor_profile` 医生信息表；扩展 `user_account`，新增 `doctor_id`、`last_login_at`；账号与医生信息绑定。 | 支持医生登录注册和医疗责任追溯。 |
| 2026-07-10 | Codex | 当前 schema 包含患者、影像、治疗方案、治疗会话、治疗记录、治疗报告、设备快照、报警、审计、配置等基础表。 | 支撑数据管理和治疗数据持久化。 |

