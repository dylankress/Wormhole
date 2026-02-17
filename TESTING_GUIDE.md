# Wormhole Testing Guide

Step-by-step guide for building and running all tests.

## Prerequisites

- **Visual Studio Developer Command Prompt** (x64 Native Tools) — `cl.exe` must be on PATH
- **MsQuic** built from the `msquic/` submodule (`git submodule update --init --recursive`, then build)
- **libsodium** pre-built binaries in `deps/libsodium/` (Windows x64)
- **Blake3** portable C sources in `deps/blake3/`
- **Reed-Solomon** codec in `deps/reed_solomon/` (rs.h, rs.c)

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

## Step 2: Run All 15 Unit Tests

From the same Developer Command Prompt:

```bat
cd src\test
test.bat
```

`test.bat` does the following for each of the 15 tests:
1. Compiles the test source against required `.obj` files from the main build
2. Runs the resulting test executable
3. Tracks pass/fail counts

At the end it prints a summary:

```
=============================================
  TEST SUMMARY: 15/15 passed
=============================================
  ALL TESTS PASSED
```

The script exits with code 0 on success, 1 if any test failed.

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

## Step 6: Manual Integration Test

Quick smoke test for end-to-end file transfer (requires relay server running):

```bash
# Terminal 1 — Create a test file and send it
dd if=/dev/urandom of=test_10mb.bin bs=1M count=10
md5sum test_10mb.bin
wormhole.exe send test_10mb.bin
# Note the ticket code (e.g., "3-guitar-battery")

# Terminal 2 — Receive it
wormhole.exe receive 3-guitar-battery
md5sum ~/Downloads/test_10mb.bin
# MD5 hashes should match
```

## Test Inventory

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

All tests use the `greatest.h` single-header test framework. Tests that need filesystem access use `setup_test_home()`/`cleanup_test_home()` to redirect HOME to a temp directory.

## Step 7: End-to-End Daemon Tests

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
