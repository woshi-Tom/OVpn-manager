from flask import Flask, render_template, request, jsonify, session, redirect, url_for, flash, make_response, send_from_directory
import config
import db
import core_client
import logging
from logging.handlers import RotatingFileHandler
from functools import wraps
import os

# 登录装饰器
def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

# 导入蓝图
from routes.index import bp as index_bp
from routes.configs import bp as configs_bp
from routes.clients import bp as clients_bp
from routes.api import bp as api_bp
from routes.sessions import bp as sessions_bp
from routes.auth import bp as auth_bp

app = Flask(__name__)

# 先加载配置，再设置SECRET_KEY
try:
    cfg = config.get_config()
    app.config['SECRET_KEY'] = cfg['flask']['secret_key']
    app.config['DEBUG'] = cfg['flask'].get('debug', False)
except Exception as e:
    app.config['SECRET_KEY'] = 'dev-secret-key-change-in-production'
    app.config['DEBUG'] = True

# Session配置
app.config['SESSION_PERMANENT'] = False
app.config['SESSION_COOKIE_HTTPONLY'] = True
app.config['SESSION_COOKIE_SECURE'] = False
app.config['PERMANENT_SESSION_LIFETIME'] = 1800

# 注册蓝图
app.register_blueprint(auth_bp)
app.register_blueprint(index_bp)
app.register_blueprint(configs_bp)
app.register_blueprint(clients_bp)
app.register_blueprint(sessions_bp)
app.register_blueprint(api_bp)

# 登录状态检查
@app.before_request
def check_login():
    from flask import request
    allowed_endpoints = ['auth.login', 'auth.logout', 'static', 'favicon_handler', 'favicon_svg']
    if request.endpoint and request.endpoint not in allowed_endpoints:
        if 'admin_id' not in session:
            from flask import redirect, url_for
            return redirect(url_for('auth.login'))

# 初始化数据库连接
db.get_connection()

# 日志配置
log_cfg = config.get_config()['log']
handler = RotatingFileHandler(log_cfg['file'], maxBytes=10485760, backupCount=5)
handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s: %(message)s'))
app.logger.addHandler(handler)
app.logger.setLevel(logging.INFO)

# 日志路由
@app.route('/logs')
@login_required
def logs():
    level = request.args.get('level', 'all')
    limit = request.args.get('limit', 100)
    
    if level == 'all':
        logs_data = db.execute_query("""
            SELECT * FROM system_logs ORDER BY timestamp DESC LIMIT %s
        """, (int(limit),), fetchall=True)
    else:
        logs_data = db.execute_query("""
            SELECT * FROM system_logs WHERE level = %s ORDER BY timestamp DESC LIMIT %s
        """, (level, int(limit)), fetchall=True)
    
    return render_template('logs.html', logs=logs_data, current_level=level)

@app.route('/logs/api')
@login_required
def logs_api():
    level = request.args.get('level', 'all')
    limit = request.args.get('limit', 100)
    
    if level == 'all':
        logs_data = db.execute_query("""
            SELECT * FROM system_logs ORDER BY timestamp DESC LIMIT %s
        """, (int(limit),), fetchall=True)
    else:
        logs_data = db.execute_query("""
            SELECT * FROM system_logs WHERE level = %s ORDER BY timestamp DESC LIMIT %s
        """, (level, int(limit)), fetchall=True)
    
    return jsonify({'status': 'ok', 'data': logs_data})

# Favicon路由 - 访问/favicon.ico时返回SVG图标
@app.route('/favicon.ico')
def favicon_handler():
    static_dir = os.path.join(os.path.dirname(__file__), 'static')
    response = send_from_directory(static_dir, 'favicon.svg', mimetype='image/svg+xml')
    response.headers['Cache-Control'] = 'no-cache, no-store, must-revalidate'
    return response

@app.route('/static/favicon.svg')
def favicon_svg():
    static_dir = os.path.join(os.path.dirname(__file__), 'static')
    response = send_from_directory(static_dir, 'favicon.svg', mimetype='image/svg+xml')
    response.headers['Cache-Control'] = 'no-cache, no-store, must-revalidate'
    return response

if __name__ == '__main__':
    flask_cfg = config.get_config()['flask']
    app.run(
        host=flask_cfg.get('host', '127.0.0.1'),
        port=flask_cfg.get('port', 5000),
        debug=flask_cfg.get('debug', False)
    )
