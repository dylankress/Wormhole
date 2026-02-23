# Wormhole Roadmap

## Completed Phases

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
- 4.1 Kademlia DHT foundation (routing table, UDP transport, PING/FIND_NODE, relay bootstrap)
- 4.2 DHT storage & chunk discovery (FIND_VALUE/STORE, iterative lookup, chunk announcement)
- 4.3 Erasure coding (RS(4,2) codec, manifest v3, parity generation)
- 4.4 Verification & incentives (proof-of-storage, health checks, storage ratio tracking)

### Phase 4.5: Integration ✅
- Wire erasure coding into daemon store path (`ErasureCoding_Encode` after chunking)
- Enforce storage ledger in CHUNK_STORE_REQUEST handling (`Ledger_ShouldAcceptStorage`)
- Connect DHT FIND_VALUE to health check recovery path (`Health_RecoverChunk`)
- Unit tests for health monitoring orchestration
- EC metadata save/load roundtrip tests
- End-to-end daemon smoke tests

### Phase 5: Multi-Platform ✅
- Linux client build system (Makefile)
- Linux unit tests (`test_linux.sh`, 17 test suites)
- Cross-platform IPC (Unix domain sockets on Linux)
- File registry (`wormhole files` command)
- Docker multi-node testing (5-node cluster)
- Windows multi-node replication test (3-node localhost)

---

### Phase 6: Production Readiness ✅
- 6A: Durability upgrade — R=4, RS(8,4) (13+ nines at 10% churn)
- 6B: Configurable auto-eviction (local copies preserved by default)
- 6C: DHT value store persistence (chunk locations survive restart)
- 6D: Multi-bootstrap resilience (up to 4 bootstrap nodes, exponential backoff)
- 6E: Enhanced health monitoring (20 chunks x 3 peers, event-driven, parity regeneration)
- 6F: Client-side encryption (XChaCha20-Poly1305, encrypt-before-chunk)
- 6G: TLS peer identity verification (Ed25519 in cert CN, MITM prevention)

---

## Phase 7: Usability & Management ✅

### Why This Phase Exists

Phases 1-6 built a working decentralized storage network, but several day-to-day usability gaps prevent Wormhole from being practical for real users:

- **No file deletion** — once stored, files cannot be removed. No CLI command, no IPC command, no cleanup path.
- **No key backup** — encryption keys live only in `~/.wormhole/files/`. Lose your machine = lose all files permanently.
- **No daemon lifecycle management** — users must manually run `wormholed` and kill it by PID.
- **No peer visibility** — can't see which peers hold your chunks or diagnose replication issues.

---

### Phase 7A: File Deletion — `wormhole delete <id>` ✅

**Effort**: Medium | **Impact**: Critical usability gap

Delete stored files: removes chunks from local store, cleans up EC metadata, removes DHT announcements, and deletes file registry entry.

Changes:
- `src/file_registry.h/c` — `FileRegistry_Delete()` removes registry file
- `src/dht/dht_store.h/c` — `DhtStore_Remove()` removes entry by key
- `src/ipc.h` — `IPC_CMD_FILE_DELETE` (0x08)
- `src/wormholed.c` — Delete handler: removes chunks, EC metadata, DHT entries, registry entry, updates stats
- `src/wormhole.c` — `wormhole delete <file-id>` CLI command
- `src/test/test_file_registry.c` — Delete roundtrip test
- `src/test/test_dht_store.c` — Remove roundtrip test

---

### Phase 7B: Key Export/Import — `wormhole export-key` / `wormhole import-key` ✅

**Effort**: Small | **Impact**: Data safety — prevents permanent file loss

Export and import per-file encryption keys for backup and cross-device access.

Changes:
- `src/ipc.h` — `IPC_CMD_EXPORT_KEY` (0x09), `IPC_CMD_IMPORT_KEY` (0x0A)
- `src/wormholed.c` — Export/import handlers
- `src/wormhole.c` — `wormhole export-key <file-id>` and `wormhole import-key <file-id> <hex-key>` CLI commands

---

### Phase 7C: Daemon Lifecycle — `wormhole daemon start/stop` ✅

**Effort**: Small | **Impact**: Usability — no more manual process management

Spawn and stop the daemon from the CLI without manual process management.

Changes:
- `src/wormhole.c` — `wormhole daemon start|stop|restart|status` subcommand
  - `start`: check IPC, spawn detached `wormholed`, write PID file, confirm startup
  - `stop`: send `IPC_CMD_SHUTDOWN`, wait for exit, clean up PID file
  - `restart`: stop then start
  - `status`: alias for existing `wormhole status`

---

### Phase 7D: Peer Visibility — `wormhole peers` + enhanced status ✅

**Effort**: Small-Medium | **Impact**: Observability — diagnose replication issues

See connected peers and per-file replication details.

Changes:
- `src/ipc.h` — `IPC_CMD_PEER_LIST` (0x0B)
- `src/wormholed.c` — Peer list handler: queries DHT routing table for all known nodes
- `src/wormhole.c` — `wormhole peers` CLI command (node ID, address, port, last seen)
- `src/wormhole.c` — `wormhole files -v` verbose mode (chunk counts, replica peers)

---

### Implementation Priority

| Order | Phase | Effort | Key Metric |
|-------|-------|--------|------------|
| 1 | 7A: File deletion | Medium | Critical usability gap |
| 2 | 7B: Key export/import | Small | Data safety |
| 3 | 7C: Daemon lifecycle | Small | Usability |
| 4 | 7D: Peer visibility | Small-Medium | Observability |

All 4 sub-phases are independent. Suggested order is by impact.
