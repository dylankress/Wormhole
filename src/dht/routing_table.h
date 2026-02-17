//
// routing_table.h
// Wormhole DHT - Kademlia K-Bucket Routing Table
// by Dylan Kress
//

#pragma once

#include "dht_protocol.h"
#include <time.h>

#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// ============================================================================
// Types
// ============================================================================

// In-memory representation of a known DHT node
typedef struct {
    uint8_t  node_id[32];
    uint8_t  addr_type;         // DHT_ADDR_IPV4 or DHT_ADDR_IPV6
    uint8_t  addr[16];          // IP address (IPv4 uses first 4 bytes)
    uint16_t port;              // Host byte order (LE on x86)
    time_t   last_seen;         // Last time we heard from this node
    uint32_t fail_count;        // Consecutive RPC failures
} ROUTING_NODE;

// K-bucket: holds up to DHT_K nodes at a specific XOR distance range
typedef struct {
    ROUTING_NODE nodes[DHT_K];  // DHT_K = 20
    uint32_t     count;
    time_t       last_refresh;
} K_BUCKET;

// Full routing table: 256 buckets (one per bit of the 256-bit node ID space)
typedef struct {
    uint8_t   self_id[32];
    K_BUCKET  buckets[DHT_BUCKET_COUNT];  // DHT_BUCKET_COUNT = 256
} ROUTING_TABLE;

// ============================================================================
// Public API
// ============================================================================

// Initialize routing table (zeroes all buckets, copies self_id)
void RoutingTable_Init(ROUTING_TABLE *rt, const uint8_t self_id[32]);

// Get bucket index for a given node ID (0-255, or -1 if same as self)
// Uses XOR distance: highest differing bit position
int RoutingTable_GetBucketIndex(const ROUTING_TABLE *rt, const uint8_t node_id[32]);

// Add or update a node in the routing table
// If bucket full: evicts LRS node if it has fail_count > 0, else discards new node
// Returns TRUE if node was added/updated
BOOLEAN RoutingTable_AddNode(ROUTING_TABLE *rt, const uint8_t node_id[32],
                              uint8_t addr_type, const uint8_t addr[16], uint16_t port);

// Find up to max closest nodes to a target (sorted by XOR distance)
uint32_t RoutingTable_FindClosest(const ROUTING_TABLE *rt, const uint8_t target[32],
                                   ROUTING_NODE *out, uint32_t max);

// Update last_seen timestamp and reset fail_count for a node
void RoutingTable_TouchNode(ROUTING_TABLE *rt, const uint8_t node_id[32]);

// Increment fail_count for a node
void RoutingTable_MarkFailed(ROUTING_TABLE *rt, const uint8_t node_id[32]);

// Remove a node from the routing table
BOOLEAN RoutingTable_RemoveNode(ROUTING_TABLE *rt, const uint8_t node_id[32]);

// Count total nodes across all buckets
uint32_t RoutingTable_GetTotalNodes(const ROUTING_TABLE *rt);

// Persist routing table to file
// Format: [4B "DHRT"][4B version][32B self_id][4B count][nodes...]
BOOLEAN RoutingTable_Save(const ROUTING_TABLE *rt, const char *path);

// Load routing table from file
BOOLEAN RoutingTable_Load(ROUTING_TABLE *rt, const char *path);
