# Wormhole

Decentralized P2P file storage — a privacy-respecting alternative to Dropbox and Google Drive, powered by QUIC.

Wormhole started as a direct file transfer tool and evolved into what it really is: a decentralized storage platform. Peers contribute disk space to the network, files are erasure-coded and replicated across multiple nodes, and anyone can store and retrieve data without relying on centralized cloud providers. It also happens to be great for quick peer-to-peer file transfers.

## How It Works

### Distributed Storage

```
Machine A:                              Machine B:
$ wormholed                             $ wormholed
Daemon started, listening on :4567      Daemon started, listening on :4567
Connected to relay                      Connected to relay
DHT bootstrap complete (1 peers)        DHT bootstrap complete (1 peers)

$ wormhole store report.pdf
Chunking... 17 chunks (4.2 MB)
Erasure coding... 17 data + 9 parity
Replicating to 2 peers...
Stored! Hash: a3f8c2...

$ wormhole get a3f8c2...              $ wormhole status
Retrieved report.pdf chunk              Chunks: 26, Peers: 1, Storage: 6.5 MB
```

### Direct Transfer

Wormhole is also great for quick one-off file transfers — no daemon required:

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

### Connection Flow

1. **Sender** registers with a relay server and gets a short ticket code (e.g., `3-guitar-battery`)
2. **Receiver** looks up the ticket to discover the sender's network endpoints
3. Both peers attempt a direct connection via NAT traversal (UDP hole punching)
4. If direct connection fails (symmetric NAT, firewalls), traffic is relayed through the server
5. File transfers over QUIC with content-addressed chunking and dedup

## Features

### Decentralized Storage
- **Persistent daemon** — `wormholed` runs in the background with QUIC listener, relay connectivity, and DHT node
- **Kademlia DHT** — decentralized peer and chunk discovery (UDP port 4568), relay bootstrap, multi-bootstrap resilience
- **Erasure coding** — RS(8,4) Reed-Solomon for fault-tolerant storage (8 data + 4 parity shards per stripe)
- **Client-side encryption** — XChaCha20-Poly1305 (libsodium) encrypt-before-chunk, so storage nodes never see plaintext
- **TLS peer identity** — Ed25519 node ID embedded in TLS certificate CN, verified on replication connections
- **Proof-of-storage** — Blake3-based challenge/response verification that peers actually hold chunks
- **Chunk replication** — 4x replication target across peers for durability
- **Storage incentives** — per-peer reciprocity tracking, reject freeloading peers (ratio < 0.5)
- **Storage quota** — configurable disk limit with LRU eviction (prefers evicting highly-replicated chunks)
- **File management** — delete stored files, export/import encryption keys, daemon lifecycle control, peer visibility
- **Configuration** — `~/.wormhole/config` INI file with 14 tunable settings

### Direct Transfer
- **Direct P2P transfer** — files go straight between machines, not through the cloud
- **Directory transfer** — send entire directories with `wormhole send <dir>` (recursive, multi-file manifest)
- **Progress bar** — live transfer speed and ETA display
- **Resumable transfers** — interrupted transfers resume from last checkpoint
- **NAT traversal** — automatic hole punching with parallel connection racing (LAN > IPv6 > public IP > relay fallback)
- **Relay fallback** — QUIC packets tunneled through relay when direct connection is impossible
- **Ticket codes** — human-friendly codes like `3-guitar-battery` (EFF wordlist)

### Shared Infrastructure
- **QUIC transport** — encrypted, multiplexed, congestion-controlled (via MsQuic)
- **Content-addressed chunking** — Blake3-hashed 256KB chunks with dedup
- **Ed25519 identity** — each peer has a persistent cryptographic identity

## Project Status

**Phases 1–7 complete.** The core platform is built and tested:

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | Done | Stabilize & ship v1.0 — congestion control, PMTUD, relay fallback, Ctrl+C cleanup |
| 2 | Done | Transfer enhancements — progress bar, resumable transfers, directory transfer, test framework |
| 3 | Done | P2P storage foundation — persistent daemon, peer discovery, chunk replication, storage quota |
| 4 | Done | Decentralized network — Kademlia DHT, erasure coding, proof-of-storage, storage incentives |
| 4.5 | Done | Integration — wire EC into daemon, enforce ledger, connect health checks, E2E tests |
| 5 | Done | Multi-platform — Linux client (Makefile + Docker), file registry, multi-node testing |
| 6 | Done | Production readiness — RS(8,4), R=4, encryption, TLS identity, DHT persistence, multi-bootstrap |
| 7 | Done | Usability & management — file deletion, key export/import, daemon lifecycle, peer visibility |

17 unit test suites + E2E daemon smoke tests + Docker multi-node integration tests (20 tests). See [TESTING_GUIDE.md](TESTING_GUIDE.md) for details.

## Building

### Prerequisites

- **Windows client**: Visual Studio 2019+ (MSVC x64)
- **Linux client**: `build-essential cmake libsodium-dev libssl-dev` (see [BUILD_LINUX.md](BUILD_LINUX.md))
- **Linux relay server**: GCC + `libsodium-dev`
- **MsQuic** — git submodule, build separately (`git submodule update --init --recursive`)
- **libsodium** — pre-built Windows binaries in `deps/libsodium/`; on Linux: `apt install libsodium-dev`
- **Docker** (optional) — for multi-node testing without a local toolchain

### Client (Windows)

```bat
cd src
build_with_env.bat
```

Output: `src/build/wormhole.exe`, `src/build/wormholed.exe` (plus `msquic.dll` and `libsodium.dll`)

### Client (Linux)

```bash
cd src
make
```

Output: `src/build/wormhole`, `src/build/wormholed` (plus `libmsquic.so`)

See [BUILD_LINUX.md](BUILD_LINUX.md) for full prerequisites including MsQuic build from source.

### Docker

```bash
cd docker
docker compose up -d        # Build images + start relay + 5 nodes
docker compose down -v       # Tear down
```

See [BUILD_LINUX.md](BUILD_LINUX.md) for detailed Docker usage (manual setup, interaction, debugging).

### Relay Server (Linux)

```bash
cd relay-server
./build.sh
```

Output: `relay-server/build/relay-server`

## Usage

### Daemon

The persistent daemon `wormholed` is the core of the storage network. It manages the QUIC listener, chunk store, relay connection, DHT node, health checks, and peer discovery. The CLI communicates with it via IPC (named pipes on Windows, Unix domain sockets on Linux).

```
wormholed [options]

Options:
  --port <port>           QUIC listener port (default: 4567)
  --data-dir <path>       Data directory (default: ~/.wormhole)
  --dht-port <port>       DHT UDP port (default: 4568)
  --bootstrap <host:port> DHT bootstrap peer (default: relay server)
  --no-relay              Disable relay connection
  --help                  Show help
```

### Store a file
```
wormhole store <file>
```
Chunks the file, erasure-codes each stripe, and stores chunks via the daemon, which replicates them to peers.

### Retrieve a chunk
```
wormhole get <hash> [-o output_file]
```
Retrieves a chunk by its Blake3 hash from the daemon's store or the network.

### List stored files
```
wormhole files [-v]
```
Lists all files stored via the daemon, showing filename, size, chunk count, replication status, and file ID. Use `-v` for verbose output with chunk details and replica peer info.

### Delete a stored file
```
wormhole delete <id> [-f]
```
Removes a stored file: deletes chunks from local store, cleans up EC metadata, removes DHT announcements, and deletes the file registry entry. Accepts full or prefix file IDs. Use `-f` to skip the confirmation prompt.

### Export/import encryption keys
```
wormhole export-key <id>       # Print the file's encryption key (hex)
wormhole import-key <id> <key> # Import a key for a file (hex)
```
Back up encryption keys for safekeeping or transfer to another device. Without the key, encrypted files cannot be decrypted.

### Manage the daemon
```
wormhole daemon start           # Start wormholed in the background
wormhole daemon stop            # Stop the running daemon
wormhole daemon restart         # Stop then start
wormhole daemon status          # Alias for 'wormhole status'
```

### List peers
```
wormhole peers
```
Shows all known DHT peers with node ID, address, port, and last-seen time.

### Daemon status
```
wormhole status
```
Shows daemon stats: peer count, stored chunks, storage used, relay connection, DHT node count.

### Configuration
```
wormhole config list              # Show all settings
wormhole config get <key>         # Get a config value
wormhole config set <key> <val>   # Set a config value
```

### Send a file or directory
```
wormhole send <file|directory>
```
Registers with the relay, creates a ticket, and waits for a receiver. Directories are sent recursively with a multi-file manifest.

### Receive a file
```
wormhole receive <ticket>
```
Looks up the sender via the ticket, connects, and downloads to `~/Downloads/`. Interrupted transfers resume automatically.

### Client global option
```
wormhole --daemon <port> <command>    # Connect to daemon on non-default port (default: 4567)
```

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Relay Server                                │
│                     (UDP, peer coordination)                         │
│   ┌──────────┬───────────┬────────────┬────────────┬──────────┐     │
│   │ Registry │  Tickets  │ Forwarding │ Rate Limit │ DHT Boot │     │
│   └──────────┴───────────┴────────────┴────────────┴──────────┘     │
└──────────────────────────┬───────────────────────┬──────────────────┘
                           │  UDP relay protocol   │
              ┌────────────┘   (11 msg types)      └────────────┐
              │                                                  │
┌─────────────▼──────────────────┐       ┌───────────────────────▼────┐
│          Peer A                │       │          Peer B            │
│  ┌───────────┐  ┌───────────┐ │ QUIC  │ ┌───────────┐ ┌─────────┐ │
│  │  CLI      │  │  Daemon   │◄├───────┤►│  Daemon   │ │  CLI    │ │
│  │ wormhole  │  │ wormholed │ │(4567) │ │ wormholed │ │wormhole │ │
│  └─────┬─────┘  └─────┬─────┘ │       │ └─────┬─────┘ └────┬────┘ │
│        │ IPC     ┌─────┴─────┐ │  DHT  │ ┌─────┴─────┐      │     │
│        └────────►│Chunk Store│ │◄─────►│ │Chunk Store│◄─────┘     │
│                  │  DHT Node │ │(4568) │ │  DHT Node │             │
│                  │  Erasure  │ │       │ │  Erasure  │             │
│                  │  Health   │ │       │ │  Health   │             │
│                  └───────────┘ │       │ └───────────┘             │
└────────────────────────────────┘       └───────────────────────────┘
```

The project has three components:

**Daemon** (`src/wormholed.c`) — The core of the network. A persistent background process that manages:
- QUIC listener for peer-to-peer chunk transfer
- Content-addressed chunk store with dedup and LRU eviction
- Relay connection with auto-reconnect for peer coordination
- Kademlia DHT node (UDP port 4568) for decentralized discovery
- RS(8,4) erasure coding for fault-tolerant storage
- Client-side encryption (XChaCha20-Poly1305) before chunking
- TLS peer identity verification (Ed25519 node ID in cert CN)
- Health monitoring with proof-of-storage challenges
- Storage incentive ledger for reciprocity enforcement
- File deletion, key export/import, and daemon lifecycle management
- IPC server for CLI communication (named pipes on Windows, Unix domain sockets on Linux)

**Client** (`src/wormhole.c`) — Cross-platform CLI (Windows + Linux) for storage commands (`store`/`get`/`delete`/`files`/`status`/`peers`/`export-key`/`import-key`/`daemon`/`config`) and direct file transfer (`send`/`receive`). Uses MsQuic for QUIC transport, libsodium for Ed25519 identity, and Blake3 for content-addressed chunking.

**Relay Server** (`relay-server/`) — Lightweight Linux UDP server for:
- Peer registration and NAT reflection
- Ticket-based peer lookup (EFF wordlist codes)
- Packet forwarding (QUIC-over-relay fallback)
- Peer discovery (FIND_PEERS protocol)
- DHT bootstrap (responds to PING/FIND_NODE)

### Protocol Stack

| Layer | Protocol | Port | Purpose |
|-------|----------|------|---------|
| Transport | QUIC (MsQuic) | 4567/UDP | Encrypted file/chunk transfer between peers |
| Discovery | Kademlia DHT | 4568/UDP | Decentralized peer and chunk lookup |
| Coordination | Relay protocol | 443/UDP | Peer registration, tickets, NAT traversal, forwarding |
| Data | Content-addressed chunks | — | 256KB Blake3-hashed chunks, RS(8,4) erasure coding |

See [CLAUDE.md](CLAUDE.md) for detailed protocol wire formats and implementation notes.

## Configuration

All settings are stored in `~/.wormhole/config` (INI format). Manage with `wormhole config list/get/set`.

| Key | Default | Description |
|-----|---------|-------------|
| `relay_host` | `wormholerelay.com` | Relay server hostname |
| `relay_port` | `443` | Relay server port |
| `max_storage_gb` | `10` | Maximum disk space for chunk storage (GB) |
| `replication_target` | `4` | Target number of copies per chunk across peers |
| `dht_enabled` | `1` | Enable Kademlia DHT node (0 = disabled) |
| `dht_port` | `4568` | UDP port for DHT protocol |
| `dht_bootstrap_nodes` | *(empty)* | Comma-separated bootstrap host:port list (default: use relay) |
| `ec_enabled` | `1` | Enable erasure coding on store (0 = disabled) |
| `ec_data_shards` | `8` | RS data shards per stripe |
| `ec_parity_shards` | `4` | RS parity shards per stripe |
| `health_check_interval_sec` | `1800` | Seconds between health check cycles (30 min) |
| `min_storage_ratio` | `50` | Minimum reciprocity ratio to accept storage (50 = 0.50) |
| `proof_cache_count` | `8` | Number of pre-computed proofs cached per chunk |
| `auto_evict_enabled` | `0` | Auto-evict local chunks after replication (0 = keep local copies) |

## License

MIT License. See [LICENSE](LICENSE) for details.
