//
// routing_table.c
// Wormhole DHT - Kademlia K-Bucket Routing Table
// by Dylan Kress
//

#include "routing_table.h"
#include "../wire_format.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ============================================================================
// Internal helpers
// ============================================================================

// Compare two nodes by their XOR distance to target (ascending)
// Uses qsort_s context parameter for thread safety
static int CompareXorDistance(void *context, const void *a, const void *b)
{
    const uint8_t *target = (const uint8_t *)context;
    const ROUTING_NODE *na = (const ROUTING_NODE *)a;
    const ROUTING_NODE *nb = (const ROUTING_NODE *)b;
    for (int i = 0; i < 32; i++) {
        uint8_t da = na->node_id[i] ^ target[i];
        uint8_t db = nb->node_id[i] ^ target[i];
        if (da < db) return -1;
        if (da > db) return 1;
    }
    return 0;
}

// ============================================================================
// Public API
// ============================================================================

void RoutingTable_Init(ROUTING_TABLE *rt, const uint8_t self_id[32])
{
    memset(rt, 0, sizeof(ROUTING_TABLE));
    memcpy(rt->self_id, self_id, 32);
}

int RoutingTable_GetBucketIndex(const ROUTING_TABLE *rt, const uint8_t node_id[32])
{
    // XOR the IDs, find the highest differing bit
    // byte[0] = MSB, byte[31] = LSB
    // Bucket index = bit position of highest set bit in XOR (0 = LSB, 255 = MSB)
    for (int i = 0; i < 32; i++) {
        uint8_t xor_byte = rt->self_id[i] ^ node_id[i];
        if (xor_byte != 0) {
            // Find highest set bit in this byte (7 = MSB, 0 = LSB)
            int bit = 7;
            while (bit > 0 && !(xor_byte & (1 << bit))) {
                bit--;
            }
            // Bucket index: byte[0] bit 7 = 255, byte[31] bit 0 = 0
            return (31 - i) * 8 + bit;
        }
    }
    return -1;  // Same ID
}

BOOLEAN RoutingTable_AddNode(ROUTING_TABLE *rt, const uint8_t node_id[32],
                              uint8_t addr_type, const uint8_t addr[16], uint16_t port)
{
    // Don't add ourselves
    if (memcmp(rt->self_id, node_id, 32) == 0) return FALSE;

    int bucket_idx = RoutingTable_GetBucketIndex(rt, node_id);
    if (bucket_idx < 0) return FALSE;

    K_BUCKET *bucket = &rt->buckets[bucket_idx];

    // Check if node already exists — update it
    for (uint32_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].node_id, node_id, 32) == 0) {
            bucket->nodes[i].addr_type = addr_type;
            memcpy(bucket->nodes[i].addr, addr, 16);
            bucket->nodes[i].port = port;
            bucket->nodes[i].last_seen = time(NULL);
            bucket->nodes[i].fail_count = 0;
            return TRUE;
        }
    }

    // Bucket not full — append
    if (bucket->count < DHT_K) {
        ROUTING_NODE *node = &bucket->nodes[bucket->count++];
        memcpy(node->node_id, node_id, 32);
        node->addr_type = addr_type;
        memcpy(node->addr, addr, 16);
        node->port = port;
        node->last_seen = time(NULL);
        node->fail_count = 0;
        return TRUE;
    }

    // Bucket full — find the Least Recently Seen (LRS) node
    uint32_t lrs_idx = 0;
    for (uint32_t i = 1; i < bucket->count; i++) {
        if (bucket->nodes[i].last_seen < bucket->nodes[lrs_idx].last_seen) {
            lrs_idx = i;
        }
    }

    // Only evict if the LRS node has failed
    if (bucket->nodes[lrs_idx].fail_count > 0) {
        ROUTING_NODE *node = &bucket->nodes[lrs_idx];
        memcpy(node->node_id, node_id, 32);
        node->addr_type = addr_type;
        memcpy(node->addr, addr, 16);
        node->port = port;
        node->last_seen = time(NULL);
        node->fail_count = 0;
        return TRUE;
    }

    // All nodes healthy — discard new node
    return FALSE;
}

uint32_t RoutingTable_FindClosest(const ROUTING_TABLE *rt, const uint8_t target[32],
                                   ROUTING_NODE *out, uint32_t max)
{
    if (max == 0) return 0;

    uint32_t total = RoutingTable_GetTotalNodes(rt);
    if (total == 0) return 0;

    // Collect all nodes
    ROUTING_NODE *all = (ROUTING_NODE *)malloc(total * sizeof(ROUTING_NODE));
    if (!all) return 0;

    uint32_t idx = 0;
    for (int b = 0; b < DHT_BUCKET_COUNT; b++) {
        const K_BUCKET *bucket = &rt->buckets[b];
        for (uint32_t n = 0; n < bucket->count; n++) {
            all[idx++] = bucket->nodes[n];
        }
    }

    // Sort by XOR distance to target (qsort_s for thread safety)
    qsort_s(all, idx, sizeof(ROUTING_NODE), CompareXorDistance, (void *)target);

    // Copy result
    uint32_t result = (idx < max) ? idx : max;
    memcpy(out, all, result * sizeof(ROUTING_NODE));
    free(all);
    return result;
}

void RoutingTable_TouchNode(ROUTING_TABLE *rt, const uint8_t node_id[32])
{
    int bucket_idx = RoutingTable_GetBucketIndex(rt, node_id);
    if (bucket_idx < 0) return;

    K_BUCKET *bucket = &rt->buckets[bucket_idx];
    for (uint32_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].node_id, node_id, 32) == 0) {
            bucket->nodes[i].last_seen = time(NULL);
            bucket->nodes[i].fail_count = 0;
            return;
        }
    }
}

void RoutingTable_MarkFailed(ROUTING_TABLE *rt, const uint8_t node_id[32])
{
    int bucket_idx = RoutingTable_GetBucketIndex(rt, node_id);
    if (bucket_idx < 0) return;

    K_BUCKET *bucket = &rt->buckets[bucket_idx];
    for (uint32_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].node_id, node_id, 32) == 0) {
            bucket->nodes[i].fail_count++;
            return;
        }
    }
}

BOOLEAN RoutingTable_RemoveNode(ROUTING_TABLE *rt, const uint8_t node_id[32])
{
    int bucket_idx = RoutingTable_GetBucketIndex(rt, node_id);
    if (bucket_idx < 0) return FALSE;

    K_BUCKET *bucket = &rt->buckets[bucket_idx];
    for (uint32_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].node_id, node_id, 32) == 0) {
            // Shift remaining nodes down
            for (uint32_t j = i; j < bucket->count - 1; j++) {
                bucket->nodes[j] = bucket->nodes[j + 1];
            }
            bucket->count--;
            return TRUE;
        }
    }
    return FALSE;
}

uint32_t RoutingTable_GetTotalNodes(const ROUTING_TABLE *rt)
{
    uint32_t total = 0;
    for (int i = 0; i < DHT_BUCKET_COUNT; i++) {
        total += rt->buckets[i].count;
    }
    return total;
}

// ============================================================================
// Persistence
// ============================================================================

// File format:
//   [4B magic "DHRT"]
//   [4B version (LE)]
//   [32B self_id]
//   [4B node_count (LE)]
//   Per node (59 bytes each):
//     [32B node_id][1B addr_type][16B addr][2B port (LE)][8B last_seen (LE)]

static const uint8_t RT_FILE_MAGIC[4] = { 'D', 'H', 'R', 'T' };
#define RT_FILE_VERSION     1
#define RT_NODE_RECORD_SIZE 59  // 32 + 1 + 16 + 2 + 8

BOOLEAN RoutingTable_Save(const ROUTING_TABLE *rt, const char *path)
{
    // Write to temp file first, then atomic rename
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return FALSE;

    uint8_t buf[8];

    // Magic
    if (fwrite(RT_FILE_MAGIC, 1, 4, f) != 4) { fclose(f); remove(tmp_path); return FALSE; }

    // Version
    WriteUint32LE(buf, RT_FILE_VERSION);
    if (fwrite(buf, 1, 4, f) != 4) { fclose(f); remove(tmp_path); return FALSE; }

    // Self ID
    if (fwrite(rt->self_id, 1, 32, f) != 32) { fclose(f); remove(tmp_path); return FALSE; }

    // Total node count
    uint32_t total = RoutingTable_GetTotalNodes(rt);
    WriteUint32LE(buf, total);
    if (fwrite(buf, 1, 4, f) != 4) { fclose(f); remove(tmp_path); return FALSE; }

    // Write each node
    for (int b = 0; b < DHT_BUCKET_COUNT; b++) {
        const K_BUCKET *bucket = &rt->buckets[b];
        for (uint32_t n = 0; n < bucket->count; n++) {
            const ROUTING_NODE *node = &bucket->nodes[n];
            if (fwrite(node->node_id, 1, 32, f) != 32) { fclose(f); remove(tmp_path); return FALSE; }
            if (fwrite(&node->addr_type, 1, 1, f) != 1) { fclose(f); remove(tmp_path); return FALSE; }
            if (fwrite(node->addr, 1, 16, f) != 16) { fclose(f); remove(tmp_path); return FALSE; }
            WriteUint16LE(buf, node->port);
            if (fwrite(buf, 1, 2, f) != 2) { fclose(f); remove(tmp_path); return FALSE; }
            WriteUint64LE(buf, (uint64_t)node->last_seen);
            if (fwrite(buf, 1, 8, f) != 8) { fclose(f); remove(tmp_path); return FALSE; }
        }
    }

    fclose(f);

    // Atomic rename: replace original with temp file
#ifdef _WIN32
    MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING);
#else
    rename(tmp_path, path);
#endif
    return TRUE;
}

BOOLEAN RoutingTable_Load(ROUTING_TABLE *rt, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;

    uint8_t buf[8];

    // Read and verify magic
    if (fread(buf, 1, 4, f) != 4 || memcmp(buf, RT_FILE_MAGIC, 4) != 0) {
        fclose(f);
        return FALSE;
    }

    // Read and verify version
    if (fread(buf, 1, 4, f) != 4) { fclose(f); return FALSE; }
    uint32_t version = ReadUint32LE(buf);
    if (version != RT_FILE_VERSION) { fclose(f); return FALSE; }

    // Read self_id
    uint8_t self_id[32];
    if (fread(self_id, 1, 32, f) != 32) { fclose(f); return FALSE; }

    // Re-initialize table with loaded self_id
    RoutingTable_Init(rt, self_id);

    // Read node count
    if (fread(buf, 1, 4, f) != 4) { fclose(f); return FALSE; }
    uint32_t count = ReadUint32LE(buf);

    // Read each node and place directly in correct bucket
    for (uint32_t i = 0; i < count; i++) {
        uint8_t rec[RT_NODE_RECORD_SIZE];
        if (fread(rec, 1, RT_NODE_RECORD_SIZE, f) != RT_NODE_RECORD_SIZE) break;

        uint8_t *node_id   = rec;           // 32 bytes
        uint8_t  addr_type = rec[32];        // 1 byte
        uint8_t *addr      = rec + 33;       // 16 bytes
        uint16_t port      = ReadUint16LE(rec + 49);   // 2 bytes
        uint64_t last_seen = ReadUint64LE(rec + 51);    // 8 bytes

        int bucket_idx = RoutingTable_GetBucketIndex(rt, node_id);
        if (bucket_idx < 0) continue;

        K_BUCKET *bucket = &rt->buckets[bucket_idx];
        if (bucket->count >= DHT_K) continue;

        ROUTING_NODE *node = &bucket->nodes[bucket->count++];
        memcpy(node->node_id, node_id, 32);
        node->addr_type = addr_type;
        memcpy(node->addr, addr, 16);
        node->port = port;
        node->last_seen = (time_t)last_seen;
        node->fail_count = 0;
    }

    fclose(f);
    return TRUE;
}
