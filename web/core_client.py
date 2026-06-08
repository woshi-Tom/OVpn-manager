import socket
import json
import os
from config import get_config

def send_command(action, params=None):
    cfg = get_config()['socket']
    core_host = cfg.get('core_host')
    core_port = cfg.get('core_port')
    sock_path = cfg.get('core_socket')

    request = {"action": action}
    if params:
        request["params"] = params
    payload = json.dumps(request).encode()

    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) if not core_host else socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.settimeout(30)
    try:
        if core_host and core_port:
            client.connect((core_host, int(core_port)))
        else:
            if not sock_path or not os.path.exists(sock_path):
                raise Exception(f"Core socket {sock_path} not found")
            client.connect(sock_path)

        client.send(payload)

        chunks = []
        while True:
            try:
                chunk = client.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk.decode())
                try:
                    return json.loads(''.join(chunks))
                except json.JSONDecodeError:
                    continue
            except socket.timeout:
                raise Exception("Core 响应超时")
    finally:
        client.close()