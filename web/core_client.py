import socket
import json
import os
from config import get_config

def send_command(action, params=None):
    cfg = get_config()['socket']
    sock_path = cfg['core_socket']
    if not os.path.exists(sock_path):
        raise Exception(f"Core socket {sock_path} not found")

    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(30)
    try:
        client.connect(sock_path)
        request = {"action": action}
        if params:
            request["params"] = params
        client.send(json.dumps(request).encode())

        # 循环读取直到获得完整 JSON 响应
        chunks = []
        while True:
            try:
                chunk = client.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk.decode())
                # 尝试解析，成功则说明读取完毕
                try:
                    return json.loads(''.join(chunks))
                except json.JSONDecodeError:
                    continue
            except socket.timeout:
                raise Exception("Core 响应超时")
    finally:
        client.close()