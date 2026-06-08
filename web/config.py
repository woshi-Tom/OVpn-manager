import yaml
import os

CONFIG_PATH = "/etc/vpn-manager/web.yaml"

_config = None

def load_config():
    global _config
    with open(CONFIG_PATH, 'r') as f:
        _config = yaml.safe_load(f)
    return _config

def get_config():
    if _config is None:
        load_config()
    return _config