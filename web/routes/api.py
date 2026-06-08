from flask import Blueprint, jsonify, session, redirect, url_for
from functools import wraps
import db

bp = Blueprint('api', __name__, url_prefix='/api')

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return jsonify({'status': 'error', 'message': '未授权'}), 401
        return f(*args, **kwargs)
    return decorated_function

@bp.route('/online-clients')
@login_required
def online_clients():
    sessions = db.execute_query("""
        SELECT s.virtual_ip, s.real_ip, s.connected_since, s.bytes_sent, s.bytes_received,
               u.username, c.config_name
        FROM vpn_sessions s
        JOIN vpn_users u ON s.user_id = u.id
        JOIN vpn_config c ON s.config_id = c.id
        WHERE s.disconnected_at IS NULL
        ORDER BY s.connected_since
    """, fetchall=True)
    return jsonify(sessions)

@bp.route('/stats')
@login_required
def stats():
    stats = db.execute_query("""
        SELECT
            (SELECT COUNT(*) FROM vpn_users WHERE disabled = false) as active_users,
            (SELECT COUNT(*) FROM vpn_sessions WHERE disconnected_at IS NULL) as online_users,
            (SELECT COUNT(*) FROM vpn_config) as total_configs,
            (SELECT SUM(bytes_sent + bytes_received) FROM vpn_sessions) as total_traffic
    """, fetchone=True)
    return jsonify(stats)

@bp.route('/load-history')
@login_required
def load_history():
    data = db.execute_query("""
        WITH RECURSIVE time_series AS (
            SELECT NOW() - INTERVAL '15 minute' as ts
            UNION ALL
            SELECT ts + INTERVAL '3 minute' FROM time_series
            WHERE ts < NOW()
        )
        SELECT 
            ts as time_point,
            COALESCE((
                SELECT COUNT(*) 
                FROM vpn_sessions 
                WHERE connected_since <= ts 
                AND (disconnected_at IS NULL OR disconnected_at > ts)
            ), 0) as online_count
        FROM time_series
        ORDER BY time_point
    """, fetchall=True)
    return jsonify(data)

@bp.route('/recent-sessions')
@login_required
def recent_sessions():
    sessions = db.execute_query("""
        SELECT s.virtual_ip, s.real_ip, s.connected_since, s.disconnected_at,
               s.bytes_sent, s.bytes_received, u.username, c.config_name
        FROM vpn_sessions s
        JOIN vpn_users u ON s.user_id = u.id
        JOIN vpn_config c ON s.config_id = c.id
        ORDER BY COALESCE(s.disconnected_at, NOW()) DESC
        LIMIT 20
    """, fetchall=True)
    return jsonify(sessions)

@bp.route('/sessions-by-time')
@login_required
def sessions_by_time():
    data = db.execute_query("""
        SELECT 
            CASE 
                WHEN s.disconnected_at IS NULL AND NOW() - s.connected_since < INTERVAL '3 minutes' THEN 'active'
                WHEN s.disconnected_at IS NOT NULL AND s.disconnected_at > NOW() - INTERVAL '3 minutes' THEN 'active'
                WHEN s.disconnected_at IS NOT NULL AND s.disconnected_at <= NOW() - INTERVAL '3 minutes' AND s.disconnected_at > NOW() - INTERVAL '9 minutes' THEN '3-9min'
                WHEN s.disconnected_at IS NOT NULL AND s.disconnected_at <= NOW() - INTERVAL '9 minutes' AND s.disconnected_at > NOW() - INTERVAL '15 minutes' THEN '9-15min'
                WHEN s.disconnected_at IS NULL AND NOW() - s.connected_since >= INTERVAL '3 minutes' AND NOW() - s.connected_since < INTERVAL '9 minutes' THEN '3-9min'
                WHEN s.disconnected_at IS NULL AND NOW() - s.connected_since >= INTERVAL '9 minutes' AND NOW() - s.connected_since < INTERVAL '15 minutes' THEN '9-15min'
                ELSE NULL
            END as time_group,
            s.virtual_ip, s.real_ip, s.connected_since, s.disconnected_at,
            s.bytes_sent, s.bytes_received, u.username, c.config_name
        FROM vpn_sessions s
        JOIN vpn_users u ON s.user_id = u.id
        JOIN vpn_config c ON s.config_id = c.id
        WHERE s.disconnected_at > NOW() - INTERVAL '15 minutes' 
           OR s.disconnected_at IS NULL
        ORDER BY s.connected_since DESC
    """, fetchall=True)
    
    active = []
    min3_9 = []
    min9_15 = []
    
    for row in data:
        group = row.get('time_group')
        if group == 'active':
            active.append(row)
        elif group == '3-9min':
            min3_9.append(row)
        elif group == '9-15min':
            min9_15.append(row)
    
    return jsonify({
        'active': active,
        '3-9min': min3_9,
        '9-15min': min9_15
    })