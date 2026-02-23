# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Wormhole is a decentralized P2P file storage platform written in C — a privacy-respecting alternative to Dropbox/Google Drive. Peers contribute disk space to the network, files are erasure-coded and replicated across multiple nodes, and anyone can store and retrieve data without centralized cloud providers. Built on QUIC (via MsQuic) with a Kademlia DHT for decentralized discovery and a UDP relay server for NAT traversal. Also supports direct peer-to-peer file transfer via ticket codes like "3-guitar-battery".

**Phases 1-8 complete.** Phase 9 (Qt GUI) is next. The daemon (`wormholed`) provides persistent chunk storage, peer discovery via Kademlia DHT, erasure coding (RS(8,4) with R=4 replication), client-side encryption, proof-of-storage verification, TLS peer identity verification, and storage incentive tracking — all wired together and tested (18 unit test suites + E2E daemon tests). Phase 8 added IPC v2 protocol (subscriptions, structured errors, operation tracking, cancellation), progress reporting for long-running operations, daemon-mediated send/receive with `--direct` fallback, config management via IPC, daemon lifecycle hardening (readiness signal, heartbeat, stale PID, log rotation), and real-time push events (peer/file/health/transfer). Direct file transfer with progress bar, resume, and directory support is also production-ready. Linux client support is functional with a Makefile build system and Docker multi-node testing.

Design decisions should keep the decentralized storage trajectory in mind. See [ROADMAP.md](ROADMAP.md) for the full development roadmap and production readiness plan.

## Build Commands

### Windows Client (requires MSVC x64 environment)
```bat
cd src
build_with_env.bat          # Sets up VS environment + builds
# Or if already in VS Developer Command Prompt:
build.bat                   # Builds both wormhole.exe and wormholed.exe
```
Output: `src/build/wormhole.exe`, `src/build/wormholed.exe` (plus `msquic.dll` and `libsodium.dll`)

### Linux Client
```bash
cd src
make                        # Builds both wormhole and wormholed
make clean                  # Remove build artifacts
```
Output: `src/build/wormhole`, `src/build/wormholed` (plus `libmsquic.so`)

Requires MsQuic built from source (`git submodule update --init --recursive`, then cmake build with OpenSSL TLS backend). See [BUILD_LINUX.md](BUILD_LINUX.md) for full prerequisites and step-by-step instructions, including Docker builds.

### Linux Relay Server
```bash
cd relay-server
./build.sh
```
Output: `relay-server/build/relay-server`

### Dependencies
- **MsQuic**: Git submodule at `msquic/` — init with `git submodule update --init --recursive`, then build separately
- **libsodium**: Pre-built Windows x64 binaries in `deps/libsodium/`; on Linux install `libsodium-dev`
- **OpenSSL**: Required on Linux for TLS cert generation and peer certificate verification; install `libssl-dev`
- **BLAKE3**: Portable C sources in `deps/blake3/` (no SIMD assembly)
- **Reed-Solomon**: GF(2^8) erasure coding codec in `deps/reed_solomon/` (rs.h/rs.c)
- **EFF wordlist**: Bundled at `deps/eff_large_wordlist.txt` (7,776 words for ticket generation)

## Usage
```
wormhole send <file|directory>         # Creates ticket, waits for receiver (via daemon if running)
wormhole receive <ticket>              # Downloads to ~/Downloads (via daemon if running, resumable)
wormhole send --direct <file>          # Direct send (standalone, bypasses daemon)
wormhole receive --direct <ticket>     # Direct receive (standalone, bypasses daemon)
wormhole store <file>                  # Store file chunks via daemon
wormhole get <hash> [-o <file>]        # Retrieve a stored file by ID
wormhole delete <id> [-f]              # Delete a stored file (-f skips prompt)
wormhole files [-v]                    # List stored files (-v for verbose)
wormhole status                        # Show daemon status
wormhole peers                         # List known DHT peers
wormhole export-key <id>              # Export file encryption key (hex)
wormhole import-key <id> <hex-key>    # Import file encryption key
wormhole daemon start|stop|restart    # Manage daemon process
wormhole config list                   # Show all settings (via daemon if running)
wormhole config get <key>              # Get a config value
wormhole config set <key> <val>        # Set a config value (hot-reload where safe)

Global flags:
  --daemon <port>                        # Connect to daemon on specified port (default 4567)
  --direct                               # Bypass daemon for send/receive (standalone mode)
```
On Windows, the executables are `wormhole.exe` and `wormholed.exe`.

## Architecture

### Three Components

**Client** (`src/`) — Cross-platform QUIC-based file transfer app (Windows + Linux)
- `wormhole.c` — Entry point, CLI (`send`/`receive`/`store`/`get`/`delete`/`files`/`status`/`peers`/`export-key`/`import-key`/`daemon`/`config`), MsQuic lifecycle, QUIC listener/connection setup, UDP hole-punch probes (WHPK), parallel connection racing, Ctrl+C cleanup
- `wormholed.c` — Persistent daemon process: QUIC listener, chunk store, relay connection with auto-reconnect, peer discovery, chunk replication (4x target), DHT node bootstrap/polling (multi-bootstrap with exponential backoff), health checks (20-chunk sample, event-driven), proof-of-storage challenge/response, storage ledger, client-side encryption on store/retrieve, configurable auto-eviction, DHT store persistence, IPC v1+v2 server (named pipes on Windows, Unix sockets on Linux), transfer manager integration, readiness signal, heartbeat, log rotation
- `transfer_mgr.c` — Daemon-mediated send/receive: manages up to 8 concurrent transfers (ACTIVE_TRANSFER), per-transfer relay client and QUIC connection, send/receive thread functions, progress callback bridging to IPC events, cancel support
- `stream.c` — Chunk-based two-stream transfer protocol: control stream (manifest request/response, chunk request, transfer complete) and data stream (chunk frames). Progress bar with speed/ETA (or StreamProgressCallback for daemon mode). Resumable transfers via `transfer_state.c`. Multi-file receive support.
- `file_io.c` — Cross-platform file ops, 64-bit size support, Downloads folder integration
- `crypto.c` — Self-signed TLS cert generation with Ed25519 node ID embedded in CN (64 hex chars), peer certificate verification, Windows Certificate Store integration
- `manifest.c` — File manifest serialization: v1 (single file), v2 (multi-file with per-file entries and chunk ranges), v3 (erasure coding metadata — ec_k, ec_m, stripe definitions with parity hashes)
- `chunker.c` — Content-addressed chunking (Blake3 hashes, 256KB chunks), `Chunker_BuildManifestFromDirectory` for recursive directory transfer, `Chunker_BuildManifestAndStoreWithProgress` for progress-reporting store operations
- `chunk_store.c` — Dedup chunk store (`ChunkStore_Has/Get/Put`), content-addressed by Blake3 hash. Replica metadata tracking (`ChunkStore_PutWithMeta`, `GetReplicaCount`, `SetReplicaLocation`). Storage quota enforcement with LRU eviction (`ChunkStore_Evict`) preferring highly-replicated chunks.
- `transfer_state.c` — Resumable transfer state: saves/loads received-chunks bitfield to `~/.wormhole/transfers/<hash>.state`
- `config.c` — Configuration management: INI-style `~/.wormhole/config` file with defaults (14 keys — see Key Configuration section). Hot-reload classification (`Config_IsHotReloadable`), value validation (`Config_ValidateValue`).
- `ipc.c` — IPC transport: named pipes on Windows (`\\.\pipe\wormhole`), Unix domain sockets on Linux (`~/.wormhole/wormhole.sock`). Server API for daemon, client API for CLI. Message framing: `[4B length][1B command][payload]`. v1 commands: STORE (0x01), GET (0x02), STATUS (0x03), SHUTDOWN (0x04), DHT_STATUS (0x05), LIST_FILES (0x06), FILE_GET (0x07), FILE_DELETE (0x08), EXPORT_KEY (0x09), IMPORT_KEY (0x0A), PEER_LIST (0x0B). v2 extensions: SUBSCRIBE (0x0C), CANCEL (0x0D), SEND (0x0E), RECEIVE (0x0F), TRANSFER_STATUS (0x10), TRANSFER_LIST (0x11), CONFIG_LIST (0x12), CONFIG_GET (0x13), CONFIG_SET (0x14), HEARTBEAT (0x15), EVENT (0xE0). v2 adds operation IDs, structured errors, subscriber management, and push events.
- `erasure.c` — RS(8,4) erasure coding integration: stripe-based encoding (`ErasureCoding_Encode`), chunk reconstruction from parity (`ErasureCoding_ReconstructChunk`), parity regeneration (`ErasureCoding_RegenerateStripeParity`), EC_GROUP serialization for manifest v3
- `proof.c` — Proof-of-storage: `Proof_Compute` (Blake3(seed || chunk_data)), pre-cached proofs per chunk (`~/.wormhole/proofs/`), challenge/response verification
- `health.c` — Chunk health monitoring: periodic DHT queries for chunk locations, proof-of-storage challenges to holders, recovery orchestration for under-replicated data chunks and missing parity chunks
- `incentives.c` — Storage ratio tracking: per-peer balance ledger (`STORAGE_LEDGER`), accept/reject storage based on reciprocity ratio (threshold 0.5), persisted to `~/.wormhole/storage_ledger.bin`
- `file_registry.c` — File-level metadata tracking (`~/.wormhole/files/`): maps stored files to their chunks, tracks status (storing → replicating → safe → offloaded), supports lookup by hex prefix, stores per-file encryption keys (v2 format), file deletion. Powers the `wormhole files` and `wormhole delete` commands.
- `file_crypto.c` — Client-side file encryption using libsodium `crypto_secretstream` (XChaCha20-Poly1305). Streaming encrypt/decrypt with 64KB chunks. Files are encrypted before chunking so storage nodes cannot read user data.
- `relay_forwarder.c` — Loopback UDP proxy for QUIC-over-relay fallback (when direct connections fail)
- `dht/` — Kademlia DHT subsystem (UDP port 4568):
  - `dht_protocol.h` — Wire format: 102-byte header `[1B type][1B version][4B txn_id][32B sender_id][64B ed25519_sig]`, messages 0x20-0x27
  - `routing_table.c` — K-bucket routing table (K=20, 256 buckets), XOR distance, LRS eviction, persistence to `~/.wormhole/dht_routing_table.bin`
  - `dht_node.c` — UDP transport, RPC dispatch (PING/PONG, FIND_NODE, FIND_VALUE, STORE), multi-bootstrap (up to 4 nodes, comma-separated config), bucket refresh, pending RPC tracking
  - `dht_store.c` — Local DHT value store: chunk hash → list of node locations (capacity 4096), 24h expiry, persistence
  - `dht_lookup.c` — Iterative Kademlia lookup (alpha=3, max 10 iterations), FIND_NODE and FIND_VALUE with shortlist convergence
- `relay/` — Relay client subsystem:
  - `peer_id.c` — Ed25519 keypair management (persisted to `~/.wormhole/identity`)
  - `relay_client.c` — UDP relay protocol (11 message types), callback-based events. Includes `RelayClient_FindPeers` for P2P peer discovery.
  - `discovery.c` — LAN/IPv6/public endpoint discovery
  - `ticket.c` — Ticket display formatting

**Relay Server** (`relay-server/`) — Linux UDP coordination server (~2,900 LOC)
- `main.c` — CLI args (`-p port`, `-w wordlist`, `--max-peers`, `--max-tickets`), signal handling
- `server.c` — Main UDP recv loop, message routing, stats tracking, `handle_find_peers` for P2P peer discovery (FIND_PEERS/PEERS_FOUND), DHT bootstrap handler (responds to PING/FIND_NODE with K closest peers from registry)
- `peer_registry.c` — Hash table of peers keyed by Ed25519 public key, stale cleanup (>60s)
- `ticket_manager.c` — EFF wordlist ticket generation ("N-word-word"), 1-hour expiry
- `rate_limiter.c` — Per-IP rate limiting (1,000 pkt/s), hash table tracking
- `crypto.c` — Ed25519 signature verification for relay messages

### Protocol Details
- ALPN: `"wormhole"`, default port: 4567 (UDP)
- Transfer protocol: Two QUIC streams — control stream for manifest negotiation, data stream for chunk frames
- Control messages (Stream 0): `MANIFEST_REQUEST` (0x01), `MANIFEST_RESPONSE` (0x02), `CHUNK_REQUEST` (0x03), `TRANSFER_COMPLETE` (0x04), `CHUNK_STORE_REQUEST` (0x05), `CHUNK_STORE_ACK` (0x06), `CHUNK_QUERY` (0x07), `CHUNK_QUERY_RESPONSE` (0x08), `PROOF_CHALLENGE` (0x09), `PROOF_RESPONSE` (0x0A)
- Data frame (Stream 1): `[4B index][32B hash][4B size][data]` (40-byte header)
- Chunk format: Blake3-hashed 256KB chunks, content-addressed for dedup
- DHT protocol (UDP port 4568): Ed25519-signed messages, types 0x20-0x27: PING (0x20), PONG (0x21), FIND_NODE (0x22), FIND_NODE_RESPONSE (0x23), FIND_VALUE (0x24), FIND_VALUE_RESPONSE (0x25), STORE (0x26), STORE_RESPONSE (0x27). 102-byte header with txn_id for RPC correlation.
- Relay protocol: Binary packed structs, little-endian, 11 message types defined in `relay-server/relay_protocol.h`: REGISTER (0x01), REGISTERED (0x02), LOOKUP (0x03), PEER_INFO (0x04), FORWARD (0x05), KEEPALIVE (0x06), GOODBYE (0x07), CREATE_TICKET (0x08), TICKET_CREATED (0x09), FIND_PEERS (0x0A), PEERS_FOUND (0x0B). Relay also handles DHT bootstrap (PING/FIND_NODE only).
- QUIC settings: Cubic congestion control (default), MTU 1200-1500 (PMTUD), 16MB stream / 64MB connection receive windows, 30s idle timeout, 10s keepalive
- `MAX_ENDPOINTS 16` per peer, defined in `relay_protocol.h`, enforced client-side in `relay_client.c`
- `REPLICATION_TARGET 4` — target copies per chunk for P2P storage durability
- `MAX_FIND_PEERS 50` — cap on peer discovery responses
- IPC v2 protocol: backward-compatible extension — connection starts in v1 mode, switches to v2 on SUBSCRIBE. v2 adds `[4B op_id]` to requests, structured error responses `[1B status][2B msg_len][msg]`, push events `[1B event_type][payload]`, operation cancellation. Max 8 subscribers, max 16 concurrent operations, 10 events/sec throttle.

### Connection Flow
1. Sender registers with relay, gets ticket
2. Receiver looks up ticket on relay, gets sender's endpoints
3. Both peers send UDP hole-punch probes (WHPK magic) to open NAT pinholes (skipped for relay endpoints, priority >= 200)
4. Receiver tries connections in parallel by priority: LAN (0) > IPv6 (75) > public IP (100)
5. If all direct connections fail, falls back to relay-forwarded QUIC (relay_forwarder.c proxies MsQuic UDP through relay FORWARD messages)
6. File streams over QUIC once connected (content-addressed chunks with dedup)

## Key Configuration
The relay server address is hardcoded in `src/wormhole.c`:
```c
#define DEFAULT_RELAY_HOST "wormholerelay.com"
#define DEFAULT_RELAY_PORT 443
```

Configurable settings in `~/.wormhole/config` (INI format, managed by `config.c`):
- `relay_host`, `relay_port` — Relay server address
- `max_storage_gb` — Storage quota (default 10)
- `replication_target` — Chunk replication target (default 4)
- `dht_enabled` — Enable DHT (default 1)
- `dht_port` — DHT UDP port (default 4568)
- `dht_bootstrap_nodes` — Comma-separated bootstrap host:port list (default empty = use relay)
- `ec_enabled` — Enable erasure coding (default 1)
- `ec_data_shards` — RS data shards per stripe (default 8)
- `ec_parity_shards` — RS parity shards per stripe (default 4)
- `health_check_interval_sec` — Health check period (default 1800)
- `min_storage_ratio` — Minimum reciprocity ratio to accept storage (default 50, i.e. 0.50 stored as integer percent)
- `proof_cache_count` — Pre-cached proofs per chunk (default 8)
- `auto_evict_enabled` — Auto-evict local chunks after replication (default 0 = keep local copies)

## Testing

### Unit tests (greatest.h framework)

**Windows:**
```bat
cd src
build.bat                               # Build main binaries first (generates .obj files)
cd test
test.bat                                # Build and run all 18 test executables
```

**Linux:**
```bash
cd src
make                                    # Build main binaries first
cd test
./test_linux.sh                         # Build and run all 18 test executables
```

Test suite (`src/test/`), 18 executables:
- `test_wire_format.c` — LE encoding/decoding (header-only, no link deps)
- `test_manifest.c` — Manifest v1 create/serialize/validate + v2 multi-file (chunk ranges, roundtrip, empty file)
- `test_chunk_store.c` — Chunk put/get/has/dedup + replica metadata (set/get/dedup/max) + LRU eviction (total size, prefers replicated)
- `test_transfer_state.c` — Resumable transfer bitfield save/load roundtrip, boundary cases (8/9 chunks), large count (1000), error handling
- `test_config.c` — INI config defaults, get/set (string/uint64/case-insensitive/overflow), file roundtrip (comments, whitespace, empty lines)
- `test_chunker.c` — File chunking (single/multi-chunk, deterministic hashing), directory chunking (multi-file, nested subdirs with '/' paths)
- `test_reed_solomon.c` — GF(2^8) codec: encode/decode, 1-2 missing shards, partial stripes, 256KB shards
- `test_erasure.c` — Stripe encoding, parity chunk storage, chunk reconstruction, EC metadata save/load roundtrip, RS(8,4) encode/reconstruct/partial stripe tests
- `test_routing_table.c` — XOR distance, bucket index, add/evict nodes, FindClosest, save/load, stale detection
- `test_dht_protocol.c` — Wire format struct sizes, Ed25519 sign/verify roundtrip, tamper detection, MTU fit
- `test_dht_store.c` — Put/get roundtrip, location merge, expiry, capacity limits, persistence
- `test_dht_lookup.c` — Shortlist seeding, response convergence, FIND_VALUE early termination, max iteration
- `test_proof.c` — Proof computation determinism, wrong seed detection, pre-compute cache hit/miss
- `test_incentives.c` — Balanced/unbalanced ledger, ratio enforcement, save/load roundtrip
- `test_health.c` — EC-based chunk recovery, health check stats, degraded chunk detection, replication needs
- `test_file_registry.c` — File registry save/load/update/list/delete, hex prefix lookup
- `test_file_crypto.c` — Encryption/decryption roundtrip (small/large/empty files), wrong key detection, ciphertext differs, null arg handling
- `test_ipc_v2.c` — IPC v2 protocol: error response write/read roundtrip, truncation, operation registry (register/get/cancel/unregister/full), v2 constants and backward compatibility

All tests use `greatest.h` (single-header test framework). Tests needing filesystem use `setup_test_home()`/`cleanup_test_home()` to redirect HOME to a temp directory.

### End-to-end daemon tests
```bat
cd src\test
test_e2e.bat                            # Automated daemon smoke test (no relay needed)
```
Tests store/get/status, EC metadata persistence, ledger persistence across restart, and EC recovery.

### Manual integration testing
```bash
dd if=/dev/urandom of=test_10mb.bin bs=1M count=10
md5sum test_10mb.bin                    # before
md5sum ~/Downloads/test_10mb.bin        # after
```
There is also a standalone test file at `src/relay/test_relay_client.c` (not integrated into the build).

### Multi-node integration tests
```bash
# Docker (Linux) — 5-node cluster
cd docker
docker compose up -d
./test_multi_node.sh                    # Automated: store, replicate, retrieve, failover
docker compose down -v
```
```bat
REM Windows — 3-node localhost cluster (requires relay connectivity)
cd src\test
test_multi_node.bat
```
Tests peer discovery, chunk replication across nodes, cross-node retrieval, node failure handling, EC recovery (data + parity), security permissions, config bounds validation, and storage quota enforcement. 22 total tests covering Phases 6-7, hardening, and post-Phase 7 improvements.

## Deployment
- Relay runs on a DigitalOcean droplet at `wormholerelay.com:443`
- `--public-addr` flag required for relay fallback endpoint injection
- Docker multi-node setup in `docker/`: `Dockerfile` (node), `Dockerfile.relay` (relay), `docker-compose.yml` (5-node cluster). See [BUILD_LINUX.md](BUILD_LINUX.md) for Docker usage.

## Platform Notes
- Client is cross-platform: Windows (MSVC, Win32 APIs, named pipe IPC) and Linux (GCC, POSIX, Unix domain socket IPC)
- Relay server targets Linux (POSIX sockets, pthreads, dual-stack IPv4/IPv6)
- All components use libsodium for Ed25519 cryptography
- Docker multi-node setup available for testing distributed scenarios on Linux

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full development roadmap.

**Completed:** Phases 1-8 (core transfer, P2P storage, DHT, erasure coding, multi-platform, production readiness, usability, GUI foundation)

### Phase 7: Usability & Management (Complete)
- 7A: File deletion — `wormhole delete <id>` removes chunks, EC metadata, DHT entries, registry entry ✅
- 7B: Key export/import — `wormhole export-key <id>` / `wormhole import-key <id> <key>` for encryption key backup ✅
- 7C: Daemon lifecycle — `wormhole daemon start|stop|restart|status` for process management ✅
- 7D: Peer visibility — `wormhole peers` lists DHT peers, `wormhole files -v` shows verbose file info ✅

### Phase 8: GUI Foundation (Complete)
- 8A: IPC Protocol v2 — subscriptions, structured errors, operation IDs, cancellation ✅
- 8B: Progress Reporting — chunker + FILE_GET progress callbacks, operation registry, cancellation ✅
- 8C: Send/Receive via Daemon — transfer_mgr.c, daemon-mediated transfers, `--direct` fallback ✅
- 8D: Config via IPC — CONFIG_LIST/GET/SET commands, hot-reload classification, value validation ✅
- 8E: Daemon Lifecycle Hardening — readiness signal, heartbeat, stale PID detection, log rotation ✅
- 8F: Push Notification Events — peer change, file status, health, transfer events ✅

### Phase 9: GUI Implementation (Planned)
- Qt 6 (C++) cross-platform GUI as thin IPC client to daemon
- See ROADMAP.md for sub-phases 9A-9E
