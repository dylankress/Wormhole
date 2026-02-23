//
// test_dht_lookup.c
// Unit tests for dht_lookup.c — Iterative Kademlia lookup.
//

#include "../dht/dht_lookup.h"
#include "greatest.h"
#include <string.h>
#include <time.h>

GREATEST_MAIN_DEFS();

// ============================================================================
// Helpers
// ============================================================================

static void fill_id(uint8_t id[32], uint8_t value)
{
    memset(id, value, 32);
}

// Create a routing table with known nodes for testing
static void setup_routing_table(ROUTING_TABLE *rt, uint8_t self_byte)
{
    uint8_t self_id[32];
    fill_id(self_id, self_byte);
    RoutingTable_Init(rt, self_id);

    // Add some nodes at various distances
    for (uint8_t i = 1; i <= 10; i++)
    {
        uint8_t node_id[32];
        fill_id(node_id, self_byte ^ i);  // XOR distance varies

        uint8_t addr[16] = { 0 };
        addr[0] = 10;
        addr[3] = i;

        RoutingTable_AddNode(rt, node_id, 0x04, addr, 4568 + i);
    }
}

// ============================================================================
// Tests
// ============================================================================

TEST test_lookup_start_seeds_shortlist(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    ASSERT(lookup.shortlist_count > 0);
    ASSERT_EQ(lookup.type, DHT_LOOKUP_NODE);
    ASSERT(memcmp(lookup.target, target, 32) == 0);
    ASSERT_FALSE(lookup.complete);
    ASSERT_FALSE(lookup.value_found);

    PASS();
}

TEST test_lookup_select_next_queries(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    DHT_SHORTLIST_ENTRY queries[DHT_ALPHA];
    uint32_t count = DhtLookup_SelectNextQueries(&lookup, queries, DHT_ALPHA);

    ASSERT(count > 0);
    ASSERT(count <= DHT_ALPHA);

    // All selected should be in PENDING state
    for (uint32_t i = 0; i < count; i++)
    {
        ASSERT_EQ(queries[i].status, DHT_ENTRY_PENDING);
    }

    PASS();
}

TEST test_lookup_mark_queried(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    // Mark first entry as queried
    DhtLookup_MarkQueried(&lookup, lookup.shortlist[0].node_id, 12345);

    ASSERT_EQ(lookup.shortlist[0].status, DHT_ENTRY_QUERIED);
    ASSERT_EQ(lookup.shortlist[0].txn_id, 12345u);
    ASSERT_EQ(lookup.queries_in_flight, 1u);

    PASS();
}

TEST test_lookup_handle_response_merges_nodes(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);
    uint32_t initial_count = lookup.shortlist_count;

    // Mark first as queried
    DhtLookup_MarkQueried(&lookup, lookup.shortlist[0].node_id, 1);

    // Simulate response with new nodes
    DHT_NODE_INFO new_nodes[2];
    memset(new_nodes, 0, sizeof(new_nodes));
    fill_id(new_nodes[0].node_id, 0x04);  // Close to target 0x05
    new_nodes[0].addr_type = 0x04;
    new_nodes[0].addr[0] = 10;
    new_nodes[0].port = 5000;
    fill_id(new_nodes[1].node_id, 0x06);
    new_nodes[1].addr_type = 0x04;
    new_nodes[1].addr[0] = 10;
    new_nodes[1].port = 5001;

    DhtLookup_HandleResponse(&lookup, lookup.shortlist[0].node_id,
                              new_nodes, 2, NULL, 0);

    // New nodes should be merged into shortlist
    ASSERT(lookup.shortlist_count >= initial_count);

    PASS();
}

TEST test_lookup_find_value_early_termination(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_VALUE, target);

    // Mark first as queried
    DhtLookup_MarkQueried(&lookup, lookup.shortlist[0].node_id, 1);

    // Simulate response with value found
    DHT_LOCATION locs[1];
    memset(locs, 0, sizeof(locs));
    fill_id(locs[0].node_id, 0xAA);
    locs[0].addr_type = 0x04;
    locs[0].port = 4568;

    DhtLookup_HandleResponse(&lookup, lookup.shortlist[0].node_id,
                              NULL, 0, locs, 1);

    ASSERT(lookup.value_found);
    ASSERT(lookup.complete);
    ASSERT_EQ(lookup.value_location_count, 1u);

    PASS();
}

TEST test_lookup_mark_failed(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);
    DhtLookup_MarkQueried(&lookup, lookup.shortlist[0].node_id, 1);

    ASSERT_EQ(lookup.queries_in_flight, 1u);

    DhtLookup_MarkFailed(&lookup, lookup.shortlist[0].node_id);

    ASSERT_EQ(lookup.shortlist[0].status, DHT_ENTRY_FAILED);
    ASSERT_EQ(lookup.queries_in_flight, 0u);

    PASS();
}

TEST test_lookup_max_iterations(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    // Simulate max iterations
    for (uint32_t iter = 0; iter < DHT_LOOKUP_MAX_ITERATIONS + 1; iter++)
    {
        if (lookup.complete) break;

        DHT_SHORTLIST_ENTRY queries[DHT_ALPHA];
        uint32_t count = DhtLookup_SelectNextQueries(&lookup, queries, DHT_ALPHA);
        if (count == 0) break;

        for (uint32_t i = 0; i < count; i++)
        {
            DhtLookup_MarkQueried(&lookup, queries[i].node_id, iter * 10 + i);
        }

        // Respond with no new nodes (just confirm)
        for (uint32_t i = 0; i < count; i++)
        {
            DhtLookup_HandleResponse(&lookup, queries[i].node_id,
                                      NULL, 0, NULL, 0);
        }
    }

    ASSERT(DhtLookup_IsComplete(&lookup));

    PASS();
}

TEST test_lookup_get_closest_nodes(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    // Mark some as queried and responded
    for (uint32_t i = 0; i < lookup.shortlist_count && i < 3; i++)
    {
        DhtLookup_MarkQueried(&lookup, lookup.shortlist[i].node_id, i);
        DhtLookup_HandleResponse(&lookup, lookup.shortlist[i].node_id,
                                  NULL, 0, NULL, 0);
    }

    ROUTING_NODE result[5];
    uint32_t count = DhtLookup_GetClosestNodes(&lookup, result, 5);
    ASSERT(count > 0);
    ASSERT(count <= 3u);

    PASS();
}

TEST test_lookup_empty_routing_table(void)
{
    uint8_t self_id[32];
    fill_id(self_id, 0x00);

    ROUTING_TABLE rt;
    RoutingTable_Init(&rt, self_id);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    ASSERT_EQ(lookup.shortlist_count, 0u);
    ASSERT(DhtLookup_IsComplete(&lookup));

    PASS();
}

TEST test_lookup_check_timeout(void)
{
    ROUTING_TABLE rt;
    setup_routing_table(&rt, 0x00);

    DHT_ITERATIVE_LOOKUP lookup;
    uint8_t target[32];
    fill_id(target, 0x05);

    DhtLookup_Start(&lookup, &rt, DHT_LOOKUP_NODE, target);

    // Should not be timed out immediately
    ASSERT_FALSE(DhtLookup_CheckTimeout(&lookup));
    ASSERT_FALSE(lookup.complete);

    // Backdate start_time to trigger timeout
    lookup.start_time = time(NULL) - DHT_LOOKUP_TIMEOUT_SEC - 1;

    ASSERT(DhtLookup_CheckTimeout(&lookup));
    ASSERT(lookup.complete);

    // Calling again on already-complete lookup should return FALSE
    ASSERT_FALSE(DhtLookup_CheckTimeout(&lookup));

    PASS();
}

// ============================================================================
// Test suite
// ============================================================================

SUITE(dht_lookup_suite)
{
    RUN_TEST(test_lookup_start_seeds_shortlist);
    RUN_TEST(test_lookup_select_next_queries);
    RUN_TEST(test_lookup_mark_queried);
    RUN_TEST(test_lookup_handle_response_merges_nodes);
    RUN_TEST(test_lookup_find_value_early_termination);
    RUN_TEST(test_lookup_mark_failed);
    RUN_TEST(test_lookup_max_iterations);
    RUN_TEST(test_lookup_get_closest_nodes);
    RUN_TEST(test_lookup_empty_routing_table);
    RUN_TEST(test_lookup_check_timeout);
}

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(dht_lookup_suite);
    GREATEST_MAIN_END();
}
