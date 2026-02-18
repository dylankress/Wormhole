# Sender:

C:\Dev\Wormhole\src\build>wormhole.exe send C:\Users\dylan\Desktop\test_10mb.bin
[22:24:38.909] === Wormhole - Secure P2P File Transfer ===

[22:24:38.909]
ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
[22:24:38.909]   WORMHOLE SEND
[22:24:38.909] ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ

[22:24:38.909] [Send] File: test_10mb.bin
[22:24:38.909] [Send] Size: 10485760 bytes

[22:24:38.909] [Send] Building file manifest...
[22:24:39.068] [Send] Manifest: 40 chunks of 262144 bytes (Blake3 hashed)
[PeerID] Loaded keypair from C:\Users\dylan\AppData\Roaming\.wormhole\identity
[RelayClient] Bound to local port 4567 (SO_REUSEADDR)
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[22:24:39.079] [Send] Discovering network endpoints...
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 3 total endpoints
[22:24:39.088] [Send] Registering with relay server...
[RelayClient] Sent REGISTER (3 endpoints)
[RelayClient] REGISTERED (session 18248779535308350973)
[22:24:39.124]
[Relay] Connected to relay server (session 18248779535308350973)
[22:24:39.124] [Relay] Your public IP (as seen by relay): 107.204.80.156:4567
[22:24:39.124] [Send] [DEBUG] Public endpoint created:
[22:24:39.124] [Send] [DEBUG]   Port (host order): 4567
[22:24:39.124] [Send] [DEBUG]   Port (network order): 0xd711
[22:24:39.124] [Send] [DEBUG]   Expected: 0x11D7 (4567 in network byte order)
[22:24:39.125] [Send] Adding public IP endpoint: 107.204.80.156:4567 (priority 100)
[22:24:39.125] [Send] Re-registering with public IP endpoint...
[22:24:39.125] [Send] [DEBUG] Re-registering with 4 endpoints:
[22:24:39.125] [Send] [DEBUG]   EP[0]: 2600:1700:7d00:1390::17:4567 (priority=75, port_raw=0x11d7)
[22:24:39.125] [Send] [DEBUG]   EP[1]: 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority=75, port_raw=0x11d7)
[22:24:39.125] [Send] [DEBUG]   EP[2]: 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority=75, port_raw=0x11d7)
[22:24:39.125] [Send] [DEBUG]   EP[3]: 107.204.80.156:4567 (priority=100, port_raw=0x11d7)
[RelayClient] Sent REGISTER (4 endpoints)
[22:24:39.125] [Send] Creating transfer ticket...
[RelayClient] Sent CREATE_TICKET (file: test_10mb.bin, size: 10485760 bytes)
[RelayClient] REGISTERED (session 18248779535308350973)
[22:24:39.156]
[Relay] Connected to relay server (session 18248779535308350973)
[22:24:39.156] [Relay] Your public IP (as seen by relay): 107.204.80.156:4567
[RelayClient] TICKET_CREATED: 2-tapioca-shame

ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
Γòæ                    WORMHOLE FILE TRANSFER                 Γòæ
ΓòáΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòú
Γòæ                                                           Γòæ
Γòæ  Your ticket:                                             Γòæ
Γòæ                                                           Γòæ
Γòæ      2-tapioca-shame                                    Γòæ
Γòæ                                                           Γòæ
Γòæ  File: test_10mb.bin                                     Γòæ
Γòæ  Size: 10.0 MB                                           Γòæ
Γòæ                                                           Γòæ
Γòæ  Share this ticket with the receiver. They can download  Γòæ
Γòæ  the file by running:                                    Γòæ
Γòæ                                                           Γòæ
Γòæ      wormhole receive 2-tapioca-shame               Γòæ
Γòæ                                                           Γòæ
Γòæ  Waiting for receiver to connect...                      Γòæ
Γòæ                                                           Γòæ
ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[22:24:39.158]
[Send] Waiting for receiver (timeout: 30 minutes)...

[22:24:39.158] [Send] Phase 1: Waiting for receiver on relay (hole punch probing)...
[RelayClient] PEER_INFO (8 endpoints)
[22:24:55.317]
[Relay] Found sender with 8 endpoints
[22:24:55.317] [Send] Receiver found! Starting hole punch probing to 8 endpoints...
[22:24:55.317] [HolePunch] Probes sending from local port 4567 (socket fd=680)
[22:24:55.318] [HolePunch] Round 1: probe -> EP[3] 172.20.10.8:4567 (priority=0) sent
[22:24:55.318] [HolePunch] Round 1: probe -> EP[6] 64.109.250.105:29477 (priority=100) sent
[22:24:55.976] [Send] Phase 2: Closing relay, starting QUIC listener on port 4567...
[RelayClient] Sent GOODBYE (reason: upgraded to direct)
[22:24:55.977]
[Relay] Disconnected from relay
[RelayClient] Destroyed
[22:24:55.977] [init] Opening MsQuic library...
[22:24:55.978] [init] MsQuic library opened successfully
[22:24:55.983] [init] Registration created successfully
[22:24:55.983] [server] Loading configuration...
[22:24:55.983] Checking for existing Wormhole-Dev certificate...
[22:24:56.282] Found existing certificate: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[22:24:56.282] [server] Using certificate thumbprint: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[22:24:56.282] [server] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on
[22:24:56.289] [server] Configuration loaded successfully
[22:24:56.292] [Send] QUIC server listening on port 4567
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[RelayClient] Sent REGISTER (4 endpoints)
[RelayClient] REGISTERED (session 2241123140350118519)
[22:24:56.323] [RelayFwd] Registered with relay (session 2241123140350118519)
[22:24:56.324] [RelayFwd] Proxy socket bound to 127.0.0.1:61675
[22:24:56.324] [RelayFwd] Started (proxy port 61675, forwarding to peer)
[22:24:56.324] [Send] Relay forwarder started (fallback for restricted NAT)
[22:24:56.324] [RelayFwd] Forwarder thread started (relay_sock=1508, proxy_sock=1512)
[22:25:25.629] [Send] Receiver connecting...
[22:25:25.735] [Send] Receiver connected! Transferring file...
[22:25:25.793] [Send] Receiver connected! Starting chunk-based file transfer...
[22:25:25.793] [ChunkSendFile] Starting chunk-based transfer: C:\Users\dylan\Desktop\test_10mb.bin (40 chunks)
[22:25:25.793] [ChunkSendFile] Control stream opened, waiting for MANIFEST_REQUEST...
[22:25:25.793] [SenderCtrl] START_COMPLETE (status: 0x0)
[22:25:25.927] [SenderCtrl] Received MANIFEST_REQUEST
[22:25:25.927] [SenderCtrl] Sent MANIFEST_RESPONSE (1508 bytes)
[22:25:25.927] [SenderCtrl] Waiting for CHUNK_REQUEST before sending data...
[22:25:26.066] [SenderCtrl] Received CHUNK_REQUEST: 40/40 chunks needed
[22:25:26.067] [SenderCtrl] Data stream opened, starting chunk send...
Sending [>                             ]   2.5% | 1.5 GB/s | ETA: 0s     [22:25:26.068] [SenderData] START_COMPLETE (status: 0x0)
[22:25:26.199] [SenderData] Ideal send buffer: 131072 bytes
[22:25:26.583] [SenderData] Ideal send buffer: 196608 bytes
[22:25:26.624] [SenderData] Ideal send buffer: 294912 bytes
[22:25:26.720] [SenderData] Ideal send buffer: 442368 bytes
[22:25:27.026] [SenderData] Ideal send buffer: 663552 bytes
[22:25:27.100] [SenderData] Ideal send buffer: 995328 bytes
[22:25:28.653] [SenderData] Ideal send buffer: 1492992 bytes
Sending [==============================] 100.0% | 1.4 MB/s | done
[22:25:35.032] [stream] Last chunk 39 queued (FIN set)
[22:25:36.918] [SenderData] SHUTDOWN_COMPLETE
[22:25:36.918] [SenderCtrl] Received TRANSFER_COMPLETE
[22:25:36.918] [SenderCtrl] Transfer complete: 10485760 bytes in 10.85 sec (943.7 KB/s)
[22:25:36.918]
[Send] File transfer complete!
[22:25:36.919] [RelayFwd] Stopping...
[22:25:36.919] [RelayFwd] Forwarder thread exiting
[RelayClient] Sent GOODBYE (reason: transfer complete)
[22:25:36.919] [RelayFwd] Disconnected from relay
[RelayClient] Destroyed
[22:25:36.919] [RelayFwd] Stopped
[22:25:36.919] [SenderCtrl] SHUTDOWN_COMPLETE
[22:25:36.919] [stream] Cleaning up CHUNK_SEND_CONTEXT
[22:25:37.135] [cleanup] Closing MsQuic resources...
[22:25:37.961] [Send] Connection shutdown complete
[22:25:37.966] [cleanup] MsQuic resources closed
[22:25:37.967]
[Send] Session ended

# Receiver:

C:\Users\dylan\Desktop\Wormhole>wormhole.exe receive 2-tapioca-shame
[22:24:57.715] === Wormhole - Secure P2P File Transfer ===

[22:24:57.715]
ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
[22:24:57.715]   WORMHOLE RECEIVE
[22:24:57.715] ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ


ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
Γòæ                    WORMHOLE FILE TRANSFER                 Γòæ
ΓòáΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòú
Γòæ                                                           Γòæ
Γòæ  Connecting with ticket:                                 Γòæ
Γòæ                                                           Γòæ
Γòæ      2-tapioca-shame                                    Γòæ
Γòæ                                                           Γòæ
Γòæ  Looking up sender...                                    Γòæ
Γòæ                                                           Γòæ
ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[PeerID] Loaded keypair from C:\Users\dylan\AppData\Roaming\.wormhole\identity
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[22:24:57.819] [Receive] Discovering network endpoints...
[Discovery] Found LAN address (priority 0)
[Discovery] Found LAN address (priority 0)
[Discovery] Found LAN address (priority 0)
[Discovery] Found LAN address (priority 0)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 6 total endpoints
[22:24:57.852] [Receive] Registering with relay server...
[RelayClient] Sent REGISTER (6 endpoints)
[RelayClient] REGISTERED (session 14314262369823567354)
[22:24:57.955]
[Relay] Connected to relay server (session 14314262369823567354)
[22:24:57.955] [Relay] Your public IP (as seen by relay): 64.109.250.105:29477
[22:24:57.955] [Receive] Looking up sender...
[RelayClient] Sent LOOKUP (ticket: 2-tapioca-shame)
[RelayClient] PEER_INFO (5 endpoints)
[22:24:58.050]
[Relay] Found sender with 5 endpoints
[22:24:58.050]
[Receive] Sender PeerID: e182e0538756c16869cb58aad6908dc37a70670fcd5c766cd045dcc12aa6f60d
[22:24:58.050] [Receive] Sender has 5 endpoints

[22:24:58.050] [Receive] [DEBUG] Received 5 endpoints from relay:
[22:24:58.050] [Receive] [DEBUG]   EP[0]: 2600:1700:7d00:1390::17:4567 (priority=75, port_raw=0x11d7)
[22:24:58.050] [Receive] [DEBUG]   EP[1]: 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority=75, port_raw=0x11d7)
[22:24:58.050] [Receive] [DEBUG]   EP[2]: 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority=75, port_raw=0x11d7)
[22:24:58.050] [Receive] [DEBUG]   EP[3]: 107.204.80.156:4567 (priority=100, port_raw=0x11d7)
[22:24:58.050] [Receive] [DEBUG]   EP[4]: 167.71.179.132:443 (priority=200, port_raw=0x01bb)
[22:24:58.050] [Receive] Sending hole-punch probes to sender's 5 endpoints (5 rounds)...
[22:24:58.050] [HolePunch] Probes sending from local port 62349 (socket fd=668)
[22:24:58.050] [HolePunch] Round 1: probe -> EP[3] 107.204.80.156:4567 (priority=100) sent
[22:24:58.896] [Receive] Hole-punch probing complete (5 rounds sent)
[22:24:58.896] [Receive] Closing relay connection (freeing port for QUIC)...
[RelayClient] Sent GOODBYE (reason: upgraded to direct)
[22:24:58.896]
[Relay] Disconnected from relay
[RelayClient] Destroyed
[22:24:58.899] [Receive] Files will be saved to: C:\Users\dylan\Downloads

[22:24:58.906] [init] Opening MsQuic library...
[22:24:58.906] [init] MsQuic library opened successfully
[22:24:58.912] [init] Registration created successfully
[22:24:58.912] [client] Loading configuration...
[22:24:58.912] [client] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on, PeerStreams=10
[22:24:58.912] [client] Configuration loaded successfully (unsecure mode)
[22:24:58.912] [Receive] Starting parallel connection race to 5 endpoints (attempt 1/4)...
[22:24:58.912] [Receive]   [0] 2600:1700:7d00:1390::17:4567 (priority 75) - starting...
[22:24:58.928] [Receive]   [1] 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority 75) - starting...
[22:24:58.929] [Receive]   [2] 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority 75) - starting...
[22:24:58.929] [Receive]   [3] 107.204.80.156:4567 (priority 100) - starting...
[22:24:58.937] [Receive]   [3] QUIC bound to local port 4567 (reusing NAT mapping)
[22:24:58.937] [Receive] All 4 connections started, waiting for first success (5s timeout)...
[22:25:03.951] [Receive] Attempt 1 failed, sender may still be starting QUIC listener...
[22:25:03.951] [Receive] Retrying connection (attempt 2/4) in 3 seconds...
[22:25:06.957] [Receive] Starting parallel connection race to 5 endpoints (attempt 2/4)...
[22:25:06.957] [Receive]   [0] 2600:1700:7d00:1390::17:4567 (priority 75) - starting...
[22:25:06.957] [Receive]   [1] 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority 75) - starting...
[22:25:06.957] [Receive]   [2] 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority 75) - starting...
[22:25:06.957] [Receive]   [3] 107.204.80.156:4567 (priority 100) - starting...
[22:25:06.983] [Receive]   [3] QUIC bound to local port 4567 (reusing NAT mapping)
[22:25:06.983] [Receive] All 4 connections started, waiting for first success (5s timeout)...
[22:25:11.993] [Receive] Attempt 2 failed, sender may still be starting QUIC listener...
[22:25:11.993] [Receive] Retrying connection (attempt 3/4) in 3 seconds...
[22:25:15.008] [Receive] Starting parallel connection race to 5 endpoints (attempt 3/4)...
[22:25:15.008] [Receive]   [0] 2600:1700:7d00:1390::17:4567 (priority 75) - starting...
[22:25:15.008] [Receive]   [1] 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority 75) - starting...
[22:25:15.008] [Receive]   [2] 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority 75) - starting...
[22:25:15.013] [Receive]   [3] 107.204.80.156:4567 (priority 100) - starting...
[22:25:15.034] [Receive]   [3] QUIC bound to local port 4567 (reusing NAT mapping)
[22:25:15.036] [Receive] All 4 connections started, waiting for first success (5s timeout)...
[22:25:20.049] [Receive] Attempt 3 failed, sender may still be starting QUIC listener...
[22:25:20.049] [Receive] Retrying connection (attempt 4/4) in 3 seconds...
[22:25:23.068] [Receive] Starting parallel connection race to 5 endpoints (attempt 4/4)...
[22:25:23.068] [Receive]   [0] 2600:1700:7d00:1390::17:4567 (priority 75) - starting...
[22:25:23.068] [Receive]   [1] 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority 75) - starting...
[22:25:23.068] [Receive]   [2] 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority 75) - starting...
[22:25:23.068] [Receive]   [3] 107.204.80.156:4567 (priority 100) - starting...
[22:25:23.075] [Receive]   [3] QUIC bound to local port 4567 (reusing NAT mapping)
[22:25:23.079] [Receive] All 4 connections started, waiting for first success (5s timeout)...
[22:25:28.105]
[Receive] Direct connections failed. Attempting relay-forwarded connection...
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[RelayClient] Sent REGISTER (6 endpoints)
[RelayClient] REGISTERED (session 14545927770717855490)
[22:25:28.235] [RelayFwd] Registered with relay (session 14545927770717855490)
[22:25:28.235] [RelayFwd] Proxy socket bound to 127.0.0.1:50854
[22:25:28.235] [RelayFwd] Started (proxy port 50854, forwarding to peer)
[22:25:28.235] [Receive] Relay forwarder started on 127.0.0.1:50854
[22:25:28.235] [RelayFwd] Forwarder thread started (relay_sock=640, proxy_sock=620)
[22:25:28.235] [Receive] Connecting to sender via relay...
[22:25:28.423] [Receive] Connected to sender!
[22:25:28.423] [Receive] Connected via relay forwarding!
[22:25:28.427] ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
[22:25:28.427] Γòæ  CONNECTION ESTABLISHED                                   Γòæ
[22:25:28.427] Γòæ  Waiting for file transfer...                             Γòæ
[22:25:28.427] ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[22:25:28.554] [client] Session ticket saved (58 bytes) for 0-RTT
[22:25:28.560] [Receive] Control stream started (bidirectional)
[22:25:28.560] [Receive] Sending MANIFEST_REQUEST...
[22:25:28.695] [RecvCtrl] Received MANIFEST_RESPONSE (1508 bytes payload)
[22:25:28.695] [RecvCtrl] Manifest: file=test_10mb.bin, size=10485760, chunks=40
[22:25:28.711] [RecvCtrl] Sent CHUNK_REQUEST: 0/40 cached, need 40
[22:25:28.711] [RecvCtrl] Ready to receive 40 chunks
[22:25:28.827] [Receive] Data stream started (unidirectional)
Receiving [==============================] 100.0% | 222.1 KB/s | done
[22:25:39.542] [RecvData] Sender finished sending data (FIN received)
[22:25:39.542] [RecvData] All 40 chunks received successfully!
[22:25:39.544] [RecvData] File saved: C:\Users\dylan\Downloads\test_10mb.bin
[22:25:39.546] [TransferState] Deleted state file
[22:25:39.548] [RecvData] Transfer: 10485760 bytes in 10.84 sec (945.0 KB/s)
[22:25:39.548] [RecvData] Sent TRANSFER_COMPLETE to sender
[22:25:39.548] [RecvData] SHUTDOWN_COMPLETE
[22:25:58.482] [Receive] Transfer in progress... (0 minutes elapsed)
[22:26:09.555]
[Receive] File transfer complete!
[22:26:09.555] [RecvCtrl] SHUTDOWN_COMPLETE
[22:26:09.555] [stream] Cleaning up CHUNK_RECEIVE_CONTEXT
[22:26:09.555] [Receive] Connection shutdown complete
[22:26:09.762] [cleanup] Closing MsQuic resources...
[22:26:09.762] [cleanup] MsQuic resources closed
[22:26:09.762] [RelayFwd] Stopping...
[22:26:09.762] [RelayFwd] Forwarder thread exiting
[RelayClient] Sent GOODBYE (reason: transfer complete)
[22:26:09.762] [RelayFwd] Disconnected from relay
[RelayClient] Destroyed
[22:26:09.762] [RelayFwd] Stopped
[22:26:09.762]
[Receive] Session ended

# Relay:

snippet
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:38 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
root@WormholeRelayUSEast:~# journalctl -u wormhole-relay.service -n 2000
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 47 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 48 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 47 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 74 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
Feb 18 04:25:37 WormholeRelayUSEast relay-server[130724]: [Server] FORWARD: 1412 bytes to peer
