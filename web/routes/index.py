from flask import Blueprint, render_template
from flask import session, redirect, url_for
from functools import wraps
import db

bp = Blueprint('index', __name__)

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

"""首页"""

@bp.route('/')
def index():
    if 'admin_id' not in session:
        return redirect(url_for('auth.login'))
    stats = db.execute_query("""
        SELECT
            (SELECT COUNT(*) FROM vpn_users WHERE disabled = false) as active_users,
            (SELECT COUNT(*) FROM vpn_sessions WHERE disconnected_at IS NULL) as online_users,
            (SELECT COUNT(*) FROM vpn_config) as total_configs
    """, fetchone=True)
    return render_template('index.html', stats=stats or {})