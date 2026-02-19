# Wormhole Testing Guide

Step-by-step guide for building and running all tests.

## Prerequisites

### Windows
- **Visual Studio Developer Command Prompt** (x64 Native Tools) — `cl.exe` must be on PATH
- **MsQuic** built from the `msquic/` submodule (`git submodule update --init --recursive`, then build)
- **libsodium** pre-built binaries in `deps/libsodium/` (Windows x64)
- **Blake3** portable C sources in `deps/blake3/`
- **Reed-Solomon** codec in `deps/reed_solomon/` (rs.h, rs.c)

### Linux
- **GCC** and **make** (`sudo apt install build-essential`)
- **libsodium** (`sudo apt install libsodium-dev`)
- **MsQuic** built from source (see [BUILD_LINUX.md](BUILD_LINUX.md))
- **Docker** (optional, for multi-node integration tests)

## Step 1: Build the Main Project

Open a **x64 Native Tools Command Prompt for VS** and run:

```bat
cd src
build.bat
```

This compiles two executables into `src\build\`:
- `wormhole.exe` — CLI client
- `wormholed.exe` — persistent daemon

It also copies `msquic.dll` and `libsodium.dll` into `src\build\`.

**Verify:** Check that both `.exe` files exist in `src\build\`.

## Step 2: Run All 16 Unit Tests

### Windows

From the same Developer Command Prompt:

```bat
cd src\test
test.bat
```

`test.bat` does the following for each of the 16 tests:
1. Compiles the test source against required `.obj` files from the main build
2. Runs the resulting test executable
3. Tracks pass/fail counts

At the end it prints a summary:

```
=============================================
  TEST SUMMARY: 16/16 passed
=============================================
  ALL TESTS PASSED
```

The script exits with code 0 on success, 1 if any test failed.

### Linux

From a terminal (after running `make` in `src/`):

```bash
cd src/test
./test_linux.sh
```

`test_linux.sh` compiles and runs all 16 test suites, linking each test against its required source files (no MsQuic needed for most tests). Output format matches the Windows test runner.

## Step 3: Interpret Results

- **PASS** — All assertions in that test suite succeeded (greatest.h reports individual test results).
- **COMPILE FAILED** — The test didn't build. Check for missing `.obj` files (did you run `build.bat` first?), missing headers, or dependency issues.
- **Runtime FAIL** — The test built but an assertion failed. The greatest.h output shows which specific test case failed and what was expected vs. actual. Fix the code and re-run.

## Step 4: Run a Single Test Individually

If you need to isolate a failure, compile and run one test manually. Examples:

```bat
REM From src\test\ in a Developer Command Prompt

REM Simple test (no link dependencies):
cl /nologo /Zi /Od /W4 /MD /D_CRT_SECURE_NO_WARNINGS=1 /I "..\..\msquic\src\inc" /I "..\..\deps\blake3" /I .. test_wire_format.c /Fe:test_wire_format.exe
test_wire_format.exe

REM Test with object dependencies (manifest needs blake3):
cl /nologo /Zi /Od /W4 /MD /D_CRT_SECURE_NO_WARNINGS=1 /I "..\..\msquic\src\inc" /I "..\..\deps\blake3" /I .. test_manifest.c ..\build\manifest.obj ..\build\blake3.obj ..\build\blake3_dispatch.obj ..\build\blake3_portable.obj /Fe:test_manifest.exe
test_manifest.exe

REM Test with libsodium (dht_protocol needs peer_id + libsodium):
cl /nologo /Zi /Od /W4 /MD /D_CRT_SECURE_NO_WARNINGS=1 /I "..\..\msquic\src\inc" /I "..\..\deps\blake3" /I .. /I "..\..\deps\libsodium\include" test_dht_protocol.c ..\build\peer_id.obj "..\..\deps\libsodium\x64\Release\v143\dynamic\libsodium.lib" /Fe:test_dht_protocol.exe
test_dht_protocol.exe
```

Refer to `test.bat` for the exact `cl` command and link dependencies for each test.

## Step 5: Build the Relay Server (Linux/WSL)

The relay server builds on Linux. In WSL or a Linux machine:

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install build-essential libsodium-dev

# Build
cd relay-server
./build.sh
```

Output: `relay-server/build/relay-server`

---

## Cross-Network Manual Testing Guide

Steps 6–22 cover comprehensive manual testing of **every user-facing feature** across a real network. Run these after all 16 unit tests and the E2E daemon tests pass. These steps show Windows commands but work equivalently on Linux (replace `wormhole.exe` with `wormhole`, `wormholed.exe` with `wormholed`, and use Linux equivalents for file operations).

### Setup & Prerequisites

#### Network Topology

You need at minimum:

| Machine | Role | OS | Notes |
|---------|------|----|-------|
| **Machine A** | Sender / Daemon A | Windows | Has `wormhole.exe` and `wormholed.exe` in `src\build\` |
| **Machine B** | Receiver / Daemon B | Windows | Same build, different machine (or same machine, different terminal for LAN tests) |
| **Relay** | Relay server | Linux / WSL | Runs `relay-server/build/relay-server` |

For LAN-only tests (Steps 6–9), you can use a single Windows machine with two terminals.
For relay/NAT tests (Steps 10–11), you need the relay server reachable from both machines.
For multi-daemon tests (Steps 13–17), run two daemon instances on different ports.

#### Firewall & Ports

Open these ports on the relevant machines:

| Port | Protocol | Used By | Direction |
|------|----------|---------|-----------|
| 443 | UDP | Relay server | Inbound on relay machine |
| 4567 | UDP | QUIC transfers | Inbound on sender (listener) |
| 4568 | UDP | DHT | Inbound on all daemon machines |

On Windows: `netsh advfirewall firewall add rule name="Wormhole QUIC" dir=in action=allow protocol=UDP localport=4567`

#### Test File Creation

Create these files on Machine A before starting. Use the same files for all tests.

```bat
REM Small file — fits in 1 chunk (< 256KB)
fsutil file createnew test_1kb.bin 1024

REM Medium file — multiple chunks
REM (PowerShell, since fsutil creates zero-filled files)
powershell -Command "$bytes = New-Object byte[] 10485760; (New-Object Random).NextBytes($bytes); [IO.File]::WriteAllBytes('test_10mb.bin', $bytes)"

REM Large file — stress test (100MB = ~400 chunks)
powershell -Command "$bytes = New-Object byte[] 104857600; (New-Object Random).NextBytes($bytes); [IO.File]::WriteAllBytes('test_100mb.bin', $bytes)"

REM Directory with nested subdirectories
mkdir test_dir\sub1\nested
mkdir test_dir\sub2
echo hello > test_dir\root.txt
echo world > test_dir\sub1\a.txt
echo nested > test_dir\sub1\nested\deep.txt
powershell -Command "$bytes = New-Object byte[] 524288; (New-Object Random).NextBytes($bytes); [IO.File]::WriteAllBytes('test_dir\sub2\binary.bin', $bytes)"

REM Compute reference hashes
certutil -hashfile test_1kb.bin MD5
certutil -hashfile test_10mb.bin MD5
certutil -hashfile test_100mb.bin MD5
certutil -hashfile test_dir\sub1\a.txt MD5
certutil -hashfile test_dir\sub1\nested\deep.txt MD5
certutil -hashfile test_dir\sub2\binary.bin MD5
```

**Record all MD5 hashes** — you'll compare these after every transfer test.

---

### Step 6: Relay Server Smoke Test

Verify the relay server starts and accepts connections.

**On Relay Machine (Linux/WSL):**

```bash
cd relay-server
./build.sh
./build/relay-server -p 443 -w ../deps/eff_large_wordlist.txt --public-addr <RELAY_PUBLIC_IP>
```

**Expected output:**

```
[relay] Starting on port 443
[relay] Loaded 7776 words from wordlist
[relay] Public address: <RELAY_PUBLIC_IP>:443
[relay] Listening...
```

**Pass criteria:**
- [ ] Server starts without errors
- [ ] Wordlist loads successfully (7776 words)
- [ ] Server stays running, no crash

**From Machine A, verify connectivity:**

```bat
REM Quick test — register and get a ticket
wormhole.exe send test_1kb.bin
REM Should print a ticket like "3-guitar-battery" and wait for receiver
REM Press Ctrl+C to cancel after seeing the ticket
```

- [ ] Ticket is generated (format: `N-word-word`)
- [ ] Relay log shows REGISTER and CREATE_TICKET messages

---

### Step 7: Single File Transfer — Small (LAN)

Test basic send/receive with a 1KB file on the same network.

**Machine A (sender):**

```bat
wormhole.exe send test_1kb.bin
REM Note the ticket code
```

**Machine B (receiver):**

```bat
wormhole.exe receive <ticket>
```

**Pass criteria:**
- [ ] Receiver connects to sender (check for "Connected" log message)
- [ ] File appears in `%USERPROFILE%\Downloads\test_1kb.bin`
- [ ] MD5 of received file matches original
- [ ] Transfer completes in under 2 seconds
- [ ] Both processes exit cleanly

```bat
REM On Machine B:
certutil -hashfile %USERPROFILE%\Downloads\test_1kb.bin MD5
REM Compare with original hash
```

---

### Step 8: Single File Transfer — Medium (10MB, LAN)

Test multi-chunk transfer with progress display.

**Machine A:**

```bat
wormhole.exe send test_10mb.bin
```

**Machine B:**

```bat
wormhole.exe receive <ticket>
```

**Pass criteria:**
- [ ] Progress bar displays during transfer (percentage, speed, ETA)
- [ ] Speed display is reasonable (not 0 B/s, not absurdly high)
- [ ] ETA counts down
- [ ] MD5 matches after transfer
- [ ] Transfer completes without errors

---

### Step 9: Single File Transfer — Large (100MB+)

Stress test with a large file.

**Machine A:**

```bat
wormhole.exe send test_100mb.bin
```

**Machine B:**

```bat
wormhole.exe receive <ticket>
```

**Pass criteria:**
- [ ] Transfer starts and progress bar updates smoothly
- [ ] No stalls or hangs mid-transfer
- [ ] Memory usage stays reasonable (check Task Manager — should not spike above ~200MB)
- [ ] MD5 matches after transfer
- [ ] Total transfer time is reasonable for network speed

---

### Step 10: Single File Transfer — Relay Fallback

Force transfer through the relay when direct connection fails.

**Setup:** Block direct UDP between Machine A and Machine B using a firewall rule so only relay-forwarded QUIC works.

```bat
REM On Machine B — block direct UDP from Machine A
netsh advfirewall firewall add rule name="Block Wormhole Direct" dir=in action=block protocol=UDP remoteip=<MACHINE_A_IP> localport=4567
```

**Machine A:**

```bat
wormhole.exe send test_10mb.bin
```

**Machine B:**

```bat
wormhole.exe receive <ticket>
```

**Pass criteria:**
- [ ] Direct connection attempts fail (hole-punch probes time out)
- [ ] Transfer falls back to relay-forwarded path
- [ ] File transfers completely through relay
- [ ] MD5 matches
- [ ] Performance is slower than direct (expected — data routes through relay)

**Cleanup:**

```bat
netsh advfirewall firewall delete rule name="Block Wormhole Direct"
```
---

### Step 11: Directory Transfer

Test multi-file directory transfer with nested subdirectories.

**Machine A:**

```bat
wormhole.exe send test_dir
```

**Machine B:**

```bat
wormhole.exe receive <ticket>
```

**Pass criteria:**
- [ ] Manifest v2 is used (multi-file transfer)
- [ ] Directory structure is preserved in `%USERPROFILE%\Downloads\test_dir\`
- [ ] All files present: `root.txt`, `sub1\a.txt`, `sub1\nested\deep.txt`, `sub2\binary.bin`
- [ ] MD5 of each file matches the original
- [ ] Empty directories are handled (if any)
- [ ] Path separators are correct on Windows (backslash in filesystem, forward slash in manifest)

```bat
REM Verify structure
dir /s %USERPROFILE%\Downloads\test_dir\
REM Verify content of each file
certutil -hashfile %USERPROFILE%\Downloads\test_dir\sub1\a.txt MD5
certutil -hashfile %USERPROFILE%\Downloads\test_dir\sub1\nested\deep.txt MD5
certutil -hashfile %USERPROFILE%\Downloads\test_dir\sub2\binary.bin MD5
```

---

### Step 12: Resumable Transfer

Test that an interrupted transfer can be resumed from where it left off.

**Machine A:**

```bat
wormhole.exe send test_100mb.bin
REM Note the ticket
```

**Machine B:**

```bat
wormhole.exe receive <ticket>
REM Wait until progress reaches ~30-50%, then press Ctrl+C
```

**Verify checkpoint state was saved:**

```bat
REM Check for .state file
dir %USERPROFILE%\.wormhole\transfers\*.state
REM Should see a file named <manifest_hash>.state
```

**Resume — Machine A must send again with the same file:**

```bat
REM Machine A:
wormhole.exe send test_100mb.bin
REM Note the NEW ticket

REM Machine B:
wormhole.exe receive <new_ticket>
```

**Pass criteria:**
- [ ] First transfer interrupted cleanly (no crash, no orphan files)
- [ ] `.state` file exists in `~\.wormhole\transfers\`
- [ ] Resumed transfer starts from where it left off (progress bar doesn't start at 0%)
- [ ] Transfer completes successfully
- [ ] MD5 of final file matches original
- [ ] `.state` file is cleaned up after successful completion

---

### Step 13: Daemon Basics — Start, Config, Status

Test the persistent daemon and CLI management commands.

**Start the daemon:**

```bat
REM Start in foreground (to see logs)
src\build\wormholed.exe --port 4567 --no-relay
```

**In another terminal, test CLI commands:**

```bat
REM Check status
wormhole.exe status
REM Expected: shows chunk count, peer count, uptime

REM List all config
wormhole.exe config list
REM Expected: shows all keys with current values

REM Get a specific config value
wormhole.exe config get max_storage_gb
REM Expected: "max_storage_gb = 10" (or current value)

REM Set a config value
wormhole.exe config set max_storage_gb 20
wormhole.exe config get max_storage_gb
REM Expected: "max_storage_gb = 20"

REM Reset it back
wormhole.exe config set max_storage_gb 10
```

**Pass criteria:**
- [ ] Daemon starts and shows "IPC server started" in logs
- [ ] `status` command returns daemon info (chunk count, peer count)
- [ ] `config list` shows all config keys with defaults
- [ ] `config get` returns correct value for a specific key
- [ ] `config set` persists the value (survives daemon restart)
- [ ] Config file at `~\.wormhole\config` is updated on disk

---

### Step 14: Daemon Store & Get

Test storing a file via the daemon and retrieving a chunk by hash.

**With daemon running (from Step 13):**

```bat
REM Store a file
wormhole.exe store test_10mb.bin
REM Expected: prints chunk hashes as they're stored
REM Record one of the printed hashes

REM Verify status shows chunks
wormhole.exe status
REM Chunk count should be > 0

REM Retrieve a chunk by hash
wormhole.exe get <chunk_hash> -o retrieved_chunk.bin
REM Expected: chunk written to retrieved_chunk.bin
```

**Pass criteria:**
- [ ] `store` command prints chunk hashes (one per 256KB chunk)
- [ ] `status` shows correct chunk count (10MB = ~40 chunks)
- [ ] `get` retrieves the correct chunk data
- [ ] Chunk files exist on disk in `~\.wormhole\store\<prefix>\<hash>`
- [ ] Erasure coding metadata saved (if `ec_enabled = 1`): check `~\.wormhole\ec\*.ec`

```bat
REM Verify EC metadata was created
dir %USERPROFILE%\.wormhole\ec\*.ec
```

---

### Step 15: Erasure Coding & Recovery

Test that erasure coding generates parity chunks and can recover missing data.

**Prerequisites:** Daemon running with `ec_enabled = 1` (default). Shorter health check interval for faster testing.

```bat
REM Set short health check interval
wormhole.exe config set health_check_interval_sec 15

REM Restart daemon to pick up the new interval
REM (Ctrl+C the old daemon, restart)
wormholed.exe --port 4567 --no-relay

REM Store a file
wormhole.exe store test_10mb.bin
```

**Verify EC metadata:**

```bat
dir %USERPROFILE%\.wormhole\ec\*.ec
REM Should see .ec files corresponding to the stored chunks
```

**Simulate chunk loss:**

```bat
REM Pick a chunk hash from the store output
REM Delete it from disk
dir %USERPROFILE%\.wormhole\store\
REM Navigate into a prefix directory, delete one chunk file
del %USERPROFILE%\.wormhole\store\<prefix>\<chunk_hash_file>
```

**Wait for health check cycle (~15 seconds), then verify recovery:**

```bat
REM Watch daemon logs for:
REM   "[daemon] EC recovered chunk ..."
REM Or re-check the store directory:
dir %USERPROFILE%\.wormhole\store\<prefix>\
REM The deleted chunk should reappear

REM Verify via get
wormhole.exe get <deleted_chunk_hash> -o recovered.bin
```

**Pass criteria:**
- [ ] `.ec` metadata files are created during store
- [ ] Deleting a chunk triggers recovery on next health check
- [ ] Daemon log shows "EC recovered chunk" message
- [ ] Recovered chunk is identical (retrievable via `get`)
- [ ] Health check reports accurate stats (checked/healthy/degraded counts)

---

### Step 16: Multi-Daemon Peer Discovery (FIND_PEERS)

Test that two daemons can discover each other via the relay's FIND_PEERS protocol.

**Prerequisites:** Relay server running.

**Machine A — Daemon A:**

```bat
wormholed.exe --port 4567
REM Connects to relay, registers peer ID
```

**Machine B — Daemon B:**

```bat
wormholed.exe --port 4567
REM Also connects to relay
```

**Pass criteria:**
- [ ] Both daemons register with the relay (relay log shows two REGISTER messages)
- [ ] Each daemon logs discovered peers from FIND_PEERS responses
- [ ] Daemon A can see Daemon B's peer ID and endpoints
- [ ] Daemon B can see Daemon A's peer ID and endpoints

**Test chunk replication:**

```bat
REM On Machine A:
wormhole.exe store test_1kb.bin
REM Daemon A should attempt to replicate to Daemon B (replication_target=3)

REM Check Daemon B logs for incoming CHUNK_STORE_REQUEST
REM Check Daemon B's store directory for replicated chunks
```

- [ ] Daemon A attempts replication to discovered peers
- [ ] Daemon B receives and stores the replicated chunks
- [ ] `status` on both daemons shows the correct chunk count

---

### Step 17: DHT Discovery & Chunk Announcement

Test Kademlia DHT bootstrap, STORE, and FIND_VALUE operations.

**Prerequisites:** Relay server running (acts as DHT bootstrap node).

**Machine A — Daemon A (DHT enabled):**

```bat
wormhole.exe config set dht_enabled 1
wormhole.exe config set dht_port 4568
REM Restart daemon
wormholed.exe --port 4567
```

**Machine B — Daemon B (DHT enabled):**

```bat
wormhole.exe config set dht_enabled 1
wormhole.exe config set dht_port 4568
REM Restart daemon
wormholed.exe --port 4567
```

**Wait for DHT bootstrap (~10 seconds), then store a file:**

```bat
REM On Machine A:
wormhole.exe store test_1kb.bin
REM Daemon A should announce chunk locations to DHT via STORE messages
```

**Pass criteria:**
- [ ] Both daemons bootstrap from relay (logs: "DHT bootstrap complete" or similar)
- [ ] Routing tables populate (each daemon knows about the other)
- [ ] Chunk STORE messages sent after storing file (daemon log shows DHT STORE)
- [ ] FIND_VALUE from Machine B can locate chunks stored on Machine A
- [ ] DHT routing table persists across daemon restart (`~\.wormhole\dht_routing_table.bin` exists)

```bat
REM Verify DHT state persists
dir %USERPROFILE%\.wormhole\dht_routing_table.bin
dir %USERPROFILE%\.wormhole\dht_store.bin
```

---

### Step 18: Proof-of-Storage Challenges

Observe proof-of-storage challenge/response between peers.

**Prerequisites:** Two daemons running with stored chunks (from Steps 16–17).

This test is observational — proof challenges happen automatically during health checks.

```bat
REM On Machine A, set short health check interval:
wormhole.exe config set health_check_interval_sec 15
REM Restart daemon
```

**Watch daemon logs for proof activity:**

```
[daemon] Sending proof challenge to <peer_id> for chunk <hash>
[daemon] Proof response from <peer_id>: VALID
```

**Pass criteria:**
- [ ] Proof challenges are sent to peers holding replicated chunks
- [ ] Peers respond with valid proofs (Blake3(seed || chunk_data))
- [ ] Invalid proofs are flagged (test by corrupting a chunk on the peer)
- [ ] Proof cache is populated (`~\.wormhole\proofs\` directory has files)

---

### Step 19: Storage Ledger & Incentives

Test that the storage ledger tracks reciprocity and enforces the accept/reject threshold.

**Prerequisites:** Two daemons running.

**Test balanced storage:**

```bat
REM Machine A stores a file (chunks replicate to B)
wormhole.exe store test_1kb.bin

REM Machine B stores a file (chunks replicate to A)
REM (on Machine B)
wormhole.exe store test_1kb.bin

REM Both daemons should accept each other's storage (balanced ratio)
```

**Test unbalanced storage (ratio enforcement):**

```bat
REM Set strict ratio
wormhole.exe config set min_storage_ratio 0.5

REM If Machine A has stored much more on B than B has stored on A,
REM B should start rejecting A's CHUNK_STORE_REQUEST
REM Watch daemon B logs for "Rejecting storage from <peer_id>: ratio too low"
```

**Verify ledger persistence:**

```bat
REM Check ledger file exists
dir %USERPROFILE%\.wormhole\storage_ledger.bin

REM Stop daemon (Ctrl+C)
REM Restart daemon
wormholed.exe --port 4567

REM Watch logs for "Loaded ledger" with correct peer count
```

**Pass criteria:**
- [ ] Balanced peers accept each other's storage requests
- [ ] Unbalanced peers are rejected when ratio < `min_storage_ratio`
- [ ] Ledger file exists on disk (`storage_ledger.bin`)
- [ ] Ledger survives daemon restart (log shows "Loaded ledger")
- [ ] `status` command shows ledger info (if exposed)

---

### Step 20: Config Verification — All Keys

Test every configurable key to verify it's read, applied, and persisted.

```bat
REM Test each config key:
wormhole.exe config set relay_host wormholerelay.com
wormhole.exe config get relay_host

wormhole.exe config set relay_port 443
wormhole.exe config get relay_port

wormhole.exe config set max_storage_gb 5
wormhole.exe config get max_storage_gb

wormhole.exe config set replication_target 2
wormhole.exe config get replication_target

wormhole.exe config set dht_enabled 1
wormhole.exe config get dht_enabled

wormhole.exe config set dht_port 4568
wormhole.exe config get dht_port

wormhole.exe config set ec_enabled 1
wormhole.exe config get ec_enabled

wormhole.exe config set ec_data_shards 4
wormhole.exe config get ec_data_shards

wormhole.exe config set ec_parity_shards 2
wormhole.exe config get ec_parity_shards

wormhole.exe config set health_check_interval_sec 1800
wormhole.exe config get health_check_interval_sec

wormhole.exe config set min_storage_ratio 0.5
wormhole.exe config get min_storage_ratio

wormhole.exe config set proof_cache_count 8
wormhole.exe config get proof_cache_count
```

**Pass criteria:**
- [ ] Each `config set` + `config get` roundtrips correctly
- [ ] Values persist in `~\.wormhole\config` (check file contents)
- [ ] Invalid values are rejected gracefully (e.g., `config set max_storage_gb -1`)
- [ ] Config changes take effect after daemon restart

---

### Step 21: Ctrl+C / Graceful Shutdown

Test that all components shut down cleanly.

**Daemon shutdown:**

```bat
REM Start daemon with some stored data
wormholed.exe --port 4567

REM Store some data
wormhole.exe store test_10mb.bin

REM Press Ctrl+C on the daemon
```

**Pass criteria:**
- [ ] Daemon shuts down within 5 seconds of Ctrl+C
- [ ] Log shows "Shutting down..." and cleanup messages
- [ ] Storage ledger saved (log: "Ledger saved")
- [ ] DHT routing table saved (if DHT was enabled)
- [ ] No orphan processes left (check Task Manager: no `wormholed.exe` remaining)
- [ ] Named pipe `\\.\pipe\wormhole` is released (subsequent daemon start works)

**Client shutdown (sender waiting for receiver):**

```bat
wormhole.exe send test_10mb.bin
REM Press Ctrl+C while waiting
```

- [ ] Client exits cleanly
- [ ] No orphan MsQuic threads or UDP sockets
- [ ] Ticket is not left dangling on relay (relay cleans up stale peers after 60s)

**Client shutdown (mid-transfer):**

```bat
REM Start a large transfer
wormhole.exe send test_100mb.bin
REM In another terminal:
wormhole.exe receive <ticket>
REM Press Ctrl+C on the RECEIVER mid-transfer
```

- [ ] Receiver exits, partial file stays in Downloads (for resume)
- [ ] `.state` file is written for resumable transfer
- [ ] Sender detects disconnection and exits cleanly (doesn't hang)

---

### Step 22: Error Cases

Test graceful handling of common error conditions.

**Invalid ticket:**

```bat
wormhole.exe receive 99-nonexistent-ticket
REM Expected: "Ticket not found" or similar error, clean exit
```

- [ ] Clear error message
- [ ] Exit code non-zero

**Unreachable relay:**

```bat
REM Point to a non-existent relay
wormhole.exe config set relay_host 192.0.2.1
wormhole.exe config set relay_port 9999
wormhole.exe send test_1kb.bin
REM Expected: connection timeout, error message
```

- [ ] Times out within a reasonable period (not hanging forever)
- [ ] Clear error message about relay being unreachable
- [ ] Clean exit

```bat
REM Reset relay config
wormhole.exe config set relay_host wormholerelay.com
wormhole.exe config set relay_port 443
```

**Daemon not running (CLI commands):**

```bat
REM Make sure no daemon is running
wormhole.exe store test_1kb.bin
REM Expected: "Cannot connect to daemon" or similar

wormhole.exe status
wormhole.exe get <some_hash> -o out.bin
```

- [ ] Each command shows clear error about daemon not running
- [ ] No crash or hang

**Corrupt chunk in store:**

```bat
REM Start daemon, store a file
wormholed.exe --port 4567 --no-relay
wormhole.exe store test_1kb.bin

REM Corrupt a chunk on disk (overwrite with garbage)
REM Find a chunk file:
dir %USERPROFILE%\.wormhole\store\
REM Overwrite it:
echo CORRUPTED > %USERPROFILE%\.wormhole\store\<prefix>\<chunk_file>

REM Try to retrieve it
wormhole.exe get <chunk_hash> -o out.bin
REM Expected: hash verification failure, error message
```

- [ ] Daemon detects hash mismatch on read
- [ ] Error message indicates corruption
- [ ] Health check flags corrupted chunk (if health monitoring runs)

---

## Results Tracking

Copy this checklist and fill in results after each test run.

| # | Test | Status | Notes |
|---|------|--------|-------|
| 6 | Relay Server Smoke Test | ⬜ | |
| 7 | Single File Transfer — Small (LAN) | ⬜ | |
| 8 | Single File Transfer — Medium (LAN) | ⬜ | |
| 9 | Single File Transfer — Large (100MB+) | ⬜ | |
| 10 | Single File Transfer — Relay Fallback | ⬜ | |
| 11 | Directory Transfer | ⬜ | |
| 12 | Resumable Transfer | ⬜ | |
| 13 | Daemon Basics — Config, Status | ⬜ | |
| 14 | Daemon Store & Get | ⬜ | |
| 15 | Erasure Coding & Recovery | ⬜ | |
| 16 | Multi-Daemon Peer Discovery | ⬜ | |
| 17 | DHT Discovery & Chunk Announcement | ⬜ | |
| 18 | Proof-of-Storage Challenges | ⬜ | |
| 19 | Storage Ledger & Incentives | ⬜ | |
| 20 | Config Verification — All Keys | ⬜ | |
| 21 | Ctrl+C / Graceful Shutdown | ⬜ | |
| 22 | Error Cases | ⬜ | |

**Status key:** ✅ PASS | ❌ FAIL | ⏭️ SKIP | ⬜ NOT RUN

---

## Unit Test Inventory

| # | Test | What It Covers | Link Dependencies |
|---|------|---------------|-------------------|
| 1 | `test_wire_format` | Little-endian encoding/decoding helpers | None (header-only) |
| 2 | `test_manifest` | Manifest v1 create/serialize/validate, v2 multi-file (chunk ranges, roundtrip, empty file) | `manifest.obj`, Blake3 objs |
| 3 | `test_chunk_store` | Chunk put/get/has/dedup, replica metadata, LRU eviction | `chunk_store.obj`, `file_io.obj`, ole32.lib |
| 4 | `test_transfer_state` | Resumable transfer bitfield save/load, boundary cases (8/9 chunks), large count (1000) | `transfer_state.obj`, `file_io.obj`, ole32.lib |
| 5 | `test_config` | INI config defaults, get/set (string/uint64/case-insensitive/overflow), file roundtrip | `config.obj` |
| 6 | `test_chunker` | File chunking (single/multi-chunk, deterministic hashing), directory chunking | `chunker.obj`, `manifest.obj`, `file_io.obj`, Blake3 objs, ole32.lib |
| 7 | `test_reed_solomon` | GF(2^8) codec: encode/decode, 1-2 missing shards, partial stripes, 256KB shards | `rs.c` (compiled directly) |
| 8 | `test_erasure` | Stripe encoding, parity chunk storage, chunk reconstruction, EC metadata save/load roundtrip | `erasure.obj`, `rs.c`, `chunk_store.obj`, `manifest.obj`, `file_io.obj`, Blake3 objs, ole32.lib |
| 9 | `test_routing_table` | XOR distance, bucket index, add/evict nodes, FindClosest, save/load, stale detection | `routing_table.obj` |
| 10 | `test_dht_protocol` | Wire format struct sizes, Ed25519 sign/verify roundtrip, tamper detection, MTU fit | `peer_id.obj`, libsodium |
| 11 | `test_dht_store` | DHT put/get roundtrip, location merge, expiry, capacity limits, persistence | `dht_store.obj` |
| 12 | `test_dht_lookup` | Shortlist seeding, response convergence, FIND_VALUE early termination, max iteration | `dht_lookup.obj`, `routing_table.obj` |
| 13 | `test_proof` | Proof computation determinism, wrong seed detection, pre-compute cache hit/miss | `proof.obj`, Blake3 objs, libsodium |
| 14 | `test_incentives` | Balanced/unbalanced ledger, ratio enforcement, save/load roundtrip | `incentives.obj` |
| 15 | `test_health` | EC-based chunk recovery, health check stats, degraded chunk detection, replication needs | `health.obj`, `erasure.obj`, `chunk_store.obj`, `manifest.obj`, `file_io.obj`, Blake3 objs, `rs.c`, ole32.lib |
| 16 | `test_file_registry` | File registry save/load/update/list, hex prefix lookup | `file_registry.obj`, `manifest.obj`, `file_io.obj`, Blake3 objs |

All tests use the `greatest.h` single-header test framework. Tests that need filesystem access use `setup_test_home()`/`cleanup_test_home()` to redirect HOME to a temp directory.

## Step 23: End-to-End Daemon Tests

`test_e2e.bat` runs an automated smoke test of the full daemon loop on a single machine with no relay server needed.

### Running the E2E tests

From a Developer Command Prompt (after running `build.bat`):

```bat
cd src\test
test_e2e.bat
```

The script creates an isolated test directory in `%TEMP%`, starts a daemon on port 14567, and exercises the store/get/status flow end-to-end. It exits with code 0 if all steps pass, 1 on any failure.

### What it tests

| Step | Test | What to expect |
|------|------|---------------|
| 1 | **Setup** | Creates temp dir, config with short health interval (15s), 512KB test file |
| 2 | **Start daemon** | `wormholed --port 14567 --no-relay --data-dir <test_dir>` starts in background |
| 3 | **Store** | `wormhole --daemon 14567 store <file>` chunks, hashes, and EC-encodes the file |
| 4 | **Status** | `wormhole --daemon 14567 status` reports chunk count > 0 |
| 5 | **Get** | `wormhole --daemon 14567 get <hash> -o <output>` retrieves a chunk by hash |
| 6 | **EC metadata** | Verifies `.ec` files exist in `<test_dir>\.wormhole\ec\` |
| 7 | **Ledger persistence** | Stops daemon, restarts, checks log for "Loaded ledger" |
| 8 | **EC recovery** | Deletes a chunk file from store, waits for health check, verifies chunk reappears |

### Manual debugging

If a step fails, you can reproduce it manually:

```bat
REM Create test directory
set TEST_DIR=C:\Temp\wh_e2e_debug
mkdir %TEST_DIR%
mkdir %TEST_DIR%\.wormhole

REM Write config
echo health_check_interval_sec = 15 > %TEST_DIR%\.wormhole\config
echo ec_enabled = 1 >> %TEST_DIR%\.wormhole\config
echo dht_enabled = 0 >> %TEST_DIR%\.wormhole\config

REM Create test file (512KB = 2 chunks)
fsutil file createnew %TEST_DIR%\test.bin 524288

REM Start daemon (foreground, so you see logs)
src\build\wormholed.exe --port 14567 --no-relay --data-dir %TEST_DIR%

REM In another terminal:
src\build\wormhole.exe --daemon 14567 store %TEST_DIR%\test.bin
src\build\wormhole.exe --daemon 14567 status
src\build\wormhole.exe --daemon 14567 get <hash_from_store_output> -o %TEST_DIR%\chunk.bin

REM Check EC metadata
dir %TEST_DIR%\.wormhole\ec\*.ec

REM Delete a chunk and wait for recovery
del %TEST_DIR%\.wormhole\store\<prefix>\<hash_file>
REM Watch daemon logs for "EC recovered chunk" after ~15 seconds
```

### Expected daemon log output

```
[daemon] Config loaded: ...
[daemon] Starting with fresh ledger
[daemon] Chunk store initialized (0 chunks)
[daemon] IPC server started
...
[daemon] Stored file: test.bin (2 chunks)
[daemon] EC metadata saved to ...\ec\<hash>.ec
...
[daemon] Health check: 2 checked, 2 healthy, 0 degraded, 0 critical
...
[daemon] EC recovered chunk 0 from <hash>.ec
```

## Step 24: Docker Multi-Node Integration Tests

`docker/test_multi_node.sh` runs automated integration tests across a 5-node Docker cluster (relay + 5 daemon nodes). This validates the full distributed storage pipeline without manual setup.

### Prerequisites

- Docker and Docker Compose installed
- Docker images built (`docker compose build` in `docker/`)

### Running the tests

```bash
cd docker
docker compose up -d                    # Start relay + 5 nodes
./test_multi_node.sh                    # Run automated test suite
docker compose down -v                  # Tear down
```

### What it tests

| # | Test | What it validates |
|---|------|-------------------|
| 1 | Daemon health check | All 5 daemons start and respond to `wormhole status` |
| 2 | Store file on node1 | File chunking, EC encoding, and local storage work |
| 3 | DHT peer discovery | Nodes discover each other via DHT (>= 2 peers) |
| 4 | Chunk replication | Chunks replicate to >= 2 additional nodes within 30s |
| 5 | Cross-node retrieval | Store on node3, retrieve on node5 |
| 6 | Node failure | Stop node2, verify other daemons survive peer failure |

### Windows multi-node test

There is also a Windows-native multi-node test that runs 3 daemon instances on localhost:

```bat
cd src\test
test_multi_node.bat
```

This starts 3 `wormholed.exe` instances on different ports, stores a file on node 1, waits for relay-mediated peer discovery and replication, then verifies chunks are present across nodes. Requires internet connectivity (relay server) and takes ~2 minutes.
