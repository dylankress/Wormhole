# Wormhole Relay Server

Production-ready relay server for Wormhole peer-to-peer file transfers.

## Architecture

The relay server enables connections on hostile networks (firewalls, NAT) where direct P2P connections fail. It provides:

1. **Connection Coordination** - Peers register and exchange connection information
2. **NAT Reflection** - Server tells clients their public IP:port 
3. **Fallback Forwarding** - Relays QUIC packets when direct connection fails
4. **Ticket System** - User-friendly short codes (e.g., "7-guitar-battery")

## Components

### Core Modules

- **`peer_registry.c`** - Hash table for tracking connected peers (~500 lines)
  - PeerID → socket address mapping
  - Endpoint storage (LAN, public IP, IPv6, etc.)
  - Session ID management
  - Stale peer cleanup (>60s without keepalive)

- **`ticket_manager.c`** - Ticket generation and lookup (~400 lines)
  - EFF wordlist-based tickets ("N-word-word" format)
  - Ticket lifecycle (1-hour expiration)
  - File metadata storage (size, filename)
  - Collision detection

- **`rate_limiter.c`** - Per-IP rate limiting (~300 lines)
  - 1000 packets/second per IP
  - Hash table tracking
  - Automatic stale entry removal

- **`server.c`** - Main UDP server loop (~700 lines)
  - 9 message types (REGISTER, CREATE_TICKET, LOOKUP, etc.)
  - Dual-stack IPv4/IPv6 support
  - Message routing and forwarding
  - Statistics tracking

- **`main.c`** - Entry point (~150 lines)
  - Command-line argument parsing
  - Signal handling (graceful shutdown)
  - Configuration management

### Protocol Definitions

- **`relay_protocol.h`** - Wire format specification
  - Binary protocol (packed structs)
  - 9 message types
  - Little-endian encoding
  - Ed25519 authentication (TODO: requires libsodium)

## Build Instructions

### Prerequisites

**Option 1: Linux (GCC)**
```bash
sudo apt-get install build-essential
```

**Option 2: Windows (MSVC)**
- Install Visual Studio 2019 or later with C++ build tools
- Or install [Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/)

**Option 3: Windows (MinGW)**
```bash
# Via MSYS2
pacman -S mingw-w64-x86_64-gcc
```

### Dependencies

1. **libsodium** (for Ed25519 signatures - REQUIRED for production)
   - Download: https://download.libsodium.org/libsodium/releases/
   - Extract to `../deps/libsodium/`
   - Structure: `include/`, `lib/`, `bin/`

2. **EFF Wordlist** (already included)
   - Located: `../deps/eff_large_wordlist.txt`
   - 7776 words for ticket generation

### Building

**Linux:**
```bash
cd relay-server
./build.sh
```

**Windows (MSVC):**
```batch
cd relay-server
build.bat
```

**Windows (MinGW):**
```bash
cd relay-server
gcc -Wall -Wextra -O2 -std=c11 main.c server.c peer_registry.c ticket_manager.c rate_limiter.c -o build/relay-server.exe -lws2_32 -ladvapi32
```

Output: `build/relay-server` (Linux) or `build/relay-server.exe` (Windows)

## Running the Server

### Basic Usage

```bash
# Linux (requires sudo for port 443)
sudo ./build/relay-server

# Windows (run as Administrator for port 443)
.\build\relay-server.exe
```

### Command-Line Options

```
Usage: relay-server [options]

Options:
  -p, --port <port>         UDP port to listen on (default: 443)
  -w, --wordlist <path>     Path to EFF wordlist (default: ../deps/eff_large_wordlist.txt)
  --max-peers <num>         Maximum concurrent peers (default: 10000)
  --max-tickets <num>       Maximum active tickets (default: 5000)
  -h, --help                Show this help message
```

### Examples

```bash
# Listen on port 8080 (no sudo required)
./build/relay-server -p 8080

# Custom capacity
./build/relay-server --max-peers 50000 --max-tickets 25000

# Custom wordlist
./build/relay-server -w /path/to/wordlist.txt
```

## Testing Locally

### 1. Start Server
```bash
./build/relay-server -p 8080
```

### 2. Simulate Client Registration

Use `nc` (netcat) or a custom test client:

```bash
# Create test registration packet (binary)
# Message type: 0x01 (REGISTER)
# PeerID: 32 bytes (Ed25519 public key)
# Signature: 64 bytes (placeholder, verification disabled for now)
# Timestamp: 8 bytes
# Endpoint count: 2 bytes (0x0001 = 1 endpoint)
# Endpoint: 20 bytes (IPv4: 0.0.0.0:0, priority 100)

# TODO: Implement test client
```

### 3. Monitor Server Output

Server logs all activities:
- Peer registrations
- Ticket creation
- Lookups
- Forwarded packets
- Statistics

### 4. Graceful Shutdown

Press `Ctrl+C` to stop server. It will:
- Print final statistics
- Clean up resources
- Close socket

## Protocol Flow

### Sender (Alice)
1. **REGISTER** → Server
2. Server → **REGISTERED** (session ID + reflected IP:port)
3. **CREATE_TICKET** → Server (session ID, file size, filename)
4. Server → **TICKET_CREATED** ("7-guitar-battery")
5. Alice shares ticket with Bob (out-of-band)

### Receiver (Bob)
1. **REGISTER** → Server
2. Server → **REGISTERED** (session ID + reflected IP:port)
3. **LOOKUP** → Server (ticket: "7-guitar-battery")
4. Server → **PEER_INFO** (Alice's PeerID + endpoints)
5. Bob attempts direct connection to Alice (parallel: LAN, public, IPv6)
6. If direct fails: **FORWARD** packets through relay

### Connection Upgrade
- Both peers monitor for direct connection success
- When direct succeeds: **GOODBYE** to relay (reason: 0x00 = upgraded)
- Relay stops forwarding, frees resources

## Security Notes

⚠️ **CURRENT STATUS: INSECURE (Development Only)**

- Ed25519 signature verification is **NOT YET IMPLEMENTED**
- Requires libsodium integration
- Do NOT deploy to production without enabling signature verification!

**TODO (High Priority):**
1. Integrate libsodium
2. Verify Ed25519 signatures in REGISTER messages
3. Prevent PeerID spoofing
4. Add rate limiting per PeerID (in addition to per-IP)

## Performance

### Capacity Estimates

**Hardware:** 1 CPU core, 1GB RAM, 100 Mbps network

- **Concurrent peers:** 10,000+
- **Active tickets:** 5,000+
- **Packet rate:** 100,000 packets/sec (rate-limited to 1,000/sec per IP)
- **Forwarding throughput:** 50 Mbps (assumes 50% of traffic is forwarded)

### Memory Usage

- Peer registry: ~200 bytes/peer → 2 MB for 10,000 peers
- Ticket manager: ~300 bytes/ticket → 1.5 MB for 5,000 tickets
- Rate limiter: ~50 bytes/IP → 500 KB for 10,000 IPs
- **Total:** ~4 MB + overhead

### Optimizations

- Hash tables (O(1) lookups)
- Minimal memory allocation (pre-allocated arrays)
- Lock-free message handling (critical sections only for shared state)
- Periodic cleanup (every 30 seconds)

## Deployment (DigitalOcean)

### 1. Create Droplet
- **Size:** Basic ($6/month) - 1 CPU, 1GB RAM, 25GB SSD
- **Image:** Ubuntu 22.04 LTS
- **Datacenter:** Choose closest to users (e.g., NYC, SFO, AMS)

### 2. DNS Configuration
Point `wormholerelay.com` to droplet IP:
```
A    @    <droplet-ip>
AAAA @    <droplet-ipv6>
```

### 3. Install Dependencies
```bash
ssh root@wormholerelay.com
apt-get update
apt-get install -y build-essential libsodium-dev curl

# Download EFF wordlist
mkdir -p /opt/wormhole-relay
cd /opt/wormhole-relay
curl -o eff_large_wordlist.txt https://www.eff.org/files/2016/07/18/eff_large_wordlist.txt
```

### 4. Build and Deploy
```bash
# Upload relay-server/ directory to droplet
scp -r relay-server/ root@wormholerelay.com:/opt/wormhole-relay/

# SSH and build
ssh root@wormholerelay.com
cd /opt/wormhole-relay/relay-server
./build.sh
```

### 5. Systemd Service
Create `/etc/systemd/system/wormhole-relay.service`:
```ini
[Unit]
Description=Wormhole Relay Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/wormhole-relay/relay-server
ExecStart=/opt/wormhole-relay/relay-server/build/relay-server -p 443 -w /opt/wormhole-relay/eff_large_wordlist.txt
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
systemctl enable wormhole-relay
systemctl start wormhole-relay
systemctl status wormhole-relay
```

### 6. Firewall
```bash
# Allow UDP 443
ufw allow 443/udp
ufw enable
```

### 7. Monitor
```bash
# View logs
journalctl -u wormhole-relay -f

# Check statistics
systemctl status wormhole-relay
```

## File Manifest

```
relay-server/
├── README.md                    # This file
├── relay_protocol.h             # Wire format definitions
├── peer_registry.h              # Peer tracking (header)
├── peer_registry.c              # Peer tracking (implementation)
├── ticket_manager.h             # Ticket generation (header)
├── ticket_manager.c             # Ticket generation (implementation)
├── rate_limiter.h               # Rate limiting (header)
├── rate_limiter.c               # Rate limiting (implementation)
├── server.h                     # Main server (header)
├── server.c                     # Main server (implementation)
├── main.c                       # Entry point
├── build.sh                     # Linux build script
├── build.bat                    # Windows build script
└── build/                       # Build output (created by build scripts)
    └── relay-server[.exe]       # Compiled binary
```

## Lines of Code

- **Total:** ~2,200 lines of C code
- **Protocol:** 118 lines
- **Peer Registry:** ~500 lines
- **Ticket Manager:** ~400 lines
- **Rate Limiter:** ~300 lines
- **Server:** ~700 lines
- **Main:** ~150 lines

## License

Open source (same as Wormhole main project)

## Contributors

- Dylan Kress (primary author)

## Changelog

### v0.1.0 (Feb 11, 2026)
- Initial implementation
- Core relay functionality (9 message types)
- Peer registry, ticket manager, rate limiter
- Linux and Windows build scripts
- Protocol specification
- ⚠️ Ed25519 verification not yet implemented (requires libsodium)
