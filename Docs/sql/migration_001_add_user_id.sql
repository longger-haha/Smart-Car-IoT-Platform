-- Migration: 为 security_audit_logs 表新增 user_id 列
-- 对应 P2 审计日志系统改造

ALTER TABLE security_audit_logs
  ADD COLUMN user_id INT NULL COMMENT '关联用户ID' AFTER target_device_id,
  ADD INDEX idx_audit_user_id (user_id);
