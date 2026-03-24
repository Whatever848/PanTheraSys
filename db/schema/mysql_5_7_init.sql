CREATE DATABASE IF NOT EXISTS panthera_sys DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE panthera_sys;

CREATE TABLE IF NOT EXISTS role (
    id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    description VARCHAR(255) NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_account (
    id VARCHAR(64) PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    display_name VARCHAR(128) NOT NULL,
    role_id VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL,
    updated_at DATETIME NOT NULL,
    CONSTRAINT fk_user_role FOREIGN KEY (role_id) REFERENCES role(id)
) ENGINE=InnoDB;

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
