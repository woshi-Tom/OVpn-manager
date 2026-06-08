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
    client.connect(sock_path)
    request = {"action": action}
    if params:
        request["params"] = params
    client.send(json.dumps(request).encode())
    response = client.recv(65536).decode()
    client.close()
    return json.loads(response)