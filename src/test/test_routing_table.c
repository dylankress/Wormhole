//
// test_routing_table.c
// Unit tests for DHT routing table — bucket index, add/evict, find, save/load.
//

#include "../dht/routing_table.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>

GREATEST_MAIN_DEFS();

// ============================================================================
// Helper: create a node ID with a specific byte set
// ============================================================================

static void make_id(uint8_t id[32], uint8_t byte_idx, uint8_t value)
{
    memset(id, 0, 32);
    id[byte_idx] = value;
}

// ============================================================================
// Bucket index tests
// ============================================================================

TEST test_bucket_index_same_id(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0xAA, 32);
    RoutingTable_Init(&rt, self_id);

    // Same ID → -1
    ASSERT_EQ(RoutingTable_GetBucketIndex(&rt, self_id), -1);
    PASS();
}

TEST test_bucket_index_msb_differ(void)
{
    // self_id = all zeros, node_id has MSB set → bucket 255
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    memset(node_id, 0, 32);
    node_id[0] = 0x80;  // bit 7 of byte[0] → position (31-0)*8 + 7 = 255

    ASSERT_EQ(RoutingTable_GetBucketIndex(&rt, node_id), 255);
    PASS();
}

TEST test_bucket_index_lsb_differ(void)
{
    // self_id = all zeros, node_id has only LSB set → bucket 0
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    memset(node_id, 0, 32);
    node_id[31] = 0x01;  // bit 0 of byte[31] → position (31-31)*8 + 0 = 0

    ASSERT_EQ(RoutingTable_GetBucketIndex(&rt, node_id), 0);
    PASS();
}

TEST test_bucket_index_middle_bit(void)
{
    // bit 4 of byte[16] → position (31-16)*8 + 4 = 124
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    memset(node_id, 0, 32);
    node_id[16] = 0x10;  // bit 4

    ASSERT_EQ(RoutingTable_GetBucketIndex(&rt, node_id), 124);
    PASS();
}

TEST test_bucket_index_nonzero_self(void)
{
    // self_id = 0xFF all, node_id = 0xFF except byte[31] = 0xFE
    // XOR = 0x00...01 → bucket 0
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0xFF, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    memset(node_id, 0xFF, 32);
    node_id[31] = 0xFE;  // XOR with 0xFF = 0x01 → bucket 0

    ASSERT_EQ(RoutingTable_GetBucketIndex(&rt, node_id), 0);
    PASS();
}

SUITE(bucket_index_suite)
{
    RUN_TEST(test_bucket_index_same_id);
    RUN_TEST(test_bucket_index_msb_differ);
    RUN_TEST(test_bucket_index_lsb_differ);
    RUN_TEST(test_bucket_index_middle_bit);
    RUN_TEST(test_bucket_index_nonzero_self);
}

// ============================================================================
// Add node tests
// ============================================================================

TEST test_add_single_node(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    make_id(node_id, 0, 0x80);  // bucket 255
    uint8_t addr[16];
    memset(addr, 0, 16);
    addr[0] = 10; addr[1] = 0; addr[2] = 0; addr[3] = 1;

    ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, 4568));
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 1u);
    PASS();
}

TEST test_add_self_rejected(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};
    ASSERT_FALSE(RoutingTable_AddNode(&rt, self_id, DHT_ADDR_IPV4, addr, 4568));
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 0u);
    PASS();
}

TEST test_add_updates_existing(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    make_id(node_id, 0, 0x80);

    uint8_t addr1[16] = {0};
    addr1[0] = 10;
    ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr1, 1000));
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 1u);

    // Add same ID with different port — should update, not add
    uint8_t addr2[16] = {0};
    addr2[0] = 10;
    ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr2, 2000));
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 1u);

    // Verify port was updated
    ROUTING_NODE closest[1];
    uint32_t count = RoutingTable_FindClosest(&rt, node_id, closest, 1);
    ASSERT_EQ(count, 1u);
    ASSERT_EQ(closest[0].port, 2000);
    PASS();
}

TEST test_fill_bucket(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Add DHT_K (20) nodes to bucket 255 (byte[0] = 0x80..0x93)
    for (int i = 0; i < DHT_K; i++) {
        uint8_t node_id[32];
        make_id(node_id, 0, (uint8_t)(0x80 + i));
        ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, (uint16_t)(4568 + i)));
    }

    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), (uint32_t)DHT_K);
    ASSERT_EQ(rt.buckets[255].count, (uint32_t)DHT_K);
    PASS();
}

TEST test_bucket_full_evict_failed(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Fill bucket 255 with 20 nodes
    for (int i = 0; i < DHT_K; i++) {
        uint8_t node_id[32];
        make_id(node_id, 0, (uint8_t)(0x80 + i));
        ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, (uint16_t)(4568 + i)));
    }

    // Mark the first node (LRS with index 0) as failed
    uint8_t failed_id[32];
    make_id(failed_id, 0, 0x80);
    RoutingTable_MarkFailed(&rt, failed_id);

    // Add a 21st node — should evict the failed node
    uint8_t new_id[32];
    make_id(new_id, 0, 0x94);
    ASSERT(RoutingTable_AddNode(&rt, new_id, DHT_ADDR_IPV4, addr, 9999));

    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), (uint32_t)DHT_K);

    // Verify the new node is present
    ROUTING_NODE closest[DHT_K];
    uint32_t count = RoutingTable_FindClosest(&rt, new_id, closest, DHT_K);
    BOOLEAN found_new = FALSE;
    BOOLEAN found_old = FALSE;
    for (uint32_t i = 0; i < count; i++) {
        if (memcmp(closest[i].node_id, new_id, 32) == 0) found_new = TRUE;
        if (memcmp(closest[i].node_id, failed_id, 32) == 0) found_old = TRUE;
    }
    ASSERT(found_new);
    ASSERT_FALSE(found_old);
    PASS();
}

TEST test_bucket_full_all_healthy(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Fill bucket 255 with 20 healthy nodes
    for (int i = 0; i < DHT_K; i++) {
        uint8_t node_id[32];
        make_id(node_id, 0, (uint8_t)(0x80 + i));
        ASSERT(RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, (uint16_t)(4568 + i)));
    }

    // Try to add a 21st node — should be discarded
    uint8_t new_id[32];
    make_id(new_id, 0, 0x94);
    ASSERT_FALSE(RoutingTable_AddNode(&rt, new_id, DHT_ADDR_IPV4, addr, 9999));

    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), (uint32_t)DHT_K);
    PASS();
}

SUITE(add_node_suite)
{
    RUN_TEST(test_add_single_node);
    RUN_TEST(test_add_self_rejected);
    RUN_TEST(test_add_updates_existing);
    RUN_TEST(test_fill_bucket);
    RUN_TEST(test_bucket_full_evict_failed);
    RUN_TEST(test_bucket_full_all_healthy);
}

// ============================================================================
// FindClosest tests
// ============================================================================

TEST test_find_closest_sorted(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Add 3 nodes at different distances from zero
    // node A: id = {0,...,0x01} → distance 1
    // node B: id = {0,...,0x04} → distance 4
    // node C: id = {0,...,0x02} → distance 2
    uint8_t id_a[32], id_b[32], id_c[32];
    make_id(id_a, 31, 0x01);
    make_id(id_b, 31, 0x04);
    make_id(id_c, 31, 0x02);

    RoutingTable_AddNode(&rt, id_a, DHT_ADDR_IPV4, addr, 1000);
    RoutingTable_AddNode(&rt, id_b, DHT_ADDR_IPV4, addr, 2000);
    RoutingTable_AddNode(&rt, id_c, DHT_ADDR_IPV4, addr, 3000);

    // Find closest to all-zeros target (same as self)
    uint8_t target[32];
    memset(target, 0, 32);
    ROUTING_NODE closest[3];
    uint32_t count = RoutingTable_FindClosest(&rt, target, closest, 3);

    ASSERT_EQ(count, 3u);
    // Should be sorted: A (dist 1), C (dist 2), B (dist 4)
    ASSERT_MEM_EQ(closest[0].node_id, id_a, 32);
    ASSERT_MEM_EQ(closest[1].node_id, id_c, 32);
    ASSERT_MEM_EQ(closest[2].node_id, id_b, 32);
    PASS();
}

TEST test_find_closest_across_buckets(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Node in bucket 0: {0,...,0x01} → distance 1
    uint8_t id_near[32];
    make_id(id_near, 31, 0x01);

    // Node in bucket 7: {0,...,0x80} → distance 128
    uint8_t id_mid[32];
    make_id(id_mid, 31, 0x80);

    // Node in bucket 255: {0x80,...,0} → very far
    uint8_t id_far[32];
    make_id(id_far, 0, 0x80);

    RoutingTable_AddNode(&rt, id_near, DHT_ADDR_IPV4, addr, 1000);
    RoutingTable_AddNode(&rt, id_mid, DHT_ADDR_IPV4, addr, 2000);
    RoutingTable_AddNode(&rt, id_far, DHT_ADDR_IPV4, addr, 3000);

    uint8_t target[32];
    memset(target, 0, 32);
    ROUTING_NODE closest[3];
    uint32_t count = RoutingTable_FindClosest(&rt, target, closest, 3);

    ASSERT_EQ(count, 3u);
    // Near (1), mid (128), far (huge)
    ASSERT_MEM_EQ(closest[0].node_id, id_near, 32);
    ASSERT_MEM_EQ(closest[1].node_id, id_mid, 32);
    ASSERT_MEM_EQ(closest[2].node_id, id_far, 32);
    PASS();
}

TEST test_find_closest_max_limit(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};

    // Add 5 nodes
    for (int i = 1; i <= 5; i++) {
        uint8_t node_id[32];
        make_id(node_id, 31, (uint8_t)i);
        RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, (uint16_t)(1000 + i));
    }

    // Request only 2
    ROUTING_NODE closest[2];
    uint32_t count = RoutingTable_FindClosest(&rt, self_id, closest, 2);
    ASSERT_EQ(count, 2u);
    PASS();
}

TEST test_find_closest_empty(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    ROUTING_NODE closest[5];
    uint32_t count = RoutingTable_FindClosest(&rt, self_id, closest, 5);
    ASSERT_EQ(count, 0u);
    PASS();
}

SUITE(find_closest_suite)
{
    RUN_TEST(test_find_closest_sorted);
    RUN_TEST(test_find_closest_across_buckets);
    RUN_TEST(test_find_closest_max_limit);
    RUN_TEST(test_find_closest_empty);
}

// ============================================================================
// TouchNode / MarkFailed / RemoveNode tests
// ============================================================================

TEST test_touch_node(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    make_id(node_id, 0, 0x80);
    uint8_t addr[16] = {0};

    RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, 4568);

    // Record initial last_seen
    time_t initial = rt.buckets[255].nodes[0].last_seen;

    // Touch should update (or at least not decrease) last_seen
    RoutingTable_TouchNode(&rt, node_id);
    ASSERT(rt.buckets[255].nodes[0].last_seen >= initial);
    ASSERT_EQ(rt.buckets[255].nodes[0].fail_count, 0u);
    PASS();
}

TEST test_mark_failed(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    make_id(node_id, 0, 0x80);
    uint8_t addr[16] = {0};

    RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, 4568);
    ASSERT_EQ(rt.buckets[255].nodes[0].fail_count, 0u);

    RoutingTable_MarkFailed(&rt, node_id);
    ASSERT_EQ(rt.buckets[255].nodes[0].fail_count, 1u);

    RoutingTable_MarkFailed(&rt, node_id);
    ASSERT_EQ(rt.buckets[255].nodes[0].fail_count, 2u);
    PASS();
}

TEST test_remove_node(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t id1[32], id2[32];
    make_id(id1, 0, 0x80);
    make_id(id2, 0, 0x81);
    uint8_t addr[16] = {0};

    RoutingTable_AddNode(&rt, id1, DHT_ADDR_IPV4, addr, 1000);
    RoutingTable_AddNode(&rt, id2, DHT_ADDR_IPV4, addr, 2000);
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 2u);

    ASSERT(RoutingTable_RemoveNode(&rt, id1));
    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 1u);

    // id2 should still be there
    ROUTING_NODE closest[1];
    uint32_t count = RoutingTable_FindClosest(&rt, id2, closest, 1);
    ASSERT_EQ(count, 1u);
    ASSERT_MEM_EQ(closest[0].node_id, id2, 32);
    PASS();
}

TEST test_remove_nonexistent(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t node_id[32];
    make_id(node_id, 0, 0x80);
    ASSERT_FALSE(RoutingTable_RemoveNode(&rt, node_id));
    PASS();
}

SUITE(node_ops_suite)
{
    RUN_TEST(test_touch_node);
    RUN_TEST(test_mark_failed);
    RUN_TEST(test_remove_node);
    RUN_TEST(test_remove_nonexistent);
}

// ============================================================================
// GetTotalNodes
// ============================================================================

TEST test_get_total_nodes(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0, 32);
    RoutingTable_Init(&rt, self_id);

    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 0u);

    uint8_t addr[16] = {0};

    // Add nodes across different buckets
    uint8_t id1[32], id2[32], id3[32];
    make_id(id1, 31, 0x01);  // bucket 0
    make_id(id2, 31, 0x80);  // bucket 7
    make_id(id3, 0, 0x80);   // bucket 255

    RoutingTable_AddNode(&rt, id1, DHT_ADDR_IPV4, addr, 1000);
    RoutingTable_AddNode(&rt, id2, DHT_ADDR_IPV4, addr, 2000);
    RoutingTable_AddNode(&rt, id3, DHT_ADDR_IPV4, addr, 3000);

    ASSERT_EQ(RoutingTable_GetTotalNodes(&rt), 3u);
    PASS();
}

SUITE(total_nodes_suite)
{
    RUN_TEST(test_get_total_nodes);
}

// ============================================================================
// Save / Load roundtrip
// ============================================================================

TEST test_save_load_roundtrip(void)
{
    ROUTING_TABLE rt;
    uint8_t self_id[32];
    memset(self_id, 0x42, 32);
    RoutingTable_Init(&rt, self_id);

    uint8_t addr[16] = {0};
    addr[0] = 192; addr[1] = 168; addr[2] = 1;

    // Add 5 nodes across different buckets
    for (int i = 0; i < 5; i++) {
        uint8_t node_id[32];
        memset(node_id, 0x42, 32);
        node_id[0] = (uint8_t)(0x42 ^ (0x80 >> i));  // Different high bits
        addr[3] = (uint8_t)(i + 1);
        RoutingTable_AddNode(&rt, node_id, DHT_ADDR_IPV4, addr, (uint16_t)(5000 + i));
    }

    uint32_t original_count = RoutingTable_GetTotalNodes(&rt);
    ASSERT(original_count > 0);

    // Save
    const char *path = "test_rt_roundtrip.bin";
    ASSERT(RoutingTable_Save(&rt, path));

    // Load into a fresh table
    ROUTING_TABLE loaded;
    ASSERT(RoutingTable_Load(&loaded, path));

    // Verify self_id
    ASSERT_MEM_EQ(loaded.self_id, self_id, 32);

    // Verify node count
    ASSERT_EQ(RoutingTable_GetTotalNodes(&loaded), original_count);

    // Verify each node is present with correct data
    for (int b = 0; b < DHT_BUCKET_COUNT; b++) {
        ASSERT_EQ(rt.buckets[b].count, loaded.buckets[b].count);
        for (uint32_t n = 0; n < rt.buckets[b].count; n++) {
            ASSERT_MEM_EQ(rt.buckets[b].nodes[n].node_id,
                          loaded.buckets[b].nodes[n].node_id, 32);
            ASSERT_EQ(rt.buckets[b].nodes[n].addr_type,
                       loaded.buckets[b].nodes[n].addr_type);
            ASSERT_MEM_EQ(rt.buckets[b].nodes[n].addr,
                          loaded.buckets[b].nodes[n].addr, 16);
            ASSERT_EQ(rt.buckets[b].nodes[n].port,
                       loaded.buckets[b].nodes[n].port);
        }
    }

    // Cleanup
    remove(path);
    PASS();
}

TEST test_load_nonexistent_file(void)
{
    ROUTING_TABLE rt;
    ASSERT_FALSE(RoutingTable_Load(&rt, "nonexistent_file_12345.bin"));
    PASS();
}

TEST test_load_invalid_magic(void)
{
    // Write garbage to a file
    const char *path = "test_rt_bad_magic.bin";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fwrite("JUNK", 1, 4, f);
    fclose(f);

    ROUTING_TABLE rt;
    ASSERT_FALSE(RoutingTable_Load(&rt, path));
    remove(path);
    PASS();
}

SUITE(save_load_suite)
{
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_load_nonexistent_file);
    RUN_TEST(test_load_invalid_magic);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(bucket_index_suite);
    RUN_SUITE(add_node_suite);
    RUN_SUITE(find_closest_suite);
    RUN_SUITE(node_ops_suite);
    RUN_SUITE(total_nodes_suite);
    RUN_SUITE(save_load_suite);
    GREATEST_MAIN_END();
}
