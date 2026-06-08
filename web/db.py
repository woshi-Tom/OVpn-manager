import psycopg2
from psycopg2 import extras
import os

AUTH_FILE = "/etc/vpn-manager/web-db-auth"

def parse_auth_file():
    config = {}
    if os.path.exists(AUTH_FILE):
        with open(AUTH_FILE, 'r') as f:
            for line in f:
                line = line.strip()
                if '=' in line:
                    key, value = line.split('=', 1)
                    config[key] = value
    if not config:
        raise Exception(f"认证文件不存在: {AUTH_FILE}")
    return config

def get_connection():
    cfg = parse_auth_file()
    max_retries = 3
    for i in range(max_retries):
        try:
            conn = psycopg2.connect(
                host=cfg.get('host', 'localhost'),
                port=int(cfg.get('port', 5432)),
                dbname=cfg.get('dbname', 'vpn_db'),
                user=cfg.get('user', 'vpn_web'),
                password=cfg.get('password', ''),
                connect_timeout=10
            )
            return conn
        except Exception as e:
            if i < max_retries - 1:
                import time
                time.sleep(1)
            else:
                raise e

def execute_query(query, params=None, fetchone=False, fetchall=False):
    conn = get_connection()
    try:
        cur = conn.cursor(cursor_factory=extras.RealDictCursor)
        cur.execute(query, params)
        if fetchone:
            result = cur.fetchone()
        elif fetchall:
            result = cur.fetchall()
        else:
            result = None
        conn.commit()
        cur.close()
        conn.close()
        return result
    except Exception as e:
        conn.rollback()
        try:
            conn.close()
        except:
            pass
        raise e
