# Wormhole

Secure peer-to-peer file transfer over QUIC. No accounts, no cloud storage, no file size limits. Share a ticket code, transfer directly.

## How It Works

```
Machine A:                          Machine B:
$ wormhole send report.pdf          $ wormhole receive 3-guitar-battery
Ticket: 3-guitar-battery            Connecting...
Waiting for receiver...             Receiving report.pdf (4.2 MB)
Sending...                          [=========>        ] 45% 12.3 MB/s ETA 0:08
Done!                               Done! Saved to ~/Downloads/report.pdf
```

Directories work too:
```
$ wormhole send ./my-project/
Scanning directory... 47 files (128.5 MB)
Ticket: 5-ocean-maple
```

1. **Sender** registers with a relay server and gets a short ticket code (e.g., `3-guitar-battery`)
2. **Receiver** looks up the ticket to discover the sender's network endpoints
3. Both peers attempt a direct connection via NAT traversal (UDP hole punching)
4. If direct connection fails (symmetric NAT, firewalls), traffic is relayed through the server
5. File transfers over QUIC with content-addressed chunking and dedup

## Features

- **Direct P2P transfer** — files go straight between machines, not through the cloud
- **Directory transfer** — send entire directories with `wormhole send <dir>` (manifest v2, recursive)
- **Progress bar** — live transfer speed and ETA display
- **Resumable transfers** — interrupted transfers resume from last checkpoint
- **NAT traversal** — automatic hole punching with parallel connection racing (LAN > IPv6 > public IP > relay fallback)
- **QUIC transport** — encrypted, multiplexed, congestion-controlled (via MsQuic)
- **Content-addressed chunking** — Blake3-hashed 256KB chunks with dedup (chunk store)
- **Ed25519 identity** — each peer has a persistent cryptographic identity
- **Relay fallback** — QUIC packets tunneled through relay when direct connection is impossible
- **Ticket codes** — human-friendly codes like `3-guitar-battery` (EFF wordlist)
- **Persistent daemon** — `wormholed` runs in the background with QUIC listener and relay connectivity
- **Peer discovery** — find active peers on the network via relay protocol
- **Chunk replication** — 3x replication target across peers for durability
- **Storage quota** — configurable disk limit with LRU eviction
- **Kademlia DHT** — decentralized peer and chunk discovery (UDP port 4568), bootstrap from relay
- **Erasure coding** — RS(4,2) Reed-Solomon codec for fault-tolerant storage (4 data + 2 parity shards per stripe)
- **Proof-of-storage** — Blake3-based challenge/response verification that peers actually hold chunks
- **Storage incentives** — per-peer reciprocity tracking, reject freeloading peers (ratio < 0.5)
- **Configuration** — `~/.wormhole/config` INI file for all settings

## Building

### Prerequisites

- **Windows** with Visual Studio 2019+ (MSVC x64) for the client
- **Linux** with GCC for the relay server
- **MsQuic** — git submodule, build separately (`git submodule update --init --recursive`)
- **libsodium** — pre-built Windows binaries in `deps/libsodium/`; on Linux: `apt install libsodium-dev`
- **Reed-Solomon** — GF(2^8) erasure coding codec in `deps/reed_solomon/` (included)

### Client (Windows)

```bat
cd src
build_with_env.bat
```

Output: `src/build/wormhole.exe`, `src/build/wormholed.exe` (plus `msquic.dll` and `libsodium.dll`)

### Relay Server (Linux)

```bash
cd relay-server
./build.sh
```

Output: `relay-server/build/relay-server`

## Usage

### Send a file or directory
```
wormhole.exe send <file|directory>
```
Registers with the relay, creates a ticket, and waits for a receiver. Directories are sent recursively with a multi-file manifest.

### Receive a file
```
wormhole.exe receive <ticket>
```
Looks up the sender via the ticket, connects, and downloads to `~/Downloads/`. Interrupted transfers resume automatically.

### Store a file (P2P storage)
```
wormhole.exe store <file>
```
Chunks the file and stores it via the daemon, which replicates chunks to peers.

### Retrieve a chunk
```
wormhole.exe get <hash>
```
Retrieves a chunk by its Blake3 hash from the daemon's store or the network.

### Daemon status
```
wormhole.exe status
```
Shows daemon stats: peer count, stored chunks, storage used, relay connection, DHT node count.

### Configuration
```
wormhole.exe config list              # Show all settings
wormhole.exe config get <key>         # Get a config value
wormhole.exe config set <key> <val>   # Set a config value
```

### Daemon
The persistent daemon `wormholed.exe` manages the QUIC listener, chunk store, relay connection, and peer discovery. The CLI communicates with it via named pipe IPC.

```
wormholed.exe                         # Start the daemon
```

## Architecture

The project has three components:

- **Client** (`src/wormhole.c`) — Windows CLI using MsQuic for QUIC transport, libsodium for Ed25519 identity, and Blake3 for content-addressed chunking. Thin client for daemon commands (`store`, `get`, `status`, `config`).
- **Daemon** (`src/wormholed.c`) — Persistent background process managing QUIC listener, chunk store, relay connection, peer discovery, chunk replication, Kademlia DHT node, health monitoring, proof-of-storage verification, and storage incentive tracking. Communicates with CLI via named pipe IPC.
- **Relay Server** (`relay-server/`) — lightweight Linux UDP server for peer coordination, NAT reflection, ticket management, peer discovery, packet forwarding, and DHT bootstrap (responds to PING/FIND_NODE)

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation.

## License

Open source. See LICENSE for details.
