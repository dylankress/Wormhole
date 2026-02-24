# Wormhole Testing Guide

Complete walkthrough for building, running automated tests, and manually verifying every functional feature. Covers Phases 1-9 (direct transfer, P2P storage, DHT, erasure coding RS(8,4), multi-platform, production readiness, usability & management, GUI foundation, Qt GUI).

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Build the Project](#2-build-the-project)
   - [Build the GUI](#21-build-the-gui)
3. [Automated Tests](#3-automated-tests)
   - [Unit Tests (18 suites)](#31-unit-tests-18-suites)
   - [E2E Daemon Tests](#32-e2e-daemon-tests)
   - [Docker Multi-Node Tests](#33-docker-multi-node-tests)
   - [GUI IPC Tests](#34-gui-ipc-tests)
4. [Manual Testing — Direct File Transfer](#4-manual-testing--direct-file-transfer)
   - [Setup & Test Files](#41-setup--test-files)
   - [Relay Server](#42-relay-server)
   - [Small File Transfer (LAN)](#43-small-file-transfer-lan)
   - [Large File Transfer (Multi-Chunk)](#44-large-file-transfer-multi-chunk)
   - [Directory Transfer](#45-directory-transfer)
   - [Resumable Transfer](#46-resumable-transfer)
   - [Relay Fallback](#47-relay-fallback)
5. [Manual Testing — Daemon & Storage](#5-manual-testing--daemon--storage)
   - [Daemon Basics](#51-daemon-basics)
   - [Store & Get](#52-store--get)
   - [Client-Side Encryption](#53-client-side-encryption)
   - [File Listing](#54-file-listing)
   - [Erasure Coding & Recovery](#55-erasure-coding--recovery)
6. [Manual Testing — Distributed Network](#6-manual-testing--distributed-network)
   - [Two-Node Setup](#61-two-node-setup)
   - [Peer Discovery (Relay + DHT)](#62-peer-discovery-relay--dht)
   - [Chunk Replication (R=4)](#63-chunk-replication-r4)
   - [Cross-Node Retrieval](#64-cross-node-retrieval)
   - [Proof-of-Storage Challenges](#65-proof-of-storage-challenges)
   - [Storage Ledger & Incentives](#66-storage-ledger--incentives)
7. [Manual Testing — Phase 6 Features](#7-manual-testing--phase-6-features)
   - [Auto-Eviction Config](#71-auto-eviction-config)
   - [DHT Store Persistence](#72-dht-store-persistence)
   - [Multi-Bootstrap](#73-multi-bootstrap)
   - [TLS Peer Identity Verification](#74-tls-peer-identity-verification)
   - [Phase 7 Features](#75-phase-7-features)
8. [Manual Testing — Config & Robustness](#8-manual-testing--config--robustness)
   - [All 14 Config Keys](#81-all-14-config-keys)
   - [Graceful Shutdown](#82-graceful-shutdown)
   - [Error Cases](#83-error-cases)
9. [Manual Testing — GUI](#9-manual-testing--gui)
10. [Results Checklist](#10-results-checklist)

---

## 1. Prerequisites

### Windows

- **Visual Studio Developer Command Prompt** (x64 Native Tools) — `cl.exe` on PATH
- **MsQuic** built from `msquic/` submodule (`git submodule update --init --recursive`)
- **libsodium** pre-built binaries in `deps/libsodium/` (Windows x64)

### Linux

- **GCC** and **make** (`sudo apt install build-essential`)
- **libsodium** (`sudo apt install libsodium-dev`)
- **OpenSSL** dev headers (`sudo apt install libssl-dev`)
- **MsQuic** built from source (see [BUILD_LINUX.md](BUILD_LINUX.md))
- **Docker + Docker Compose** (optional, for multi-node tests)

### Both Platforms

- **Blake3** portable C sources in `deps/blake3/` (included)
- **Reed-Solomon** codec in `deps/reed_solomon/` (included)
- **EFF wordlist** at `deps/eff_large_wordlist.txt` (included)

---

## 2. Build the Project

### Windows

Open a **x64 Native Tools Command Prompt for VS**:

```bat
cd src
build.bat
```

Output: `src\build\wormhole.exe`, `src\build\wormholed.exe` (plus `msquic.dll`, `libsodium.dll`)

### Linux

```bash
cd src
make
```

Output: `src/build/wormhole`, `src/build/wormholed` (plus `libmsquic.so`)

### Relay Server (Linux only)

```bash
cd relay-server
./build.sh
```

Output: `relay-server/build/relay-server`

**Verify:** Check that all binaries exist before proceeding.

### 2.1 Build the GUI

The Qt GUI is a separate CMake project in `gui/`. It communicates with the daemon via IPC — no MsQuic dependency.

**Prerequisites:** Qt 6 and CMake 3.21+

**Windows** (from x64 Native Tools Command Prompt):

```bat
cd gui
cmake -B build_windows -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64
cmake --build build_windows --config Release
```

The first build automatically runs `windeployqt` to copy Qt DLLs alongside the executable. Run from anywhere:

```bat
gui\build_windows\Release\wormhole-gui.exe
```

**Linux:**

```bash
cd gui
cmake -B build_linux
cmake --build build_linux
./build_linux/wormhole-gui
```

Qt6 is typically found automatically on Linux if installed via your package manager (`sudo apt install qt6-base-dev`).

**Quick rebuild** after code changes — just re-run the build command in the appropriate build directory:

```bash
cmake --build build_linux        # Linux
cmake --build build_windows      # Windows
```

**Note:** The daemon (`wormholed`) must be running for the GUI to connect. Start it first with `wormhole daemon start` or `wormholed --port 4567`. The GUI status bar shows "Connected" when the IPC link is active.

---

## 3. Automated Tests

### 3.1 Unit Tests (18 suites)

**Windows** (from Developer Command Prompt, after `build.bat`):

```bat
cd src\test
test.bat
```

**Linux** (after `make`):

```bash
cd src/test
./test_linux.sh
```

**Expected output:**

```
============================================
  Results: 18/18 passed, 0 failed
============================================
```

**Test inventory:**

| # | Test | Covers |
|---|------|--------|
| 1 | `test_wire_format` | Little-endian encoding/decoding (header-only) |
| 2 | `test_manifest` | Manifest v1/v2 create/serialize/validate, multi-file |
| 3 | `test_chunk_store` | Chunk put/get/has/dedup, replica metadata, LRU eviction |
| 4 | `test_transfer_state` | Resumable transfer bitfield save/load, boundary cases |
| 5 | `test_config` | INI config defaults (14 keys), get/set, file roundtrip |
| 6 | `test_chunker` | File/directory chunking, deterministic hashing |
| 7 | `test_reed_solomon` | GF(2^8) encode/decode, 1-2 missing shards, 256KB shards |
| 8 | `test_erasure` | RS(4,2) + RS(8,4) stripes, parity, reconstruction, EC metadata |
| 9 | `test_routing_table` | XOR distance, K-bucket add/evict, FindClosest, persistence |
| 10 | `test_dht_protocol` | Wire format sizes, Ed25519 sign/verify, tamper detection |
| 11 | `test_dht_store` | DHT put/get, location merge, expiry, capacity, persistence |
| 12 | `test_dht_lookup` | Shortlist seeding, convergence, FIND_VALUE, max iteration |
| 13 | `test_proof` | Proof-of-storage determinism, wrong seed, pre-compute cache |
| 14 | `test_incentives` | Balanced/unbalanced ledger, ratio enforcement, persistence |
| 15 | `test_health` | EC recovery, health check stats, degraded detection |
| 16 | `test_file_registry` | File registry save/load/update/list, hex prefix lookup |
| 17 | `test_file_crypto` | Encrypt/decrypt roundtrip (small/large/empty), wrong key, null args |
| 18 | `test_ipc_v2` | IPC v2 error responses, operation registry, v2 constants, backward compat |

If a test fails:
- **COMPILE FAILED** — Did you run `build.bat` / `make` first?
- **Runtime FAIL** — greatest.h output shows which assertion failed and expected vs actual values.

### 3.2 E2E Daemon Tests

**Windows only** (no relay needed):

```bat
cd src\test
test_e2e.bat
```

Tests: daemon startup, store/get/status, EC metadata persistence, ledger persistence across restart, EC chunk recovery. Runs in an isolated temp directory on port 14567.

### 3.3 Docker Multi-Node Tests

**Linux** (requires Docker):

```bash
cd docker
docker compose build              # One-time image build
docker compose up -d              # Start 5-node cluster
./test_multi_node.sh              # Run automated tests
docker compose down -v            # Tear down
```

Tests: daemon health across 5 nodes, file store, DHT peer discovery, chunk replication to 3+ additional nodes (R=4), cross-node retrieval, node failure survival, EC recovery (data + parity chunks), security permissions, config bounds validation, and storage quota enforcement. 22 total tests covering Phase 6 (production readiness), Phase 7 (usability & management), hardening, and post-Phase 7 improvements.

**Windows** (3-node localhost, requires relay connectivity):

```bat
cd src\test
test_multi_node.bat
```

### 3.4 GUI IPC Tests

Headless integration tests that exercise the Qt GUI's `DaemonClient`/`IpcWorker` against live daemons — no display server required. Available in two modes:

| Mode | Tests | Command | Requirements |
|------|-------|---------|-------------|
| **Localhost** (quick) | 15 (1-14 + 22) | `cd gui && ./test_gui_ipc.sh` | Daemon + Qt6 |
| **Docker** (full) | 22 | `cd docker && ./test_gui_ipc.sh` | Docker + Docker Compose |

The 22 tests cover: connect, status, DHT, peer list, config CRUD, store/retrieve with integrity check, key export/import, file events, multi-node replication (15-18), P2P file transfer (19-21), and file deletion (22).

**Why two modes?** Multi-node replication and P2P transfer (tests 15-21) require each daemon to have a distinct network address. On localhost, all daemons share one IP, so peers can't reach each other. Docker gives each node its own container IP, enabling real replication and transfer.

#### Localhost Mode (15 tests, ~30 seconds)

Runs a single daemon — tests 15-21 (replication/transfer) are automatically skipped.

**Linux:**

```bash
# 1. Build the daemon
cd src && make

# 2. Build the GUI test binary (requires Qt 6 and CMake 3.21+)
cd gui
cmake -B build_linux
cmake --build build_linux

# 3. Run the test
./test_gui_ipc.sh
```

**Windows** (from x64 Native Tools Command Prompt):

```bat
REM 1. Build the daemon
cd src
build.bat

REM 2. Build the GUI test binary (requires Qt 6 and CMake 3.21+)
cd gui
cmake -B build_windows -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64
cmake --build build_windows --config Release

REM 3. Run the test
test_gui_ipc.bat
```

**Expected output (localhost):**

```
=== GUI IPC Test (Single-Node, 15 tests) ===

Starting daemon (QUIC:4567, DHT:14568)...
Daemon running

--- Running test binary ---

=== Headless GUI IPC Test Suite ===
Test file:  /tmp/.../test_1mb.bin
Nodes:      /tmp/.../home1/.wormhole/wormhole_4567.sock

--- Daemon & IPC ---
  1/22  [PASS]  Connect to daemon
  2/22  [PASS]  Status poll  (peers=0, chunks=0)
  3/22  [PASS]  DHT status
  4/22  [PASS]  Peer list
--- Config ---
  5/22  [PASS]  Config list  (14 keys)
  6/22  [PASS]  Config set  (max_storage_gb=5)
  7/22  [PASS]  Config verify roundtrip
--- File Storage ---
  8/22  [PASS]  Store file  (1048576 bytes)
  9/22  [PASS]  List files  (1 file(s))
 10/22  [PASS]  Export key  (32-byte key)
 11/22  [PASS]  Import key
 12/22  [PASS]  File status events
 13/22  [PASS]  Retrieve file
 14/22  [PASS]  Verify store integrity  (MD5 match)
--- Replication ---
 15/22  [SKIP]  Connect daemon 2  (No --socket2 provided)
 16/22  [SKIP]  Connect daemon 3  (No --socket3 provided)
 17/22  [SKIP]  Replication wait  (No multi-node daemons connected)
 18/22  [SKIP]  Verify replication  (No multi-node daemons connected)
--- P2P Transfer ---
 19/22  [SKIP]  Transfer send  (No daemon 2 -- need multi-node for transfer)
 20/22  [SKIP]  Transfer receive  (No ticket or no daemon 2)
 21/22  [SKIP]  Transfer verify  (No transfer completed or no output dir)
--- Cleanup ---
 22/22  [PASS]  Delete & verify

=== RESULTS: 15 passed, 0 failed, 7 skipped (of 22) ===

=== ALL TESTS PASSED ===
```

#### Docker Mode (22 tests, ~3 minutes)

Runs 3 daemon containers + a local relay + a test-runner container. Each daemon has its own IP so replication and transfer work correctly.

```bash
cd docker
./test_gui_ipc.sh
```

**Expected output (Docker):**

```
============================================
  GUI IPC Test (Docker, 22 tests)
============================================

Building Docker images...
Starting containers...
Writing daemon configs...
Starting daemons...
  Waiting for node1... ready (3s)
  Waiting for node2... ready (4s)
  Waiting for node3... ready (4s)

--- Running test binary ---

=== Headless GUI IPC Test Suite ===
  ...
 15/22  [PASS]  Connect daemon 2
 16/22  [PASS]  Connect daemon 3
 17/22  [PASS]  Replication wait  (chunks appeared after 47s)
 18/22  [PASS]  Verify replication  (node2=4, node3=4 chunks)
 19/22  [PASS]  Transfer send  (ticket: 3-guitar-battery)
 20/22  [PASS]  Transfer receive  (1048576 bytes in 2.1s)
 21/22  [PASS]  Transfer verify  (MD5 match)
 22/22  [PASS]  Delete & verify

=== RESULTS: 22 passed, 0 failed, 0 skipped (of 22) ===

=== ALL 22 TESTS PASSED ===
```

**If tests fail:**
- **Localhost**: Check that Qt 6 is installed (`qt6-base-dev` on Linux, or `-DCMAKE_PREFIX_PATH` on Windows), daemon builds successfully, and no port conflicts on 4567/14568
- **Docker**: Check `docker compose` is available, images build successfully, and no port conflicts. The script dumps daemon logs on failure.
- On Windows, verify `windeployqt` ran for `test_gui_ipc` (Qt6Test.dll must be in the build directory)

---

## 4. Manual Testing — Direct File Transfer

These tests verify the peer-to-peer file transfer system (send/receive via ticket codes). No daemon needed.

### 4.1 Setup & Test Files

You need two terminals (same machine for LAN, or two machines for WAN). Commands below use Windows syntax — on Linux, drop `.exe` extensions and use Linux file tools.

Create test files:

```bat
REM Small file (< 1 chunk)
fsutil file createnew test_1kb.bin 1024

REM Medium file (multi-chunk, ~40 chunks)
powershell -Command "$b = New-Object byte[] 10485760; (New-Object Random).NextBytes($b); [IO.File]::WriteAllBytes('test_10mb.bin', $b)"

REM Large file (~400 chunks)
powershell -Command "$b = New-Object byte[] 104857600; (New-Object Random).NextBytes($b); [IO.File]::WriteAllBytes('test_100mb.bin', $b)"

REM Directory with nested subdirs
mkdir test_dir\sub1\nested
mkdir test_dir\sub2
echo hello > test_dir\root.txt
echo world > test_dir\sub1\a.txt
echo nested > test_dir\sub1\nested\deep.txt
powershell -Command "$b = New-Object byte[] 524288; (New-Object Random).NextBytes($b); [IO.File]::WriteAllBytes('test_dir\sub2\binary.bin', $b)"
```

**Linux equivalent:**

```bash
dd if=/dev/urandom of=test_1kb.bin bs=1024 count=1
dd if=/dev/urandom of=test_10mb.bin bs=1M count=10
dd if=/dev/urandom of=test_100mb.bin bs=1M count=100

mkdir -p test_dir/sub1/nested test_dir/sub2
echo hello > test_dir/root.txt
echo world > test_dir/sub1/a.txt
echo nested > test_dir/sub1/nested/deep.txt
dd if=/dev/urandom of=test_dir/sub2/binary.bin bs=1K count=512
```

Record reference hashes for all files:

```bash
# Linux
md5sum test_1kb.bin test_10mb.bin test_100mb.bin
md5sum test_dir/sub1/a.txt test_dir/sub1/nested/deep.txt test_dir/sub2/binary.bin
```

```bat
REM Windows
certutil -hashfile test_1kb.bin MD5
certutil -hashfile test_10mb.bin MD5
certutil -hashfile test_100mb.bin MD5
```

### 4.2 Relay Server

Start the relay (Linux/WSL):

```bash
cd relay-server
./build/relay-server -p 443 -w ../deps/eff_large_wordlist.txt --public-addr <RELAY_PUBLIC_IP>
```

**Verify:**
- [ ] Server starts, prints "Listening..."
- [ ] Wordlist loads (7776 words)

Quick connectivity test from a client machine:

```
wormhole send test_1kb.bin
```

- [ ] Ticket is printed (format: `N-word-word`)
- [ ] Relay log shows REGISTER + CREATE_TICKET
- [ ] Press Ctrl+C to cancel

### 4.3 Small File Transfer (LAN)

**Terminal 1 (sender):**

```
wormhole send test_1kb.bin
```

Note the ticket code.

**Terminal 2 (receiver):**

```
wormhole receive <ticket>
```

**Verify:**
- [ ] Receiver connects to sender
- [ ] File appears in `~/Downloads/test_1kb.bin`
- [ ] MD5 matches original
- [ ] Completes in under 2 seconds
- [ ] Both processes exit cleanly

### 4.4 Large File Transfer (Multi-Chunk)

**Sender:** `wormhole send test_10mb.bin`
**Receiver:** `wormhole receive <ticket>`

**Verify:**
- [ ] Progress bar displays (percentage, speed, ETA)
- [ ] Speed is reasonable (not 0, not absurd)
- [ ] ETA counts down
- [ ] MD5 matches
- [ ] No errors

Repeat with `test_100mb.bin` for stress testing:
- [ ] No stalls or hangs
- [ ] Memory stays reasonable (< 200MB)

### 4.5 Directory Transfer

**Sender:** `wormhole send test_dir`
**Receiver:** `wormhole receive <ticket>`

**Verify:**
- [ ] Directory structure preserved in `~/Downloads/test_dir/`
- [ ] All files present: `root.txt`, `sub1/a.txt`, `sub1/nested/deep.txt`, `sub2/binary.bin`
- [ ] MD5 of each file matches original
- [ ] Path separators correct on each platform

### 4.6 Resumable Transfer

**Sender:** `wormhole send test_100mb.bin`
**Receiver:** `wormhole receive <ticket>` — wait for ~30-50%, then Ctrl+C

**Verify checkpoint:**

```bash
ls ~/.wormhole/transfers/*.state        # Linux
dir %USERPROFILE%\.wormhole\transfers\  # Windows
```

- [ ] `.state` file exists

**Resume:** Sender sends the same file again (new ticket). Receiver runs `wormhole receive <new_ticket>`.

- [ ] Progress bar does NOT start at 0% — resumes from checkpoint
- [ ] Transfer completes, MD5 matches
- [ ] `.state` file cleaned up after completion

### 4.7 Relay Fallback

Block direct UDP between sender and receiver so only relay-forwarded QUIC works.

```bat
REM Windows — block direct UDP from sender
netsh advfirewall firewall add rule name="Block Wormhole Direct" dir=in action=block protocol=UDP remoteip=<SENDER_IP> localport=4567
```

**Sender:** `wormhole send test_10mb.bin`
**Receiver:** `wormhole receive <ticket>`

**Verify:**
- [ ] Direct hole-punch probes time out
- [ ] Falls back to relay-forwarded path
- [ ] File transfers completely, MD5 matches
- [ ] Slower than direct (expected)

**Cleanup:** Remove the firewall rule.

---

## 5. Manual Testing — Daemon & Storage

These tests verify the persistent daemon (`wormholed`) and local storage operations. Start with a single daemon — no relay or peers needed yet.

### 5.1 Daemon Basics

**Start the daemon (foreground, no relay):**

```
wormholed --port 4567 --no-relay
```

**In another terminal:**

```bash
# Check status
wormhole status
# Expected: chunk count, peer count, uptime

# List config
wormhole config list
# Expected: all 14 keys with current values

# Get/set a config value
wormhole config get max_storage_gb
wormhole config set max_storage_gb 20
wormhole config get max_storage_gb
# Expected: "20"

# Reset
wormhole config set max_storage_gb 10
```

**Verify:**
- [ ] Daemon starts, logs "IPC server started"
- [ ] `status` returns info
- [ ] `config list` shows 14 keys
- [ ] `config get/set` roundtrips correctly
- [ ] Config file at `~/.wormhole/config` updated on disk

### 5.2 Store & Get

With the daemon running:

```bash
wormhole store test_10mb.bin
# Note: prints chunk hashes as they're stored
# Record one hash for the next step

wormhole status
# Chunk count should be > 0

wormhole get <chunk_hash> -o retrieved_chunk.bin
# Retrieves a single chunk
```

**Verify:**
- [ ] `store` prints chunk hashes
- [ ] `status` shows correct chunk count (~40 for 10MB)
- [ ] `get` retrieves correct chunk data
- [ ] Chunk files exist on disk in `~/.wormhole/store/<prefix>/<hash>`
- [ ] EC metadata saved: `~/.wormhole/ec/*.ec`

### 5.3 Client-Side Encryption

Files are now encrypted before chunking. This is automatic and transparent — verify it works by inspecting the underlying data.

```bash
# Store a file with known content
echo "Hello Wormhole Encryption Test" > plaintext.txt
wormhole store plaintext.txt

# List stored files
wormhole files
# Note the file ID

# Look at the raw chunks on disk
ls ~/.wormhole/store/
# Pick any chunk file and inspect it:
xxd ~/.wormhole/store/<prefix>/<hash> | head -5
# The chunk content should be ciphertext (not readable plaintext)
```

**Verify:**
- [ ] `store` succeeds (prints hashes)
- [ ] Raw chunk data on disk is NOT readable plaintext (it's encrypted ciphertext)
- [ ] File registry entry exists in `~/.wormhole/files/` (v2 format with encryption key)

**Verify decryption on retrieval** (requires two daemons — or test via E2E flow):

If you stored a full file and later retrieve it:
- [ ] Retrieved file content matches original plaintext exactly
- [ ] Decrypt is transparent — no extra commands needed

### 5.4 File Listing

```bash
# After storing some files:
wormhole files
```

**Verify:**
- [ ] Lists all stored files with name, size, chunk count, status
- [ ] Status shows progression: `storing` -> `replicating` -> `safe` (with peers) or stays at `storing` (single node)

### 5.5 Erasure Coding & Recovery

**Shorten health check interval for faster testing:**

```bash
wormhole config set health_check_interval_sec 15
# Restart daemon to pick up new interval
```

**Store a file and verify EC metadata:**

```bash
wormhole store test_10mb.bin

# Check EC metadata files
ls ~/.wormhole/ec/*.ec               # Linux
dir %USERPROFILE%\.wormhole\ec\*.ec  # Windows
```

**Simulate chunk loss:**

```bash
# Find a chunk in the store
ls ~/.wormhole/store/

# Delete one chunk file
rm ~/.wormhole/store/<prefix>/<chunk_hash>
```

**Wait ~15 seconds for health check, then verify recovery:**

- [ ] Daemon log shows "EC recovered chunk ..."
- [ ] Deleted chunk reappears in the store directory
- [ ] If a parity chunk was deleted, daemon log shows "EC regenerated parity for stripe ..."
- [ ] Both data and parity chunks are covered by the health check EC recovery pass
- [ ] `wormhole get <deleted_hash> -o recovered.bin` succeeds

**RS(8,4) specifics:**
- A full stripe has 8 data chunks + 4 parity chunks = 12 total
- Can tolerate loss of up to 4 chunks per stripe
- Files < 2MB have partial stripes (padded with zeros)

---

## 6. Manual Testing — Distributed Network

These tests require two daemon instances (either two machines, or two instances on different ports on localhost).

### 6.1 Two-Node Setup

**Option A: Two machines** (Machine A and Machine B, relay server running)

```bash
# Machine A
wormholed --port 4567

# Machine B
wormholed --port 4567
```

**Option B: Localhost** (two terminals, no relay needed for DHT — use --no-relay with manual bootstrap)

```bash
# Terminal 1 — Daemon A
wormholed --port 4567 --no-relay --data-dir /tmp/wh_node_a

# Terminal 2 — Daemon B
wormholed --port 4568 --no-relay --data-dir /tmp/wh_node_b
```

For localhost testing, you'll need to manually point one daemon at the other for DHT bootstrap (see [Multi-Bootstrap](#73-multi-bootstrap)).

### 6.2 Peer Discovery (Relay + DHT)

**With two daemons running against the relay:**

Wait ~10 seconds for DHT bootstrap, then check:

```bash
# On Machine A
wormhole status
# Should show peer_count >= 1
```

**Verify:**
- [ ] Both daemons register with relay (relay log shows two REGISTER messages)
- [ ] Each daemon discovers the other via FIND_PEERS
- [ ] DHT routing tables populate (each knows about the other)
- [ ] Daemon logs show "DHT bootstrap" messages

### 6.3 Chunk Replication (R=4)

```bash
# On Machine A — store a file
wormhole store test_1kb.bin
# Daemon A replicates chunks to discovered peers (target: 4 total copies)
```

**Verify:**
- [ ] Machine A's daemon log shows outbound CHUNK_STORE_REQUEST to peers
- [ ] Machine B's daemon log shows incoming chunk storage
- [ ] Machine B's store directory contains replicated chunks
- [ ] `wormhole status` on Machine B shows chunk count > 0
- [ ] With R=4 and 2 nodes, Machine B should have a copy of each chunk

### 6.4 Cross-Node Retrieval

```bash
# Store on Machine A
wormhole store test_1kb.bin
# Note one of the chunk hashes

# Wait for replication (~30 seconds)

# Retrieve on Machine B
wormhole get <chunk_hash> -o /tmp/cross_node.bin
```

**Verify:**
- [ ] Machine B can retrieve chunks that were originally stored on Machine A
- [ ] Retrieved data matches original chunk

### 6.5 Proof-of-Storage Challenges

Proof challenges happen automatically during health checks. Shorten the interval:

```bash
wormhole config set health_check_interval_sec 15
# Restart daemon
```

**Watch daemon logs for:**

```
[daemon] Sending proof challenge to <peer_id> for chunk <hash>
[daemon] Proof response from <peer_id>: VALID
```

**Verify:**
- [ ] Proof challenges sent to peers holding replicated chunks
- [ ] Peers respond with valid proofs
- [ ] 20 chunks sampled per cycle, up to 3 peers challenged per chunk
- [ ] Proof cache populated in `~/.wormhole/proofs/`

### 6.6 Storage Ledger & Incentives

**Test balanced storage:**

```bash
# Machine A stores a file (replicates to B)
# on Machine A:
wormhole store test_1kb.bin

# Machine B stores a file (replicates to A)
# on Machine B:
wormhole store test_1kb.bin

# Both should accept each other's storage (balanced ratio)
```

**Test ratio enforcement:**

```bash
wormhole config set min_storage_ratio 50
# If Machine A has stored much more on B than vice versa,
# B should reject A's CHUNK_STORE_REQUEST
# Watch daemon B log for "Rejecting storage" messages
```

**Verify ledger persistence:**

```bash
# Check ledger file
ls ~/.wormhole/storage_ledger.bin

# Stop daemon (Ctrl+C), restart
# Watch logs for "Loaded ledger" with correct peer count
```

**Verify:**
- [ ] Balanced peers accept each other's storage
- [ ] Unbalanced peers rejected when ratio < threshold
- [ ] Ledger file exists on disk
- [ ] Ledger survives daemon restart

---

## 7. Manual Testing — Phase 6 Features

These tests target the Phase 6 production readiness features specifically.

### 7.1 Auto-Eviction Config

By default, auto-eviction is **disabled** — the originating node keeps local copies of chunks even after they're fully replicated.

```bash
# Check default
wormhole config get auto_evict_enabled
# Expected: 0

# Store a file, wait for replication to complete
wormhole store test_1kb.bin
# After replication completes, check:
wormhole files
# Status should be "safe" (not "offloaded") — local chunks preserved
ls ~/.wormhole/store/
# Chunks still present locally
```

**Test with auto-eviction enabled:**

```bash
wormhole config set auto_evict_enabled 1
# Restart daemon, store another file, wait for replication
# After replication completes:
wormhole files
# Status should be "offloaded" — local chunks removed
```

**Verify:**
- [ ] Default is 0 (disabled)
- [ ] With auto_evict_enabled=0: files reach "safe" status, local chunks preserved
- [ ] With auto_evict_enabled=1: files reach "offloaded" status, local chunks removed
- [ ] Reset to 0 after testing: `wormhole config set auto_evict_enabled 0`

### 7.2 DHT Store Persistence

The DHT value store (which tracks "who has what chunk") now persists to disk and is saved periodically.

```bash
# Start daemon, store a file, wait for DHT STORE announcements
wormhole store test_10mb.bin
# Wait 5+ minutes (or restart daemon to trigger save)

# Check for DHT store file
ls ~/.wormhole/dht_store.bin

# Stop daemon (Ctrl+C)
# Restart daemon
# Watch logs for DHT store load message
```

**Verify:**
- [ ] `~/.wormhole/dht_store.bin` exists after first periodic save (every 5 min)
- [ ] DHT store also saved on graceful shutdown
- [ ] After restart, daemon loads existing DHT store (chunk locations survive restart)
- [ ] DHT routing table also persists (`~/.wormhole/dht_routing_table.bin`)

### 7.3 Multi-Bootstrap

The DHT now supports up to 4 bootstrap nodes instead of just the relay.

```bash
# Set multiple bootstrap nodes (comma-separated host:port)
wormhole config set dht_bootstrap_nodes "node1.example.com:4568,node2.example.com:4568"

# Restart daemon — it will try each bootstrap node in sequence
# Watch logs for bootstrap attempts to each node
```

**Verify with exponential backoff:**

```bash
# Point to an unreachable bootstrap (to test retry behavior)
wormhole config set dht_bootstrap_nodes "192.0.2.1:4568"
# Restart daemon
# Watch logs for retry attempts with increasing intervals:
#   30s -> 60s -> 120s -> 300s (cap)
# Daemon should NOT give up — retries indefinitely
```

**Verify:**
- [ ] Multiple bootstrap nodes parsed from comma-separated config
- [ ] Daemon tries each node in sequence
- [ ] If all fail, exponential backoff: 30s, 60s, 120s, 300s cap
- [ ] Never gives up (no max retry limit)
- [ ] Backoff resets on successful bootstrap
- [ ] Reset config: `wormhole config set dht_bootstrap_nodes ""`

### 7.4 TLS Peer Identity Verification

The daemon's TLS certificate now embeds its Ed25519 node ID in the CN field (format: `WH<64 hex chars>`). Outbound replication connections verify the peer's certificate.

**Verify certificate CN:**

```bash
# Linux — inspect the generated certificate
openssl x509 -in ~/.wormhole/cert.pem -noout -subject
# Expected: subject=CN = WH<64 hex chars matching your node's public key>

# Compare with your node's identity
wormhole status
# The hex in the CN should match the node's peer ID
```

**Verify peer verification on replication:**

With two daemons running and replicating chunks:

```bash
# Watch the replicating daemon's logs for:
# "[replicate] Peer TLS identity verified"
# This confirms the peer's cert CN matches the expected node ID
```

**Verify rejection of mismatched certs** (advanced):

If a peer presents a cert with a different/missing node ID in CN:

```
[replicate] Peer TLS certificate does not match expected node ID — rejecting
```

The connection is closed with `QUIC_STATUS_BAD_CERTIFICATE`.

**Verify:**
- [ ] `~/.wormhole/cert.pem` has CN starting with "WH" followed by your node's hex pubkey
- [ ] Certificate regenerated on each daemon start (to match current identity)
- [ ] Replication connections log "Peer TLS identity verified"
- [ ] Mismatched certs would be rejected (hard to test without modifying a node)

### 7.5 Phase 7 Features

#### File Deletion

```bash
# Store a file, then delete it
wormhole store test_1kb.bin
wormhole files
# Note the file ID

wormhole delete <file-id>
wormhole files
# File should be gone
```

**Verify:**
- [ ] `delete` succeeds, prints confirmation
- [ ] File no longer appears in `wormhole files`
- [ ] Chunks removed from `~/.wormhole/store/`
- [ ] EC metadata removed from `~/.wormhole/ec/`
- [ ] File registry entry removed from `~/.wormhole/files/`
- [ ] Prefix matching works (e.g., `wormhole delete a3f8` matches `a3f8c2...`)

#### Key Export/Import

```bash
# Store a file
wormhole store test_1kb.bin
wormhole files
# Note the file ID

# Export the encryption key
wormhole export-key <file-id>
# Prints hex key — save this

# Import a key (e.g., on another device)
wormhole import-key <file-id> <hex-key>
```

**Verify:**
- [ ] `export-key` prints a hex string (the XChaCha20-Poly1305 key)
- [ ] `import-key` succeeds with a valid hex key
- [ ] Prefix matching works for file IDs
- [ ] Error on invalid/missing file ID

#### Daemon Lifecycle

```bash
# Start the daemon in the background
wormhole daemon start
# Should print PID and confirm startup

wormhole daemon status
# Should show daemon stats (alias for 'wormhole status')

wormhole daemon restart
# Should stop then start

wormhole daemon stop
# Should stop the running daemon
```

**Verify:**
- [ ] `start` spawns a background `wormholed` process
- [ ] `status` shows daemon info
- [ ] `restart` stops and restarts cleanly
- [ ] `stop` terminates the daemon
- [ ] PID file created/cleaned up properly
- [ ] Double `start` detected (daemon already running)

#### Peer Visibility

```bash
# With daemon running and connected to peers:
wormhole peers
# Lists known DHT peers with node ID, address, port, last seen

wormhole files -v
# Verbose file listing with chunk details and replica info
```

**Verify:**
- [ ] `peers` lists known DHT nodes
- [ ] Each peer shows node ID, address, port, last-seen time
- [ ] `files -v` shows more detail than plain `files`
- [ ] Works with zero peers (empty list, no crash)

---

## 8. Manual Testing — Config & Robustness

### 8.1 All 14 Config Keys

Test that every config key can be set, retrieved, and persists:

```bash
wormhole config set relay_host wormholerelay.com
wormhole config get relay_host

wormhole config set relay_port 443
wormhole config get relay_port

wormhole config set max_storage_gb 5
wormhole config get max_storage_gb

wormhole config set replication_target 4
wormhole config get replication_target

wormhole config set dht_enabled 1
wormhole config get dht_enabled

wormhole config set dht_port 4568
wormhole config get dht_port

wormhole config set dht_bootstrap_nodes ""
wormhole config get dht_bootstrap_nodes

wormhole config set ec_enabled 1
wormhole config get ec_enabled

wormhole config set ec_data_shards 8
wormhole config get ec_data_shards

wormhole config set ec_parity_shards 4
wormhole config get ec_parity_shards

wormhole config set health_check_interval_sec 1800
wormhole config get health_check_interval_sec

wormhole config set min_storage_ratio 50
wormhole config get min_storage_ratio

wormhole config set proof_cache_count 8
wormhole config get proof_cache_count

wormhole config set auto_evict_enabled 0
wormhole config get auto_evict_enabled
```

**Verify:**
- [ ] Each key roundtrips correctly
- [ ] Values persist in `~/.wormhole/config`
- [ ] Config changes take effect after daemon restart

### 8.2 Graceful Shutdown

**Daemon shutdown:**

```bash
# Start daemon with stored data
wormholed --port 4567 --no-relay
wormhole store test_10mb.bin

# Press Ctrl+C on the daemon
```

**Verify:**
- [ ] Daemon shuts down within 5 seconds
- [ ] Log shows "Shutting down..." and cleanup messages
- [ ] Storage ledger saved
- [ ] DHT routing table saved
- [ ] DHT value store saved
- [ ] No orphan processes remain
- [ ] IPC socket/pipe released (next daemon start works)

**Client shutdown (sender waiting):**

```bash
wormhole send test_10mb.bin
# Press Ctrl+C while waiting for receiver
```

- [ ] Client exits cleanly
- [ ] Ticket cleaned up on relay after timeout (~60s)

**Client shutdown (mid-transfer):**

```bash
# Start large transfer, Ctrl+C the receiver at ~30%
```

- [ ] Receiver exits, partial file stays for resume
- [ ] `.state` file written
- [ ] Sender detects disconnection and exits cleanly

### 8.3 Error Cases

**Invalid ticket:**

```bash
wormhole receive 99-nonexistent-ticket
```

- [ ] Clear error message, non-zero exit code

**Unreachable relay:**

```bash
wormhole config set relay_host 192.0.2.1
wormhole config set relay_port 9999
wormhole send test_1kb.bin
# Should timeout, not hang forever
```

- [ ] Times out within reasonable period
- [ ] Clear error message
- [ ] Reset config afterward

**Daemon not running:**

```bash
wormhole store test_1kb.bin
wormhole status
wormhole get <some_hash> -o out.bin
```

- [ ] Each shows "Cannot connect to daemon" or similar
- [ ] No crash or hang

**Corrupt chunk in store:**

```bash
# With daemon running, store a file
wormhole store test_1kb.bin
# Corrupt a chunk on disk
echo CORRUPTED > ~/.wormhole/store/<prefix>/<chunk_file>
# Try to retrieve it
wormhole get <chunk_hash> -o out.bin
```

- [ ] Daemon detects hash mismatch
- [ ] Error message indicates corruption
- [ ] Health check flags it for EC recovery

---

## 9. Manual Testing — GUI

These tests verify the Qt 6 desktop application. The daemon must be running for all GUI tests.

### 9.1 Prerequisites

Build the GUI (see [Section 2.1](#21-build-the-gui) above). Start the daemon:

```bash
wormhole daemon start
# or: wormholed --port 4567
```

Launch the GUI:

```bash
gui/build/wormhole-gui           # Linux
gui\build\Release\wormhole-gui   # Windows
```

### 9.2 Daemon Connection

**Verify:**
- [ ] Status bar shows "Connected" when daemon is running
- [ ] Status bar shows "Disconnected" when daemon is not running
- [ ] Auto-reconnect works (stop daemon, restart it — GUI reconnects)
- [ ] "Start Daemon" option available in File menu when disconnected
- [ ] On first disconnect, GUI offers to start the daemon

### 9.3 Transfer Tab

**Send a file:**
- [ ] Click "Send File", select a file — ticket displayed
- [ ] Progress bar updates during transfer
- [ ] Transfer appears in queue with status

**Send a directory:**
- [ ] Click "Send Directory", select a directory — ticket displayed
- [ ] Progress bar updates during transfer

**Receive:**
- [ ] Click "Receive", enter ticket code, choose destination directory
- [ ] Progress bar updates during transfer
- [ ] File saved to chosen destination

**Cancel:**
- [ ] Start a transfer, click Cancel — transfer stops
- [ ] Queue shows cancelled status

**Empty state:**
- [ ] With no transfers, placeholder text is shown

### 9.4 Files Tab

**Store via drag-and-drop:**
- [ ] Drag a file onto the Files tab — store operation starts
- [ ] File appears in list after store completes

**File list:**
- [ ] Files displayed with name, size, chunks, status columns
- [ ] Search/filter narrows the list
- [ ] Column headers clickable for sorting
- [ ] Refresh button updates the list

**Delete:**
- [ ] Right-click a file → Delete — confirmation dialog shows file size and status
- [ ] File removed from list after deletion

**Key export/import:**
- [ ] Right-click a file → Export Key — key displayed
- [ ] Right-click a file → Import Key — key input accepted

**Empty state:**
- [ ] With no stored files, placeholder text is shown

### 9.5 Network Tab

**Peer table:**
- [ ] Peers displayed with node ID, address, port, last seen
- [ ] Full 64-char node ID shown as tooltip on hover
- [ ] IPv6 addresses displayed correctly
- [ ] Refresh button updates the table

**Empty state:**
- [ ] With no peers, placeholder text is shown

### 9.6 Settings Dialog

- [ ] Open via menu → Settings
- [ ] All 14 config keys displayed with appropriate widgets (spinbox, checkbox, line edit)
- [ ] Each setting has a descriptive tooltip
- [ ] Modify a hot-reloadable setting (e.g., `health_check_interval_sec`) — applies without restart prompt
- [ ] Modify a restart-required setting (e.g., `dht_port`) — "Restart Daemon" button appears

### 9.7 System Tray

- [ ] Minimize window — app goes to system tray
- [ ] Click tray icon — window restores
- [ ] Tray context menu has Show/Hide, Start Daemon, Quit
- [ ] Transfer complete triggers tray notification (distinguishes send vs receive)
- [ ] Health alert triggers tray notification with chunk count
- [ ] Quit from tray menu shows confirmation dialog

### 9.8 Edge Cases

- [ ] Launch GUI without daemon running — shows "Disconnected", offers to start daemon
- [ ] Disconnect daemon while GUI running — status updates, timers pause
- [ ] Reconnect daemon — all widgets refresh automatically
- [ ] Launch second GUI instance — blocked by single-instance lock (`~/.wormhole/gui.lock`)
- [ ] Close and reopen GUI — window position and size restored

---

## 10. Results Checklist

Copy this table and fill in after each test run.

### Automated Tests

| Test | Status | Notes |
|------|--------|-------|
| Unit tests (18/18) | | |
| E2E daemon tests | | |
| Docker multi-node tests (22/22) | | |
| GUI IPC tests — localhost (15/15) | | |
| GUI IPC tests — Docker (22/22) | | |

### Direct File Transfer

| Test | Status | Notes |
|------|--------|-------|
| Relay server smoke test | | |
| Small file transfer (LAN) | | |
| Large file transfer (10MB) | | |
| Stress test (100MB) | | |
| Directory transfer | | |
| Resumable transfer | | |
| Relay fallback | | |

### Daemon & Storage

| Test | Status | Notes |
|------|--------|-------|
| Daemon basics (start/status/config) | | |
| Store & get | | |
| Client-side encryption | | |
| File listing | | |
| Erasure coding RS(8,4) & recovery | | |

### Distributed Network

| Test | Status | Notes |
|------|--------|-------|
| Peer discovery (relay + DHT) | | |
| Chunk replication (R=4) | | |
| Cross-node retrieval | | |
| Proof-of-storage challenges | | |
| Storage ledger & incentives | | |

### Phase 6 Features

| Test | Status | Notes |
|------|--------|-------|
| Auto-eviction config | | |
| DHT store persistence | | |
| Multi-bootstrap resilience | | |
| TLS peer identity verification | | |

### Phase 7 Features

| Test | Status | Notes |
|------|--------|-------|
| File deletion (`wormhole delete`) | | |
| Key export (`wormhole export-key`) | | |
| Key import (`wormhole import-key`) | | |
| Daemon start/stop/restart/status | | |
| Peer listing (`wormhole peers`) | | |
| Verbose file listing (`wormhole files -v`) | | |

### Config & Robustness

| Test | Status | Notes |
|------|--------|-------|
| All 14 config keys | | |
| Graceful shutdown (daemon) | | |
| Graceful shutdown (client) | | |
| Error: invalid ticket | | |
| Error: unreachable relay | | |
| Error: daemon not running | | |
| Error: corrupt chunk | | |

### GUI

| Test | Status | Notes |
|------|--------|-------|
| GUI builds (Linux/Windows) | | |
| Daemon connection/disconnect | | |
| Send file via GUI | | |
| Send directory via GUI | | |
| Receive via GUI | | |
| Transfer cancel | | |
| File list + search | | |
| Store via drag-and-drop | | |
| Delete stored file | | |
| Key export/import | | |
| Settings editor | | |
| Peer list display | | |
| System tray minimize/restore | | |
| Tray notifications | | |
