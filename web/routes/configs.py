from flask import Blueprint, render_template, request, jsonify
from flask import session, redirect, url_for
from functools import wraps
import db
import core_client

bp = Blueprint('configs', __name__, url_prefix='/configs')

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'admin_id' not in session:
            return redirect(url_for('auth.login'))
        return f(*args, **kwargs)
    return decorated_function

@bp.route('/')
@login_required
def list_configs():
    configs = db.execute_query("""
        SELECT c.id, c.config_name, c.mode, c.proto, c.port, c.remote, c.ca_cert, c.server_cert,
               t.server_ip, t.subnet_mask::text as subnet_mask_text, 
               COALESCE(t.subnet_mask::text, t.server_ip::text) as subnet_display,
               t.push_dns,
               c.status as config_status
        FROM vpn_config c
        LEFT JOIN vpn_config_tun t ON c.id = t.config_id
        WHERE c.mode = 'tun'
    """, fetchall=True)
    
    tap_configs = db.execute_query("""
        SELECT c.id, c.config_name, c.mode, c.proto, c.port, c.remote, c.ca_cert, c.server_cert,
               t.bridge_name, t.physical_if, t.dhcp_mode,
               c.status as config_status
        FROM vpn_config c
        LEFT JOIN vpn_config_tap t ON c.id = t.config_id
        WHERE c.mode = 'tap'
    """, fetchall=True)
    
    return render_template('configs.html', configs=configs, tap_configs=tap_configs)

@bp.route('/add', methods=['GET', 'POST'])
@login_required
def add_config():
    if request.method == 'GET':
        return render_template('add_config.html')
    
    data = request.get_json()
    config_name = data.get('config_name')
    mode = data.get('mode')
    proto = data.get('proto', 'udp')
    port = data.get('port', 1194)
    remote = data.get('remote')
    ca_cert = data.get('ca_cert', '')
    server_cert = data.get('server_cert', '')
    server_key = data.get('server_key', '')
    
    if not config_name or not mode or not remote:
        return jsonify({'status': 'error', 'message': '缺少必要字段'})
    
    existing = db.execute_query("SELECT id FROM vpn_config WHERE config_name = %s", (config_name,), fetchone=True)
    if existing:
        return jsonify({'status': 'error', 'message': '配置名称已存在'})
    
    try:
        port = int(port)
    except:
        port = 1194
    
    try:
        result = db.execute_query("""
            INSERT INTO vpn_config (config_name, mode, proto, port, remote, ca_cert, server_cert, server_key)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            RETURNING id
        """, (config_name, mode, proto, port, remote, ca_cert, server_cert, server_key), fetchone=True)
        
        if not result:
            return jsonify({'status': 'error', 'message': '创建配置失败'})
        
        config_id = result['id']
        
        if mode == 'tun':
            server_ip = data.get('server_ip', '')
            subnet_mask = data.get('subnet_mask', '')
            push_dns = data.get('push_dns', '')
            enable_nat = data.get('enable_nat', True)
            push_redirect_gateway = data.get('push_redirect_gateway', False)
            push_routes = data.get('push_routes', '')
            
            if not server_ip or not subnet_mask:
                db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                return jsonify({'status': 'error', 'message': 'TUN模式需要填写服务器IP和子网掩码'})
            
            # 验证并处理 subnet_mask 格式
            import re
            cidr_pattern = re.compile(r'^(\d{1,3}\.){3}\d{1,3}/\d{1,2}$')
            netmask_pattern = re.compile(r'^(\d{1,3}\.){3}\d{1,3}$')
            
            # 如果是纯 netmask 格式 (如 255.255.255.0)，转换为 CIDR 格式
            if netmask_pattern.match(subnet_mask):
                # 验证是否是有效的 netmask
                parts = subnet_mask.split('.')
                if len(parts) != 4:
                    db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                    return jsonify({'status': 'error', 'message': '子网掩码格式错误'})
                try:
                    # 将 netmask 转换为 CIDR 位数
                    # 例如 255.255.255.0 -> 24
                    ip_int = (int(parts[0]) << 24) + (int(parts[1]) << 16) + (int(parts[2]) << 8) + int(parts[3])
                    # 计算连续1的位数
                    bits = 0
                    for i in range(32):
                        if (ip_int >> (31 - i)) & 1:
                            bits += 1
                        else:
                            break
                    # 使用 server_ip 作为网络地址，组合成 CIDR
                    subnet_cidr = f"{server_ip}/{bits}"
                    print(f"DEBUG: netmask={subnet_mask}, server_ip={server_ip}, bits={bits}, subnet_cidr={subnet_cidr}")
                    subnet_mask = subnet_cidr
                except Exception as e:
                    print(f"DEBUG: netmask conversion error: {e}")
                    db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                    return jsonify({'status': 'error', 'message': '子网掩码转换失败'})
            elif not cidr_pattern.match(subnet_mask):
                db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                return jsonify({'status': 'error', 'message': '子网掩码格式错误，请使用 CIDR 格式(如 10.8.0.0/24) 或点分十进制格式(如 255.255.255.0)'})
            else:
                # 已经是 CIDR 格式，直接使用
                print(f"DEBUG: subnet_mask is already CIDR format: {subnet_mask}")
            
            try:
                db.execute_query("""
                    INSERT INTO vpn_config_tun (config_id, server_ip, subnet_mask, push_dns, enable_nat, push_redirect_gateway, push_routes)
                    VALUES (%s, %s, %s, %s, %s, %s, %s)
                """, (config_id, server_ip, subnet_mask, push_dns.split(',') if push_dns else [], 
                      enable_nat, push_redirect_gateway, push_routes.split(',') if push_routes else []))
            except Exception as e:
                db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                return jsonify({'status': 'error', 'message': '保存TUN配置失败: ' + str(e)})
        
        elif mode == 'tap':
            bridge_name = data.get('bridge_name')
            physical_if = data.get('physical_if')
            dhcp_mode = data.get('dhcp_mode', 'none')
            server_ip = data.get('server_ip') or None
            subnet_mask = data.get('subnet_mask') or None
            
            try:
                db.execute_query("""
                    INSERT INTO vpn_config_tap (config_id, bridge_name, physical_if, dhcp_mode, server_ip, subnet_mask)
                    VALUES (%s, %s, %s, %s, %s, %s)
                """, (config_id, bridge_name, physical_if, dhcp_mode, server_ip, subnet_mask))
            except Exception as e:
                db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
                return jsonify({'status': 'error', 'message': '保存TAP配置失败: ' + str(e)})
        
        return jsonify({'status': 'ok', 'message': '配置创建成功', 'data': {'id': config_id}})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:config_id>/edit', methods=['GET', 'POST'])
@login_required
def edit_config(config_id):
    if request.method == 'GET':
        config = db.execute_query("""
            SELECT c.*, t.server_ip, t.subnet_mask, t.push_dns, t.enable_nat, 
                   t.push_redirect_gateway, t.push_routes
            FROM vpn_config c
            LEFT JOIN vpn_config_tun t ON c.id = t.config_id
            WHERE c.id = %s
        """, (config_id,), fetchone=True)
        
        if not config:
            config = db.execute_query("""
                SELECT c.*, t.bridge_name, t.physical_if, t.dhcp_mode, t.server_ip, t.subnet_mask
                FROM vpn_config c
                LEFT JOIN vpn_config_tap t ON c.id = t.config_id
                WHERE c.id = %s
            """, (config_id,), fetchone=True)
        
        return render_template('edit_config.html', config=config)
    
    data = request.get_json()
    config_name = data.get('config_name')
    proto = data.get('proto')
    port = data.get('port')
    remote = data.get('remote')
    
    try:
        db.execute_query("""
            UPDATE vpn_config SET config_name = %s, proto = %s, port = %s, remote = %s, updated_at = NOW()
            WHERE id = %s
        """, (config_name, proto, port, remote, config_id))
        
        mode = data.get('mode')
        if mode == 'tun':
            server_ip = data.get('server_ip', '')
            subnet_mask = data.get('subnet_mask', '')
            push_dns = data.get('push_dns', '')
            enable_nat = data.get('enable_nat', True)
            push_redirect_gateway = data.get('push_redirect_gateway', False)
            push_routes = data.get('push_routes', '')
            
            # 验证并处理 subnet_mask 格式
            import re
            cidr_pattern = re.compile(r'^(\d{1,3}\.){3}\d{1,3}/\d{1,2}$')
            netmask_pattern = re.compile(r'^(\d{1,3}\.){3}\d{1,3}$')
            
            # 如果是纯 netmask 格式 (如 255.255.255.0)，转换为 CIDR 格式
            if netmask_pattern.match(subnet_mask):
                parts = subnet_mask.split('.')
                if len(parts) != 4:
                    return jsonify({'status': 'error', 'message': '子网掩码格式错误'})
                try:
                    ip_int = (int(parts[0]) << 24) + (int(parts[1]) << 16) + (int(parts[2]) << 8) + int(parts[3])
                    if ip_int == 0:
                        bits = 0
                    else:
                        bits = 32 - (ip_int ^ (ip_int - 1)).bit_length()
                    subnet_mask = f"{server_ip}/{bits}"
                except:
                    return jsonify({'status': 'error', 'message': '子网掩码转换失败'})
            elif subnet_mask and not cidr_pattern.match(subnet_mask):
                return jsonify({'status': 'error', 'message': '子网掩码格式错误，请使用 CIDR 格式(如 10.8.0.0/24) 或点分十进制格式(如 255.255.255.0)'})
            
            push_dns_arr = push_dns.split(',') if push_dns else []
            push_routes_arr = push_routes.split(',') if push_routes else []
            
            db.execute_query("""
                UPDATE vpn_config_tun SET server_ip = %s::inet, subnet_mask = %s::cidr, push_dns = %s, 
                enable_nat = %s, push_redirect_gateway = %s, push_routes = %s
                WHERE config_id = %s
            """, (server_ip, subnet_mask, push_dns_arr, enable_nat, push_redirect_gateway, push_routes_arr, config_id))
        
        elif mode == 'tap':
            bridge_name = data.get('bridge_name')
            physical_if = data.get('physical_if')
            dhcp_mode = data.get('dhcp_mode', 'none')
            server_ip = data.get('server_ip') or None
            subnet_mask = data.get('subnet_mask') or None
            
            db.execute_query("""
                UPDATE vpn_config_tap SET bridge_name = %s, physical_if = %s, dhcp_mode = %s,
                server_ip = %s::inet, subnet_mask = %s::inet
                WHERE config_id = %s
            """, (bridge_name, physical_if, dhcp_mode, server_ip, subnet_mask, config_id))
        
        return jsonify({'status': 'ok', 'message': '配置更新成功'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:config_id>/delete', methods=['POST'])
@login_required
def delete_config(config_id):
    try:
        db.execute_query("DELETE FROM vpn_config WHERE id = %s", (config_id,))
        return jsonify({'status': 'ok', 'message': '配置删除成功'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/<int:config_id>/apply', methods=['POST'])
@login_required
def apply_config(config_id):
    try:
        resp = core_client.send_command('apply_config', {'config_id': config_id})
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})


@bp.route('/<int:config_id>/stop', methods=['POST'])
@login_required
def stop_config(config_id):
    try:
        resp = core_client.send_command('stop_config', {'config_id': config_id})
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})


@bp.route('/<int:config_id>/status')
@login_required
def config_status(config_id):
    online = db.execute_query("""
        SELECT COUNT(*) as count FROM vpn_sessions
        WHERE config_id = %s AND disconnected_at IS NULL
    """, (config_id,), fetchone=True)
    return jsonify(online)

@bp.route('/cert/generate-ca', methods=['GET', 'POST'])
@login_required
def generate_ca():
    if request.method == 'GET':
        return render_template('generate_ca.html')
    
    data = request.get_json()
    common_name = data.get('common_name')
    valid_days = data.get('valid_days', 3650)
    
    if not common_name:
        return jsonify({'status': 'error', 'message': '请输入CA名称'})
    
    try:
        resp = core_client.send_command('generate_ca_cert', {
            'common_name': common_name,
            'valid_days': int(valid_days)
        })
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/cert/sign-server', methods=['GET', 'POST'])
@login_required
def sign_server_cert():
    if request.method == 'GET':
        cas = db.execute_query("SELECT id, common_name, created_at FROM vpn_ca ORDER BY created_at DESC", fetchall=True)
        if not cas:
            return render_template('sign_server_cert.html', configs=[], cas=[], ca_missing=True)
        configs = db.execute_query("SELECT id, config_name FROM vpn_config", fetchall=True)
        return render_template('sign_server_cert.html', configs=configs, cas=cas, ca_missing=False)
    
    data = request.get_json()
    config_id = data.get('config_id')
    ca_id = data.get('ca_id')
    common_name = data.get('common_name')
    valid_days = data.get('valid_days', 365)
    
    if not config_id or not common_name:
        return jsonify({'status': 'error', 'message': '请填写完整信息'})
    
    try:
        params = {
            'config_id': int(config_id),
            'common_name': common_name,
            'valid_days': int(valid_days) if valid_days else 365
        }
        if ca_id:
            try:
                params['ca_id'] = int(ca_id)
            except:
                pass
        
        resp = core_client.send_command('sign_server_cert', params)
        return jsonify(resp)
    except Exception as e:
        import traceback
        return jsonify({'status': 'error', 'message': '签发失败: ' + str(e) + ' | ' + traceback.format_exc()})

@bp.route('/cert/ca-info')
@login_required
def ca_info():
    try:
        ca = db.execute_query("""
            SELECT id, common_name, ca_cert, created_at 
            FROM vpn_ca 
            ORDER BY created_at DESC LIMIT 1
        """, fetchone=True)
        
        if not ca:
            return jsonify({'status': 'error', 'message': '未找到CA证书'})
        
        import subprocess
        import tempfile
        import os
        
        cert = ca['ca_cert']
        expires_at = '未知'
        
        if cert and '-----BEGIN' in cert:
            with tempfile.NamedTemporaryFile(mode='w', suffix='.crt', delete=False) as f:
                f.write(cert)
                cert_file = f.name
            
            try:
                result = subprocess.run(['openssl', 'x509', '-in', cert_file, '-noout', '-enddate'],
                                      capture_output=True, text=True, timeout=5)
                if result.returncode == 0 and 'notAfter=' in result.stdout:
                    expires_at = result.stdout.strip().replace('notAfter=', '')
                else:
                    expires_at = '解析失败'
            except Exception as e:
                expires_at = '解析错误'
            finally:
                try:
                    os.unlink(cert_file)
                except:
                    pass
        
        return jsonify({
            'status': 'ok', 
            'data': {
                'id': ca['id'],
                'common_name': ca['common_name'],
                'created_at': str(ca['created_at']),
                'expires_at': expires_at
            }
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/cert/ca-list')
@login_required
def ca_list():
    try:
        cas = db.execute_query("""
            SELECT ca.id, ca.common_name, ca.ca_fingerprint, ca.created_at,
                   (SELECT COUNT(*) FROM vpn_config cf WHERE cf.ca_fingerprint = ca.ca_fingerprint) as ref_count
            FROM vpn_ca ca
            ORDER BY ca.created_at DESC
        """, fetchall=True)
        
        return render_template('ca_list.html', cas=cas)
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@bp.route('/cert/delete-ca', methods=['POST'])
@login_required
def delete_ca():
    data = request.get_json()
    ca_id = data.get('ca_id')
    
    if not ca_id:
        return jsonify({'status': 'error', 'message': '请提供CA证书ID'})
    
    try:
        resp = core_client.send_command('delete_ca_cert', {
            'ca_id': int(ca_id)
        })
        return jsonify(resp)
    except Exception as e:
        return jsonify({'status': 'error', 'message': '删除失败: ' + str(e)})



@bp.route('/check-port', methods=['POST'])
@login_required
def check_port():
    """检查端口是否已被占用"""
    data = request.get_json()
    port = data.get('port')
    proto = data.get('proto', 'udp')
    exclude_id = data.get('exclude_id')  # 编辑时排除自身
    
    if not port:
        return jsonify({'available': True})
    
    try:
        port = int(port)
    except:
        return jsonify({'available': False, 'message': '端口必须是数字'})
    
    if port < 1 or port > 65535:
        return jsonify({'available': False, 'message': '端口范围应为 1-65535'})
    
    # 查询数据库中已存在的配置
    if exclude_id:
        query = "SELECT id, config_name, proto FROM vpn_config WHERE port = %s AND id != %s"
        params = (port, exclude_id)
    else:
        query = "SELECT id, config_name, proto FROM vpn_config WHERE port = %s"
        params = (port,)
    
    existing = db.execute_query(query, params, fetchall=True)
    
    if existing:
        conflicts = []
        for c in existing:
            conflicts.append(f"{c['config_name']} ({c['proto'].upper()})")
        return jsonify({
            'available': False,
            'message': f'端口 {port} 已被占用: {", ".join(conflicts)}',
            'conflicts': conflicts
        })
    
    return jsonify({'available': True, 'message': '端口可用'})
