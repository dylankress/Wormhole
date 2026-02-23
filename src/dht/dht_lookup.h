//
// dht_lookup.h
// Wormhole DHT - Iterative Kademlia lookup (FIND_NODE / FIND_VALUE).
// by Dylan Kress
//

#pragma once

#include "dht_protocol.h"
#include "routing_table.h"
#include "dht_store.h"

// ============================================================================
// Constants
// ============================================================================

#define DHT_LOOKUP_MAX_SHORTLIST 64
#define DHT_LOOKUP_MAX_ITERATIONS 10

// ============================================================================
// Types
// ============================================================================

// Lookup type
typedef enum {
    DHT_LOOKUP_NODE,   // Find closest nodes to a target
    DHT_LOOKUP_VALUE   // Find nodes that store a value (chunk locations)
} DHT_LOOKUP_TYPE;

// Shortlist entry status
typedef enum {
    DHT_ENTRY_PENDING,   // Not yet queried
    DHT_ENTRY_QUERIED,   // Query sent, awaiting response
    DHT_ENTRY_RESPONDED, // Response received
    DHT_ENTRY_FAILED     // Timed out or error
} DHT_ENTRY_STATUS;

// A single node in the shortlist
typedef struct {
    uint8_t           node_id[32];
    uint8_t           addr_type;
    uint8_t           addr[16];
    uint16_t          port;
    DHT_ENTRY_STATUS  status;
    uint32_t          txn_id;    // Transaction ID if queried
} DHT_SHORTLIST_ENTRY;

// Iterative lookup state
typedef struct {
    DHT_LOOKUP_TYPE    type;
    uint8_t            target[32];

    // Shortlist: nodes sorted by XOR distance to target
    DHT_SHORTLIST_ENTRY shortlist[DHT_LOOKUP_MAX_SHORTLIST];
    uint32_t            shortlist_count;

    // Value result (for FIND_VALUE)
    BOOLEAN             value_found;
    DHT_LOCATION        value_locations[DHT_STORE_MAX_LOCATIONS];
    uint32_t            value_location_count;

    // Progress tracking
    uint32_t            iteration;
    uint32_t            queries_in_flight;
    BOOLEAN             complete;
    time_t              start_time;
} DHT_ITERATIVE_LOOKUP;

// ============================================================================
// Public API
// ============================================================================

// Initialize a lookup by seeding the shortlist from the routing table.
// type: DHT_LOOKUP_NODE or DHT_LOOKUP_VALUE
// target: the 32-byte ID to search for
void DhtLookup_Start(DHT_ITERATIVE_LOOKUP *lookup, const ROUTING_TABLE *rt,
                     DHT_LOOKUP_TYPE type, const uint8_t target[32]);

// Select the next ALPHA unqueried nodes closest to the target.
// Writes their info into out_nodes (up to DHT_ALPHA).
// Returns the count of nodes selected (0 if none available).
uint32_t DhtLookup_SelectNextQueries(DHT_ITERATIVE_LOOKUP *lookup,
                                      DHT_SHORTLIST_ENTRY *out_nodes,
                                      uint32_t max);

// Mark a shortlist entry as queried with the given transaction ID.
void DhtLookup_MarkQueried(DHT_ITERATIVE_LOOKUP *lookup,
                            const uint8_t node_id[32], uint32_t txn_id);

// Handle a response from a node. Merges returned nodes into shortlist.
// For FIND_VALUE: if value found, sets value_found and copies locations.
// sender_id: the responding node's ID
// nodes/node_count: returned closer nodes (may be NULL/0 if value found)
// locations/location_count: value locations (FIND_VALUE only, NULL/0 for FIND_NODE)
void DhtLookup_HandleResponse(DHT_ITERATIVE_LOOKUP *lookup,
                               const uint8_t sender_id[32],
                               const DHT_NODE_INFO *nodes, uint32_t node_count,
                               const DHT_LOCATION *locations, uint32_t location_count);

// Mark a node as failed (timeout)
void DhtLookup_MarkFailed(DHT_ITERATIVE_LOOKUP *lookup, const uint8_t node_id[32]);

// Check if the lookup has exceeded its wall-clock timeout (DHT_LOOKUP_TIMEOUT_SEC).
// If timed out, sets complete = TRUE and returns TRUE. Otherwise returns FALSE.
BOOLEAN DhtLookup_CheckTimeout(DHT_ITERATIVE_LOOKUP *lookup);

// Check if the lookup is complete.
// Complete when: value found, max iterations reached, or no closer unqueried nodes.
BOOLEAN DhtLookup_IsComplete(const DHT_ITERATIVE_LOOKUP *lookup);

// Get the K closest responded nodes (for FIND_NODE result).
uint32_t DhtLookup_GetClosestNodes(const DHT_ITERATIVE_LOOKUP *lookup,
                                    ROUTING_NODE *out, uint32_t max);
