# QUIC Performance Optimization Research

Research into cutting-edge QUIC best practices for maximizing file transfer throughput in the Wormhole project. Covers MsQuic-specific tuning, techniques from other QUIC implementations, academic research, and open-source project analysis.

**Date:** February 2026
**Focus:** Upload/download speed optimization for P2P file transfer over MsQuic

---

## Table of Contents

1. [Current Wormhole Configuration](#1-current-wormhole-configuration)
2. [MsQuic-Specific Tuning](#2-msquic-specific-tuning)
3. [Congestion Control Algorithms](#3-congestion-control-algorithms)
4. [Flow Control & Window Tuning](#4-flow-control--window-tuning)
5. [Zero-Copy & Kernel Bypass](#5-zero-copy--kernel-bypass)
6. [GSO/GRO (Segmentation Offload)](#6-gsogro-segmentation-offload)
7. [Pacing vs Burst Sending](#7-pacing-vs-burst-sending)
8. [MTU Optimization](#8-mtu-optimization)
9. [Multipath QUIC](#9-multipath-quic)
10. [0-RTT Resumption & Connection Migration](#10-0-rtt-resumption--connection-migration)
11. [CPU Affinity & Threading](#11-cpu-affinity--threading)
12. [Other QUIC Projects & Implementations](#12-other-quic-projects--implementations)
13. [Academic Research](#13-academic-research)
14. [Prioritized Recommendations](#14-prioritized-recommendations)
15. [Sources](#15-sources)

---

## 1. Current Wormhole Configuration

### MsQuic Registration
- **App Name:** "wormhole"
- **Execution Profile:** `QUIC_EXECUTION_PROFILE_LOW_LATENCY` (suboptimal for bulk transfer)
- **Files:** `src/wormhole.c:166-170`

### Server/Client QUIC Settings (`src/wormhole.c:260-394`)

| Setting | Value | Notes |
|---------|-------|-------|
| IdleTimeoutMs | 30,000 (30s) | Detect dead peers |
| DisconnectTimeoutMs | 30,000 (30s) | Match idle timeout |
| KeepAliveIntervalMs | 10,000 (10s) | NAT pinhole maintenance |
| StreamRecvWindowDefault | 16,777,216 (16 MB) | Per-stream receive buffer |
| ConnFlowControlWindow | 67,108,864 (64 MB) | Connection-wide receive buffer |
| SendBufferingEnabled | TRUE | MsQuic copies buffers internally |
| InitialWindowPackets | 10 | RFC default congestion window |
| MinimumMtu | 1,200 bytes | QUIC minimum safe baseline |
| MaximumMtu | 1,500 bytes | Standard Ethernet (PMTUD enabled) |
| CongestionControlAlgorithm | BBR | Fast recovery, loss-insensitive |
| EcnEnabled | TRUE | Explicit Congestion Notification |
| PeerBidiStreamCount | 1 (server) / 10 (client) | Bidirectional streams from peer |
| PeerUnidiStreamCount | 10 (client only) | Unidirectional streams from peer |

### Daemon Settings (`src/wormholed.c:294-322`)
- Same as above except **IdleTimeoutMs = 300,000 (5 min)** for persistent connections
- Daemon client (outbound replication): IdleTimeoutMs = 30,000

### Data Transfer Architecture (`src/stream.c`, `src/protocol.h`)

**Two-stream model:**
- Stream 0 (bidirectional): Control messages (manifest negotiation, chunk requests, proof challenges)
- Stream 1 (unidirectional): Data chunks from sender to receiver

**Chunk format:**
- Fixed 256 KB chunks (`CHUNK_SIZE = 256 * 1024`)
- Data frame header: 40 bytes (`[4B chunk_index][32B blake3_hash][4B data_size]`)
- Complete frame: 256,040 bytes per chunk

**Pipelining:**
- `MAX_CHUNKS_IN_FLIGHT = 16` — up to 16 chunks queued simultaneously (~4 MB in flight)
- `SEND_COMPLETE` callback drives pipelining: when a send completes, next chunk is immediately queued
- Each chunk is separately `malloc`'d and `free`'d in the SEND_COMPLETE callback

**Control message header:** 5 bytes = `[1B type][4B payload_length]`

---

## 2. MsQuic-Specific Tuning

### Execution Profile

MsQuic offers multiple execution profiles that change internal threading, batching, and timer behavior:

- `QUIC_EXECUTION_PROFILE_LOW_LATENCY` — Optimizes for interactive workloads (current)
- `QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT` — Optimizes for bulk data transfer
- `QUIC_EXECUTION_PROFILE_TYPE_SCAVENGER` — Background transfers, yield to other traffic
- `QUIC_EXECUTION_PROFILE_TYPE_REAL_TIME` — Lowest possible latency

**Recommendation:** Switch to `MAX_THROUGHPUT` for file transfer operations. This changes MsQuic's internal batching and timer granularity to favor sustained throughput.

### Send Buffering Modes

**Default mode (`SendBufferingEnabled = TRUE`):**
- MsQuic copies data into internal buffers on `StreamSend()`
- `SEND_COMPLETE` fires immediately (data buffered, not yet sent)
- Simpler for application — fire-and-forget
- Extra memory copy for every send

**Zero-copy mode (`SendBufferingEnabled = FALSE`):**
- MsQuic uses application buffers directly — no copy
- `SEND_COMPLETE` fires when MsQuic is done with the buffer (may be delayed)
- **Requirement:** Application must keep 2+ sends pending at all times to prevent idle periods
- Higher throughput due to eliminated memcpy

**For Wormhole:** Zero-copy is safe because we already pipeline 16 chunks and our SEND_COMPLETE callback correctly manages buffer lifetime.

### IDEAL_SEND_BUFFER_SIZE Event

MsQuic provides `QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE` to tell the application exactly how much data should be queued. This is based on the current BDP (Bandwidth-Delay Product) estimate. Using this instead of a hardcoded constant allows adaptive pipelining that matches network conditions.

### Threading Architecture

MsQuic uses a two-tier threading model:
- **Data path threads:** Handle UDP receive and basic QUIC validation (lock-free)
- **Core worker threads:** Process connection queues

**Important limitation:** Multiple streams on the same connection share the same worker thread. Using multiple parallel streams won't improve throughput for bulk data on a single connection.

### Published Performance

MsQuic achieves **6-7 Gbps** in benchmarks with optimized settings, making it the highest-performance QUIC implementation available. Double the goodput of LSQUIC (the second fastest).

**References:**
- [MsQuic Performance Tuning Discussion](https://github.com/microsoft/msquic/discussions/3926)
- [MsQuic Large File Transfer Discussion](https://github.com/microsoft/msquic/discussions/4908)
- [MsQuic Streams Documentation](https://microsoft.github.io/msquic/msquicdocs/docs/Streams.html)
- [MsQuic Settings Documentation](https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_SETTINGS.md)
- [MsQuic Performance Dashboard](https://microsoft.github.io/msquic/)

---

## 3. Congestion Control Algorithms

### CUBIC (Default in most stacks)
- Window-based, loss-driven
- Reduces congestion window by 30% on packet loss (vs Reno's 50%)
- Slower startup phase (1-2 seconds to reach steady-state)
- Better for multi-flow fairness scenarios
- Very sensitive to random packet loss

### BBR (Google, already used by Wormhole)
- Model-based: estimates bandwidth and RTT independently
- **Insensitive to random packet loss** — major advantage in lossy networks
- Faster startup phase — significantly outperforms CUBIC in high-latency networks
- Lower latency for small transfers
- Can be too aggressive in multi-flow scenarios (may starve concurrent flows)

### BBRv3 (Experimental)
- Addresses BBR's fairness issues
- Better interaction with CUBIC flows
- Early-stage testing in Cloudflare quiche shows promise

### HyStart++ (Used by Cloudflare quiche)
- Prevents slow-start overshooting via Limited Slow Start (LSS) phase
- Reduces false congestion detection during ramp-up
- Complementary to CUBIC, not a standalone algorithm

### Performance Comparison

| Scenario | CUBIC | BBR | Notes |
|----------|-------|-----|-------|
| LAN (low latency, no loss) | Good | Good | Both reach similar steady-state |
| WAN (high latency) | Slow ramp-up | Fast ramp-up | BBR 20-50% better during ramp |
| Lossy network (0.5-2% loss) | Severe throughput drop | Minimal impact | BBR dramatically better |
| Multi-flow fairness | Fair | Aggressive | BBR may starve others |

**Wormhole status:** Already using BBR — this is the right choice for P2P file transfer.

**References:**
- [Cloudflare - CUBIC and HyStart++ in quiche](https://blog.cloudflare.com/cubic-and-hystart-support-in-quiche/)
- [BBR Congestion Control in QUIC and HTTP/3](https://blog.litespeedtech.com/2019/10/28/bbr-congestion-control-quic-http-3/)
- [Performance Evaluation of QUIC with BBR in Satellite Internet](https://ieeexplore.ieee.org/document/8637347/)

---

## 4. Flow Control & Window Tuning

### Fundamentals

QUIC has two levels of flow control:
- **Stream-level:** Limits bytes in flight per stream
- **Connection-level:** Limits total bytes in flight across all streams

The receive window must be >= BDP (Bandwidth-Delay Product) to prevent the sender from stalling.

### BDP Calculation

```
BDP = Bandwidth × Round-Trip-Time
```

| Link | RTT | BDP | Minimum Window |
|------|-----|-----|----------------|
| LAN (1 Gbps) | 1 ms | 125 KB | 256 KB |
| LAN (10 Gbps) | 1 ms | 1.25 MB | 2.5 MB |
| WAN (100 Mbps) | 50 ms | 625 KB | 1.25 MB |
| WAN (1 Gbps) | 50 ms | 6.25 MB | 12.5 MB |
| WAN (1 Gbps) | 100 ms | 12.5 MB | 25 MB |

### Auto-Tuning

QUIC implementations auto-tune windows:
- Window update triggers at 50% consumption (giving 4 RTTs total before stall)
- Doubles window when fully consumed within one RTT
- Stops at configured maximum

### Best Practices

- **Initial window:** Set to expected BDP for target network
- **Maximum window:** For gigabit links, allocate 100+ MB connection-level
- **Memory tradeoff:** Oversized windows waste memory but don't harm throughput
- **Stream vs connection:** Connection limit prevents any single stream from monopolizing

### quic-go Reference Configuration
```
InitialStreamReceiveWindow: 1 MB
MaxStreamReceiveWindow:     6 MB
InitialConnectionReceiveWindow: 2 MB
MaxConnectionReceiveWindow: 12 MB
```

### Wormhole Status

Current settings (16 MB stream, 64 MB connection) are well-tuned for typical scenarios. No change recommended unless targeting 10+ Gbps links.

**References:**
- [Flow Control in QUIC - Google Design Doc](https://docs.google.com/document/d/1F2YfdDXKpy20WVKJueEf4abn_LVZHhMUMS5gX6Pgjl4/mobilebasic)
- [quic-go Flow Control Documentation](https://quic-go.net/docs/quic/flowcontrol/)
- [RFC 9000: QUIC Transport](https://www.rfc-editor.org/rfc/rfc9000.html)

---

## 5. Zero-Copy & Kernel Bypass

### io_uring Zero-Copy Receive (Linux)
- Removes kernel-to-user copy on receive path
- Packet data received directly into userspace memory
- Kernel still processes TCP/UDP headers
- Available since Linux 5.1+, ZC Rx more recent

### Approach Comparison

| Approach | Max Speed | Compatibility | Complexity |
|----------|-----------|---------------|------------|
| Standard sockets | Good | Universal | Low |
| io_uring | High | Linux 5.1+ | Medium |
| XDP (eBPF) | Very High | Linux 4.8+ | Medium-High |
| DPDK | Highest | Requires custom driver | Very High |

### QUIC-Specific Work
- TUM researchers demonstrated QUIC acceleration with XDP for early packet processing
- Most QUIC stacks (including MsQuic) are userspace — kernel bypass primarily helps reduce syscalls and copies

### Applicability to Wormhole
- **Windows client:** No io_uring equivalent; MsQuic on Windows is already well-optimized with IOCP
- **Linux relay server:** io_uring could reduce overhead for packet forwarding
- **Impact:** For file transfer, bulk memory copies are typically a small percentage of overhead compared to NIC/CPU limits

**References:**
- [io_uring Zero-Copy Rx - Linux Kernel Docs](https://docs.kernel.org/networking/iou-zcrx.html)
- [Accelerating QUIC with XDP (TUM)](https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2024-04-1/NET-2024-04-1_03.pdf)

---

## 6. GSO/GRO (Segmentation Offload)

### GSO (Generic Segmentation Offload) — Send Path
- Application passes single large buffer (up to 64 KB) to kernel
- Kernel segments into MTU-sized packets before transmission
- Reduces syscalls: 1 syscall for 64 KB instead of ~50 for individual packets
- Available since Linux 4.18 for UDP

### GRO (Generic Receive Offload) — Receive Path
- Reassembles small received packets into larger buffers before delivering to userspace
- Software implementation (no hardware required)
- Opposite of GSO on receive path

### Measured Impact

**quic-go v0.36.0:** Implemented UDP GSO, "drastically increased packet send rate"

**MsQuic on Linux:** Critical finding — MsQuic does NOT activate GRO even when available:
- Without GRO: 5.2 Gbps
- With GRO (hardcoded in testing): **8.3 Gbps** (60% improvement)
- Tracked in [MsQuic Issue #3914](https://github.com/microsoft/msquic/issues/3914)

### Applicability to Wormhole
- **Windows:** Lacks GSO/GRO equivalent for UDP (may come in future Windows Server)
- **Linux relay:** Could benefit significantly from GSO on send path
- **Future Linux client:** When Wormhole gets Linux client support, GRO activation is a major win

**References:**
- [Tailscale - UDP Throughput for QUIC](https://blog.tailscale.com/quic-udp-throughput)
- [quic-go Optimizations](https://quic-go.net/docs/quic/optimizations/)
- [Cloudflare - Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
- [MsQuic GRO Issue #3914](https://github.com/microsoft/msquic/issues/3914)

---

## 7. Pacing vs Burst Sending

### The Tradeoff
- **Pacing:** Small delays between packets = smoother traffic, less congestion/loss
- **Burst/GSO:** Batch packets for efficient transmission = fewer syscalls, higher throughput
- Perfect pacing prevents GSO batching; perfect batching prevents pacing

### Implementation Approaches

| Implementation | Strategy | Details |
|----------------|----------|---------|
| picoquic | Leaky bucket | Credit-based, allows small bursts after inactivity |
| quiche (Cloudflare) | SO_TXTIME | Kernel-level pacing via fair-queue discipline |
| ngtcp2 | Interval-based | Similar to RFC 9002 |
| quicly | GSO bursts | 10-packet bursts (acceptable per RFC) |

### Best Practice for File Transfer
1. Enable GSO/batch transmission for throughput
2. Implement light pacing (per-burst, not per-packet)
3. Target: 10-packet bursts with small delays between bursts
4. Coordinate with congestion control (don't fight the CC algorithm)

### Impact
- Bursty traffic without pacing → higher packet loss in congested networks
- Aggressive pacing without batching → prevents GSO optimization, reduces throughput

**References:**
- [QUIC Steps: Evaluating Pacing Strategies (2025)](https://arxiv.org/html/2505.09222v1)
- [Cloudflare - Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
- [Optimizing QUIC Performance (privateoctopus)](https://www.privateoctopus.com/2023/12/12/quic-performance.html)

---

## 8. MTU Optimization

### Standard Path
- Ethernet default: 1500 bytes
- QUIC packets: ~1200-1300 bytes (room for IP/UDP headers)
- PMTUD discovers maximum safe MTU for the path

### Jumbo Frames (MTU 9000-9216)
- 6x larger than standard Ethernet
- Benefits: fewer frame headers, less CPU overhead, less processing per byte
- **Only works on dedicated LANs** with matching switches/NICs
- Internet/WAN: almost all routers/ISPs cap at 1500

### Wormhole Status
- PMTUD enabled (MinimumMtu=1200, MaximumMtu=1500) — correct for Internet paths
- Jumbo frames could help LAN transfers but require network configuration
- No change recommended for default settings

**References:**
- [ESnet MTU Tuning Guide](https://fasterdata.es.net/network-tuning/mtu-issues/)

---

## 9. Multipath QUIC

### Status (2025-2026)
- IETF draft-ietf-quic-multipath (currently draft-19, not yet finalized)
- Single QUIC connection spans multiple network paths (WiFi + LTE, multiple NICs)
- Separate packet number spaces per path

### Research Results
- **mcMPQUIC:** Achieved 20 Gbps with 10 paths (each on separate CPU core)
- 5x improvement over baseline MPQUIC
- Demonstrates viable multi-core scaling

### Use Cases for Wormhole
1. Mobile devices with WiFi + LTE (redundancy + throughput)
2. Servers with multiple NICs (stripe across paths)
3. Network resilience (path failover without reconnection)

### Current Limitations
- Not standardized (draft stage)
- MsQuic: unclear if multipath is supported
- Limited implementation availability

**Recommendation:** Track for post-v1.0. Focus on single-path optimization first.

**References:**
- [IETF Multipath QUIC Draft](https://datatracker.ietf.org/doc/draft-ietf-quic-multipath/)
- [Multipath QUIC Project](https://multipath-quic.org/)

---

## 10. 0-RTT Resumption & Connection Migration

### 0-RTT Resumption
- Client sends application data in the very first packet (no handshake wait)
- Requires stored session tickets from previous connection
- Reduces reconnection latency by 1-2 RTTs

### Connection Migration
- QUIC uses connection IDs instead of IP:port tuples
- Connection survives IP/port changes without new handshake
- Critical for mobile devices switching networks mid-transfer

### Performance Impact

| Protocol | Reconnect Time |
|----------|----------------|
| TCP + TLS | ~3 RTTs (SYN + TLS handshake) |
| QUIC (fresh) | ~1 RTT |
| QUIC (0-RTT) | 0 RTTs |

### Security Caveat
- 0-RTT does NOT provide forward secrecy
- Don't send sensitive data (auth tokens) in 0-RTT phase
- Replay attacks possible on 0-RTT data

### Applicability to Wormhole
- Relay reconnection: instant resume with cached session
- Daemon-to-daemon replication: faster connection setup
- Combine with existing `transfer_state.c` for seamless resume
- MsQuic supports via `QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED`

**References:**
- [Cloudflare - 0-RTT Resumption](https://blog.cloudflare.com/even-faster-connection-establishment-with-quic-0-rtt-resumption/)
- [QUIC vs TCP Development Guide (Catchpoint)](https://www.catchpoint.com/http2-vs-tcp/quic-vs-tcp)

---

## 11. CPU Affinity & Threading

### Benefits of Core Pinning

| Metric | Without Affinity | With Affinity |
|--------|-----------------|---------------|
| L1-dcache-load-misses | ~7.84% | ~0.6% (13x reduction) |
| Context switches | Frequent | Minimal |
| Cache hit rate | Lower | Higher |

### Best Practices for QUIC/Wormhole
1. Pin QUIC worker thread to dedicated core (reduces context switching)
2. Separate I/O threads: network I/O on core 0, storage I/O on core 1, QUIC on core 2
3. Pin per NUMA node for localized memory access
4. Relay server: pin UDP recv loop to core 0, processing to cores 1-N

### MsQuic Threading
- Already optimized with data-path and core-worker threads
- Can further benefit from core pinning on high-throughput scenarios
- Pin worker thread group to NUMA node for localized memory

### Tools
- Linux: `taskset`, `numactl`, `sched_setaffinity()`
- Windows: `SetThreadAffinityMask()`

**References:**
- [ARM - Thread Affinity Optimization](https://learn.arm.com/learning-paths/servers-and-cloud-computing/pinning-threads/thread_affinity/)

---

## 12. Other QUIC Projects & Implementations

### Cloudflare quiche
- CUBIC + HyStart++ for congestion control
- `set_send_capacity_factor()` for stream data buffering
- BBRv3 in early testing
- Focus on HTTP/3 serving, not bulk file transfer

### quic-go (Most optimized Go implementation)
- UDP GSO enabled by default (Linux 4.18+)
- Auto-tunes RX/TX socket buffers (recommends `net.core.rmem_max = 7340032`)
- DPLPMTUD probes larger packet sizes automatically
- Parallel stream multiplexing for file transfer

### LSQUIC (LiteSpeed, C library)
- `sendmmsg()`/`recvmmsg()` for batch syscalls
- DPLPMTUD for MTU optimization (25% goodput improvement documented)
- Delayed ACKs to reduce overhead
- Achieves ~5 Gbps on Intel CPUs

### QUIC-based File Transfer Tools
- **qft** — UDP-based with reliability measures
- **quic-send** — Rust, UDP hole-punching for P2P
- **qcp** — Experimental high-performance remote file copy
- **Common pattern:** Multiple parallel streams for concurrent chunk delivery

### IPFS/libp2p QUIC Transport
- QUIC enabled by default since go-ipfs 0.6.0
- Focus on security/reliability over raw throughput
- Rate limiting and DoS protection for P2P

### Syncthing
- Added QUIC in v1.2.0 (2019)
- TCP remains preferred by default
- No major QUIC-specific optimizations published

---

## 13. Academic Research

### "QUIC is not Quick Enough over Fast Internet" (ACM WWW 2024)

**Critical finding:** 45.2% throughput reduction on fast Internet (QUIC vs TCP)

**Root causes:**
1. **Excessive packet count:** No UDP GRO = order of magnitude more packets than TCP
2. **Userspace ACK overhead:** No kernel optimization like TCP enjoys
3. **Per-packet CPU cost:** Higher per-packet processing in userspace

**Solutions proposed:**
1. Deploy UDP GRO kernel feature
2. Implement delayed ACKs in QUIC
3. Use batch syscalls (`recvmmsg`)
4. Multi-threaded packet handling

**Impact on Wormhole:** On very fast LANs (10+ Gbps), QUIC will inherently lag behind TCP. On WAN/lossy networks, QUIC excels due to BBR and built-in encryption.

### Estimated Real-World Throughput

| Scenario | TCP | QUIC (Current) | QUIC (Optimized) |
|----------|-----|-----------------|-------------------|
| LAN (1 Gbps, 1ms, 0% loss) | ~900 Mbps | ~300-400 Mbps | ~500-700 Mbps |
| WAN (50 Mbps, 50ms, 0.1% loss) | ~45-48 Mbps | ~35-40 Mbps | ~42-48 Mbps |
| Mobile (10 Mbps, 100ms, 0.5% loss) | ~8-9 Mbps | ~8-9 Mbps | ~9-10 Mbps |

**Key insight:** QUIC excels at recovering from loss (mobile/WAN). QUIC lags on high-speed, low-loss networks (LAN). For Wormhole's P2P use case (primarily WAN transfers through relay or direct over Internet), QUIC is the right choice.

**References:**
- [QUIC is not Quick Enough (arXiv)](https://arxiv.org/html/2310.09423v2)
- [QUIC Network Stack Optimization with io_uring (ETH Zurich thesis)](https://nsg.ethz.ch/files/public/theses/2024-io_uring_quic_network_stack/thesis-2.pdf)

---

## 14. Prioritized Recommendations

### Tier 1: Quick Wins (1-2 days total)

| # | Change | Expected Gain | Effort | Files |
|---|--------|---------------|--------|-------|
| 1 | Execution Profile → MAX_THROUGHPUT | 10-30% | Trivial (1 line) | wormhole.c, wormholed.c |
| 2 | SendBufferingEnabled → FALSE | 10-20% | Low | wormhole.c, wormholed.c |
| 3 | IDEAL_SEND_BUFFER_SIZE event | Adaptive | Medium | stream.c, stream.h |
| 4 | InitialWindowPackets → 20 | Faster start | Trivial (1 line) | wormhole.c, wormholed.c |

### Tier 2: Medium Effort (3-5 days total)

| # | Change | Expected Gain | Effort | Files |
|---|--------|---------------|--------|-------|
| 5 | Pre-allocated ring buffer | CPU reduction | Medium | stream.c, stream.h |
| 6 | 0-RTT connection resumption | 1-2 RTT saved | Medium | wormhole.c, wormholed.c |
| 7 | Configurable chunk size (1 MB) | 5-10% | Medium | protocol.h, stream.c, chunker.c, config.c |

### Tier 3: Advanced / Future

| # | Change | Expected Gain | Effort | Status |
|---|--------|---------------|--------|--------|
| 8 | Multiple parallel streams | Unknown | Medium | Benchmark first (MsQuic limitation) |
| 9 | GSO/GRO on Linux | Up to 60% | High | Future Linux client |
| 10 | Multipath QUIC | Up to 5x | High | Post-v1.0, not standardized |

### What NOT to Change

| Setting | Current | Rationale |
|---------|---------|-----------|
| Stream Recv Window | 16 MB | Already optimal for gigabit links |
| Connection Flow Control | 64 MB | Adequate for 2-stream model |
| BBR Congestion Control | Enabled | Best for P2P file transfer |
| ECN | Enabled | Reduces retransmissions |
| PMTUD | 1200-1500 | Correct for Internet paths |
| KeepAlive | 10s | Correct for NAT maintenance |

---

## 15. Sources

### MsQuic Documentation & Discussions
- [MsQuic Performance Tuning Discussion](https://github.com/microsoft/msquic/discussions/3926)
- [MsQuic Large File Transfer Discussion](https://github.com/microsoft/msquic/discussions/4908)
- [MsQuic Streams Documentation](https://microsoft.github.io/msquic/msquicdocs/docs/Streams.html)
- [MsQuic Settings Documentation](https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_SETTINGS.md)
- [MsQuic Performance Dashboard](https://microsoft.github.io/msquic/)
- [MsQuic GRO Issue #3914](https://github.com/microsoft/msquic/issues/3914)
- [MsQuic + XDP Performance Blog](https://techcommunity.microsoft.com/blog/networkingblog/balance-performance-in-msquic-and-xdp/3627665)

### QUIC Implementations
- [Cloudflare - CUBIC and HyStart++ in quiche](https://blog.cloudflare.com/cubic-and-hystart-support-in-quiche/)
- [quic-go Optimizations](https://quic-go.net/docs/quic/optimizations/)
- [quic-go Flow Control](https://quic-go.net/docs/quic/flowcontrol/)
- [quic-go UDP Buffer Sizing](https://github.com/quic-go/quic-go/wiki/UDP-Buffer-Sizes)
- [LSQUIC Library Documentation](https://lsquic.readthedocs.io/en/latest/internals.html)
- [BBR in QUIC and HTTP/3 (LiteSpeed)](https://blog.litespeedtech.com/2019/10/28/bbr-congestion-control-quic-http-3/)

### Performance Research
- [QUIC is not Quick Enough over Fast Internet (ACM WWW 2024)](https://arxiv.org/html/2310.09423v2)
- [QUIC Network Stack Optimization with io_uring (ETH Zurich)](https://nsg.ethz.ch/files/public/theses/2024-io_uring_quic_network_stack/thesis-2.pdf)
- [QUIC BBR in Satellite Internet (IEEE)](https://ieeexplore.ieee.org/document/8637347/)
- [QUIC Pacing Strategies Evaluation (2025)](https://arxiv.org/html/2505.09222v1)
- [Accelerating QUIC with XDP (TUM)](https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2024-04-1/NET-2024-04-1_03.pdf)

### Network Optimization
- [Cloudflare - Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
- [Tailscale - UDP Throughput for QUIC](https://blog.tailscale.com/quic-udp-throughput)
- [Cloudflare - 0-RTT Resumption](https://blog.cloudflare.com/even-faster-connection-establishment-with-quic-0-rtt-resumption/)
- [QUIC Flow Control Design Doc (Google)](https://docs.google.com/document/d/1F2YfdDXKpy20WVKJueEf4abn_LVZHhMUMS5gX6Pgjl4/mobilebasic)
- [RFC 9000: QUIC Transport](https://www.rfc-editor.org/rfc/rfc9000.html)
- [RFC 9002: QUIC Loss Detection and Congestion Control](https://datatracker.ietf.org/doc/rfc9002/)
- [ESnet MTU Tuning Guide](https://fasterdata.es.net/network-tuning/mtu-issues/)

### Other Projects
- [IPFS 0.6.0 - QUIC Transport](https://blog.ipfs.tech/2020-06-26-go-ipfs-0-6-0/)
- [Syncthing QUIC Support](https://github.com/syncthing/syncthing/issues/5377)
- [qft (GitHub)](https://github.com/TudbuT/qft)
- [quic-send (GitHub)](https://github.com/maxomatic458/quic-send)
- [qcp (GitHub)](https://github.com/crazyscot/qcp)
- [Multipath QUIC IETF Draft](https://datatracker.ietf.org/doc/draft-ietf-quic-multipath/)
