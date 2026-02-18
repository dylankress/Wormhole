# Machine A:

C:\Dev\Wormhole\src\build>wormholed.exe --port 4567
[22:49:05.140] [config] Loaded config from C:\Users\dylan\.wormhole\config (12 entries)
[22:49:05.141] [daemon] Storage quota: 10 GB
[22:49:05.141] [daemon] Replication target: 3
[PeerID] Loaded keypair from C:\Users\dylan\.wormhole\identity
[22:49:05.141] [daemon] PeerID: 6fa5bf2570e0da1b5b3da4d84e3b66eeb1990487bb3490995c58066032984b0a
[22:49:05.141] === wormholed - Wormhole Persistent Node Daemon ===
[22:49:05.142]   Port:  4567
[22:49:05.142]   Relay: enabled
[22:49:05.142] ================================================
[22:49:05.142] [daemon] Initializing chunk store...
[22:49:05.142] [daemon] Chunk store ready
[22:49:05.142] [daemon] Opening MsQuic library...
[22:49:05.152] [daemon] MsQuic initialized
[22:49:05.152] [daemon] Loading server configuration...
[22:49:05.152] Checking for existing Wormhole-Dev certificate...
[22:49:05.447] Found existing certificate: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[22:49:05.447] [daemon] Certificate thumbprint: 130F4FEFFC1A5CF305A1ED7FCD8C5FA4D86B9F85
[22:49:05.454] [daemon] Server configuration loaded
[22:49:05.455] [daemon] Client configuration loaded
[22:49:05.457] [daemon] QUIC listener started on port 4567
[22:49:05.458] [ipc] Server listening on \\.\pipe\wormhole_4567
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 3 total endpoints
[22:49:05.466] [daemon] Discovered 3 endpoints
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[RelayClient] Sent REGISTER (3 endpoints)
[22:49:05.476] [daemon] Relay registration sent
[22:49:05.479] [daemon] DHT node initialized on port 4568
[dht] Bootstrap: sending FIND_NODE(self) to 167.71.179.132:4568
[22:49:05.480] [daemon] Loaded ledger (0 peers)
[22:49:05.480] [daemon] Daemon is running. Press Ctrl+C to stop.
[22:50:15.370] [ipc] Client connected
[22:50:15.371] [daemon] STORE request: C:\Users\dylan\Desktop\test_1kb.bin
[22:50:15.371] [daemon] Stored 1/1 chunks for C:\Users\dylan\Desktop\test_1kb.bin
[22:50:15.387] [daemon] EC: 1 stripes with RS(4,2)
[dht] Announcing chunk to 0 nodes
[dht] Announcing chunk to 0 nodes
[dht] Announcing chunk to 0 nodes
[22:50:15.463] [daemon] EC metadata saved to C:\Users\dylan\.wormhole\ec\7f484e12cbc1d5a38629e464612eead35a00ae53bc56de2b73849d6bddee5590.ec
[22:50:59.185]
[daemon] Shutdown requested, cleaning up...
[22:50:59.272] [daemon] Shutting down...
[22:50:59.272] [ipc] Stopping server...
[22:50:59.273] [ipc] Client connected
[22:50:59.273] [ipc] Server thread exiting
[22:50:59.381] [ipc] Server stopped
[22:50:59.382] [daemon] DHT node shut down
[RelayClient] Sent GOODBYE (reason: error)
[22:50:59.383] [daemon] Relay disconnected
[RelayClient] Destroyed
[22:50:59.383] [daemon] Listener stop complete
[22:50:59.383] [daemon] QUIC listener stopped
[22:50:59.388] [daemon] Shutdown complete


# Machine A Terminal 2

C:\Dev\Wormhole\src\build>wormhole.exe store C:\Users\dylan\Desktop\test_1kb.bin
[22:50:15.370] === Wormhole - Secure P2P File Transfer ===

[22:50:15.463] Stored C:\Users\dylan\Desktop\test_1kb.bin (1 chunks)
[22:50:15.463]   Manifest: 7f484e12cbc1d5a38629e464612eead35a00ae53bc56de2b73849d6bddee5590
[22:50:15.465]   Chunk   0: d6fd9de5bccf223f523b316c9cd1cf9a9d87ea42473d68e011dad13f09bf8917

# Machine B:

C:\Users\dylan\Desktop\Wormhole>wormholed.exe --port 4567
[22:49:25.111] [config] Loaded config from C:\Users\dylan\.wormhole\config (12 entries)
[22:49:25.111] [daemon] Storage quota: 10 GB
[22:49:25.111] [daemon] Replication target: 3
[PeerID] Loaded keypair from C:\Users\dylan\.wormhole\identity
[22:49:25.121] [daemon] PeerID: cbbb9452a3c01d4d3a2f6b53d215fd6a3c9b6fb456e0efccc830b8909e7877af
[22:49:25.121] === wormholed - Wormhole Persistent Node Daemon ===
[22:49:25.121]   Port:  4567
[22:49:25.121]   Relay: enabled
[22:49:25.122] ================================================
[22:49:25.122] [daemon] Initializing chunk store...
[22:49:25.122] [daemon] Chunk store ready
[22:49:25.122] [daemon] Opening MsQuic library...
[22:49:25.135] [daemon] MsQuic initialized
[22:49:25.135] [daemon] Loading server configuration...
[22:49:25.135] Checking for existing Wormhole-Dev certificate...
[22:49:25.676] Found existing certificate: 0576AA94D8CAB0FDFB3960EC45CC051C9AEA18CB
[22:49:25.676] [daemon] Certificate thumbprint: 0576AA94D8CAB0FDFB3960EC45CC051C9AEA18CB
[22:49:25.690] [daemon] Server configuration loaded
[22:49:25.690] [daemon] Client configuration loaded
[22:49:25.693] [daemon] QUIC listener started on port 4567
[22:49:25.693] [ipc] Server listening on \\.\pipe\wormhole_4567
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found IPv6 address (priority 75)
[Discovery] Found 4 total endpoints
[22:49:25.710] [daemon] Discovered 4 endpoints
[RelayClient] Created client for wormholerelay.com:443 (IPv4)
[RelayClient] Sent REGISTER (4 endpoints)
[22:49:25.710] [daemon] Relay registration sent
[22:49:25.722] [daemon] DHT node initialized on port 4568
[dht] Bootstrap: sending FIND_NODE(self) to 167.71.179.132:4568
[22:49:25.724] [daemon] Loaded ledger (0 peers)
[22:49:25.724] [daemon] Daemon is running. Press Ctrl+C to stop.
[22:51:05.629]
[daemon] Shutdown requested, cleaning up...
[22:51:05.725] [daemon] Shutting down...
[22:51:05.725] [ipc] Stopping server...
[22:51:05.725] [ipc] Client connected
[22:51:05.725] [ipc] Server thread exiting
[22:51:05.837] [ipc] Server stopped
[22:51:05.839] [daemon] DHT node shut down
[RelayClient] Sent GOODBYE (reason: error)
[22:51:05.853] [daemon] Relay disconnected
[RelayClient] Destroyed
[22:51:05.855] [daemon] Listener stop complete
[22:51:05.857] [daemon] QUIC listener stopped
[22:51:05.857] [daemon] Shutdown complete

# Relay:

nothing on the logs for this...
