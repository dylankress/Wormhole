# Wormhole

Secure peer-to-peer file transfer and decentralized storage over QUIC. No accounts, no cloud, no file size limits.

Wormhole started as a direct file transfer tool — share a ticket code, transfer directly between machines. It has since evolved into a decentralized P2P storage platform: peers contribute disk space to the network, files are erasure-coded and replicated across multiple nodes, and anyone can store and retrieve data without relying on centralized cloud providers.

## How It Works

### Direct Transfer

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

### Connection Flow

1. **Sender** registers with a relay server and gets a short ticket code (e.g., `3-guitar-battery`)
2. **Receiver** looks up the ticket to discover the sender's network endpoints
3. Both peers attempt a direct connection via NAT traversal (UDP hole punching)
4. If direct connection fails (symmetric NAT, firewalls), traffic is relayed through the server
5. File transfers over QUIC with content-addressed chunking and dedup

## Features

- **Direct P2P transfer** — files go straight between machines, not through the cloud
- **Directory transfer** — send entire directories with `wormhole send <dir>` (recursive, multi-file manifest)
- **Progress bar** — live transfer speed and ETA display
- **Resumable transfers** — interrupted transfers resume from last checkpoint
- **NAT traversal** — automatic hole punching with parallel connection racing (LAN > IPv6 > public IP > relay fallback)
- **QUIC transport** — encrypted, multiplexed, congestion-controlled (via MsQuic)
- **Content-addressed chunking** — Blake3-hashed 256KB chunks with dedup
- **Ed25519 identity** — each peer has a persistent cryptographic identity
- **Relay fallback** — QUIC packets tunneled through relay when direct connection is impossible
- **Ticket codes** — human-friendly codes like `3-guitar-battery` (EFF wordlist)
- **Persistent daemon** — `wormholed` runs in the background with QUIC listener, relay connectivity, and DHT node
- **Kademlia DHT** — decentralized peer and chunk discovery (UDP port 4568), relay bootstrap
- **Erasure coding** — RS(4,2) Reed-Solomon for fault-tolerant storage (4 data + 2 parity shards per stripe)
- **Proof-of-storage** — Blake3-based challenge/response verification that peers actually hold chunks
- **Chunk replication** — 3x replication target across peers for durability
- **Storage incentives** — per-peer reciprocity tracking, reject freeloading peers (ratio < 0.5)
- **Storage quota** — configurable disk limit with LRU eviction (prefers evicting highly-replicated chunks)
- **Configuration** — `~/.wormhole/config` INI file with 12 tunable settings

## Project Status

**Phases 1–4.5 complete.** The core platform is built and tested:

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | Done | Stabilize & ship v1.0 — congestion control, PMTUD, relay fallback, Ctrl+C cleanup |
| 2 | Done | Transfer enhancements — progress bar, resumable transfers, directory transfer, test framework |
| 3 | Done | P2P storage foundation — persistent daemon, peer discovery, chunk replication, storage quota |
| 4 | Done | Decentralized network — Kademlia DHT, erasure coding, proof-of-storage, storage incentives |
| 4.5 | Done | Integration — wire EC into daemon, enforce ledger, connect health checks, E2E tests |
| 5 | Next | Multi-platform support (Linux client) |

15 unit test suites + E2E daemon smoke tests. See [TESTING_GUIDE.md](TESTING_GUIDE.md) for details.

## Building

### Prerequisites

- **Windows** with Visual Studio 2019+ (MSVC x64) for the client
- **Linux** with GCC for the relay server
- **MsQuic** — git submodule, build separately (`git submodule update --init --recursive`)
- **libsodium** — pre-built Windows binaries in `deps/libsodium/`; on Linux: `apt install libsodium-dev`

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
wormhole send <file|directory>
```
Registers with the relay, creates a ticket, and waits for a receiver. Directories are sent recursively with a multi-file manifest.

### Receive a file
```
wormhole receive <ticket>
```
Looks up the sender via the ticket, connects, and downloads to `~/Downloads/`. Interrupted transfers resume automatically.

### Store a file (P2P storage)
```
wormhole store <file>
```
Chunks the file, erasure-codes each stripe, and stores chunks via the daemon, which replicates them to peers.

### Retrieve a chunk
```
wormhole get <hash> [-o output_file]
```
Retrieves a chunk by its Blake3 hash from the daemon's store or the network.

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

### Client global option
```
wormhole --daemon <port> <command>    # Connect to daemon on non-default port (default: 4567)
```

### Daemon

The persistent daemon `wormholed` manages the QUIC listener, chunk store, relay connection, DHT node, health checks, and peer discovery. The CLI communicates with it via named pipe IPC.

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

**Client** (`src/wormhole.c`) — Windows CLI for direct file transfer (`send`/`receive`) and daemon commands (`store`/`get`/`status`/`config`). Uses MsQuic for QUIC transport, libsodium for Ed25519 identity, and Blake3 for content-addressed chunking.

**Daemon** (`src/wormholed.c`) — Persistent background process that manages:
- QUIC listener for peer-to-peer chunk transfer
- Content-addressed chunk store with dedup and LRU eviction
- Relay connection with auto-reconnect for peer coordination
- Kademlia DHT node (UDP port 4568) for decentralized discovery
- RS(4,2) erasure coding for fault-tolerant storage
- Health monitoring with proof-of-storage challenges
- Storage incentive ledger for reciprocity enforcement
- Named pipe IPC server for CLI communication

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
| Data | Content-addressed chunks | — | 256KB Blake3-hashed chunks, RS(4,2) erasure coding |

See [CLAUDE.md](CLAUDE.md) for detailed protocol wire formats and implementation notes.

## Configuration

All settings are stored in `~/.wormhole/config` (INI format). Manage with `wormhole config list/get/set`.

| Key | Default | Description |
|-----|---------|-------------|
| `relay_host` | `wormholerelay.com` | Relay server hostname |
| `relay_port` | `443` | Relay server port |
| `max_storage_gb` | `10` | Maximum disk space for chunk storage (GB) |
| `replication_target` | `3` | Target number of copies per chunk across peers |
| `dht_enabled` | `1` | Enable Kademlia DHT node (0 = disabled) |
| `dht_port` | `4568` | UDP port for DHT protocol |
| `ec_enabled` | `1` | Enable erasure coding on store (0 = disabled) |
| `ec_data_shards` | `4` | RS data shards per stripe |
| `ec_parity_shards` | `2` | RS parity shards per stripe |
| `health_check_interval_sec` | `1800` | Seconds between health check cycles (30 min) |
| `min_storage_ratio` | `50` | Minimum reciprocity ratio to accept storage (50 = 0.50) |
| `proof_cache_count` | `8` | Number of pre-computed proofs cached per chunk |

## License

MIT License. See [LICENSE](LICENSE) for details.
