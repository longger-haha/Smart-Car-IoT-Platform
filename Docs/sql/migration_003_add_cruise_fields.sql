-- migration_003_add_cruise_fields.sql
-- 新增巡航状态字段: cruise_active, cruise_state, avoid_state

ALTER TABLE telemetry_points
  ADD COLUMN cruise_active  TINYINT(1)   NULL COMMENT '巡航是否激活' AFTER seq,
  ADD COLUMN cruise_state   VARCHAR(16)  NULL COMMENT '巡航状态(idle/avoiding/recovery/stuck)' AFTER cruise_active,
  ADD COLUMN avoid_state    SMALLINT     NULL COMMENT '避障状态机(0-5)' AFTER cruise_state;
