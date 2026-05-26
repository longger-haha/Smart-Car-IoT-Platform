-- ============================================================
-- SmartRover IoT 安全平台 数据库初始化脚本
-- 数据库: smartrover_db  字符集: utf8mb4
-- ============================================================

CREATE DATABASE IF NOT EXISTS smartrover_db
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

USE smartrover_db;

-- ============================================================
-- 表 1: users — 用户账户表
-- ============================================================
CREATE TABLE IF NOT EXISTS users (
    id              INT           NOT NULL AUTO_INCREMENT,
    username        VARCHAR(64)   NOT NULL UNIQUE,
    password_hash   VARCHAR(256)  NOT NULL,
    role            ENUM('admin','user') NOT NULL DEFAULT 'user',
    created_at      DATETIME      NOT NULL DEFAULT NOW(),
    last_login_at   DATETIME      NULL,
    PRIMARY KEY (id),
    INDEX idx_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户账户表';

-- 预置管理员账号 (密码: admin123, bcrypt hash)
-- 实际部署时由后端初始化脚本写入，此处仅占位注释
-- INSERT INTO users (username, password_hash, role) VALUES
--   ('admin', '<bcrypt_hash>', 'admin');

-- ============================================================
-- 表 2: devices — 设备白名单表
-- ============================================================
CREATE TABLE IF NOT EXISTS devices (
    id              INT           NOT NULL AUTO_INCREMENT,
    user_id         INT           NOT NULL         COMMENT '所属租户ID',
    device_id       VARCHAR(64)   NOT NULL UNIQUE COMMENT '设备唯一标识 (如MAC地址)',
    device_secret   VARCHAR(128)  NOT NULL         COMMENT 'HMAC签名派生密钥 (强随机)',
    name            VARCHAR(128)  NULL             COMMENT '设备别名',
    status          ENUM('online','offline') NOT NULL DEFAULT 'offline',
    last_seen_at    DATETIME      NULL             COMMENT '最近心跳时间',
    registered_at   DATETIME      NOT NULL DEFAULT NOW(),
    PRIMARY KEY (id),
    INDEX idx_devices_device_id (device_id),
    INDEX idx_devices_status (status),
    INDEX idx_devices_user (user_id),
    CONSTRAINT fk_devices_user
        FOREIGN KEY (user_id) REFERENCES users(id)
        ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='设备白名单表';

-- ============================================================
-- 表 3: telemetry_points — 传感器遥测数据表
-- ============================================================
CREATE TABLE IF NOT EXISTS telemetry_points (
    id              BIGINT        NOT NULL AUTO_INCREMENT,
    device_id       VARCHAR(64)   NOT NULL COMMENT '关联设备ID',
    latitude        DECIMAL(10,7) NULL     COMMENT 'GPS纬度',
    longitude       DECIMAL(10,7) NULL     COMMENT 'GPS经度',
    temperature     FLOAT         NULL     COMMENT '温度(°C)',
    humidity        FLOAT         NULL     COMMENT '湿度(%)',
    ultrasonic_cm   FLOAT         NULL     COMMENT '超声波距离(cm)',
    ir_obstacle     TINYINT(1)    NULL     COMMENT '红外避障(0=无障碍,1=检测到)',
    ir_l            TINYINT(1)    NULL     COMMENT '左红外(0=安全,1=障碍)',
    ir_r            TINYINT(1)    NULL     COMMENT '右红外(0=安全,1=障碍)',
    imu_heading     FLOAT         NULL     COMMENT 'IMU航向角(°)',
    imu_gyro_z      FLOAT         NULL     COMMENT 'IMU Z轴角速度(°/s)',
    imu_ax          FLOAT         NULL     COMMENT '加速度X(g)',
    imu_ay          FLOAT         NULL     COMMENT '加速度Y(g)',
    imu_az          FLOAT         NULL     COMMENT '加速度Z(g)',
    imu_gx          FLOAT         NULL     COMMENT '角速度X(°/s)',
    imu_gy          FLOAT         NULL     COMMENT '角速度Y(°/s)',
    imu_gz          FLOAT         NULL     COMMENT '角速度Z(°/s)',
    speed_pwm       SMALLINT      NULL     COMMENT '电机PWM值',
    altitude        FLOAT         NULL     COMMENT 'GPS海拔(m)',
    speed_kmh       FLOAT         NULL     COMMENT 'GPS速度(km/h)',
    satellites      SMALLINT      NULL     COMMENT 'GPS卫星数',
    bat_mv          INT           NULL     COMMENT '电池电压(mV)',
    seq             INT           NULL     COMMENT '消息序号',
    cruise_active   TINYINT(1)    NULL     COMMENT '巡航是否激活',
    cruise_state    VARCHAR(16)   NULL     COMMENT '巡航状态(idle/avoiding/recovery/stuck)',
    avoid_state     SMALLINT      NULL     COMMENT '避障状态机(0-5)',
    raw_ciphertext  TEXT          NULL     COMMENT '原始密文(供审计展示)',
    recorded_at     DATETIME      NOT NULL DEFAULT NOW() COMMENT '数据时间戳',
    PRIMARY KEY (id),
    INDEX idx_telemetry_device_time (device_id, recorded_at),
    CONSTRAINT fk_telemetry_device
        FOREIGN KEY (device_id) REFERENCES devices(device_id)
        ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='传感器遥测数据表';

-- ============================================================
-- 表 4: security_audit_logs — 安全攻防日志表
-- ============================================================
CREATE TABLE IF NOT EXISTS security_audit_logs (
    id                BIGINT    NOT NULL AUTO_INCREMENT,
    event_type        ENUM('replay','ddos','auth_fail','rbac_deny','sig_invalid') NOT NULL COMMENT '攻击类型',
    source_ip         VARCHAR(64)  NULL COMMENT '攻击来源IP',
    target_device_id  VARCHAR(64)  NULL COMMENT '被针对的设备ID',
    user_id           INT          NULL COMMENT '关联用户ID',
    detail            TEXT         NULL COMMENT '详细描述',
    is_blocked        TINYINT(1)   NOT NULL DEFAULT 1 COMMENT '是否成功拦截',
    occurred_at       DATETIME     NOT NULL DEFAULT NOW() COMMENT '事件发生时间',
    PRIMARY KEY (id),
    INDEX idx_audit_occurred_at (occurred_at),
    INDEX idx_audit_event_type (event_type),
    INDEX idx_audit_device (target_device_id),
    INDEX idx_audit_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='安全攻防日志表';
