//
// peer_registry.h
// Wormhole Relay Server - Peer Registration and Tracking
// by Dylan Kress
//

#pragma once

#include "relay_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef struct sockaddr_storage SOCKET_ADDR;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
typedef struct sockaddr_storage SOCKET_ADDR;
#endif

// Peer entry in the registry
typedef struct PEER_ENTRY {
    uint8_t peer_id[32];              // Ed25519 public key (unique identifier)
    uint64_t session_id;              // Unique session ID for this connection
    SOCKET_ADDR socket_addr;          // UDP socket address of this peer
    socklen_t addr_len;               // Length of socket address
    
    ENDPOINT endpoints[MAX_ENDPOINTS]; // Connection candidates (LAN, public, IPv6, etc.)
    uint16_t endpoint_count;          // Number of valid endpoints
    
    time_t registered_at;             // When peer registered
    time_t last_keepalive;            // Last keepalive timestamp
    
    char* ticket;                     // Ticket string (if this peer created one), NULL otherwise
    
    struct PEER_ENTRY* next;          // Hash table collision chain
} PEER_ENTRY;

// Peer registry (hash table)
typedef struct {
    PEER_ENTRY** buckets;             // Array of hash table buckets
    uint32_t bucket_count;            // Number of buckets (power of 2)
    uint32_t peer_count;              // Current number of registered peers
    
#ifdef _WIN32
    CRITICAL_SECTION lock;            // Thread safety (Windows)
#else
    pthread_mutex_t lock;             // Thread safety (POSIX)
#endif
} PEER_REGISTRY;

// Initialize peer registry
// Returns: true on success, false on failure
bool PeerRegistry_Init(PEER_REGISTRY* registry, uint32_t initial_capacity);

// Cleanup peer registry and free all resources
void PeerRegistry_Cleanup(PEER_REGISTRY* registry);

// Register a new peer (or update existing)
// Returns: session_id (>0) on success, 0 on failure
uint64_t PeerRegistry_RegisterPeer(
    PEER_REGISTRY* registry,
    const uint8_t peer_id[32],
    const SOCKET_ADDR* socket_addr,
    socklen_t addr_len,
    const ENDPOINT* endpoints,
    uint16_t endpoint_count
);

// Find peer by PeerID
// Returns: Pointer to peer entry, or NULL if not found
PEER_ENTRY* PeerRegistry_FindByPeerID(PEER_REGISTRY* registry, const uint8_t peer_id[32]);

// Find peer by session ID
// Returns: Pointer to peer entry, or NULL if not found
PEER_ENTRY* PeerRegistry_FindBySessionID(PEER_REGISTRY* registry, uint64_t session_id);

// Find peer by ticket
// Returns: Pointer to peer entry, or NULL if not found
PEER_ENTRY* PeerRegistry_FindByTicket(PEER_REGISTRY* registry, const char* ticket);

// Update peer's last keepalive timestamp
// Returns: true if peer found and updated, false otherwise
bool PeerRegistry_UpdateKeepalive(PEER_REGISTRY* registry, uint64_t session_id);

// Associate a ticket with a peer (for sender)
// Returns: true on success, false on failure
bool PeerRegistry_SetTicket(PEER_REGISTRY* registry, uint64_t session_id, const char* ticket);

// Remove peer from registry
// Returns: true if peer found and removed, false otherwise
bool PeerRegistry_RemovePeer(PEER_REGISTRY* registry, uint64_t session_id);

// Remove stale peers (no keepalive for >60 seconds)
// Returns: Number of peers removed
uint32_t PeerRegistry_RemoveStalePeers(PEER_REGISTRY* registry, time_t current_time);

// Get registry statistics
void PeerRegistry_GetStats(PEER_REGISTRY* registry, uint32_t* peer_count, uint32_t* bucket_count);
