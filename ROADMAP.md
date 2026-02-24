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
- Linux unit tests (`test_linux.sh`, 18 test suites)
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

---

## Phase 8: GUI Foundation ✅

### Why This Phase Exists

Phases 1-7 built a functionally complete decentralized storage system, but the daemon-client interface has critical gaps that would force a GUI into ugly workarounds:

- **IPC too limited** — only 3 status codes, no error messages, no async notifications, no progress reporting, no cancellation
- **Send/receive bypasses daemon** — ~300+ lines of blocking code in wormhole.c with printf progress bars and global state
- **No config via IPC** — GUI would need to parse `~/.wormhole/config` directly
- **Daemon lifecycle gaps** — no readiness signal, no health heartbeat, stale PID files on crash

Fixing these first creates a clean, natural GUI integration point. After Phase 8, the CLI becomes a thin IPC client — identical architecture to what a GUI would use.

---

### Phase 8A: IPC Protocol v2 — Subscriptions, Errors, and Operations ✅

**Effort**: Medium | **Impact**: Foundation for all GUI features

Extend IPC to support everything a GUI needs:

1. **Protocol versioning** — 2-byte version header so v1 clients still work unchanged
2. **Structured errors** — `[1B status][2B error_msg_len][error_msg_utf8]` when status != OK
3. **Operation IDs** — 4-byte client-assigned ID in every request/response for correlation
4. **Subscription mode** — `IPC_CMD_SUBSCRIBE` (0x0C): client specifies event types, daemon keeps connection open and pushes events:
   - `EVENT_PROGRESS` — bytes/chunks transferred, speed, ETA
   - `EVENT_OP_COMPLETE` — operation finished with success/failure
   - `EVENT_PEER_CHANGE` — peer connected/disconnected
   - `EVENT_FILE_STATUS` — replication status changed
   - `EVENT_HEALTH` — health check results
5. **Cancellation** — `IPC_CMD_CANCEL` (0x0D) with operation ID

Changes:
- `src/ipc.h` — v2 constants, event types, IPC_CMD_SUBSCRIBE/CANCEL, structured error helpers
- `src/ipc.c` — v2 framing (backward-compatible), subscription tracking, event push delivery
- `src/wormholed.c` — Subscribe/cancel handlers, event emission helpers
- `src/wormhole.c` — v2 client helpers for subscription and error parsing
- `src/test/test_ipc_v2.c` — v2 framing, error parsing, subscription delivery, cancel roundtrip

---

### Phase 8B: Progress Reporting for Daemon Operations ✅

**Effort**: Medium | **Impact**: Real-time feedback for GUI and CLI

Wire progress reporting into every long-running daemon operation:

1. **Progress context struct** — `OPERATION_PROGRESS` with op_id, cancelled flag, bytes/chunks done/total, timestamps
2. **FILE_GET progress** — emit events after each chunk retrieval, check cancel flag in loop
3. **STORE progress** — add progress callback to chunker API for per-chunk reporting
4. **Background work progress** — replication batch and erasure encoding emit progress events
5. **Operation registry** — array of active operations (max 16), enables cancel-by-ID
6. **Event throttling** — max 10 progress events/second per operation

Changes:
- `src/ipc.h` — `OPERATION_PROGRESS` struct, operation registry API
- `src/ipc.c` — Operation registry management, throttled event emission
- `src/wormholed.c` — Progress hooks in STORE and FILE_GET handlers, work queue progress
- `src/chunker.h/c` — Progress callback parameter for `Chunker_BuildManifestAndStore`
- `src/wormhole.c` — Subscribe + display progress bar for store/get operations

---

### Phase 8C: Send/Receive via Daemon ✅

**Effort**: Large | **Impact**: Transfers persist beyond CLI/GUI lifetime

Move send/receive into the daemon so transfers persist beyond CLI/GUI lifetime:

1. **New IPC commands:**
   - `IPC_CMD_SEND` (0x0E) — daemon creates ticket, manages relay/QUIC, pushes progress
   - `IPC_CMD_RECEIVE` (0x0F) — daemon looks up sender, races connections, receives file
   - `IPC_CMD_TRANSFER_STATUS` (0x10) — query active transfers
   - `IPC_CMD_TRANSFER_LIST` (0x11) — list all active/recent transfers
2. **New module: `transfer_mgr.h/c`** — manages ACTIVE_TRANSFER structs (max 8 concurrent), each with its own relay client instance, QUIC connection, progress tracking
3. **Relay client per transfer** — separate from daemon's storage relay client
4. **Connection racing in daemon** — port `ParallelConnectPeers()` logic from wormhole.c
5. **Stream progress callback** — replace `PrintProgressBar()` printf calls with callback; CLI sets callback to print, daemon sets callback to emit IPC events
6. **CLI becomes thin client** — `cmd_send()` → IPC_CMD_SEND + subscribe + progress bar
7. **Preserve `--direct` flag** — standalone send/receive without daemon for quick one-off transfers

Changes:
- New `src/transfer_mgr.h`, `src/transfer_mgr.c` — transfer lifecycle management
- `src/stream.h/c` — Progress callback in CHUNK_SEND_CONTEXT/CHUNK_RECEIVE_CONTEXT
- `src/ipc.h` — Transfer IPC commands (0x0E-0x11)
- `src/wormholed.c` — Transfer command handlers, transfer manager integration
- `src/wormhole.c` — Thin client send/receive via IPC, `--direct` flag bypass
- `src/Makefile`, `src/build.bat` — Add transfer_mgr.c to build

---

### Phase 8D: Config via IPC ✅

**Effort**: Small | **Impact**: GUI can manage settings without file parsing

1. **New IPC commands:**
   - `IPC_CMD_CONFIG_LIST` (0x12) — returns all config entries
   - `IPC_CMD_CONFIG_GET` (0x13) — get single value
   - `IPC_CMD_CONFIG_SET` (0x14) — set value with validation, response includes `restart_required` flag
2. **Hot-reload classification** — safe settings (health_check_interval, min_storage_ratio) apply immediately; unsafe settings (dht_port, ec_data_shards) require restart
3. **CLI update** — `wormhole config` uses IPC when daemon running, direct file access when not

Changes:
- `src/ipc.h` — Config IPC commands (0x12-0x14), hot-reload flag
- `src/config.h/c` — `Config_IsHotReloadable()` classification function
- `src/wormholed.c` — Config list/get/set handlers, live config update for safe keys
- `src/wormhole.c` — Config commands use IPC when daemon is running

---

### Phase 8E: Daemon Lifecycle Hardening ✅

**Effort**: Small-Medium | **Impact**: Reliable daemon management

1. **Readiness signal** — daemon writes `~/.wormhole/wormholed.ready` after all subsystems init; CLI waits for file with timeout
2. **Health heartbeat** — daemon updates `~/.wormhole/wormholed.heartbeat` every 10s; `daemon status` checks freshness
3. **Stale PID detection** — check if PID process actually exists before declaring "daemon already running"
4. **Log rotation** — on startup, rotate log if > 10MB (keep one backup)
5. **Graceful shutdown timeout** — 5-second grace period for active operations
6. **Heartbeat IPC** — `IPC_CMD_HEARTBEAT` (0x15) lightweight ping returning uptime

Changes:
- `src/wormholed.c` — Readiness file, heartbeat thread, log rotation, graceful shutdown
- `src/wormhole.c` — Wait for readiness, check heartbeat in `daemon status`, stale PID recovery
- `src/ipc.h` — `IPC_CMD_HEARTBEAT` (0x15)

---

### Phase 8F: Push Notification Events ✅

**Effort**: Small | **Impact**: Real-time GUI visibility into network activity

Wire remaining real-time events for GUI visibility:

1. **Peer events** — DHT peer added/removed (hook `RoutingTable_AddNode`)
2. **File status events** — replication status transitions (REPLICATING → SAFE)
3. **Health events** — health check cycle summaries
4. **Transfer events** — started, completed, failed

Changes:
- `src/ipc.h/c` — Event emission helpers for each event type
- `src/wormholed.c` — Event hooks in replication, health check, file status update paths
- `src/dht/routing_table.c` — Optional callback on node add/remove
- `src/health.c` — Health check summary event emission

---

### Dependency Graph & Parallelism

```
8A (IPC v2)  [foundation]
  ├──> 8B (Progress)         [depends on 8A]
  │      └──> 8C (Send/Recv) [depends on 8A, 8B]
  │                └──> 8F (Events) [depends on 8A, 8C]
  ├──> 8D (Config)           [depends on 8A, parallel with 8B]
  └──> 8E (Lifecycle)        [depends on 8A, parallel with 8B]
```

**Critical path:** 8A → 8B → 8C → 8F

---

## Phase 9: GUI Implementation — Qt (C++) ✅

**Framework:** Qt 6 (C++) — native widgets, cross-platform (Windows + Linux), calls C IPC functions directly via `extern "C"`.

With Phase 8 complete, the GUI is a thin event-driven Qt client connecting to the daemon via IPC. 22 files in `gui/` directory.

### 9A: Qt Project Setup ✅
- CMake build integration (`gui/CMakeLists.txt`, Qt6 Widgets, CMake 3.21+)
- Standalone `ipc_client.h/c` (extracted from `src/ipc.c`, no MsQuic dependency)
- `IpcWorker` + `DaemonClient` Qt wrappers (background QThread, signal forwarding, auto-reconnect)
- `MainWindow` with tabs (Transfers, Files, Network), menu bar, status bar with daemon connection indicator

### 9B: Send/Receive UI — TransferWidget ✅
- Send file picker and directory picker (detects directories via `IsDirectory()`)
- Receive by ticket with destination directory chooser
- Real-time QProgressBar per transfer, transfer queue view
- Cancel support via IPC operation cancellation
- Ticket display parsed from push transfer events

### 9C: Storage Management — FileListWidget ✅
- File tree (QTreeWidget) with name, size, chunks, status columns
- Store via drag-and-drop (`dragEnterEvent`/`dropEvent`)
- Get/delete actions with context menu
- Key export/import via context menu
- Search/filter QLineEdit, column sorting
- Delete confirmation with file size and status details

### 9D: Settings & Network — SettingsDialog + PeerListWidget ✅
- Settings panel with typed widgets per config key (spinbox, checkbox, line edit)
- Descriptive tooltips for each setting
- Hot-reload detection — shows "Restart Daemon" button when restart-required settings change
- Peer table with node ID (truncated, full hex as tooltip), address, port, last seen
- IPv6 address display via `inet_ntop`
- DHT status indicator, refresh button

### 9E: System Integration — TrayManager ✅
- QSystemTrayIcon with context menu (Show/Hide, Start Daemon, Quit)
- Minimize-to-tray on window close
- Desktop notifications on transfer complete (distinguishes send vs receive)
- Health alert notifications with chunk count
- Quit confirmation dialog

### GUI Audit Fixes ✅

19 post-implementation audit fixes applied:

| Fix | Description |
|-----|-------------|
| Ticket display | PushTransferEvent includes ticket bytes, GUI parses and shows ticket |
| Error feedback | IpcWorker sendFile/receiveFile parse errors, emit transferFailed signal |
| Daemon auto-start | MainWindow offers to start daemon on first disconnect, File menu "Start Daemon" |
| closeEvent fix | No-tray path calls `m_client->stop()` + `QApplication::quit()` |
| Destination dir picker | `onReceive()` opens `QFileDialog::getExistingDirectory` |
| Empty states | TransferWidget, FileListWidget, PeerListWidget show placeholder text |
| Window geometry | QSettings save/restore in MainWindow constructor/closeEvent |
| Single instance | QLockFile on `~/.wormhole/gui.lock` |
| Refresh buttons | FileListWidget and PeerListWidget have Refresh buttons |
| Reconnect refresh | `MainWindow::onConnected()` refreshes all widgets + transfer list |
| Relay config | `transfer_mgr.c` reads relay_host/relay_port from daemon config |
| File search | FileListWidget has search/filter QLineEdit |
| Column sorting | FileListWidget `setSortingEnabled(true)` |
| IPv6 display | PeerListWidget uses `inet_ntop` for actual IPv6 addresses |
| Full node ID | PeerListWidget shows full 64-char hex as tooltip |
| Timer control | FileListWidget and PeerListWidget pause timers on disconnect |
| Better delete confirm | Shows file size and status in delete dialog |
| Settings tooltips | Each config key has descriptive tooltip |
| Restart button | SettingsDialog shows "Restart Daemon" when restart-required settings change |

**Build:**
```bash
cd gui
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
cmake --build build
```
Requires Qt 6 (`sudo apt install qt6-base-dev` on Linux) and CMake 3.21+. See [TESTING_GUIDE.md](TESTING_GUIDE.md) for platform-specific details.
