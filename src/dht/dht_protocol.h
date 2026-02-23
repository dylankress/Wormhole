//
// dht_protocol.h
// Wormhole DHT - Wire Format Definitions (Kademlia-based)
// by Dylan Kress
//

#pragma once

#include <stdint.h>

// ============================================================================
// Kademlia parameters
// ============================================================================

#define DHT_K                   20      // Replication parameter / max bucket size
#define DHT_ALPHA               3       // Concurrency parameter for lookups
#define DHT_BUCKET_COUNT        256     // One bucket per bit of node ID
#define DHT_DEFAULT_PORT        4568    // Separate from MsQuic (4567)
#define DHT_PROTOCOL_VERSION    1
#define DHT_MAX_PENDING_RPCS    64
#define DHT_BUCKET_REFRESH_SEC  900     // 15 minutes
#define DHT_RPC_TIMEOUT_MS      5000    // 5 seconds
#define DHT_LOOKUP_TIMEOUT_SEC  30      // Wall-clock timeout for iterative lookups

// ============================================================================
// Address types (shared convention with relay protocol)
// ============================================================================

#define DHT_ADDR_IPV4   0x04
#define DHT_ADDR_IPV6   0x06

// ============================================================================
// DHT message types (0x20-0x27, no conflict with relay 0x01-0x0B)
// ============================================================================

#define DHT_MSG_PING                0x20
#define DHT_MSG_PONG                0x21
#define DHT_MSG_FIND_NODE           0x22
#define DHT_MSG_FIND_NODE_RESPONSE  0x23
#define DHT_MSG_FIND_VALUE          0x24
#define DHT_MSG_FIND_VALUE_RESPONSE 0x25
#define DHT_MSG_STORE               0x26
#define DHT_MSG_STORE_RESPONSE      0x27

// ============================================================================
// Wire format sizes
// ============================================================================

#define DHT_HEADER_SIZE         102     // 1 + 1 + 4 + 32 + 64
#define DHT_NODE_INFO_SIZE      51      // 32 + 1 + 16 + 2
#define DHT_SIGNATURE_OFFSET    38      // 1 + 1 + 4 + 32 = offset of signature

// ============================================================================
// Packed wire structures
// ============================================================================

#pragma pack(push, 1)

// Common header for all DHT messages (102 bytes)
// [1B msg_type][1B version][4B txn_id][32B sender_id][64B ed25519_signature]
typedef struct {
    uint8_t  msg_type;
    uint8_t  version;
    uint32_t txn_id;            // Little-endian transaction ID
    uint8_t  sender_id[32];     // Ed25519 public key
    uint8_t  signature[64];     // Ed25519 detached signature
} DHT_HEADER;

// Node contact info (51 bytes)
// [32B node_id][1B addr_type][16B addr][2B port]
typedef struct {
    uint8_t  node_id[32];       // Ed25519 public key
    uint8_t  addr_type;         // DHT_ADDR_IPV4 or DHT_ADDR_IPV6
    uint8_t  addr[16];          // IPv4 uses first 4 bytes
    uint16_t port;              // Little-endian UDP port
} DHT_NODE_INFO;

// PING: header only (102 bytes)
typedef struct {
    DHT_HEADER header;
} DHT_PING_MSG;

// PONG: header only (102 bytes)
typedef struct {
    DHT_HEADER header;
} DHT_PONG_MSG;

// FIND_NODE: header + 32-byte target (134 bytes)
typedef struct {
    DHT_HEADER header;
    uint8_t    target_id[32];
} DHT_FIND_NODE_MSG;

// FIND_NODE_RESPONSE: header + count + variable nodes
// Total with K=20 nodes: 102 + 1 + 20*51 = 1123 bytes (MTU safe)
typedef struct {
    DHT_HEADER header;
    uint8_t    node_count;      // Number of DHT_NODE_INFO following
    // Followed by node_count x DHT_NODE_INFO
} DHT_FIND_NODE_RESPONSE_MSG;

// FIND_VALUE: header + 32-byte key (134 bytes)
typedef struct {
    DHT_HEADER header;
    uint8_t    key[32];
} DHT_FIND_VALUE_MSG;

// FIND_VALUE_RESPONSE: header + found flag + count/data
typedef struct {
    DHT_HEADER header;
    uint8_t    found;           // 1 = value follows, 0 = nodes follow
    uint8_t    node_count;      // If found==0: number of closer nodes
    // If found==1: [4B value_size][value_data]
    // If found==0: node_count x DHT_NODE_INFO
} DHT_FIND_VALUE_RESPONSE_MSG;

// STORE: header + key + value size + variable data
typedef struct {
    DHT_HEADER header;
    uint8_t    key[32];
    uint32_t   value_size;      // Little-endian
    // Followed by value_size bytes of data
} DHT_STORE_MSG;

// STORE_RESPONSE: header + status (103 bytes)
typedef struct {
    DHT_HEADER header;
    uint8_t    status;          // 0 = success, 1 = error
} DHT_STORE_RESPONSE_MSG;

#pragma pack(pop)
