# VPN Manager

A lightweight, self-hosted VPN management platform built on top of OpenVPN. Provides a Web-based administration interface for managing VPN server configurations, client certificates, and real-time session monitoring.

> ⚠️ **Warning:** This project is under active development. Please change the default admin password (`admin`/`admin123`) immediately after deployment.

## Features

- **Web Management UI** — Intuitive dashboard for VPN administration, no command-line required
- **Client Certificate Management** — Create, download, and revoke client certificates with one click
- **CA Certificate Management** — Generate and manage Certificate Authorities
- **Server Certificate Signing** — Sign server certificates through the Web UI
- **Real-time Session Monitoring** — View online users, traffic stats, and connection history
- **Multi-configuration Support** — Run multiple VPN configurations (TUN/TAP) simultaneously
- **TUN & TAP Modes** — Support both routed (TUN) and bridged (TAP) network modes
- **NAT & Route Pushing** — Configure NAT and custom routes from the Web UI
- **System Logging** — Operation audit trail with configurable log levels

## Architecture

```
┌─────────────┐    Unix Socket (JSON)    ┌──────────────┐
│   Web UI    │ ◄──────────────────────► │   Core (C)   │
│  (Flask)    │                          │              │
└──────┬──────┘                          └──────┬───────┘
       │                                        │
       ▼                                        ▼
┌─────────────┐                          ┌──────────────┐
│ PostgreSQL  │ ◄───────────────────────►│   OpenVPN    │
│   Database  │                          │  (managed)   │
└─────────────┘                          └──────────────┘
```

- **Core (C)** — Runs as root, manages OpenVPN processes, handles PKI operations, monitors sessions via management sockets. Communicates with Web through a Unix domain socket IPC.
- **Web (Python/Flask)** — Runs as a non-privileged user, provides HTTP interface on port 5000. Communicates with Core through Unix socket, reads database directly for display.
- **PostgreSQL** — Stores all configurations, user data, encrypted certificates, and session logs.

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Core Engine | C (GCC), OpenSSL, libpq, libyaml, cJSON, pthreads |
| Web UI | Python 3, Flask, Jinja2, psycopg2 |
| Database | PostgreSQL 14+ |
| VPN Server | OpenVPN (managed as child processes) |
| IPC | Unix Domain Socket with JSON protocol |
| Crypto | RSA key generation, X.509 certificates, AES-256-CBC private key encryption |
| Deployment | systemd services, Bash install scripts |

## Requirements

- Ubuntu 20.04+ / Debian 11+
- PostgreSQL 14+
- OpenVPN 2.5+
- GCC (for building the Core)
- Python 3.8+
- Root access (for Core process and OpenVPN management)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/woshi-Tom/OVpn-manager.git
cd OVpn-manager

# Run the installation script
chmod +x packaging/scripts/install.sh
sudo ./packaging/scripts/install.sh
```

## Management Commands

```bash
# Start / Stop / Restart services
sudo vpn-manager start
sudo vpn-manager stop
sudo vpn-manager restart

# Check service status
sudo vpn-manager status

# View logs
sudo vpn-manager logs
```

## Directory Structure

```
/etc/vpn-manager/          # Configuration files
/var/log/vpn-manager/      # Core logs
/var/log/openvpn/          # OpenVPN logs
/var/run/openvpn/          # PID and management sockets
/var/run/vpn-manager/      # Core IPC socket
```

## Configuration

After installation, configure via:

- **Core:** `/etc/vpn-manager/core.yaml`
- **Web:** `/etc/vpn-manager/web.yaml`
- **Database Auth:** `/etc/vpn-manager/web-db-auth` (auto-generated)

## Security Notes

- Private keys are encrypted with AES-256-CBC before database storage
- The Web process runs as a non-privileged user with restricted database access
- Sensitive data (passwords, keys) is masked in log output
- **Change the default admin password immediately after first deployment**
- Use HTTPS in production (configure a reverse proxy like Nginx)

## License

[MIT License](packaging/LICENSE)

## Contributing

See [CONTRIBUTING.md](packaging/CONTRIBUTING.md) for guidelines.
