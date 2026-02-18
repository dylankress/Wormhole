//
// dht_lookup.c
// Wormhole DHT - Iterative Kademlia lookup (FIND_NODE / FIND_VALUE).
// by Dylan Kress
//

#include "dht_lookup.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Internal: XOR distance comparison
// ============================================================================

// Compare XOR distance of two node IDs to a target.
// Returns: -1 if a is closer, +1 if b is closer, 0 if equal.
static int CompareXorDistance(const uint8_t a[32], const uint8_t b[32],
                              const uint8_t target[32])
{
    for (int i = 0; i < 32; i++)
    {
        uint8_t da = a[i] ^ target[i];
        uint8_t db = b[i] ^ target[i];
        if (da < db) return -1;
        if (da > db) return 1;
    }
    return 0;
}

// Check if a node_id is already in the shortlist
static int FindInShortlist(const DHT_ITERATIVE_LOOKUP *lookup, const uint8_t node_id[32])
{
    for (uint32_t i = 0; i < lookup->shortlist_count; i++)
    {
        if (memcmp(lookup->shortlist[i].node_id, node_id, 32) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

// Insert a node into the shortlist maintaining sorted order by XOR distance.
// Returns TRUE if inserted (or already present).
static BOOLEAN InsertSorted(DHT_ITERATIVE_LOOKUP *lookup,
                             const uint8_t node_id[32], uint8_t addr_type,
                             const uint8_t addr[16], uint16_t port)
{
    // Skip if already in shortlist
    if (FindInShortlist(lookup, node_id) >= 0)
    {
        return TRUE;
    }

    // Find insertion point (sorted by XOR distance to target)
    uint32_t insert_at = lookup->shortlist_count;
    for (uint32_t i = 0; i < lookup->shortlist_count; i++)
    {
        if (CompareXorDistance(node_id, lookup->shortlist[i].node_id,
                               lookup->target) < 0)
        {
            insert_at = i;
            break;
        }
    }

    // If shortlist is full and new node would go at the end, skip it
    if (lookup->shortlist_count >= DHT_LOOKUP_MAX_SHORTLIST &&
        insert_at >= DHT_LOOKUP_MAX_SHORTLIST)
    {
        return FALSE;
    }

    // Make room by shifting entries
    uint32_t entries_to_shift;
    if (lookup->shortlist_count < DHT_LOOKUP_MAX_SHORTLIST)
    {
        entries_to_shift = lookup->shortlist_count - insert_at;
        lookup->shortlist_count++;
    }
    else
    {
        // Shortlist is full, drop the last entry
        entries_to_shift = lookup->shortlist_count - 1 - insert_at;
    }

    if (entries_to_shift > 0)
    {
        memmove(&lookup->shortlist[insert_at + 1],
                &lookup->shortlist[insert_at],
                entries_to_shift * sizeof(DHT_SHORTLIST_ENTRY));
    }

    // Insert the new entry
    DHT_SHORTLIST_ENTRY *e = &lookup->shortlist[insert_at];
    memcpy(e->node_id, node_id, 32);
    e->addr_type = addr_type;
    memcpy(e->addr, addr, 16);
    e->port = port;
    e->status = DHT_ENTRY_PENDING;
    e->txn_id = 0;

    return TRUE;
}

// ============================================================================
// Public API
// ============================================================================

void DhtLookup_Start(DHT_ITERATIVE_LOOKUP *lookup, const ROUTING_TABLE *rt,
                     DHT_LOOKUP_TYPE type, const uint8_t target[32])
{
    memset(lookup, 0, sizeof(DHT_ITERATIVE_LOOKUP));
    lookup->type = type;
    memcpy(lookup->target, target, 32);

    // Seed shortlist from routing table's closest nodes
    ROUTING_NODE closest[DHT_K];
    uint32_t count = RoutingTable_FindClosest(rt, target, closest, DHT_K);

    for (uint32_t i = 0; i < count; i++)
    {
        InsertSorted(lookup, closest[i].node_id, closest[i].addr_type,
                     closest[i].addr, closest[i].port);
    }

    // No nodes to query — lookup is immediately complete
    if (lookup->shortlist_count == 0) {
        lookup->complete = TRUE;
    }
}

uint32_t DhtLookup_SelectNextQueries(DHT_ITERATIVE_LOOKUP *lookup,
                                      DHT_SHORTLIST_ENTRY *out_nodes,
                                      uint32_t max)
{
    if (lookup->complete) return 0;

    uint32_t selected = 0;
    uint32_t limit = max < DHT_ALPHA ? max : DHT_ALPHA;

    // Select up to ALPHA closest pending (unqueried) nodes
    for (uint32_t i = 0; i < lookup->shortlist_count && selected < limit; i++)
    {
        if (lookup->shortlist[i].status == DHT_ENTRY_PENDING)
        {
            out_nodes[selected] = lookup->shortlist[i];
            selected++;
        }
    }

    return selected;
}

void DhtLookup_MarkQueried(DHT_ITERATIVE_LOOKUP *lookup,
                            const uint8_t node_id[32], uint32_t txn_id)
{
    int idx = FindInShortlist(lookup, node_id);
    if (idx >= 0)
    {
        lookup->shortlist[idx].status = DHT_ENTRY_QUERIED;
        lookup->shortlist[idx].txn_id = txn_id;
        lookup->queries_in_flight++;
    }
}

void DhtLookup_HandleResponse(DHT_ITERATIVE_LOOKUP *lookup,
                               const uint8_t sender_id[32],
                               const DHT_NODE_INFO *nodes, uint32_t node_count,
                               const DHT_LOCATION *locations, uint32_t location_count)
{
    // Mark sender as responded
    int idx = FindInShortlist(lookup, sender_id);
    if (idx >= 0)
    {
        lookup->shortlist[idx].status = DHT_ENTRY_RESPONDED;
        if (lookup->queries_in_flight > 0)
            lookup->queries_in_flight--;
    }

    // For FIND_VALUE: if value was found, record it
    if (lookup->type == DHT_LOOKUP_VALUE && locations != NULL && location_count > 0)
    {
        lookup->value_found = TRUE;
        uint32_t to_copy = location_count;
        if (to_copy > DHT_STORE_MAX_LOCATIONS)
            to_copy = DHT_STORE_MAX_LOCATIONS;
        memcpy(lookup->value_locations, locations, to_copy * sizeof(DHT_LOCATION));
        lookup->value_location_count = to_copy;
        lookup->complete = TRUE;
        return;
    }

    // Merge returned nodes into shortlist
    for (uint32_t i = 0; i < node_count; i++)
    {
        InsertSorted(lookup, nodes[i].node_id, nodes[i].addr_type,
                     nodes[i].addr, nodes[i].port);
    }

    // Only increment iteration when a full round completes (no more queries in flight)
    if (lookup->queries_in_flight == 0)
    {
        lookup->iteration++;
    }

    // Check completion: max iterations or no more pending nodes
    if (lookup->iteration >= DHT_LOOKUP_MAX_ITERATIONS)
    {
        lookup->complete = TRUE;
        return;
    }

    // Check if any pending nodes remain
    BOOLEAN has_pending = FALSE;
    for (uint32_t i = 0; i < lookup->shortlist_count; i++)
    {
        if (lookup->shortlist[i].status == DHT_ENTRY_PENDING)
        {
            has_pending = TRUE;
            break;
        }
    }

    if (!has_pending && lookup->queries_in_flight == 0)
    {
        lookup->complete = TRUE;
    }
}

void DhtLookup_MarkFailed(DHT_ITERATIVE_LOOKUP *lookup, const uint8_t node_id[32])
{
    int idx = FindInShortlist(lookup, node_id);
    if (idx >= 0)
    {
        lookup->shortlist[idx].status = DHT_ENTRY_FAILED;
        if (lookup->queries_in_flight > 0)
            lookup->queries_in_flight--;
    }
}

BOOLEAN DhtLookup_IsComplete(const DHT_ITERATIVE_LOOKUP *lookup)
{
    return lookup->complete;
}

uint32_t DhtLookup_GetClosestNodes(const DHT_ITERATIVE_LOOKUP *lookup,
                                    ROUTING_NODE *out, uint32_t max)
{
    uint32_t count = 0;

    // Return responded nodes in order (shortlist is already sorted by distance)
    for (uint32_t i = 0; i < lookup->shortlist_count && count < max; i++)
    {
        if (lookup->shortlist[i].status == DHT_ENTRY_RESPONDED)
        {
            memcpy(out[count].node_id, lookup->shortlist[i].node_id, 32);
            out[count].addr_type = lookup->shortlist[i].addr_type;
            memcpy(out[count].addr, lookup->shortlist[i].addr, 16);
            out[count].port = lookup->shortlist[i].port;
            out[count].last_seen = 0;
            out[count].fail_count = 0;
            count++;
        }
    }

    return count;
}
