CREATE DATABASE IF NOT EXISTS panthera_sys DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE panthera_sys;

CREATE TABLE IF NOT EXISTS role (
    id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    description VARCHAR(255) NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS doctor_profile (
    id VARCHAR(64) PRIMARY KEY,
    full_name VARCHAR(128) NOT NULL,
    department VARCHAR(128) NOT NULL,
    title_name VARCHAR(128) NOT NULL,
    license_number VARCHAR(64) NULL,
    phone VARCHAR(64) NULL,
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL,
    UNIQUE KEY uk_doctor_license (license_number)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_account (
    id VARCHAR(64) PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    display_name VARCHAR(128) NOT NULL,
    role_id VARCHAR(64) NOT NULL,
    doctor_id VARCHAR(64) NULL,
    password_hash VARCHAR(255) NOT NULL,
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL,
    last_login_at DATETIME NULL,
    CONSTRAINT fk_user_role FOREIGN KEY (role_id) REFERENCES role(id),
    CONSTRAINT fk_user_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id)
) ENGINE=InnoDB;

INSERT INTO role (id, name, description) VALUES
    ('operator', '操作员', '设备操作与治疗执行人员'),
    ('physician', '医生', '临床医生账号'),
    ('engineer', '工程师', '设备维护与工程调试人员'),
    ('administrator', '管理员', '系统管理人员')
ON DUPLICATE KEY UPDATE name = VALUES(name), description = VALUES(description);

CREATE TABLE IF NOT EXISTS patient (
    id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    age INT NOT NULL,
    gender VARCHAR(32) NOT NULL,
    diagnosis VARCHAR(255) NOT NULL,
    contact VARCHAR(64) NULL,
    deleted_at DATETIME NULL,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS image_series (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    type VARCHAR(32) NOT NULL,
    storage_path VARCHAR(255) NOT NULL,
    acquisition_date DATE NOT NULL,
    notes VARCHAR(255) NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_image_series_patient FOREIGN KEY (patient_id) REFERENCES patient(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS therapy_plan (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL,
    pattern VARCHAR(32) NOT NULL,
    approval_state VARCHAR(32) NOT NULL,
    planned_power_watts DOUBLE NOT NULL,
    spacing_mm DOUBLE NOT NULL,
    respiratory_tracking_enabled TINYINT(1) NOT NULL DEFAULT 0,
    serialized_payload MEDIUMTEXT NOT NULL,
    created_at DATETIME NOT NULL,
    approved_at DATETIME NULL,
    approved_by VARCHAR(64) NULL,
    CONSTRAINT fk_therapy_plan_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_therapy_plan_approver FOREIGN KEY (approved_by) REFERENCES user_account(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS therapy_segment (
    id VARCHAR(64) PRIMARY KEY,
    plan_id VARCHAR(64) NOT NULL,
    order_index INT NOT NULL,
    label_name VARCHAR(128) NOT NULL,
    planned_duration_seconds DOUBLE NOT NULL,
    serialized_payload MEDIUMTEXT NOT NULL,
    CONSTRAINT fk_therapy_segment_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_session (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    plan_id VARCHAR(64) NOT NULL,
    treatment_date DATETIME NOT NULL,
    lesion_type VARCHAR(128) NOT NULL,
    total_energy_j DOUBLE NOT NULL,
    total_duration_seconds DOUBLE NOT NULL,
    path_summary VARCHAR(255) NOT NULL,
    dose DOUBLE NOT NULL,
    status VARCHAR(32) NOT NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_treatment_session_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_treatment_session_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_record (
    id VARCHAR(64) PRIMARY KEY,
    session_id VARCHAR(64) NOT NULL,
    segment_index INT NOT NULL,
    point_index INT NOT NULL,
    delivered_energy_j DOUBLE NOT NULL,
    delivered_dose DOUBLE NOT NULL,
    executed_at DATETIME NOT NULL,
    CONSTRAINT fk_treatment_record_session FOREIGN KEY (session_id) REFERENCES treatment_session(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_report (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    treatment_session_id VARCHAR(64) NOT NULL,
    generated_at DATETIME NOT NULL,
    title VARCHAR(128) NOT NULL,
    content_html MEDIUMTEXT NOT NULL,
    notes VARCHAR(255) NULL,
    CONSTRAINT fk_treatment_report_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_treatment_report_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS device_snapshot (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    voltage DOUBLE NOT NULL,
    current DOUBLE NOT NULL,
    power_watts DOUBLE NOT NULL,
    water_pressure_mpa DOUBLE NOT NULL,
    water_flow_lpm DOUBLE NOT NULL,
    transducer_temp_c DOUBLE NOT NULL,
    vibration_frequency_mhz DOUBLE NOT NULL,
    motor_load_percent DOUBLE NOT NULL,
    position_x DOUBLE NOT NULL,
    position_y DOUBLE NOT NULL,
    position_z DOUBLE NOT NULL,
    position_a DOUBLE NOT NULL,
    position_b DOUBLE NOT NULL,
    position_c DOUBLE NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS alarm_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    raised_at DATETIME NOT NULL,
    level_name VARCHAR(32) NOT NULL,
    reason_code VARCHAR(64) NOT NULL,
    message_text VARCHAR(255) NOT NULL,
    source_name VARCHAR(64) NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS audit_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    occurred_at DATETIME NOT NULL,
    actor_name VARCHAR(128) NOT NULL,
    category_name VARCHAR(64) NOT NULL,
    details TEXT NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS config_profile (
    id VARCHAR(64) PRIMARY KEY,
    profile_name VARCHAR(128) NOT NULL,
    category_name VARCHAR(64) NOT NULL,
    version_name VARCHAR(64) NOT NULL,
    checksum_value VARCHAR(128) NOT NULL,
    approval_state VARCHAR(32) NOT NULL,
    serialized_payload MEDIUMTEXT NOT NULL,
    created_at DATETIME NOT NULL,
    approved_at DATETIME NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS doctor_login_session (
    id VARCHAR(64) PRIMARY KEY,
    user_account_id VARCHAR(64) NOT NULL,
    doctor_id VARCHAR(64) NULL,
    login_at DATETIME NOT NULL,
    logout_at DATETIME NULL,
    login_result VARCHAR(32) NOT NULL,
    failure_reason VARCHAR(255) NULL,
    workstation_name VARCHAR(128) NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_login_session_user FOREIGN KEY (user_account_id) REFERENCES user_account(id),
    CONSTRAINT fk_login_session_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    INDEX idx_login_session_user_time (user_account_id, login_at),
    INDEX idx_login_session_doctor_time (doctor_id, login_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS clinical_operation_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    doctor_id VARCHAR(64) NULL,
    patient_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    occurred_at DATETIME NOT NULL,
    screen_name VARCHAR(64) NOT NULL,
    module_name VARCHAR(64) NOT NULL,
    function_name VARCHAR(128) NOT NULL,
    operation_name VARCHAR(128) NOT NULL,
    operation_payload MEDIUMTEXT NULL,
    result_state VARCHAR(32) NOT NULL,
    result_message VARCHAR(255) NULL,
    evidence_level VARCHAR(32) NOT NULL DEFAULT 'normal',
    workstation_name VARCHAR(128) NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_operation_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_operation_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_operation_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    CONSTRAINT fk_operation_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_operation_treatment_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_operation_time (occurred_at),
    INDEX idx_operation_doctor_time (doctor_id, occurred_at),
    INDEX idx_operation_patient_time (patient_id, occurred_at),
    INDEX idx_operation_session_time (treatment_session_id, occurred_at),
    INDEX idx_operation_screen_module (screen_name, module_name)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS device_control_event (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    occurred_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    doctor_id VARCHAR(64) NULL,
    patient_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    subsystem_name VARCHAR(64) NOT NULL,
    function_name VARCHAR(128) NOT NULL,
    command_name VARCHAR(128) NOT NULL,
    request_payload MEDIUMTEXT NULL,
    result_state VARCHAR(32) NOT NULL,
    result_message VARCHAR(255) NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_device_event_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_device_event_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_device_event_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    CONSTRAINT fk_device_event_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_device_event_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_device_event_time (occurred_at),
    INDEX idx_device_event_subsystem_time (subsystem_name, occurred_at),
    INDEX idx_device_event_session_time (treatment_session_id, occurred_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS water_pump_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    serial_port VARCHAR(64) NULL,
    baud_rate INT NULL,
    upper_tank_target_cm DOUBLE NULL,
    upper_tank_level_cm DOUBLE NULL,
    pump_03_state VARCHAR(32) NULL,
    pump_02_state VARCHAR(32) NULL,
    pump_ro_state VARCHAR(32) NULL,
    vacuum_pump_state VARCHAR(32) NULL,
    loop_state VARCHAR(32) NULL,
    connection_state VARCHAR(32) NOT NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_water_pump_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_water_pump_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_water_pump_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_water_pump_time (captured_at),
    INDEX idx_water_pump_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS temperature_control_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    serial_port VARCHAR(64) NULL,
    baud_rate INT NULL,
    channel_name VARCHAR(32) NULL,
    controller_address VARCHAR(32) NULL,
    set_temperature_c DOUBLE NULL,
    measured_temperature_c DOUBLE NULL,
    connection_state VARCHAR(32) NOT NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_temp_control_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_temp_control_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_temp_control_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_temp_control_time (captured_at),
    INDEX idx_temp_control_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS liquid_level_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    serial_port VARCHAR(64) NULL,
    baud_rate INT NULL,
    device_address VARCHAR(32) NULL,
    current_level_value DOUBLE NULL,
    upper_limit_state VARCHAR(32) NULL,
    lower_limit_state VARCHAR(32) NULL,
    connection_state VARCHAR(32) NOT NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_liquid_level_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_liquid_level_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_liquid_level_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_liquid_level_time (captured_at),
    INDEX idx_liquid_level_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS robot_arm_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    controller_ip VARCHAR(64) NULL,
    connection_state VARCHAR(32) NOT NULL,
    power_button_state TINYINT(1) NULL,
    robot_connected_state TINYINT(1) NULL,
    enabled_state TINYINT(1) NULL,
    drag_mode_state TINYINT(1) NULL,
    trajectory_name VARCHAR(128) NULL,
    position_x DOUBLE NULL,
    position_y DOUBLE NULL,
    position_z DOUBLE NULL,
    position_rx DOUBLE NULL,
    position_ry DOUBLE NULL,
    position_rz DOUBLE NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_robot_arm_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_robot_arm_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_robot_arm_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_robot_arm_time (captured_at),
    INDEX idx_robot_arm_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS three_motor_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    sdk_path VARCHAR(255) NULL,
    gateway_name VARCHAR(128) NULL,
    connection_state VARCHAR(32) NOT NULL,
    emergency_stop_state VARCHAR(32) NULL,
    motor_6_node VARCHAR(64) NULL,
    motor_6_position_value DOUBLE NULL,
    motor_6_position_unit VARCHAR(32) NULL,
    motor_6_state VARCHAR(32) NULL,
    motor_7_node VARCHAR(64) NULL,
    motor_7_position_value DOUBLE NULL,
    motor_7_position_unit VARCHAR(32) NULL,
    motor_7_state VARCHAR(32) NULL,
    motor_8_node VARCHAR(64) NULL,
    motor_8_position_value DOUBLE NULL,
    motor_8_position_unit VARCHAR(32) NULL,
    motor_8_state VARCHAR(32) NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_three_motor_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_three_motor_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_three_motor_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_three_motor_time (captured_at),
    INDEX idx_three_motor_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS power_device_status (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    captured_at DATETIME NOT NULL,
    login_session_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    device_type VARCHAR(64) NOT NULL,
    resource_name VARCHAR(255) NULL,
    serial_port VARCHAR(64) NULL,
    baud_rate INT NULL,
    channel_name VARCHAR(32) NULL,
    output_enabled TINYINT(1) NULL,
    duty_cycle_percent DOUBLE NULL,
    amplification_factor DOUBLE NULL,
    input_voltage_v DOUBLE NULL,
    output_current_a DOUBLE NULL,
    output_power_w DOUBLE NULL,
    connection_state VARCHAR(32) NOT NULL,
    error_message VARCHAR(255) NULL,
    raw_message TEXT NULL,
    CONSTRAINT fk_power_device_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    CONSTRAINT fk_power_device_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_power_device_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    INDEX idx_power_device_time (captured_at),
    INDEX idx_power_device_type_time (device_type, captured_at),
    INDEX idx_power_device_session_time (treatment_session_id, captured_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS robot_acquisition_path (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    plan_id VARCHAR(64) NULL,
    created_by VARCHAR(64) NULL,
    doctor_id VARCHAR(64) NULL,
    path_name VARCHAR(128) NOT NULL,
    source_file_path VARCHAR(255) NULL,
    layer_count INT NULL,
    step_mm DOUBLE NULL,
    total_length_mm DOUBLE NULL,
    coordinate_payload MEDIUMTEXT NOT NULL,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL,
    CONSTRAINT fk_robot_path_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_robot_path_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id),
    CONSTRAINT fk_robot_path_user FOREIGN KEY (created_by) REFERENCES user_account(id),
    CONSTRAINT fk_robot_path_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    INDEX idx_robot_path_patient_time (patient_id, created_at),
    INDEX idx_robot_path_plan (plan_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS historical_treatment_image (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    treatment_session_id VARCHAR(64) NULL,
    image_series_id VARCHAR(64) NULL,
    storage_path VARCHAR(255) NOT NULL,
    thumbnail_path VARCHAR(255) NULL,
    image_type VARCHAR(64) NOT NULL,
    captured_at DATETIME NULL,
    imported_at DATETIME NOT NULL,
    imported_by VARCHAR(64) NULL,
    notes VARCHAR(255) NULL,
    CONSTRAINT fk_historical_image_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_historical_image_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_historical_image_series FOREIGN KEY (image_series_id) REFERENCES image_series(id),
    CONSTRAINT fk_historical_image_user FOREIGN KEY (imported_by) REFERENCES user_account(id),
    INDEX idx_historical_image_patient_time (patient_id, imported_at),
    INDEX idx_historical_image_session (treatment_session_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_image_capture (
    id VARCHAR(64) PRIMARY KEY,
    patient_id VARCHAR(64) NOT NULL,
    plan_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    acquisition_path_id VARCHAR(64) NULL,
    captured_by VARCHAR(64) NULL,
    source_channel VARCHAR(64) NULL,
    layer_index INT NOT NULL,
    slice_index INT NOT NULL,
    storage_path VARCHAR(255) NOT NULL,
    thumbnail_path VARCHAR(255) NULL,
    robot_position_x DOUBLE NULL,
    robot_position_y DOUBLE NULL,
    robot_position_z DOUBLE NULL,
    robot_position_rx DOUBLE NULL,
    robot_position_ry DOUBLE NULL,
    robot_position_rz DOUBLE NULL,
    captured_at DATETIME NOT NULL,
    acquisition_parameters MEDIUMTEXT NULL,
    CONSTRAINT fk_capture_image_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_capture_image_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id),
    CONSTRAINT fk_capture_image_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_capture_image_path FOREIGN KEY (acquisition_path_id) REFERENCES robot_acquisition_path(id),
    CONSTRAINT fk_capture_image_user FOREIGN KEY (captured_by) REFERENCES user_account(id),
    INDEX idx_capture_image_patient_time (patient_id, captured_at),
    INDEX idx_capture_image_plan_slice (plan_id, layer_index, slice_index),
    INDEX idx_capture_image_session (treatment_session_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_image_annotation (
    id VARCHAR(64) PRIMARY KEY,
    captured_image_id VARCHAR(64) NOT NULL,
    patient_id VARCHAR(64) NOT NULL,
    plan_id VARCHAR(64) NULL,
    treatment_session_id VARCHAR(64) NULL,
    annotated_by VARCHAR(64) NULL,
    annotation_state VARCHAR(32) NOT NULL DEFAULT 'saved',
    target_label VARCHAR(128) NULL,
    annotated_image_path VARCHAR(255) NOT NULL,
    mask_path VARCHAR(255) NULL,
    annotation_payload MEDIUMTEXT NOT NULL,
    target_area_mm2 DOUBLE NULL,
    target_volume_mm3 DOUBLE NULL,
    saved_reason VARCHAR(64) NOT NULL,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL,
    CONSTRAINT fk_annotation_capture FOREIGN KEY (captured_image_id) REFERENCES treatment_image_capture(id),
    CONSTRAINT fk_annotation_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_annotation_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id),
    CONSTRAINT fk_annotation_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_annotation_user FOREIGN KEY (annotated_by) REFERENCES user_account(id),
    INDEX idx_annotation_capture_time (captured_image_id, updated_at),
    INDEX idx_annotation_patient_time (patient_id, updated_at),
    INDEX idx_annotation_session_time (treatment_session_id, updated_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_continue_import (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    previous_session_id VARCHAR(64) NULL,
    new_session_id VARCHAR(64) NULL,
    annotation_id VARCHAR(64) NOT NULL,
    imported_by VARCHAR(64) NULL,
    imported_at DATETIME NOT NULL,
    import_reason VARCHAR(255) NULL,
    CONSTRAINT fk_continue_previous_session FOREIGN KEY (previous_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_continue_new_session FOREIGN KEY (new_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_continue_annotation FOREIGN KEY (annotation_id) REFERENCES treatment_image_annotation(id),
    CONSTRAINT fk_continue_user FOREIGN KEY (imported_by) REFERENCES user_account(id),
    INDEX idx_continue_previous_session (previous_session_id),
    INDEX idx_continue_new_session (new_session_id),
    INDEX idx_continue_annotation (annotation_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS therapy_plan_evaluation (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    plan_id VARCHAR(64) NOT NULL,
    patient_id VARCHAR(64) NOT NULL,
    evaluated_by VARCHAR(64) NULL,
    evaluated_at DATETIME NOT NULL,
    expected_volume_cm3 DOUBLE NULL,
    treated_volume_cm3 DOUBLE NULL,
    treatment_progress_percent DOUBLE NULL,
    current_power_w DOUBLE NULL,
    evaluation_payload MEDIUMTEXT NULL,
    result_state VARCHAR(32) NOT NULL,
    result_message VARCHAR(255) NULL,
    CONSTRAINT fk_plan_eval_plan FOREIGN KEY (plan_id) REFERENCES therapy_plan(id),
    CONSTRAINT fk_plan_eval_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_plan_eval_user FOREIGN KEY (evaluated_by) REFERENCES user_account(id),
    INDEX idx_plan_eval_plan_time (plan_id, evaluated_at),
    INDEX idx_plan_eval_patient_time (patient_id, evaluated_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS treatment_termination_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    treatment_session_id VARCHAR(64) NULL,
    patient_id VARCHAR(64) NULL,
    doctor_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    login_session_id VARCHAR(64) NULL,
    terminated_at DATETIME NOT NULL,
    termination_type VARCHAR(32) NOT NULL,
    reason_text VARCHAR(255) NULL,
    final_status VARCHAR(32) NOT NULL,
    continue_treatment_selected TINYINT(1) NOT NULL DEFAULT 0,
    summary_payload MEDIUMTEXT NULL,
    CONSTRAINT fk_termination_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_termination_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_termination_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    CONSTRAINT fk_termination_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    CONSTRAINT fk_termination_login_session FOREIGN KEY (login_session_id) REFERENCES doctor_login_session(id),
    INDEX idx_termination_session_time (treatment_session_id, terminated_at),
    INDEX idx_termination_patient_time (patient_id, terminated_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS medical_evidence_document (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    treatment_session_id VARCHAR(64) NULL,
    patient_id VARCHAR(64) NULL,
    doctor_id VARCHAR(64) NULL,
    login_user_id VARCHAR(64) NULL,
    generated_at DATETIME NOT NULL,
    document_type VARCHAR(64) NOT NULL DEFAULT 'treatment_operation_evidence',
    docx_path VARCHAR(255) NOT NULL,
    source_log_start_at DATETIME NULL,
    source_log_end_at DATETIME NULL,
    checksum_sha256 VARCHAR(128) NULL,
    generation_state VARCHAR(32) NOT NULL,
    generated_by VARCHAR(128) NULL,
    notes VARCHAR(255) NULL,
    CONSTRAINT fk_evidence_session FOREIGN KEY (treatment_session_id) REFERENCES treatment_session(id),
    CONSTRAINT fk_evidence_patient FOREIGN KEY (patient_id) REFERENCES patient(id),
    CONSTRAINT fk_evidence_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id),
    CONSTRAINT fk_evidence_user FOREIGN KEY (login_user_id) REFERENCES user_account(id),
    INDEX idx_evidence_session_time (treatment_session_id, generated_at),
    INDEX idx_evidence_patient_time (patient_id, generated_at)
) ENGINE=InnoDB;
