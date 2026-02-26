//
// dht_node.h
// Wormhole DHT - Node (UDP transport, RPC dispatch, bootstrap)
// by Dylan Kress
//

#pragma once

#include "routing_table.h"
#include "dht_store.h"
#include "../relay/peer_id.h"

// ============================================================================
// Types
// ============================================================================

// Pending RPC: tracks an outbound request awaiting a response
typedef struct {
    uint32_t txn_id;
    uint8_t  msg_type;
    uint8_t  target_id[32];     // What we searched for (FIND_NODE target, etc.)
    time_t   sent_at;
    void    *callback_ctx;
} DHT_PENDING_RPC;

// DHT node: manages the routing table, UDP socket, and RPC state
typedef struct DHT_NODE {
    KEYPAIR        *keypair;
    ROUTING_TABLE   routing_table;
    DHT_VALUE_STORE value_store;
    int             socket_fd;
    uint16_t        listen_port;

    // Pending outbound RPCs
    DHT_PENDING_RPC pending_rpcs[DHT_MAX_PENDING_RPCS];
    uint32_t        pending_count;
    uint32_t        next_txn_id;

    // Bootstrap nodes (up to 4)
#define DHT_MAX_BOOTSTRAP_NODES 4
    struct {
        uint8_t  addr_type;
        uint8_t  addr[16];
        uint16_t port;
    } bootstrap_nodes[DHT_MAX_BOOTSTRAP_NODES];
    uint32_t        bootstrap_count;
    BOOLEAN         bootstrapped;

    // Bootstrap config (stored for DNS re-resolution on retry)
    char            bootstrap_host_str[512];
    uint16_t        bootstrap_default_port;

    // Anti-flooding: rate limiter for incoming STORE operations
    DHT_STORE_RATE_LIMITER store_rate_limiter;

    // Stats
    uint64_t        msgs_sent;
    uint64_t        msgs_received;
    uint32_t        active_lookups;
} DHT_NODE;

// ============================================================================
// Public API
// ============================================================================

// Initialize DHT node: open UDP socket, resolve bootstrap, load saved state
// bootstrap_host/bootstrap_port may be NULL/0 if no bootstrap peer
BOOLEAN DhtNode_Init(DHT_NODE *node, KEYPAIR *keypair, uint16_t port,
                     const char *bootstrap_host, uint16_t bootstrap_port);

// Poll for incoming messages with timeout (select/poll)
// Dispatches received messages and expires timed-out RPCs
void DhtNode_Poll(DHT_NODE *node, int timeout_ms);

// Bootstrap: send FIND_NODE(self_id) to the bootstrap node
BOOLEAN DhtNode_Bootstrap(DHT_NODE *node);

// Re-resolve bootstrap DNS (for containers where DNS fails at startup)
// Returns TRUE if bootstrap_count > 0 after resolution attempt
BOOLEAN DhtNode_ReResolveBootstrap(DHT_NODE *node);

// Send a PING to a specific node
BOOLEAN DhtNode_SendPing(DHT_NODE *node, const uint8_t addr[16],
                          uint8_t addr_type, uint16_t port);

// Send a FIND_NODE request to a specific node
BOOLEAN DhtNode_SendFindNode(DHT_NODE *node, const uint8_t addr[16],
                              uint8_t addr_type, uint16_t port,
                              const uint8_t target_id[32]);

// Refresh stale buckets (>15 min since last refresh)
void DhtNode_RefreshBuckets(DHT_NODE *node);

// Remove RPCs that have exceeded DHT_RPC_TIMEOUT_MS
void DhtNode_ExpirePendingRPCs(DHT_NODE *node);

// Save routing table and close socket
void DhtNode_Shutdown(DHT_NODE *node);

// Get total nodes in routing table
uint32_t DhtNode_GetNodeCount(const DHT_NODE *node);

// ============================================================================
// DHT Storage Operations (Sub-phase 4.2)
// ============================================================================

// Send a FIND_VALUE request to a specific node
BOOLEAN DhtNode_SendFindValue(DHT_NODE *node, const uint8_t addr[16],
                               uint8_t addr_type, uint16_t port,
                               const uint8_t key[32]);

// Send a STORE request to a specific node
BOOLEAN DhtNode_SendStore(DHT_NODE *node, const uint8_t addr[16],
                           uint8_t addr_type, uint16_t port,
                           const uint8_t key[32],
                           const DHT_LOCATION *locations, uint32_t location_count);

// Announce that we have a chunk: iterative FIND_NODE for chunk_hash,
// then STORE to K closest nodes. quic_port is the QUIC listener port for chunk fetching.
void DhtNode_AnnounceChunk(DHT_NODE *node, const uint8_t chunk_hash[32], uint16_t quic_port);

// Find which nodes store a chunk: iterative FIND_VALUE.
// Copies locations into out_locations, returns count found.
uint32_t DhtNode_FindChunkLocations(DHT_NODE *node, const uint8_t chunk_hash[32],
                                     DHT_LOCATION *out_locations, uint32_t max);
