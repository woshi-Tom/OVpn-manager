from flask import Blueprint, render_template, jsonify, request
from flask import session, redirect, url_for
from functools import wraps
import db
import core_client

bp = Blueprint('sessions', __name__, url_prefix='/sessions')

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

@bp.route('/')
@login_required
def list_sessions():
    sessions = db.execute_query("""
        SELECT s.id, u.username, u.client_name, c.config_name,
               s.virtual_ip, s.real_ip, s.connected_since,
               s.bytes_sent, s.bytes_received, s.last_update
        FROM vpn_sessions s
        LEFT JOIN vpn_users u ON s.user_id = u.id
        LEFT JOIN vpn_config c ON s.config_id = c.id
        WHERE s.disconnected_at IS NULL
        ORDER BY s.connected_since DESC
    """, fetchall=True)
    
    if sessions:
        for session in sessions:
            if session.get('bytes_sent'):
                session['bytes_sent'] = format_bytes(session['bytes_sent'])
            if session.get('bytes_received'):
                session['bytes_received'] = format_bytes(session['bytes_received'])
    
    return render_template('sessions.html', sessions=sessions or [])

@bp.route('/api/online')
@login_required
def api_online_sessions():
    sessions = db.execute_query("""
        SELECT s.id, u.username, u.client_name, c.config_name,
               s.virtual_ip, s.real_ip, s.connected_since,
               s.bytes_sent, s.bytes_received, s.last_update
        FROM vpn_sessions s
        LEFT JOIN vpn_users u ON s.user_id = u.id
        LEFT JOIN vpn_config c ON s.config_id = c.id
        WHERE s.disconnected_at IS NULL
        ORDER BY s.connected_since DESC
    """, fetchall=True)
    return jsonify({'status': 'ok', 'data': sessions})

@bp.route('/api/kick', methods=['POST'])
@login_required
def api_kick():
    data = request.get_json()
    username = data.get('username')
    if not username:
        return jsonify({'status': 'error', 'message': '缺少用户名'})
    
    config_data = db.execute_query("""
        SELECT cp.config_id
        FROM vpn_users u
        JOIN vpn_client_profiles cp ON u.id = cp.user_id
        WHERE u.username = %s
    """, (username,), fetchone=True)
    
    if not config_data:
        return jsonify({'status': 'error', 'message': '用户不存在'})
    
    try:
        resp = core_client.send_command('kick_client', {
            'config_id': config_data['config_id'],
            'target': username
        })
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

def format_bytes(bytes_val):
    if bytes_val is None:
        return '0 B'
    if bytes_val < 1024:
        return f"{bytes_val} B"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val / 1024:.1f} KB"
    elif bytes_val < 1024 * 1024 * 1024:
        return f"{bytes_val / (1024 * 1024):.1f} MB"
    else:
        return f"{bytes_val / (1024 * 1024 * 1024):.2f} GB"
