# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Wormhole is a secure peer-to-peer file transfer tool written in C. It uses QUIC (via MsQuic) for encrypted file streaming and a custom UDP relay server for peer coordination and NAT traversal. Users share files with ticket codes like "3-guitar-battery".

**Current focus:** Phases 1–4.5 complete. File transfer with progress bar, resume, and directory support is production-ready. Daemon (`wormholed`) provides persistent chunk storage, peer discovery via Kademlia DHT, erasure coding (RS(4,2)), proof-of-storage verification, and storage incentive tracking — all wired together and tested (15 unit test suites + E2E daemon tests).

**Long-term vision:** Wormhole will evolve into a decentralized P2P file storage platform — a privacy-respecting alternative to Dropbox/Google Drive. Peers contribute available disk space to the network, and files are distributed (like BitTorrent) so users can store and recover their files on demand without centralized cloud providers. Design decisions should keep this trajectory in mind.

## Build Commands

### Windows Client (requires MSVC x64 environment)
```bat
cd src
build_with_env.bat          # Sets up VS environment + builds
# Or if already in VS Developer Command Prompt:
build.bat                   # Builds both wormhole.exe and wormholed.exe
```
Output: `src/build/wormhole.exe`, `src/build/wormholed.exe` (plus `msquic.dll` and `libsodium.dll`)

### Linux Relay Server
```bash
cd relay-server
./build.sh
```
Output: `relay-server/build/relay-server`

### Dependencies
- **MsQuic**: Git submodule at `msquic/` — init with `git submodule update --init --recursive`, then build separately
- **libsodium**: Pre-built Windows x64 binaries in `deps/libsodium/`; on Linux install `libsodium-dev`
- **BLAKE3**: Portable C sources in `deps/blake3/` (no SIMD assembly)
- **Reed-Solomon**: GF(2^8) erasure coding codec in `deps/reed_solomon/` (rs.h/rs.c)
- **EFF wordlist**: Bundled at `deps/eff_large_wordlist.txt` (7,776 words for ticket generation)

## Usage
```
wormhole.exe send <file|directory>      # Creates ticket, waits for receiver
wormhole.exe receive <ticket>           # Downloads to ~/Downloads (resumable)
wormhole.exe store <file>               # Store file chunks via daemon
wormhole.exe get <hash> [-o <file>]     # Retrieve a chunk by hash
wormhole.exe status                     # Show daemon status
wormhole.exe config list                # Show all settings
wormhole.exe config get <key>           # Get a config value
wormhole.exe config set <key> <val>     # Set a config value

Global flags:
  --daemon <port>                        # Connect to daemon on specified port (default 4567)
```

## Architecture

### Three Components

**Client** (`src/`) — Windows QUIC-based file transfer app
- `wormhole.c` — Entry point, CLI (`send`/`receive`/`store`/`get`/`status`/`config`), MsQuic lifecycle, QUIC listener/connection setup, UDP hole-punch probes (WHPK), parallel connection racing, Ctrl+C cleanup
- `wormholed.c` — Persistent daemon process: QUIC listener, chunk store, relay connection with auto-reconnect, peer discovery, chunk replication (3x target), DHT node bootstrap/polling, health checks, proof-of-storage challenge/response, storage ledger, named pipe IPC server
- `stream.c` — Chunk-based two-stream transfer protocol: control stream (manifest request/response, chunk request, transfer complete) and data stream (chunk frames). Progress bar with speed/ETA. Resumable transfers via `transfer_state.c`. Multi-file receive support.
- `file_io.c` — Cross-platform file ops, 64-bit size support, Downloads folder integration
- `crypto.c` — Self-signed TLS cert generation, Windows Certificate Store integration
- `manifest.c` — File manifest serialization: v1 (single file), v2 (multi-file with per-file entries and chunk ranges), v3 (erasure coding metadata — ec_k, ec_m, stripe definitions with parity hashes)
- `chunker.c` — Content-addressed chunking (Blake3 hashes, 256KB chunks), `Chunker_BuildManifestFromDirectory` for recursive directory transfer
- `chunk_store.c` — Dedup chunk store (`ChunkStore_Has/Get/Put`), content-addressed by Blake3 hash. Replica metadata tracking (`ChunkStore_PutWithMeta`, `GetReplicaCount`, `SetReplicaLocation`). Storage quota enforcement with LRU eviction (`ChunkStore_Evict`) preferring highly-replicated chunks.
- `transfer_state.c` — Resumable transfer state: saves/loads received-chunks bitfield to `~/.wormhole/transfers/<hash>.state`
- `config.c` — Configuration management: INI-style `~/.wormhole/config` file with defaults (12 keys — see Key Configuration section)
- `ipc.c` — Named pipe IPC (`\\.\pipe\wormhole`): server API for daemon, client API for CLI. Message framing: `[4B length][1B command][payload]`. Commands: STORE, GET, STATUS, SHUTDOWN, DHT_STATUS.
- `erasure.c` — RS(4,2) erasure coding integration: stripe-based encoding (`ErasureCoding_Encode`), chunk reconstruction from parity (`ErasureCoding_ReconstructChunk`), EC_GROUP serialization for manifest v3
- `proof.c` — Proof-of-storage: `Proof_Compute` (Blake3(seed || chunk_data)), pre-cached proofs per chunk (`~/.wormhole/proofs/`), challenge/response verification
- `health.c` — Chunk health monitoring: periodic DHT queries for chunk locations, proof-of-storage challenges to holders, recovery orchestration for under-replicated chunks
- `incentives.c` — Storage ratio tracking: per-peer balance ledger (`STORAGE_LEDGER`), accept/reject storage based on reciprocity ratio (threshold 0.5), persisted to `~/.wormhole/storage_ledger.bin`
- `relay_forwarder.c` — Loopback UDP proxy for QUIC-over-relay fallback (when direct connections fail)
- `dht/` — Kademlia DHT subsystem (UDP port 4568):
  - `dht_protocol.h` — Wire format: 102-byte header `[1B type][1B version][4B txn_id][32B sender_id][64B ed25519_sig]`, messages 0x20-0x27
  - `routing_table.c` — K-bucket routing table (K=20, 256 buckets), XOR distance, LRS eviction, persistence to `~/.wormhole/dht_routing_table.bin`
  - `dht_node.c` — UDP transport, RPC dispatch (PING/PONG, FIND_NODE, FIND_VALUE, STORE), bootstrap, bucket refresh, pending RPC tracking
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
- `REPLICATION_TARGET 3` — target copies per chunk for P2P storage durability
- `MAX_FIND_PEERS 50` — cap on peer discovery responses

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
- `replication_target` — Chunk replication target (default 3)
- `dht_enabled` — Enable DHT (default 1)
- `dht_port` — DHT UDP port (default 4568)
- `ec_enabled` — Enable erasure coding (default 1)
- `ec_data_shards` — RS data shards per stripe (default 4)
- `ec_parity_shards` — RS parity shards per stripe (default 2)
- `health_check_interval_sec` — Health check period (default 1800)
- `min_storage_ratio` — Minimum reciprocity ratio to accept storage (default 50, i.e. 0.50 stored as integer percent)
- `proof_cache_count` — Pre-cached proofs per chunk (default 8)

## Testing

### Unit tests (greatest.h framework)
```bat
cd src
build.bat                               # Build main binaries first (generates .obj files)
cd test
test.bat                                # Build and run all 15 test executables
```
Test suite (`src/test/`), 15 executables:
- `test_wire_format.c` — LE encoding/decoding (header-only, no link deps)
- `test_manifest.c` — Manifest v1 create/serialize/validate + v2 multi-file (chunk ranges, roundtrip, empty file)
- `test_chunk_store.c` — Chunk put/get/has/dedup + replica metadata (set/get/dedup/max) + LRU eviction (total size, prefers replicated)
- `test_transfer_state.c` — Resumable transfer bitfield save/load roundtrip, boundary cases (8/9 chunks), large count (1000), error handling
- `test_config.c` — INI config defaults, get/set (string/uint64/case-insensitive/overflow), file roundtrip (comments, whitespace, empty lines)
- `test_chunker.c` — File chunking (single/multi-chunk, deterministic hashing), directory chunking (multi-file, nested subdirs with '/' paths)
- `test_reed_solomon.c` — GF(2^8) codec: encode/decode, 1-2 missing shards, partial stripes, 256KB shards
- `test_erasure.c` — Stripe encoding, parity chunk storage, chunk reconstruction, EC metadata save/load roundtrip
- `test_routing_table.c` — XOR distance, bucket index, add/evict nodes, FindClosest, save/load, stale detection
- `test_dht_protocol.c` — Wire format struct sizes, Ed25519 sign/verify roundtrip, tamper detection, MTU fit
- `test_dht_store.c` — Put/get roundtrip, location merge, expiry, capacity limits, persistence
- `test_dht_lookup.c` — Shortlist seeding, response convergence, FIND_VALUE early termination, max iteration
- `test_proof.c` — Proof computation determinism, wrong seed detection, pre-compute cache hit/miss
- `test_incentives.c` — Balanced/unbalanced ledger, ratio enforcement, save/load roundtrip
- `test_health.c` — EC-based chunk recovery, health check stats, degraded chunk detection, replication needs

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

## Deployment
- Relay runs on a DigitalOcean droplet at `wormholerelay.com:443`
- `--public-addr` flag required for relay fallback endpoint injection
- No systemd/Docker config in repo (manual deployment)

## Platform Notes
- Client targets Windows (uses Win32 APIs, MSVC compiler, Windows Certificate Store)
- Relay server targets Linux (POSIX sockets, pthreads, dual-stack IPv4/IPv6)
- Both use libsodium for Ed25519 cryptography

## Roadmap

### Phase 1: Stabilize & Ship v1.0 ✅
- Ephemeral receiver port, dead code cleanup
- Cubic congestion control + PMTUD
- Ctrl+C cleanup, periodic progress, error messages
- Relay-forwarded QUIC fallback
- Documentation

### Phase 2: Transfer Enhancements ✅
- 2.1 Live progress bar with speed/ETA
- 2.2 Resumable transfers (checkpoint + CHUNK_REQUEST)
- 2.3 Multi-file directory transfer (manifest v2)
- 2.4 Test framework + unit tests

### Phase 3: P2P Storage Foundation ✅
- 3.1 Persistent daemon (wormholed) + IPC
- 3.2 Peer discovery (FIND_PEERS relay protocol)
- 3.3 Chunk replication (3x target)
- 3.4 Storage quota + LRU eviction + config

### Phase 4: Decentralized Network ✅
- 4.1 Kademlia DHT foundation (routing table, UDP transport, PING/FIND_NODE, relay bootstrap) ✅
- 4.2 DHT storage & chunk discovery (FIND_VALUE/STORE, iterative lookup, chunk announcement) ✅
- 4.3 Erasure coding (RS(4,2) codec, manifest v3, parity generation) ✅
- 4.4 Verification & incentives (proof-of-storage, health checks, storage ratio tracking) ✅

### Phase 4.5: Integration ✅
- Wire erasure coding into daemon store path (`ErasureCoding_Encode` after chunking) ✅
- Enforce storage ledger in CHUNK_STORE_REQUEST handling (`Ledger_ShouldAcceptStorage`) ✅
- Connect DHT FIND_VALUE to health check recovery path (`Health_RecoverChunk`) ✅
- Add `test_health.c` unit tests for health monitoring orchestration ✅
- EC metadata save/load roundtrip unit tests ✅
- End-to-end daemon smoke tests (`test_e2e.bat`) ✅

### Phase 5: Next Steps
- Multi-platform support (Linux client)
