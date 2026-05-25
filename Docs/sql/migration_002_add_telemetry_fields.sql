-- Migration 002: 为 telemetry_points 表添加 ESP32 v1.1 新增字段
-- ESP32 固件上报了 ir_l/ir_r/imu_ax~gz/bat_mv/seq, 但数据库表缺少这些列
-- 导致 MQTT 写入时这些字段被丢弃, 前端无法显示传感器数据

ALTER TABLE telemetry_points
  ADD COLUMN ir_l   TINYINT(1) NULL COMMENT '左红外(0=安全,1=障碍)' AFTER ir_obstacle,
  ADD COLUMN ir_r   TINYINT(1) NULL COMMENT '右红外(0=安全,1=障碍)' AFTER ir_l,
  ADD COLUMN imu_ax FLOAT      NULL COMMENT '加速度X(g)'              AFTER imu_gyro_z,
  ADD COLUMN imu_ay FLOAT      NULL COMMENT '加速度Y(g)'              AFTER imu_ax,
  ADD COLUMN imu_az FLOAT      NULL COMMENT '加速度Z(g)'              AFTER imu_ay,
  ADD COLUMN imu_gx FLOAT      NULL COMMENT '角速度X(°/s)'           AFTER imu_az,
  ADD COLUMN imu_gy FLOAT      NULL COMMENT '角速度Y(°/s)'           AFTER imu_gx,
  ADD COLUMN imu_gz FLOAT      NULL COMMENT '角速度Z(°/s)'           AFTER imu_gy,
  ADD COLUMN bat_mv INT        NULL COMMENT '电池电压(mV)'            AFTER satellites,
  ADD COLUMN seq    INT        NULL COMMENT '消息序号'                AFTER bat_mv;
