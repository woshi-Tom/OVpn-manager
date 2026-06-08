from flask import Blueprint, render_template, request, jsonify, send_file
from flask import session, redirect, url_for
from functools import wraps
import db
import core_client
import tempfile
import os
import atexit

bp = Blueprint('clients', __name__, url_prefix='/clients')

# 临时文件跟踪，应用退出时清理
_temp_files = []
atexit.register(lambda: [os.unlink(f) for f in _temp_files if os.path.exists(f)])

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

@bp.route('/')
@login_required
def list_clients():
    clients = db.execute_query("""
        SELECT u.id, u.username, u.client_name, u.email, u.disabled,
               cp.id as profile_id, cp.revoked,
               cp.assigned_ip, cp.use_static_ip,
               c.config_name
        FROM vpn_users u
        LEFT JOIN vpn_client_profiles cp ON u.id = cp.user_id
        LEFT JOIN vpn_config c ON cp.config_id = c.id
        ORDER BY u.id
    """, fetchall=True)
    return render_template('clients.html', clients=clients)

@bp.route('/add', methods=['GET', 'POST'])
@login_required
def add_client():
    if request.method == 'GET':
        configs = db.execute_query("SELECT id, config_name FROM vpn_config", fetchall=True)
        return render_template('add_client.html', configs=configs)
    data = request.get_json()
    username = data.get('username')
    client_name = data.get('client_name')
    email = data.get('email')
    config_id = data.get('config_id')
    assigned_ip = data.get('assigned_ip') or None
    cert_cn = data.get('cert_cn') or None
    valid_days = data.get('valid_days', 365)
    try:
        valid_days = int(valid_days)
    except:
        valid_days = 365
    if not username or not config_id:
        return jsonify({'status': 'error', 'message': '缺少必要字段'})
    try:
        resp = core_client.send_command('gen_client_cert', {
            'username': username,
            'client_name': client_name,
            'email': email,
            'config_id': int(config_id),
            'assigned_ip': assigned_ip,
            'cert_cn': cert_cn,
            'valid_days': valid_days
        })
        if resp.get('status') == 'ok':
            profile_id = resp.get('profile_id')
            return jsonify({'status': 'ok', 'message': '客户端创建成功', 'profile_id': profile_id})
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:client_id>/kick', methods=['POST'])
@login_required
def kick_client(client_id):
    data = db.execute_query("""
        SELECT u.username, cp.config_id
        FROM vpn_users u
        JOIN vpn_client_profiles cp ON u.id = cp.user_id
        WHERE u.id = %s
    """, (client_id,), fetchone=True)
    if not data:
        return jsonify({'status': 'error', 'message': 'Client not found'})
    try:
        resp = core_client.send_command('kick_client', {
            'config_id': data['config_id'],
            'target': data['username']
        })
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:client_id>/download-config', methods=['GET'])
@login_required
def download_config_page(client_id):
    # 显示密码输入页面
    return render_template('download_config.html', client_id=client_id)

@bp.route('/<int:client_id>/get-config', methods=['POST'])
@login_required
def get_client_config(client_id):
    try:
        resp = core_client.send_command('get_client_config', {
            'client_id': client_id
        })
        if resp.get('status') == 'ok':
            config_data = resp.get('data')
            with tempfile.NamedTemporaryFile(mode='w', suffix='.ovpn', delete=False) as f:
                f.write(config_data)
                temp_path = f.name
            _temp_files.append(temp_path)
            def cleanup(path):
                try:
                    os.unlink(path)
                    if path in _temp_files:
                        _temp_files.remove(path)
                except OSError:
                    pass
            import threading
            threading.Timer(60.0, cleanup, args=[temp_path]).start()
            return send_file(temp_path, as_attachment=True, download_name=f'client_{client_id}.ovpn')
        else:
            return jsonify({'status': 'error', 'message': resp.get('message', '未知错误')})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:client_id>/revoke', methods=['POST'])
@login_required
def revoke_client(client_id):
    data = request.get_json() or {}
    reason = data.get('reason', 'revoked by admin')
    try:
        resp = core_client.send_command('revoke_client_cert', {
            'client_profile_id': client_id,
            'reason': reason
        })
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})