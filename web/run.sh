#!/bin/bash
cd /srv/opencode-deepseek/myProject/web
. /root/openvpn-manager/web/venv_vpn/bin/activate
export PYTHONPATH=.
python3 app.py
