# Sender:

C:\Dev\Wormhole\src\build>wormhole.exe send C:\Users\dylan\Desktop\test_100mb.bin
[00:09:02.270] === Wormhole - Secure P2P File Transfer ===

[00:09:02.270]
ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
[00:09:02.270]   WORMHOLE SEND
[00:09:02.270] ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ

[00:09:02.271] [Send] File: test_100mb.bin
[00:09:02.271] [Send] Size: 104857600 bytes

[00:09:02.271] [Send] Building file manifest...
[00:09:03.864] [Send] Manifest: 400 chunks of 262144 bytes (Blake3 hashed)
[PeerID] Loaded keypair from C:\Users\dylan\AppData\Roaming\.wormhole\identity
[RelayClient] Bound to local port 4567 (SO_REUSEADDR)
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[00:09:03.872] [Send] Discovering network endpoints...
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 3 total endpoints
[00:09:03.881] [Send] Registering with relay server...
[RelayClient] Sent REGISTER (3 endpoints)
[RelayClient] REGISTERED (session 18386688601764650357)
[00:09:03.917]
[Relay] Connected to relay server (session 18386688601764650357)
[00:09:03.917] [Relay] Your public IP (as seen by relay): 107.204.80.156:4567
[00:09:03.917] [Send] [DEBUG] Public endpoint created:
[00:09:03.917] [Send] [DEBUG]   Port (host order): 4567
[00:09:03.918] [Send] [DEBUG]   Port (network order): 0xd711
[00:09:03.918] [Send] [DEBUG]   Expected: 0x11D7 (4567 in network byte order)
[00:09:03.918] [Send] Adding public IP endpoint: 107.204.80.156:4567 (priority 100)
[00:09:03.918] [Send] Re-registering with public IP endpoint...
[00:09:03.918] [Send] [DEBUG] Re-registering with 4 endpoints:
[00:09:03.918] [Send] [DEBUG]   EP[0]: 2600:1700:7d00:1390::17:4567 (priority=75, port_raw=0x11d7)
[00:09:03.918] [Send] [DEBUG]   EP[1]: 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority=75, port_raw=0x11d7)
[00:09:03.918] [Send] [DEBUG]   EP[2]: 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority=75, port_raw=0x11d7)
[00:09:03.919] [Send] [DEBUG]   EP[3]: 107.204.80.156:4567 (priority=100, port_raw=0x11d7)
[RelayClient] Sent REGISTER (4 endpoints)
[00:09:03.919] [Send] Creating transfer ticket...
[RelayClient] Sent CREATE_TICKET (file: test_100mb.bin, size: 104857600 bytes)
[RelayClient] REGISTERED (session 18386688601764650357)
[00:09:03.951]
[Relay] Connected to relay server (session 18386688601764650357)
[00:09:03.951] [Relay] Your public IP (as seen by relay): 107.204.80.156:4567
[RelayClient] TICKET_CREATED: 9-expansive-duffel

ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
Γòæ                    WORMHOLE FILE TRANSFER                 Γòæ
ΓòáΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòú
Γòæ                                                           Γòæ
Γòæ  Your ticket:                                             Γòæ
Γòæ                                                           Γòæ
Γòæ      9-expansive-duffel                                 Γòæ
Γòæ                                                           Γòæ
Γòæ  File: test_100mb.bin                                    Γòæ
Γòæ  Size: 100.0 MB                                          Γòæ
Γòæ                                                           Γòæ
Γòæ  Share this ticket with the receiver. They can download  Γòæ
Γòæ  the file by running:                                    Γòæ
Γòæ                                                           Γòæ
Γòæ      wormhole receive 9-expansive-duffel            Γòæ
Γòæ                                                           Γòæ
Γòæ  Waiting for receiver to connect...                      Γòæ
Γòæ                                                           Γòæ
ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[00:09:03.953]
[Send] Waiting for receiver (timeout: 30 minutes)...

[00:09:03.953] [Send] Phase 1: Waiting for receiver on relay (hole punch probing)...
[RelayClient] PEER_INFO (6 endpoints)
[00:09:19.286]
[Relay] Found sender with 6 endpoints
[00:09:19.286] [Send] Receiver found! Starting hole punch probing to 6 endpoints...
[00:09:19.287] [HolePunch] Probes sending from local port 4567 (socket fd=668)
[00:09:19.287] [HolePunch] Round 1: probe -> EP[4] 107.204.80.156:60121 (priority=100) sent
[00:09:20.053] [Send] Phase 2: Closing relay, starting QUIC listener on port 4567...
[RelayClient] Sent GOODBYE (reason: upgraded to direct)
[00:09:20.053]
[Relay] Disconnected from relay
[RelayClient] Destroyed
[00:09:20.054] [init] Opening MsQuic library...
[00:09:20.055] [init] MsQuic library opened successfully
[00:09:20.060] [init] Registration created successfully
[00:09:20.060] [server] Loading configuration...
[00:09:20.060] Checking for existing Wormhole-Dev certificate...
[00:09:20.361] Found existing certificate: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[00:09:20.361] [server] Using certificate thumbprint: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[00:09:20.361] [server] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on
[00:09:20.368] [server] Configuration loaded successfully
[00:09:20.370] [Send] QUIC server listening on port 4567
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[RelayClient] Sent REGISTER (4 endpoints)
[RelayClient] REGISTERED (session 3195320015684815522)
[00:09:20.401] [RelayFwd] Registered with relay (session 3195320015684815522)
[00:09:20.403] [RelayFwd] Proxy socket bound to 127.0.0.1:62306
[00:09:20.403] [RelayFwd] Started (proxy port 62306, forwarding to peer)
[00:09:20.403] [Send] Relay forwarder started (fallback for restricted NAT)
[00:09:20.403] [RelayFwd] Forwarder thread started (relay_sock=1484, proxy_sock=1488)
[00:09:21.189] [Send] Receiver connecting...
[00:09:21.217] [Send] Rejecting extra connection (already have receiver)
[00:09:21.217] [Send] Rejecting extra connection (already have receiver)
[00:09:21.218] [Send] Receiver connected! Starting chunk-based file transfer...
[00:09:21.219] [ChunkSendFile] Starting chunk-based transfer: C:\Users\dylan\Desktop\test_100mb.bin (400 chunks)
[00:09:21.219] [ChunkSendFile] Control stream opened, waiting for MANIFEST_REQUEST...
[00:09:21.219] [SenderCtrl] START_COMPLETE (status: 0x0)
[00:09:21.225] [SenderCtrl] Received MANIFEST_REQUEST
[00:09:21.226] [SenderCtrl] Sent MANIFEST_RESPONSE (14469 bytes)
[00:09:21.226] [SenderCtrl] Waiting for CHUNK_REQUEST before sending data...
[00:09:21.251] [SenderCtrl] Received CHUNK_REQUEST: 400/400 chunks needed
[00:09:21.251] [SenderCtrl] Data stream opened, starting chunk send...
Sending [>                             ]   0.2% | 828.9 MB/s | ETA: 0s     [00:09:21.254] [SenderData] START_COMPLETE (status: 0x0)
[00:09:21.257] [SenderData] Ideal send buffer: 131072 bytes
[00:09:21.268] [SenderData] Ideal send buffer: 196608 bytes
[00:09:21.269] [SenderData] Ideal send buffer: 294912 bytes
[00:09:21.274] [Send] Receiver connected! Transferring file...
[00:09:21.281] [SenderData] Ideal send buffer: 442368 bytes
[00:09:21.307] [SenderData] Ideal send buffer: 663552 bytes
[00:09:21.344] [SenderData] Ideal send buffer: 995328 bytes
Sending [=>                            ]   4.2% | 17.9 MB/s | ETA: 5s     [00:09:21.478] [SenderData] Ideal send buffer: 1492992 bytes
Sending [====>                         ]  15.2% | 22.1 MB/s | ETA: 4s     [00:09:22.077] [SenderData] Ideal send buffer: 2239488 bytes
Sending [==============================] 100.0% | 1.2 GB/s | done
[00:09:26.128] [stream] Last chunk 399 queued (FIN set)
[00:09:26.204] [SenderData] SHUTDOWN_COMPLETE
[00:09:26.205] [SenderCtrl] Received TRANSFER_COMPLETE
[00:09:26.205] [SenderCtrl] Transfer complete: 104857600 bytes in 4.95 sec (20672.6 KB/s)
[00:09:26.205]
[Send] File transfer complete!
[00:09:26.205] [RelayFwd] Stopping...
[00:09:26.206] [RelayFwd] Forwarder thread exiting
[RelayClient] Sent GOODBYE (reason: transfer complete)
[00:09:26.206] [RelayFwd] Disconnected from relay
[RelayClient] Destroyed
[00:09:26.206] [RelayFwd] Stopped
[00:09:26.206] [SenderCtrl] SHUTDOWN_COMPLETE
[00:09:26.206] [stream] Cleaning up CHUNK_SEND_CONTEXT
[00:09:26.209] [Send] Connection shutdown complete
[00:09:26.412] [cleanup] Closing MsQuic resources...
[00:09:26.418] [cleanup] MsQuic resources closed
[00:09:26.418]
[Send] Session ended

# Receiver:

C:\Users\dylan\Desktop\Wormhole>wormhole.exe receive 9-expansive-duffel
[00:09:19.282] === Wormhole - Secure P2P File Transfer ===

[00:09:19.282]
ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
[00:09:19.282]   WORMHOLE RECEIVE
[00:09:19.282] ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ


ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
Γòæ                    WORMHOLE FILE TRANSFER                 Γòæ
ΓòáΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòú
Γòæ                                                           Γòæ
Γòæ  Connecting with ticket:                                 Γòæ
Γòæ                                                           Γòæ
Γòæ      9-expansive-duffel                                 Γòæ
Γòæ                                                           Γòæ
Γòæ  Looking up sender...                                    Γòæ
Γòæ                                                           Γòæ
ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[PeerID] Loaded keypair from C:\Users\dylan\AppData\Roaming\.wormhole\identity
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[00:09:19.299] [Receive] Discovering network endpoints...
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 4 total endpoints
[00:09:19.318] [Receive] Registering with relay server...
[RelayClient] Sent REGISTER (4 endpoints)
[RelayClient] REGISTERED (session 12186884631976241258)
[00:09:19.342]
[Relay] Connected to relay server (session 12186884631976241258)
[00:09:19.342] [Relay] Your public IP (as seen by relay): 107.204.80.156:60121
[00:09:19.342] [Receive] Looking up sender...
[RelayClient] Sent LOOKUP (ticket: 9-expansive-duffel)
[RelayClient] PEER_INFO (5 endpoints)
[00:09:19.383]
[Relay] Found sender with 5 endpoints
[00:09:19.383]
[Receive] Sender PeerID: e182e0538756c16869cb58aad6908dc37a70670fcd5c766cd045dcc12aa6f60d
[00:09:19.383] [Receive] Sender has 5 endpoints

[00:09:19.383] [Receive] [DEBUG] Received 5 endpoints from relay:
[00:09:19.383] [Receive] [DEBUG]   EP[0]: 2600:1700:7d00:1390::17:4567 (priority=75, port_raw=0x11d7)
[00:09:19.383] [Receive] [DEBUG]   EP[1]: 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority=75, port_raw=0x11d7)
[00:09:19.383] [Receive] [DEBUG]   EP[2]: 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority=75, port_raw=0x11d7)
[00:09:19.383] [Receive] [DEBUG]   EP[3]: 107.204.80.156:4567 (priority=100, port_raw=0x11d7)
[00:09:19.383] [Receive] [DEBUG]   EP[4]: 167.71.179.132:443 (priority=200, port_raw=0x01bb)
[00:09:19.383] [Receive] Sending hole-punch probes to sender's 5 endpoints (5 rounds)...
[00:09:19.383] [HolePunch] Probes sending from local port 60121 (socket fd=668)
[00:09:19.383] [HolePunch] Round 1: probe -> EP[3] 107.204.80.156:4567 (priority=100) sent
[00:09:20.219] [Receive] Hole-punch probing complete (5 rounds sent)
[00:09:20.219] [Receive] Closing relay connection (freeing port for QUIC)...
[RelayClient] Sent GOODBYE (reason: upgraded to direct)
[00:09:20.219]
[Relay] Disconnected from relay
[RelayClient] Destroyed
[00:09:20.242] [Receive] Files will be saved to: C:\Users\dylan\Downloads

[00:09:20.244] [init] Opening MsQuic library...
[00:09:20.246] [init] MsQuic library opened successfully
[00:09:20.258] [init] Registration created successfully
[00:09:20.260] [client] Loading configuration...
[00:09:20.260] [client] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on, PeerStreams=10
[00:09:20.266] [client] Configuration loaded successfully (unsecure mode)
[00:09:20.266] [Receive] Starting parallel connection race to 5 endpoints (attempt 1/4)...
[00:09:20.266] [Receive]   [0] 2600:1700:7d00:1390::17:4567 (priority 75) - starting...
[00:09:20.268] [Receive]   [1] 2600:1700:7d00:1390:2a0b:6016:7e14:574c:4567 (priority 75) - starting...
[00:09:20.274] [Receive]   [2] 2600:1700:7d00:1390:cd96:50a1:8be8:2f63:4567 (priority 75) - starting...
[00:09:20.282] [Receive]   [3] 107.204.80.156:4567 (priority 100) - starting...
[00:09:20.282] [Receive]   [3] QUIC bound to local port 4567 (reusing NAT mapping)
[00:09:20.282] [Receive] All 4 connections started, waiting for first success (5s timeout)...
[00:09:21.302] [Receive] Endpoint 0 connected first - winner!
[00:09:21.302] [Receive] Connected via endpoint 0: 2600:1700:7d00:1390::17:4567 (priority 75)
[00:09:21.302] ΓòöΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòù
[00:09:21.317] Γòæ  CONNECTION ESTABLISHED                                   Γòæ
[00:09:21.317] Γòæ  Waiting for file transfer...                             Γòæ
[00:09:21.317] ΓòÜΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓò¥

[00:09:21.317] [client] Session ticket saved (58 bytes) for 0-RTT
[00:09:21.317] [Receive] Control stream started (bidirectional)
[00:09:21.317] [Receive] Sending MANIFEST_REQUEST...
[00:09:21.329] [RecvCtrl] Received MANIFEST_RESPONSE (14469 bytes payload)
[00:09:21.329] [RecvCtrl] Manifest: file=test_100mb.bin, size=104857600, chunks=400
[00:09:21.347] [RecvCtrl] Sent CHUNK_REQUEST: 0/400 cached, need 400
[00:09:21.347] [RecvCtrl] Ready to receive 400 chunks
[00:09:21.354] [Receive] Data stream started (unidirectional)
Receiving [======>                       ]  22.0% | 22.5 MB/s | ETA: 3s     [00:09:22.472]
[Shutdown] Ctrl+C received, cleaning up...
[00:09:22.472] [cleanup] Closing MsQuic resources...
Receiving [==============================] 100.0% | 27.2 MB/s | done
[00:09:26.295] [RecvData] Sender finished sending data (FIN received)
[00:09:26.295] [RecvData] All 400 chunks received successfully!
[00:09:26.295] [RecvData] File saved: C:\Users\dylan\Downloads\test_100mb.bin
[00:09:26.299] [TransferState] Deleted state file
[00:09:26.301] [RecvData] Transfer: 104857600 bytes in 4.95 sec (20673.1 KB/s)
[00:09:26.301] [RecvData] Sent TRANSFER_COMPLETE to sender
[00:09:26.301] [RecvData] SHUTDOWN_COMPLETE
[00:09:26.301]
[Receive] File transfer complete!
[00:09:26.301] [RecvCtrl] SHUTDOWN_COMPLETE
[00:09:26.301] [stream] Cleaning up CHUNK_RECEIVE_CONTEXT
[00:09:26.301] [Receive] Connection shutdown complete
[00:09:26.508] [cleanup] Closing MsQuic resources...

# Relay:

Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[2]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[3]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Registered new peer (session 12428763197101337482, total peers: 2)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTERED sent (session 12428763197101337482)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] LOOKUP from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [TicketManager] Found ticket '4-silicon-corrosive' (expires in 3582 seconds)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG] Sending PEER_INFO to receiver with 5 endpoints:
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[0]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[1]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[2]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[3]: type=0x04, port=4567, priority=100
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[4]: type=0x04, port=443, priority=200
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] PEER_INFO sent to receiver (5 endpoints)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG] Sending PEER_INFO to sender with 6 endpoints:
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[0]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[1]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[2]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[3]: type=0x06, port=4567, priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[4]: type=0x04, port=55490, priority=100
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] [DEBUG]   EP[5]: type=0x04, port=443, priority=200
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] PEER_INFO sent to sender (6 endpoints, bidirectional notification)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] GOODBYE from client (reason: upgraded to direct)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Removed peer (session 5205230205496236088, total peers: 1)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] Removed peer (session 5205230205496236088) on GOODBYE
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] GOODBYE from client (reason: upgraded to direct)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Removed peer (session 12428763197101337482, total peers: 0)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] Removed peer (session 12428763197101337482) on GOODBYE
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTER from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Crypto] Ed25519 signature verified successfully
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG] Storing 4 endpoints:
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[0]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[1]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[2]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[3]: type=0x04, port=0x11d7 (4567), priority=100
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Registered new peer (session 5607974446261297544, total peers: 1)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTERED sent (session 5607974446261297544)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] GOODBYE from client (reason: transfer complete)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Removed peer (session 5607974446261297544, total peers: 0)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] Removed peer (session 5607974446261297544) on GOODBYE
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTER from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Crypto] Ed25519 signature verified successfully
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG] Storing 3 endpoints:
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[0]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[1]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[2]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Registered new peer (session 18386688601764650357, total peers: 1)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTERED sent (session 18386688601764650357)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTER from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Crypto] Ed25519 signature verified successfully
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Updated existing peer (session 18386688601764650357)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTERED sent (session 18386688601764650357)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] CREATE_TICKET from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [TicketManager] Generated ticket '9-expansive-duffel' for file 'test_100mb.bin' (104857600 bytes, expires in 3>
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Associated ticket '9-expansive-duffel' with session 18386688601764650357
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] TICKET_CREATED sent: 9-expansive-duffel
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTER from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Crypto] Ed25519 signature verified successfully
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG] Storing 4 endpoints:
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[0]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[1]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[2]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] [DEBUG]   EP[3]: type=0x06, port=0x11d7 (4567), priority=75
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [PeerRegistry] Registered new peer (session 12186884631976241258, total peers: 2)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] REGISTERED sent (session 12186884631976241258)
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [Server] LOOKUP from client
Feb 18 06:09:19 WormholeRelayUSEast relay-server[137257]: [TicketManager] Found ticket '9-expansive-duffel' (expires in 3585 seconds)
