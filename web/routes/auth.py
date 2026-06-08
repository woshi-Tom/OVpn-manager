import hashlib
from flask import Blueprint, render_template, request, redirect, url_for, session, flash
import db
import core_client
from utils.system_logger import log_event

bp = Blueprint('auth', __name__, url_prefix='/auth')

def hash_password(password):
    """兼容旧的 SHA-256 哈希，验证时同时支持 bcrypt 和 SHA-256"""
    return hashlib.sha256(password.encode()).hexdigest()

def verify_password(password, stored_hash):
    """验证密码：优先尝试 bcrypt，回退到 SHA-256 兼容旧数据"""
    try:
        import bcrypt
        if stored_hash.startswith('$2'):
            return bcrypt.checkpw(password.encode(), stored_hash.encode())
    except ImportError:
        pass
    # 回退到 SHA-256 验证（兼容旧密码）
    return hash_password(password) == stored_hash

def hash_password_bcrypt(password):
    """新密码使用 bcrypt 哈希"""
    try:
        import bcrypt
        return bcrypt.hashpw(password.encode(), bcrypt.gensalt()).decode()
    except ImportError:
        # bcrypt 不可用时回退到 SHA-256
        return hash_password(password)

def update_admin_login(admin_id):
    try:
        core_client.send_command('update_admin_login', {'admin_id': admin_id})
    except Exception as e:
        print(f"更新登录时间失败: {e}")

def login_required(f):
    from functools import wraps
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

@bp.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')

        user = db.execute_query("""
            SELECT id, username, password FROM vpn_admins
            WHERE username = %s
        """, (username,), fetchone=True)

        if user and verify_password(password, user['password']):
            session['admin_id'] = user['id']
            session['admin_username'] = user['username']
            session.permanent = True
            update_admin_login(user['id'])
            log_event('info', 'web', f'管理员登录成功: {username}')
            return redirect(url_for('index.index'))
        else:
            log_event('warn', 'web', f'管理员登录失败: {username}')
            flash('用户名或密码错误', 'danger')
    
    return render_template('login.html')

@bp.route('/logout')
def logout():
    username = session.get('admin_username', 'unknown')
    session.clear()
    log_event('info', 'web', f'管理员登出: {username}')
    return redirect(url_for('auth.login'))

@bp.route('/change-password', methods=['GET', 'POST'])
@login_required
def change_password():
    if request.method == 'POST':
        current_password = request.form.get('current_password')
        new_password = request.form.get('new_password')
        confirm_password = request.form.get('confirm_password')
        
        if not current_password or not new_password or not confirm_password:
            flash('所有字段都必须填写', 'danger')
            return redirect(url_for('auth.change_password'))
        
        if new_password != confirm_password:
            flash('两次输入的新密码不一致', 'danger')
            return redirect(url_for('auth.change_password'))
        
        if len(new_password) < 6:
            flash('新密码长度至少6位', 'danger')
            return redirect(url_for('auth.change_password'))
        
        admin_id = session.get('admin_id')

        user = db.execute_query("""
            SELECT id, password FROM vpn_admins
            WHERE id = %s
        """, (admin_id,), fetchone=True)

        if not user or not verify_password(current_password, user['password']):
            flash('当前密码错误', 'danger')
            return redirect(url_for('auth.change_password'))

        new_hash = hash_password_bcrypt(new_password)
        db.execute_query("""
            UPDATE vpn_admins SET password = %s WHERE id = %s
        """, (new_hash, admin_id))
        
        log_event('info', 'web', f'管理员修改密码成功')
        session.clear()
        return redirect(url_for('auth.login'))
    
    return render_template('change_password.html')
