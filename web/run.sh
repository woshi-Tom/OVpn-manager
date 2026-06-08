#!/bin/bash
cd /workspace/web
source /opt/vpn-web-venv/bin/activate
export PYTHONPATH=.
python3 app.py
