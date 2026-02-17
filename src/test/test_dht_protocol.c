//
// test_dht_protocol.c
// Unit tests for DHT protocol — packed struct sizes, wire format, sign/verify.
//

#include "../dht/dht_protocol.h"
#include "../relay/peer_id.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>

GREATEST_MAIN_DEFS();

// ============================================================================
// Struct size tests (verify packed wire format)
// ============================================================================

TEST test_header_size(void)
{
    // DHT_HEADER: 1 + 1 + 4 + 32 + 64 = 102
    ASSERT_EQ(sizeof(DHT_HEADER), 102u);
    PASS();
}

TEST test_node_info_size(void)
{
    // DHT_NODE_INFO: 32 + 1 + 16 + 2 = 51
    ASSERT_EQ(sizeof(DHT_NODE_INFO), 51u);
    PASS();
}

TEST test_ping_msg_size(void)
{
    // PING = header only
    ASSERT_EQ(sizeof(DHT_PING_MSG), 102u);
    PASS();
}

TEST test_pong_msg_size(void)
{
    ASSERT_EQ(sizeof(DHT_PONG_MSG), 102u);
    PASS();
}

TEST test_find_node_msg_size(void)
{
    // FIND_NODE: header(102) + target_id(32) = 134
    ASSERT_EQ(sizeof(DHT_FIND_NODE_MSG), 134u);
    PASS();
}

TEST test_find_node_response_header_size(void)
{
    // FIND_NODE_RESPONSE header: header(102) + node_count(1) = 103
    ASSERT_EQ(sizeof(DHT_FIND_NODE_RESPONSE_MSG), 103u);
    PASS();
}

TEST test_find_node_response_mtu_safe(void)
{
    // With K=20 nodes: 103 + 20*51 = 1123 bytes
    // Must fit in MTU (~1300 bytes with UDP/IP overhead)
    size_t full_response = sizeof(DHT_FIND_NODE_RESPONSE_MSG) + DHT_K * sizeof(DHT_NODE_INFO);
    ASSERT(full_response < 1300);
    ASSERT_EQ(full_response, 1123u);
    PASS();
}

TEST test_store_msg_header_size(void)
{
    // STORE: header(102) + key(32) + value_size(4) = 138
    ASSERT_EQ(sizeof(DHT_STORE_MSG), 138u);
    PASS();
}

TEST test_store_response_msg_size(void)
{
    // STORE_RESPONSE: header(102) + status(1) = 103
    ASSERT_EQ(sizeof(DHT_STORE_RESPONSE_MSG), 103u);
    PASS();
}

TEST test_header_field_offsets(void)
{
    DHT_HEADER hdr;
    uint8_t *base = (uint8_t *)&hdr;

    // msg_type at offset 0
    ASSERT_EQ((uint8_t *)&hdr.msg_type - base, 0);
    // version at offset 1
    ASSERT_EQ((uint8_t *)&hdr.version - base, 1);
    // txn_id at offset 2
    ASSERT_EQ((uint8_t *)&hdr.txn_id - base, 2);
    // sender_id at offset 6
    ASSERT_EQ((uint8_t *)&hdr.sender_id - base, 6);
    // signature at offset 38
    ASSERT_EQ((uint8_t *)&hdr.signature - base, 38);

    // Verify DHT_SIGNATURE_OFFSET constant matches
    ASSERT_EQ(DHT_SIGNATURE_OFFSET, 38);
    PASS();
}

SUITE(struct_size_suite)
{
    RUN_TEST(test_header_size);
    RUN_TEST(test_node_info_size);
    RUN_TEST(test_ping_msg_size);
    RUN_TEST(test_pong_msg_size);
    RUN_TEST(test_find_node_msg_size);
    RUN_TEST(test_find_node_response_header_size);
    RUN_TEST(test_find_node_response_mtu_safe);
    RUN_TEST(test_store_msg_header_size);
    RUN_TEST(test_store_response_msg_size);
    RUN_TEST(test_header_field_offsets);
}

// ============================================================================
// Sign / Verify tests
// ============================================================================

TEST test_sign_verify_roundtrip(void)
{
    KEYPAIR kp;
    ASSERT(PeerID_Generate(&kp));

    // Build a PING message
    DHT_PING_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msg_type = DHT_MSG_PING;
    msg.header.version = DHT_PROTOCOL_VERSION;
    msg.header.txn_id = 42;
    memcpy(msg.header.sender_id, kp.public_key, 32);

    // Sign: zero signature field, sign full message, copy signature back
    uint8_t sig[64];
    ASSERT(PeerID_Sign(&kp, (uint8_t *)&msg, sizeof(msg), sig));
    memcpy(msg.header.signature, sig, 64);

    // Verify: save signature, zero field, verify, restore
    uint8_t saved_sig[64];
    memcpy(saved_sig, msg.header.signature, 64);
    memset(msg.header.signature, 0, 64);
    ASSERT(PeerID_Verify(kp.public_key, (uint8_t *)&msg, sizeof(msg), saved_sig));
    memcpy(msg.header.signature, saved_sig, 64);

    PASS();
}

TEST test_tampered_message_fails_verify(void)
{
    KEYPAIR kp;
    ASSERT(PeerID_Generate(&kp));

    // Build and sign a PING message
    DHT_PING_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msg_type = DHT_MSG_PING;
    msg.header.version = DHT_PROTOCOL_VERSION;
    msg.header.txn_id = 42;
    memcpy(msg.header.sender_id, kp.public_key, 32);

    uint8_t sig[64];
    ASSERT(PeerID_Sign(&kp, (uint8_t *)&msg, sizeof(msg), sig));
    memcpy(msg.header.signature, sig, 64);

    // Tamper with the message (change msg_type)
    msg.header.msg_type = DHT_MSG_PONG;

    // Verify should fail
    uint8_t saved_sig[64];
    memcpy(saved_sig, msg.header.signature, 64);
    memset(msg.header.signature, 0, 64);
    ASSERT_FALSE(PeerID_Verify(kp.public_key, (uint8_t *)&msg, sizeof(msg), saved_sig));

    PASS();
}

TEST test_wrong_key_fails_verify(void)
{
    KEYPAIR kp1, kp2;
    ASSERT(PeerID_Generate(&kp1));
    ASSERT(PeerID_Generate(&kp2));

    // Sign with kp1
    DHT_PING_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msg_type = DHT_MSG_PING;
    msg.header.version = DHT_PROTOCOL_VERSION;
    msg.header.txn_id = 99;
    memcpy(msg.header.sender_id, kp1.public_key, 32);

    uint8_t sig[64];
    ASSERT(PeerID_Sign(&kp1, (uint8_t *)&msg, sizeof(msg), sig));
    memcpy(msg.header.signature, sig, 64);

    // Verify with kp2's public key — should fail
    uint8_t saved_sig[64];
    memcpy(saved_sig, msg.header.signature, 64);
    memset(msg.header.signature, 0, 64);
    ASSERT_FALSE(PeerID_Verify(kp2.public_key, (uint8_t *)&msg, sizeof(msg), saved_sig));

    PASS();
}

TEST test_find_node_sign_verify(void)
{
    KEYPAIR kp;
    ASSERT(PeerID_Generate(&kp));

    // Build a FIND_NODE message (larger than PING)
    DHT_FIND_NODE_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msg_type = DHT_MSG_FIND_NODE;
    msg.header.version = DHT_PROTOCOL_VERSION;
    msg.header.txn_id = 1234;
    memcpy(msg.header.sender_id, kp.public_key, 32);
    memset(msg.target_id, 0xBB, 32);

    // Sign
    uint8_t sig[64];
    ASSERT(PeerID_Sign(&kp, (uint8_t *)&msg, sizeof(msg), sig));
    memcpy(msg.header.signature, sig, 64);

    // Verify
    uint8_t saved_sig[64];
    memcpy(saved_sig, msg.header.signature, 64);
    memset(msg.header.signature, 0, 64);
    ASSERT(PeerID_Verify(kp.public_key, (uint8_t *)&msg, sizeof(msg), saved_sig));

    PASS();
}

SUITE(sign_verify_suite)
{
    RUN_TEST(test_sign_verify_roundtrip);
    RUN_TEST(test_tampered_message_fails_verify);
    RUN_TEST(test_wrong_key_fails_verify);
    RUN_TEST(test_find_node_sign_verify);
}

// ============================================================================
// Message type constants
// ============================================================================

TEST test_message_types_no_conflict(void)
{
    // DHT types (0x20-0x27) must not overlap with relay types (0x01-0x0B)
    ASSERT(DHT_MSG_PING >= 0x20);
    ASSERT(DHT_MSG_STORE_RESPONSE <= 0x27);
    ASSERT(DHT_MSG_PING > 0x0B);

    // Verify sequential numbering
    ASSERT_EQ(DHT_MSG_PING, 0x20);
    ASSERT_EQ(DHT_MSG_PONG, 0x21);
    ASSERT_EQ(DHT_MSG_FIND_NODE, 0x22);
    ASSERT_EQ(DHT_MSG_FIND_NODE_RESPONSE, 0x23);
    ASSERT_EQ(DHT_MSG_FIND_VALUE, 0x24);
    ASSERT_EQ(DHT_MSG_FIND_VALUE_RESPONSE, 0x25);
    ASSERT_EQ(DHT_MSG_STORE, 0x26);
    ASSERT_EQ(DHT_MSG_STORE_RESPONSE, 0x27);
    PASS();
}

SUITE(constants_suite)
{
    RUN_TEST(test_message_types_no_conflict);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    // Initialize libsodium (required for Ed25519 sign/verify)
    if (!PeerID_Init()) {
        fprintf(stderr, "FATAL: Failed to initialize libsodium\n");
        return 1;
    }

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(struct_size_suite);
    RUN_SUITE(sign_verify_suite);
    RUN_SUITE(constants_suite);
    GREATEST_MAIN_END();
}
