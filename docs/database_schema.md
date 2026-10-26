# PanTheraSys MySQL 数据库表结构说明

## 文档用途

本文档记录 PanTheraSys 当前 MySQL 数据库的表、字段职责、关键关联关系和临床数据留存流程。数据库结构每次新增或调整后，必须同步更新本文档及 `db/schema/mysql_5_7_init.sql`。

当前数据库：`panthera_sys`
字符集：`utf8mb4`
排序规则：`utf8mb4_unicode_ci`
存储引擎：`InnoDB`
证据文件根目录：`runtime/evidence_documents`

## 设计原则

1. 患者、医生、治疗方案、治疗过程、影像、设备数据和操作日志必须可关联、可追溯。
2. 原始采集影像与靶点圈画影像必须分开保存，禁止用圈画结果覆盖原始影像。
3. 治疗执行过程中产生的图层、点位、能量和时长数据必须保存到数据库。
4. 结束整个治疗方案时，必须生成 `.docx` 证据文档，并将绝对路径和 SHA-256 哈希登记到数据库。
5. 医疗证据相关数据不得由普通数据维护界面直接删除；真实临床部署前还需落实备份、权限、时间源和电子签名策略。

## 表总览

| 表名 | 模块 | 作用 |
| --- | --- | --- |
| `role` | 权限 | 系统角色定义，例如医生、管理员。 |
| `doctor_profile` | 医生 | 医生姓名、工号、科室、执业信息。 |
| `user_account` | 登录 | 登录账号、密码哈希、角色和医生绑定。 |
| `doctor_login_session` | 登录审计 | 医生登录、登出、工作站和会话状态。 |
| `patient` | 患者 | 患者基础档案、诊断和联系方式。 |
| `image_series` | 影像 | 数据管理页显示的影像序列及本地存储路径。 |
| `historical_treatment_image` | 既往影像 | 用于治疗前对比的既往治疗影像。 |
| `therapy_plan` | 方案 | 治疗方案主记录、审批状态和全局参数。 |
| `therapy_segment` | 方案 | 按图层或切片保存治疗段、焦点和轨迹信息。 |
| `therapy_plan_evaluation` | 方案评估 | 计划体积、治疗体积、进度和评估结论。 |
| `robot_acquisition_path` | 采集路径 | 图像采集时机械臂坐标、轴 7 位置和路径顺序。 |
| `treatment_image_capture` | 采集影像 | 图像采集后生成的原始当前治疗影像。 |
| `treatment_image_annotation` | 靶点影像 | 圈画靶点后的影像、笔迹 JSON、靶区面积和体积。 |
| `treatment_continue_import` | 历史续治 | 仅用于保留旧版本续治数据的追溯记录；当前界面不再发起续治。 |
| `treatment_session` | 治疗数据 | 一次终止治疗时形成的会话汇总。 |
| `treatment_record` | 治疗数据 | 按图层、按焦点保存实际输出能量和剂量。 |
| `treatment_report` | 治疗报告 | 数据管理页的治疗报告正文和摘要。 |
| `treatment_termination_log` | 治疗终止 | 医生确认“终止治疗”的操作与结果。 |
| `medical_evidence_document` | 医疗证据 | `.docx` 文件路径、哈希、生成时间和状态。 |
| `clinical_operation_log` | 临床审计 | 医生在各页面的操作、输入数据和执行结果。 |
| `audit_log` | 系统审计 | 系统级一般审计日志。 |
| `device_snapshot` | 设备 | 设备通用状态快照。 |
| `device_control_event` | 设备 | 设备控制命令及执行结果。 |
| `water_pump_status` | 水泵 | 水泵状态、流量、工作模式和通信结果。 |
| `temperature_control_status` | 温控 | 温度设定、当前温度、通道和通信结果。 |
| `liquid_level_status` | 液位 | 液位值、上下限状态和通信结果。 |
| `robot_arm_status` | 机械臂 | 机械臂连接、坐标、使能和轨迹执行状态。 |
| `three_motor_status` | 三电机 | 6、7、8 号电机节点、位置、限位和使能状态。 |
| `power_device_status` | 功放/信号源 | 信号源、功率放大器、占空比和输出状态。 |
| `alarm_log` | 安全 | 报警、联锁、确认人与确认时间。 |
| `config_profile` | 配置 | 设备与系统配置版本。 |

## 身份与权限

### `role`

主要列：`id`、`code`、`name`、`description`、`created_at`。
用于定义系统权限角色。

### `doctor_profile`

主要列：`id`、`name`、`employee_no`、`department`、`title`、`license_no`、`phone`、`email`、`status`、`created_at`、`updated_at`。
医生资料与 `user_account.doctor_id`、治疗操作日志和证据文档关联。

### `user_account`

主要列：`id`、`username`、`password_hash`、`password_salt`、`role_id`、`doctor_id`、`status`、`last_login_at`、`created_at`、`updated_at`。
必须保存密码哈希，不得保存明文密码。当前登录用户通过该表绑定医生身份。

### `doctor_login_session`

主要列：`id`、`user_id`、`doctor_id`、`login_at`、`logout_at`、`workstation_name`、`client_ip`、`login_result`、`failure_reason`。
用于回答“何人、何时、在哪台工作站登录系统”。

## 患者、方案与影像

### `patient`

主要列：`id`、`name`、`age`、`gender`、`diagnosis`、`contact`、`deleted_at`、`created_at`、`updated_at`。
`deleted_at` 用于逻辑归档；不得因为界面归档而删除治疗和证据数据。

### `image_series` 与 `historical_treatment_image`

`image_series` 保存患者影像序列：`id`、`patient_id`、`series_type`、`storage_path`、`study_date`、`notes`、`created_at`。
`historical_treatment_image` 保存既往治疗影像：`id`、`patient_id`、`treatment_session_id`、`image_series_id`、`storage_path`、`image_type`、`imported_by`、`imported_at`、`notes`。

### `therapy_plan` 与 `therapy_segment`

`therapy_plan` 是方案主表，主要列：`id`、`patient_id`、`name`、`approval_state`、`planned_power_watts`、`spacing_mm`、`dwell_seconds`、`delivery_mode`、`created_by`、`approved_by`、`created_at`、`updated_at`。
`therapy_segment` 是图层或切片表，主要列：`id`、`plan_id`、`order_index`、`label`、`axis7_position_steps`、`source_image_path`、`target_points_payload`、`planned_duration_seconds`、`created_at`。

### `robot_acquisition_path`

主要列：`id`、`patient_id`、`plan_id`、`path_name`、`path_order`、`robot_x`、`robot_y`、`robot_z`、`robot_rx`、`robot_ry`、`robot_rz`、`axis7_position_steps`、`path_payload`、`created_at`。
该表保存图像采集时的机械臂坐标，使后续治疗、复核和采集路径复现有依据。

### 原始影像：`treatment_image_capture`

主要列：`id`、`patient_id`、`plan_id`、`treatment_session_id`、`captured_by`、`layer_index`、`slice_index`、`storage_path`、`captured_at`、`acquisition_parameters`。
该表只记录采集后的原始影像。`storage_path` 必须指向未被靶点标注覆盖的源文件。

### 圈画靶点影像：`treatment_image_annotation`

主要列：`id`、`captured_image_id`、`patient_id`、`plan_id`、`treatment_session_id`、`annotated_by`、`annotation_state`、`target_label`、`annotated_image_path`、`mask_path`、`annotation_payload`、`target_area_mm2`、`target_volume_mm3`、`saved_reason`、`created_at`、`updated_at`。
该表与原始影像严格分离：`annotated_image_path` 指向带圈画结果的影像；`annotation_payload` 保存笔迹、颜色、靶点和点线治疗参数。医生从一张影像切换到下一张影像时，当前切片必须立即写入或更新本表。

## 治疗执行与方案终止

### 图层结束与治疗终止

治疗执行页提供两个不同的操作：

1. **结束**：仅安全停止当前图层的输出，不关闭方案。已完成点位保留，医生可切换到其他图层继续治疗，也可回到当前图层继续未完成点位。
2. **终止治疗**：结束当前方案的全部图层。打开确认框后只能“确认终止治疗”或“取消”。确认前系统必须先停止治疗输出；确认后清除当前活动方案，不提供继续治疗路径。

确认“终止治疗”时：

1. 汇总方案全部图层的已执行焦点、实际能量、剂量与时长，写入 `treatment_session`。
2. 将每个图层、每个焦点分别写入 `treatment_record.segment_index` 和 `treatment_record.point_index`。
3. 创建 `treatment_report`，供数据管理页的治疗报告区域显示。
4. 写入 `clinical_operation_log` 与 `treatment_termination_log`，包括患者、医生、时间、操作和结果。
5. 写入 `device_snapshot` 以及水循环、温控、液位、机械臂、三电机和功率链路的专属状态表，关联本次治疗会话并保留采样时间、关键读数、通信状态和原始摘要。
6. 生成 `.docx` 医疗证据文档，写入 `runtime/evidence_documents/<治疗会话ID>/<治疗会话ID>.docx`；文档包含患者、操作医生、已执行靶点位置、影像路径、圈画靶区量化信息和终止时的硬件快照。
7. 对文档计算 SHA-256，并将绝对路径及哈希写入 `medical_evidence_document`。

### `treatment_session`、`treatment_record` 与 `treatment_report`

`treatment_session` 主要列：`id`、`patient_id`、`plan_id`、`treatment_date`、`lesion_type`、`total_energy_j`、`total_duration_seconds`、`path_summary`、`dose`、`status`、`created_at`。
`treatment_record` 主要列：`id`、`session_id`、`segment_index`、`point_index`、`delivered_energy_j`、`delivered_dose`、`executed_at`。
`treatment_report` 主要列：`id`、`patient_id`、`treatment_session_id`、`generated_at`、`title`、`content_html`、`notes`。

### `treatment_continue_import`

主要列：`id`、`previous_session_id`、`new_session_id`、`annotation_id`、`imported_by`、`imported_at`、`import_reason`。
用于保留历史续治数据的关联证据；当前临床界面不再创建续治记录。

### `treatment_termination_log`

主要列：`id`、`treatment_session_id`、`patient_id`、`doctor_id`、`login_user_id`、`login_session_id`、`terminated_at`、`termination_type`、`reason_text`、`final_status`、`continue_treatment_selected`、`summary_payload`。
当前流程固定写入 `continue_treatment_selected=0`，表示已终止整个方案；`1` 仅兼容历史数据。

## 医疗证据与文档查看

### `clinical_operation_log`

主要列：`id`、`login_session_id`、`login_user_id`、`doctor_id`、`patient_id`、`treatment_session_id`、`occurred_at`、`screen_name`、`module_name`、`function_name`、`operation_name`、`operation_payload`、`result_state`、`result_message`、`evidence_level`、`workstation_name`、`created_at`。
用于记录医生在什么时间、通过哪个页面、执行了什么操作、输入或产生了哪些数据以及执行结果。

### `medical_evidence_document`

主要列：`id`、`treatment_session_id`、`patient_id`、`doctor_id`、`login_user_id`、`generated_at`、`document_type`、`docx_path`、`source_log_start_at`、`source_log_end_at`、`checksum_sha256`、`generation_state`、`generated_by`、`notes`。
`docx_path` 保存绝对路径。数据管理页的“证据文档”列只对已存在文档显示“查看文档”；对于数据库记录丢失但本地文件仍存在的旧记录，系统会尝试从本地证据目录找到文件并补登记。

历史 `待继续治疗` 会话不会生成最终结束证据文档，因此其“证据文档”按钮保持不可用，避免将历史中间记录误当作完整治疗证据。

## 设备、报警与配置

`device_snapshot` 保存通用快照；`device_control_event` 保存每个设备控制命令和执行结果。
`water_pump_status`、`temperature_control_status`、`liquid_level_status`、`robot_arm_status`、`three_motor_status`、`power_device_status` 分别保存水泵、温控、液位、机械臂、三电机和功率链路的专属状态。每类状态记录都应含采样时间、通信状态、关键读数、执行结果或错误消息。

`alarm_log` 保存安全报警、联锁和确认过程；`audit_log` 保存系统级审计；`config_profile` 保存系统配置版本。

## 关键关系

```text
doctor_profile -> user_account.doctor_id -> doctor_login_session.doctor_id
doctor_profile -> clinical_operation_log.doctor_id
doctor_profile -> treatment_termination_log.doctor_id
doctor_profile -> medical_evidence_document.doctor_id

patient -> therapy_plan.patient_id -> therapy_segment.plan_id
patient -> treatment_session.patient_id -> treatment_record.session_id
therapy_plan -> treatment_image_capture.plan_id -> treatment_image_annotation.plan_id
treatment_image_capture -> treatment_image_annotation.captured_image_id
treatment_session -> treatment_report.treatment_session_id
treatment_session -> treatment_termination_log.treatment_session_id
treatment_session -> medical_evidence_document.treatment_session_id
treatment_image_annotation -> treatment_continue_import.annotation_id
```

## 更新记录

| 日期 | 变更 |
| --- | --- |
| 2026-07-16 | 区分“结束当前图层”和“终止治疗”；终止治疗不再提供继续治疗路径，终止后汇总全部图层并生成、登记 `.docx` 证据文档。 |
