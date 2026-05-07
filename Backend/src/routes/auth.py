"""
auth.py — 鉴权路由蓝图
POST /api/auth/login     → 返回 JWT Token + 用户角色
POST /api/auth/register  → 用户注册（默认 user 角色）

T019 [US1]: 登录接口实现
"""

import bcrypt
from datetime import datetime

from flask import Blueprint, request, jsonify
from flask_jwt_extended import create_access_token

from src.extensions import db
from src.models.user import User
from src.models.audit_log import SecurityAuditLog

auth_bp = Blueprint('auth', __name__)


@auth_bp.post('/login')
def login():
    """
    用户登录接口

    Request JSON:
        { "username": "admin", "password": "admin123" }

    Response 200:
        {
            "access_token": "<JWT>",
            "role": "admin",
            "username": "admin"
        }

    Response 401:
        { "error": "用户名或密码错误" }
    """
    data     = request.get_json(silent=True) or {}
    username = data.get('username', '').strip()
    password = data.get('password', '')

    if not username or not password:
        return jsonify({'error': '用户名和密码不能为空'}), 400

    user = User.query.filter_by(username=username).first()

    if not user or not bcrypt.checkpw(password.encode('utf-8'),
                                       user.password_hash.encode('utf-8')):
        # 登录失败 → 写审计日志
        SecurityAuditLog.record(
            event_type='auth_fail',
            source_ip=request.remote_addr,
            detail=f'Login failed for username={username!r}',
        )
        db.session.commit()
        return jsonify({'error': '用户名或密码错误'}), 401

    # 更新最近登录时间
    user.last_login_at = datetime.now()
    db.session.commit()

    # 生成 JWT，将 role 写入 additional_claims
    access_token = create_access_token(
        identity=username,
        additional_claims={'role': user.role},
    )

    return jsonify({
        'access_token': access_token,
        'role':         user.role,
        'username':     user.username,
    }), 200


@auth_bp.post('/register')
def register():
    """
    用户注册接口

    Request JSON:
        {
            "username": "zhangsan",
            "password": "mypassword"
        }

    Response 201:
        {
            "message":  "注册成功",
            "username": "zhangsan",
            "role":     "user"
        }

    Response 400:
        { "error": "用户名和密码不能为空" }

    Response 409:
        { "error": "用户名已存在" }
    """
    data     = request.get_json(silent=True) or {}
    username = data.get('username', '').strip()
    password = data.get('password', '')

    if not username or not password:
        return jsonify({'error': '用户名和密码不能为空'}), 400

    if len(password) < 6:
        return jsonify({'error': '密码长度不能少于 6 位'}), 400

    if User.query.filter_by(username=username).first():
        return jsonify({'error': f'用户名 {username!r} 已存在'}), 409

    hashed = bcrypt.hashpw(password.encode('utf-8'), bcrypt.gensalt()).decode('utf-8')
    user   = User(username=username, password_hash=hashed, role='user')
    db.session.add(user)
    db.session.commit()

    return jsonify({
        'message':  '注册成功',
        'username': user.username,
        'role':     user.role,
    }), 201


@auth_bp.get('/me')
def me():
    """
    健康检查辅助接口：返回当前已创建的用户数量（不需要鉴权）。
    用于初始化确认 admin 用户是否存在。
    """
    count = User.query.count()
    return jsonify({'total_users': count}), 200


@auth_bp.cli.command('init-admin')
def init_admin():
    """
    CLI 命令：初始化默认 admin 账号 (密码: admin123)。
    用法: flask --app app auth init-admin
    """
    if User.query.filter_by(username='admin').first():
        print('Admin user already exists.')
        return

    hashed = bcrypt.hashpw(b'admin123', bcrypt.gensalt()).decode('utf-8')
    admin  = User(username='admin', password_hash=hashed, role='admin')
    db.session.add(admin)
    db.session.commit()
    print('Admin user created: username=admin, password=admin123')
