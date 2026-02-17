//
// dht_node.c
// Wormhole DHT - Node (UDP transport, RPC dispatch, bootstrap)
// by Dylan Kress
//

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#endif

#include "dht_node.h"
#include "dht_lookup.h"
#include "../wire_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DHT_MAX_PACKET_SIZE 2048

// ============================================================================
// Internal helpers: path utilities
// ============================================================================

static BOOLEAN GetDhtRoutingPath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\dht_routing_table.bin", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/dht_routing_table.bin", home);
#endif
    return TRUE;
}

static void EnsureWormholeDir(void)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s\\.wormhole", home);
    CreateDirectoryA(path, NULL);
#else
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.wormhole", home);
    mkdir(path, 0755);
#endif
}

// ============================================================================
// Internal helpers: address conversion
// ============================================================================

static void SockaddrToNodeInfo(const struct sockaddr_storage *addr,
                                uint8_t *addr_type, uint8_t *addr_out, uint16_t *port)
{
    memset(addr_out, 0, 16);
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        *addr_type = DHT_ADDR_IPV4;
        memcpy(addr_out, &sin->sin_addr, 4);
        *port = ntohs(sin->sin_port);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
        *addr_type = DHT_ADDR_IPV6;
        memcpy(addr_out, &sin6->sin6_addr, 16);
        *port = ntohs(sin6->sin6_port);
    }
}

static BOOLEAN NodeInfoToSockaddr(uint8_t addr_type, const uint8_t *addr, uint16_t port,
                                   struct sockaddr_storage *out, socklen_t *out_len)
{
    memset(out, 0, sizeof(*out));
    if (addr_type == DHT_ADDR_IPV4) {
        struct sockaddr_in *sin = (struct sockaddr_in *)out;
        sin->sin_family = AF_INET;
        memcpy(&sin->sin_addr, addr, 4);
        sin->sin_port = htons(port);
        *out_len = sizeof(struct sockaddr_in);
        return TRUE;
    } else if (addr_type == DHT_ADDR_IPV6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)out;
        sin6->sin6_family = AF_INET6;
        memcpy(&sin6->sin6_addr, addr, 16);
        sin6->sin6_port = htons(port);
        *out_len = sizeof(struct sockaddr_in6);
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Internal helpers: message signing
// ============================================================================

// Sign a DHT message in-place (zeros signature field, signs, copies sig back)
static BOOLEAN SignMessage(const KEYPAIR *keypair, uint8_t *msg, size_t msg_len)
{
    if (msg_len < DHT_HEADER_SIZE) return FALSE;

    // Zero signature field before signing
    memset(msg + DHT_SIGNATURE_OFFSET, 0, 64);

    // Sign the whole message
    uint8_t sig[64];
    if (!PeerID_Sign(keypair, msg, msg_len, sig)) return FALSE;

    // Copy signature into message
    memcpy(msg + DHT_SIGNATURE_OFFSET, sig, 64);
    return TRUE;
}

// Verify a DHT message signature (saves/restores signature field)
static BOOLEAN VerifyMessage(const uint8_t *sender_id, uint8_t *msg, size_t msg_len)
{
    if (msg_len < DHT_HEADER_SIZE) return FALSE;

    // Save the signature
    uint8_t sig[64];
    memcpy(sig, msg + DHT_SIGNATURE_OFFSET, 64);

    // Zero the signature field for verification
    memset(msg + DHT_SIGNATURE_OFFSET, 0, 64);

    // Verify
    BOOLEAN result = PeerID_Verify(sender_id, msg, msg_len, sig) ? TRUE : FALSE;

    // Restore the signature
    memcpy(msg + DHT_SIGNATURE_OFFSET, sig, 64);
    return result;
}

// ============================================================================
// Internal helpers: UDP transport
// ============================================================================

static BOOLEAN SendRawUDP(DHT_NODE *node, const struct sockaddr_storage *addr,
                           socklen_t addr_len, const uint8_t *data, size_t len)
{
    int sent = sendto(node->socket_fd, (const char *)data, (int)len, 0,
                      (const struct sockaddr *)addr, (int)addr_len);
    return (sent > 0) ? TRUE : FALSE;
}

// ============================================================================
// Internal helpers: header / RPC management
// ============================================================================

static void FillHeader(DHT_HEADER *hdr, DHT_NODE *node, uint8_t msg_type)
{
    hdr->msg_type = msg_type;
    hdr->version = DHT_PROTOCOL_VERSION;
    hdr->txn_id = node->next_txn_id++;
    memcpy(hdr->sender_id, node->keypair->public_key, 32);
    memset(hdr->signature, 0, 64);
}

static BOOLEAN AddPendingRPC(DHT_NODE *node, uint32_t txn_id, uint8_t msg_type,
                              const uint8_t *target_id)
{
    if (node->pending_count >= DHT_MAX_PENDING_RPCS) return FALSE;

    DHT_PENDING_RPC *rpc = &node->pending_rpcs[node->pending_count++];
    rpc->txn_id = txn_id;
    rpc->msg_type = msg_type;
    if (target_id) {
        memcpy(rpc->target_id, target_id, 32);
    } else {
        memset(rpc->target_id, 0, 32);
    }
    rpc->sent_at = time(NULL);
    rpc->callback_ctx = NULL;
    return TRUE;
}

static int FindPendingRPC(DHT_NODE *node, uint32_t txn_id)
{
    for (uint32_t i = 0; i < node->pending_count; i++) {
        if (node->pending_rpcs[i].txn_id == txn_id) {
            return (int)i;
        }
    }
    return -1;
}

static void RemovePendingRPC(DHT_NODE *node, uint32_t index)
{
    if (node->pending_count == 0) return;
    if (index < node->pending_count - 1) {
        node->pending_rpcs[index] = node->pending_rpcs[node->pending_count - 1];
    }
    node->pending_count--;
}

// ============================================================================
// Internal helpers: bucket refresh
// ============================================================================

// Generate a random node ID that falls in a specific bucket
static void GenerateRandomIdForBucket(const uint8_t self_id[32], int bucket, uint8_t out[32])
{
    int byte_idx = 31 - (bucket / 8);
    int bit_idx = bucket % 8;

    // Build XOR distance with highest bit at 'bucket'
    uint8_t xor_dist[32];
    memset(xor_dist, 0, 32);

    // Set the target bit
    xor_dist[byte_idx] = (uint8_t)(1 << bit_idx);

    // Randomize lower bits in the same byte
    for (int b = 0; b < bit_idx; b++) {
        if (rand() & 1) {
            xor_dist[byte_idx] |= (uint8_t)(1 << b);
        }
    }

    // Randomize all lower bytes
    for (int i = byte_idx + 1; i < 32; i++) {
        xor_dist[i] = (uint8_t)(rand() & 0xFF);
    }

    // XOR with self_id to get the target node ID
    for (int i = 0; i < 32; i++) {
        out[i] = self_id[i] ^ xor_dist[i];
    }
}

// ============================================================================
// Message handlers
// ============================================================================

static void HandlePing(DHT_NODE *node, DHT_HEADER *req_hdr,
                       struct sockaddr_storage *from_addr, socklen_t from_len)
{
    DHT_PONG_MSG pong;
    memset(&pong, 0, sizeof(pong));
    FillHeader(&pong.header, node, DHT_MSG_PONG);
    pong.header.txn_id = req_hdr->txn_id;  // Echo transaction ID

    SignMessage(node->keypair, (uint8_t *)&pong, sizeof(pong));
    SendRawUDP(node, from_addr, from_len, (uint8_t *)&pong, sizeof(pong));
    node->msgs_sent++;
}

static void HandlePong(DHT_NODE *node, DHT_HEADER *hdr)
{
    int rpc_idx = FindPendingRPC(node, hdr->txn_id);
    if (rpc_idx >= 0) {
        RemovePendingRPC(node, (uint32_t)rpc_idx);
    }
    // Sender was already added/touched in DhtDispatchMessage
}

static void HandleFindNode(DHT_NODE *node, uint8_t *data, size_t len,
                            struct sockaddr_storage *from_addr, socklen_t from_len)
{
    if (len < sizeof(DHT_FIND_NODE_MSG)) return;

    DHT_FIND_NODE_MSG *msg = (DHT_FIND_NODE_MSG *)data;

    // Find K closest nodes to the target
    ROUTING_NODE closest[DHT_K];
    uint32_t count = RoutingTable_FindClosest(&node->routing_table,
                                               msg->target_id, closest, DHT_K);

    // Build response: header + 1B count + N * NODE_INFO
    size_t resp_len = sizeof(DHT_HEADER) + 1 + count * sizeof(DHT_NODE_INFO);
    uint8_t *resp = (uint8_t *)malloc(resp_len);
    if (!resp) return;
    memset(resp, 0, resp_len);

    DHT_HEADER *hdr = (DHT_HEADER *)resp;
    FillHeader(hdr, node, DHT_MSG_FIND_NODE_RESPONSE);
    hdr->txn_id = msg->header.txn_id;  // Echo transaction ID

    resp[sizeof(DHT_HEADER)] = (uint8_t)count;

    DHT_NODE_INFO *nodes = (DHT_NODE_INFO *)(resp + sizeof(DHT_HEADER) + 1);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(nodes[i].node_id, closest[i].node_id, 32);
        nodes[i].addr_type = closest[i].addr_type;
        memcpy(nodes[i].addr, closest[i].addr, 16);
        nodes[i].port = closest[i].port;
    }

    SignMessage(node->keypair, resp, resp_len);
    SendRawUDP(node, from_addr, from_len, resp, resp_len);
    node->msgs_sent++;
    printf("[dht] Received FIND_NODE, returning %u nodes\n", count);

    free(resp);
}

static void HandleFindNodeResponse(DHT_NODE *node, uint8_t *data, size_t len)
{
    if (len < sizeof(DHT_HEADER) + 1) return;

    DHT_HEADER *hdr = (DHT_HEADER *)data;
    uint8_t node_count = data[sizeof(DHT_HEADER)];

    // Verify response length
    size_t expected_len = sizeof(DHT_HEADER) + 1 + node_count * sizeof(DHT_NODE_INFO);
    if (len < expected_len) return;

    // Match and remove pending RPC
    int rpc_idx = FindPendingRPC(node, hdr->txn_id);
    if (rpc_idx >= 0) {
        RemovePendingRPC(node, (uint32_t)rpc_idx);
    }

    // Add returned nodes to routing table
    DHT_NODE_INFO *nodes = (DHT_NODE_INFO *)(data + sizeof(DHT_HEADER) + 1);
    for (uint8_t i = 0; i < node_count; i++) {
        // Don't add ourselves
        if (memcmp(nodes[i].node_id, node->keypair->public_key, 32) == 0) continue;
        RoutingTable_AddNode(&node->routing_table, nodes[i].node_id,
                              nodes[i].addr_type, nodes[i].addr, nodes[i].port);
    }
    printf("[dht] FIND_NODE response: %u nodes\n", node_count);
}

// ============================================================================
// Message handlers: FIND_VALUE / STORE (Sub-phase 4.2)
// ============================================================================

static void HandleFindValue(DHT_NODE *node, uint8_t *data, size_t len,
                             struct sockaddr_storage *from_addr, socklen_t from_len)
{
    if (len < sizeof(DHT_FIND_VALUE_MSG)) return;

    DHT_FIND_VALUE_MSG *msg = (DHT_FIND_VALUE_MSG *)data;

    // Check if we have the value locally
    DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
    uint32_t loc_count = DhtStore_Get(&node->value_store, msg->key, locations,
                                       DHT_STORE_MAX_LOCATIONS);

    if (loc_count > 0)
    {
        // Found! Send value response
        // Format: header(102) + found(1) + count(1) + locations(count * 51)
        size_t resp_len = sizeof(DHT_HEADER) + 1 + 1 + loc_count * sizeof(DHT_NODE_INFO);
        uint8_t *resp = (uint8_t *)malloc(resp_len);
        if (!resp) return;
        memset(resp, 0, resp_len);

        DHT_HEADER *hdr = (DHT_HEADER *)resp;
        FillHeader(hdr, node, DHT_MSG_FIND_VALUE_RESPONSE);
        hdr->txn_id = msg->header.txn_id;

        resp[sizeof(DHT_HEADER)] = 1;  // found = true
        resp[sizeof(DHT_HEADER) + 1] = (uint8_t)loc_count;

        DHT_NODE_INFO *nodes = (DHT_NODE_INFO *)(resp + sizeof(DHT_HEADER) + 2);
        for (uint32_t i = 0; i < loc_count; i++)
        {
            memcpy(nodes[i].node_id, locations[i].node_id, 32);
            nodes[i].addr_type = locations[i].addr_type;
            memcpy(nodes[i].addr, locations[i].addr, 16);
            nodes[i].port = locations[i].port;
        }

        SignMessage(node->keypair, resp, resp_len);
        SendRawUDP(node, from_addr, from_len, resp, resp_len);
        node->msgs_sent++;
        free(resp);
    }
    else
    {
        // Not found — return closest nodes (same as FIND_NODE)
        ROUTING_NODE closest[DHT_K];
        uint32_t count = RoutingTable_FindClosest(&node->routing_table,
                                                   msg->key, closest, DHT_K);

        size_t resp_len = sizeof(DHT_HEADER) + 1 + 1 + count * sizeof(DHT_NODE_INFO);
        uint8_t *resp = (uint8_t *)malloc(resp_len);
        if (!resp) return;
        memset(resp, 0, resp_len);

        DHT_HEADER *hdr = (DHT_HEADER *)resp;
        FillHeader(hdr, node, DHT_MSG_FIND_VALUE_RESPONSE);
        hdr->txn_id = msg->header.txn_id;

        resp[sizeof(DHT_HEADER)] = 0;  // found = false
        resp[sizeof(DHT_HEADER) + 1] = (uint8_t)count;

        DHT_NODE_INFO *nodes = (DHT_NODE_INFO *)(resp + sizeof(DHT_HEADER) + 2);
        for (uint32_t i = 0; i < count; i++)
        {
            memcpy(nodes[i].node_id, closest[i].node_id, 32);
            nodes[i].addr_type = closest[i].addr_type;
            memcpy(nodes[i].addr, closest[i].addr, 16);
            nodes[i].port = closest[i].port;
        }

        SignMessage(node->keypair, resp, resp_len);
        SendRawUDP(node, from_addr, from_len, resp, resp_len);
        node->msgs_sent++;
        free(resp);
    }
}

static void HandleFindValueResponse(DHT_NODE *node, uint8_t *data, size_t len)
{
    if (len < sizeof(DHT_HEADER) + 2) return;

    DHT_HEADER *hdr = (DHT_HEADER *)data;
    uint8_t found = data[sizeof(DHT_HEADER)];
    uint8_t node_count = data[sizeof(DHT_HEADER) + 1];

    // Match pending RPC — save target_id (the key) before removing
    int rpc_idx = FindPendingRPC(node, hdr->txn_id);
    uint8_t target_key[32] = {0};
    BOOLEAN have_key = FALSE;
    if (rpc_idx >= 0)
    {
        memcpy(target_key, node->pending_rpcs[rpc_idx].target_id, 32);
        have_key = TRUE;
        RemovePendingRPC(node, (uint32_t)rpc_idx);
    }

    if (found && have_key)
    {
        // Value locations returned — parse and store locally
        size_t expected = sizeof(DHT_HEADER) + 2 + node_count * sizeof(DHT_NODE_INFO);
        if (len < expected) return;

        DHT_NODE_INFO *infos = (DHT_NODE_INFO *)(data + sizeof(DHT_HEADER) + 2);
        DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
        uint32_t actual = node_count < DHT_STORE_MAX_LOCATIONS ? node_count : DHT_STORE_MAX_LOCATIONS;

        for (uint32_t i = 0; i < actual; i++)
        {
            memcpy(locations[i].node_id, infos[i].node_id, 32);
            locations[i].addr_type = infos[i].addr_type;
            memcpy(locations[i].addr, infos[i].addr, 16);
            locations[i].port = infos[i].port;
        }

        DhtStore_Put(&node->value_store, target_key, locations, actual);
        printf("[dht] FIND_VALUE response: %u locations stored\n", actual);
    }
    else if (found && !have_key)
    {
        printf("[dht] FIND_VALUE response: found but no matching RPC (dropped)\n");
    }
    else
    {
        // Closer nodes returned — add to routing table
        size_t expected = sizeof(DHT_HEADER) + 2 + node_count * sizeof(DHT_NODE_INFO);
        if (len < expected) return;

        DHT_NODE_INFO *nodes = (DHT_NODE_INFO *)(data + sizeof(DHT_HEADER) + 2);
        for (uint8_t i = 0; i < node_count; i++)
        {
            if (memcmp(nodes[i].node_id, node->keypair->public_key, 32) == 0) continue;
            RoutingTable_AddNode(&node->routing_table, nodes[i].node_id,
                                  nodes[i].addr_type, nodes[i].addr, nodes[i].port);
        }
    }
}

static void HandleStore(DHT_NODE *node, uint8_t *data, size_t len,
                         struct sockaddr_storage *from_addr, socklen_t from_len)
{
    if (len < sizeof(DHT_STORE_MSG)) return;

    DHT_STORE_MSG *msg = (DHT_STORE_MSG *)data;
    uint32_t value_size = msg->value_size;

    // Parse locations from value
    // Value format: location_count(1) + locations(count * 51)
    const uint8_t *value_data = data + sizeof(DHT_STORE_MSG);
    size_t value_avail = len - sizeof(DHT_STORE_MSG);

    if (value_avail < 1) goto send_error;

    uint8_t loc_count = value_data[0];
    if (value_avail < 1 + loc_count * sizeof(DHT_NODE_INFO)) goto send_error;

    {
        DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
        uint32_t actual = loc_count < DHT_STORE_MAX_LOCATIONS ? loc_count : DHT_STORE_MAX_LOCATIONS;

        const DHT_NODE_INFO *infos = (const DHT_NODE_INFO *)(value_data + 1);
        for (uint32_t i = 0; i < actual; i++)
        {
            memcpy(locations[i].node_id, infos[i].node_id, 32);
            locations[i].addr_type = infos[i].addr_type;
            memcpy(locations[i].addr, infos[i].addr, 16);
            locations[i].port = infos[i].port;
        }

        // Replace zero addresses with sender's actual address (implied address pattern)
        uint8_t sender_addr_type;
        uint8_t sender_addr[16];
        uint16_t sender_port_unused;
        SockaddrToNodeInfo(from_addr, &sender_addr_type, sender_addr, &sender_port_unused);

        for (uint32_t i = 0; i < actual; i++)
        {
            uint8_t zero[16] = {0};
            if (memcmp(locations[i].addr, zero, 16) == 0 &&
                memcmp(locations[i].node_id, msg->header.sender_id, 32) == 0)
            {
                locations[i].addr_type = sender_addr_type;
                memcpy(locations[i].addr, sender_addr, 16);
            }
        }

        DhtStore_Put(&node->value_store, msg->key, locations, actual);
        printf("[dht] Received STORE: %u locations for key\n", actual);
    }

    // Send success response
    {
        DHT_STORE_RESPONSE_MSG resp;
        memset(&resp, 0, sizeof(resp));
        FillHeader(&resp.header, node, DHT_MSG_STORE_RESPONSE);
        resp.header.txn_id = msg->header.txn_id;
        resp.status = 0;  // success

        SignMessage(node->keypair, (uint8_t *)&resp, sizeof(resp));
        SendRawUDP(node, from_addr, from_len, (uint8_t *)&resp, sizeof(resp));
        node->msgs_sent++;
    }
    return;

send_error:
    {
        DHT_STORE_RESPONSE_MSG resp;
        memset(&resp, 0, sizeof(resp));
        FillHeader(&resp.header, node, DHT_MSG_STORE_RESPONSE);
        resp.header.txn_id = msg->header.txn_id;
        resp.status = 1;  // error

        SignMessage(node->keypair, (uint8_t *)&resp, sizeof(resp));
        SendRawUDP(node, from_addr, from_len, (uint8_t *)&resp, sizeof(resp));
        node->msgs_sent++;
    }
}

static void HandleStoreResponse(DHT_NODE *node, uint8_t *data, size_t len)
{
    if (len < sizeof(DHT_STORE_RESPONSE_MSG)) return;

    DHT_HEADER *hdr = (DHT_HEADER *)data;
    int rpc_idx = FindPendingRPC(node, hdr->txn_id);
    if (rpc_idx >= 0)
    {
        RemovePendingRPC(node, (uint32_t)rpc_idx);
    }
}

// ============================================================================
// Message dispatch
// ============================================================================

static void DhtDispatchMessage(DHT_NODE *node, uint8_t *data, size_t len,
                                struct sockaddr_storage *from_addr, socklen_t from_len)
{
    if (len < DHT_HEADER_SIZE) return;

    DHT_HEADER *hdr = (DHT_HEADER *)data;

    // Verify protocol version
    if (hdr->version != DHT_PROTOCOL_VERSION) return;

    // Verify signature
    if (!VerifyMessage(hdr->sender_id, data, len)) return;

    // Update routing table with sender info
    uint8_t addr_type;
    uint8_t addr[16];
    uint16_t port;
    SockaddrToNodeInfo(from_addr, &addr_type, addr, &port);
    RoutingTable_AddNode(&node->routing_table, hdr->sender_id, addr_type, addr, port);
    RoutingTable_TouchNode(&node->routing_table, hdr->sender_id);

    node->msgs_received++;

    {
        char addr_str[64] = {0};
        if (from_addr->ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)from_addr;
            inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
        } else if (from_addr->ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)from_addr;
            inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
        }
        printf("[dht] Received msg type 0x%02x from %s:%u\n",
               hdr->msg_type, addr_str, port);
    }

    switch (hdr->msg_type) {
    case DHT_MSG_PING:
        HandlePing(node, hdr, from_addr, from_len);
        break;
    case DHT_MSG_PONG:
        HandlePong(node, hdr);
        break;
    case DHT_MSG_FIND_NODE:
        HandleFindNode(node, data, len, from_addr, from_len);
        break;
    case DHT_MSG_FIND_NODE_RESPONSE:
        HandleFindNodeResponse(node, data, len);
        break;
    case DHT_MSG_FIND_VALUE:
        HandleFindValue(node, data, len, from_addr, from_len);
        break;
    case DHT_MSG_FIND_VALUE_RESPONSE:
        HandleFindValueResponse(node, data, len);
        break;
    case DHT_MSG_STORE:
        HandleStore(node, data, len, from_addr, from_len);
        break;
    case DHT_MSG_STORE_RESPONSE:
        HandleStoreResponse(node, data, len);
        break;
    default:
        break;
    }
}

// ============================================================================
// Public API
// ============================================================================

BOOLEAN DhtNode_Init(DHT_NODE *node, KEYPAIR *keypair, uint16_t port,
                     const char *bootstrap_host, uint16_t bootstrap_port)
{
    memset(node, 0, sizeof(DHT_NODE));
    node->keypair = keypair;
    node->listen_port = port;
    node->socket_fd = -1;

    // Initialize routing table with our public key as node ID
    RoutingTable_Init(&node->routing_table, keypair->public_key);

    // Initialize value store
    DhtStore_Init(&node->value_store);

    // Try to load saved routing table
    char rt_path[512];
    if (GetDhtRoutingPath(rt_path, sizeof(rt_path))) {
        ROUTING_TABLE loaded;
        if (RoutingTable_Load(&loaded, rt_path)) {
            if (memcmp(loaded.self_id, keypair->public_key, 32) == 0) {
                node->routing_table = loaded;
            }
        }
    }

    // Initialize Winsock on Windows
#ifdef _WIN32
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            fprintf(stderr, "[DHT] WSAStartup failed\n");
            return FALSE;
        }
    }
#endif

    // Create UDP socket
    node->socket_fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (node->socket_fd < 0) {
        fprintf(stderr, "[DHT] Failed to create UDP socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return FALSE;
    }

    // Bind to port
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(port);

    if (bind(node->socket_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        fprintf(stderr, "[DHT] Failed to bind to port %u\n", port);
#ifdef _WIN32
        closesocket(node->socket_fd);
        WSACleanup();
#else
        close(node->socket_fd);
#endif
        node->socket_fd = -1;
        return FALSE;
    }

    // If port was 0, retrieve the assigned port
    if (port == 0) {
        struct sockaddr_in actual;
        socklen_t actual_len = sizeof(actual);
        if (getsockname(node->socket_fd, (struct sockaddr *)&actual, &actual_len) == 0) {
            node->listen_port = ntohs(actual.sin_port);
        }
    }

    // Resolve bootstrap node
    if (bootstrap_host && bootstrap_port > 0) {
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%u", bootstrap_port);

        if (getaddrinfo(bootstrap_host, port_str, &hints, &result) == 0) {
            if (result->ai_family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in *)result->ai_addr;
                node->bootstrap_addr_type = DHT_ADDR_IPV4;
                memcpy(node->bootstrap_addr, &sin->sin_addr, 4);
                node->bootstrap_port = bootstrap_port;
            }
            freeaddrinfo(result);
        } else {
            fprintf(stderr, "[DHT] Failed to resolve bootstrap host: %s\n", bootstrap_host);
        }
    }

    // Seed transaction IDs
    node->next_txn_id = (uint32_t)time(NULL);

    return TRUE;
}

void DhtNode_Poll(DHT_NODE *node, int timeout_ms)
{
    // Wait for data on the socket
#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET((SOCKET)node->socket_fd, &read_fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select(0, &read_fds, NULL, NULL, &tv);
#else
    struct pollfd pfd;
    pfd.fd = node->socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = poll(&pfd, 1, timeout_ms);
#endif

    if (ready > 0) {
        uint8_t buf[DHT_MAX_PACKET_SIZE];
        struct sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t n = recvfrom(node->socket_fd, (char *)buf, sizeof(buf), 0,
                              (struct sockaddr *)&from_addr, &from_len);
        if (n >= (ssize_t)DHT_HEADER_SIZE) {
            DhtDispatchMessage(node, buf, (size_t)n, &from_addr, from_len);
        }
    }

    // Expire timed-out RPCs
    DhtNode_ExpirePendingRPCs(node);
}

BOOLEAN DhtNode_Bootstrap(DHT_NODE *node)
{
    if (node->bootstrap_addr_type == 0) return FALSE;

    {
        char addr_str[64] = {0};
        struct sockaddr_storage sa;
        socklen_t sa_len;
        if (NodeInfoToSockaddr(node->bootstrap_addr_type, node->bootstrap_addr,
                                node->bootstrap_port, &sa, &sa_len)) {
            if (sa.ss_family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in *)&sa;
                inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
            } else if (sa.ss_family == AF_INET6) {
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&sa;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
            }
        }
        printf("[dht] Bootstrap: sending FIND_NODE(self) to %s:%u\n",
               addr_str, node->bootstrap_port);
    }

    // Standard Kademlia bootstrap: FIND_NODE for our own ID
    return DhtNode_SendFindNode(node, node->bootstrap_addr, node->bootstrap_addr_type,
                                 node->bootstrap_port, node->keypair->public_key);
}

BOOLEAN DhtNode_SendPing(DHT_NODE *node, const uint8_t addr[16],
                          uint8_t addr_type, uint16_t port)
{
    DHT_PING_MSG msg;
    memset(&msg, 0, sizeof(msg));
    FillHeader(&msg.header, node, DHT_MSG_PING);

    if (!SignMessage(node->keypair, (uint8_t *)&msg, sizeof(msg))) return FALSE;

    struct sockaddr_storage sa;
    socklen_t sa_len;
    if (!NodeInfoToSockaddr(addr_type, addr, port, &sa, &sa_len)) return FALSE;

    BOOLEAN result = SendRawUDP(node, &sa, sa_len, (uint8_t *)&msg, sizeof(msg));
    if (result) {
        AddPendingRPC(node, msg.header.txn_id, DHT_MSG_PING, NULL);
        node->msgs_sent++;
    }
    return result;
}

BOOLEAN DhtNode_SendFindNode(DHT_NODE *node, const uint8_t addr[16],
                              uint8_t addr_type, uint16_t port,
                              const uint8_t target_id[32])
{
    DHT_FIND_NODE_MSG msg;
    memset(&msg, 0, sizeof(msg));
    FillHeader(&msg.header, node, DHT_MSG_FIND_NODE);
    memcpy(msg.target_id, target_id, 32);

    if (!SignMessage(node->keypair, (uint8_t *)&msg, sizeof(msg))) return FALSE;

    struct sockaddr_storage sa;
    socklen_t sa_len;
    if (!NodeInfoToSockaddr(addr_type, addr, port, &sa, &sa_len)) return FALSE;

    BOOLEAN result = SendRawUDP(node, &sa, sa_len, (uint8_t *)&msg, sizeof(msg));
    if (result) {
        AddPendingRPC(node, msg.header.txn_id, DHT_MSG_FIND_NODE, target_id);
        node->msgs_sent++;
    }
    return result;
}

void DhtNode_RefreshBuckets(DHT_NODE *node)
{
    time_t now = time(NULL);
    for (int b = 0; b < DHT_BUCKET_COUNT; b++) {
        K_BUCKET *bucket = &node->routing_table.buckets[b];
        if (bucket->count == 0) continue;
        if (difftime(now, bucket->last_refresh) < DHT_BUCKET_REFRESH_SEC) continue;

        // Generate random ID in this bucket's range
        uint8_t random_id[32];
        GenerateRandomIdForBucket(node->routing_table.self_id, b, random_id);

        // Send FIND_NODE to first node in bucket
        ROUTING_NODE *target = &bucket->nodes[0];
        DhtNode_SendFindNode(node, target->addr, target->addr_type,
                              target->port, random_id);

        bucket->last_refresh = now;
    }
}

void DhtNode_ExpirePendingRPCs(DHT_NODE *node)
{
    time_t now = time(NULL);
    uint32_t i = 0;
    while (i < node->pending_count) {
        DHT_PENDING_RPC *rpc = &node->pending_rpcs[i];
        double elapsed_ms = difftime(now, rpc->sent_at) * 1000.0;
        if (elapsed_ms >= DHT_RPC_TIMEOUT_MS) {
            RemovePendingRPC(node, i);
            // Don't increment i — swapped last element into this slot
        } else {
            i++;
        }
    }
}

void DhtNode_Shutdown(DHT_NODE *node)
{
    // Save routing table to disk
    char rt_path[512];
    if (GetDhtRoutingPath(rt_path, sizeof(rt_path))) {
        EnsureWormholeDir();
        RoutingTable_Save(&node->routing_table, rt_path);
    }

    // Destroy value store lock
    DhtStore_Destroy(&node->value_store);

    // Close socket
    if (node->socket_fd >= 0) {
#ifdef _WIN32
        closesocket(node->socket_fd);
        WSACleanup();
#else
        close(node->socket_fd);
#endif
        node->socket_fd = -1;
    }
}

uint32_t DhtNode_GetNodeCount(const DHT_NODE *node)
{
    return RoutingTable_GetTotalNodes(&node->routing_table);
}

// ============================================================================
// DHT Storage Operations (Sub-phase 4.2)
// ============================================================================

BOOLEAN DhtNode_SendFindValue(DHT_NODE *node, const uint8_t addr[16],
                               uint8_t addr_type, uint16_t port,
                               const uint8_t key[32])
{
    DHT_FIND_VALUE_MSG msg;
    memset(&msg, 0, sizeof(msg));
    FillHeader(&msg.header, node, DHT_MSG_FIND_VALUE);
    memcpy(msg.key, key, 32);

    if (!SignMessage(node->keypair, (uint8_t *)&msg, sizeof(msg))) return FALSE;

    struct sockaddr_storage sa;
    socklen_t sa_len;
    if (!NodeInfoToSockaddr(addr_type, addr, port, &sa, &sa_len)) return FALSE;

    BOOLEAN result = SendRawUDP(node, &sa, sa_len, (uint8_t *)&msg, sizeof(msg));
    if (result)
    {
        AddPendingRPC(node, msg.header.txn_id, DHT_MSG_FIND_VALUE, key);
        node->msgs_sent++;
    }
    return result;
}

BOOLEAN DhtNode_SendStore(DHT_NODE *node, const uint8_t addr[16],
                           uint8_t addr_type, uint16_t port,
                           const uint8_t key[32],
                           const DHT_LOCATION *locations, uint32_t location_count)
{
    if (location_count == 0 || location_count > DHT_STORE_MAX_LOCATIONS) return FALSE;

    // Build STORE message: header + key[32] + value_size[4] + loc_count[1] + locs[N*51]
    uint32_t value_size = 1 + location_count * sizeof(DHT_NODE_INFO);
    size_t msg_len = sizeof(DHT_STORE_MSG) + value_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;
    memset(msg, 0, msg_len);

    DHT_STORE_MSG *store = (DHT_STORE_MSG *)msg;
    FillHeader(&store->header, node, DHT_MSG_STORE);
    memcpy(store->key, key, 32);
    store->value_size = value_size;

    // Value: [1B loc_count][N * DHT_NODE_INFO]
    uint8_t *value = msg + sizeof(DHT_STORE_MSG);
    value[0] = (uint8_t)location_count;

    DHT_NODE_INFO *infos = (DHT_NODE_INFO *)(value + 1);
    for (uint32_t i = 0; i < location_count; i++)
    {
        memcpy(infos[i].node_id, locations[i].node_id, 32);
        infos[i].addr_type = locations[i].addr_type;
        memcpy(infos[i].addr, locations[i].addr, 16);
        infos[i].port = locations[i].port;
    }

    if (!SignMessage(node->keypair, msg, msg_len))
    {
        free(msg);
        return FALSE;
    }

    struct sockaddr_storage sa;
    socklen_t sa_len;
    if (!NodeInfoToSockaddr(addr_type, addr, port, &sa, &sa_len))
    {
        free(msg);
        return FALSE;
    }

    BOOLEAN result = SendRawUDP(node, &sa, sa_len, msg, msg_len);
    if (result)
    {
        AddPendingRPC(node, store->header.txn_id, DHT_MSG_STORE, key);
        node->msgs_sent++;
    }

    free(msg);
    return result;
}

void DhtNode_AnnounceChunk(DHT_NODE *node, const uint8_t chunk_hash[32], uint16_t quic_port)
{
    // Build our location info
    DHT_LOCATION self_loc;
    memset(&self_loc, 0, sizeof(self_loc));
    memcpy(self_loc.node_id, node->keypair->public_key, 32);
    self_loc.addr_type = DHT_ADDR_IPV4;
    // addr stays 0.0.0.0 — peers know our address from the UDP source
    self_loc.port = quic_port;

    // Don't store zero-addr self-entry locally — it's useless for lookups.
    // Receiving peers will fill in our real address via HandleStore.

    // Find K closest nodes to the chunk hash
    ROUTING_NODE closest[DHT_K];
    uint32_t count = RoutingTable_FindClosest(&node->routing_table,
                                               chunk_hash, closest, DHT_K);

    // Send STORE to each
    printf("[dht] Announcing chunk to %u nodes\n", count);
    for (uint32_t i = 0; i < count; i++)
    {
        DhtNode_SendStore(node, closest[i].addr, closest[i].addr_type,
                           closest[i].port, chunk_hash, &self_loc, 1);
    }
}

uint32_t DhtNode_FindChunkLocations(DHT_NODE *node, const uint8_t chunk_hash[32],
                                     DHT_LOCATION *out_locations, uint32_t max)
{
    // First check our local value store
    uint32_t local_count = DhtStore_Get(&node->value_store, chunk_hash,
                                         out_locations, max);
    if (local_count > 0)
    {
        return local_count;
    }

    // Send FIND_VALUE to K closest nodes (non-blocking, results arrive via HandleFindValueResponse)
    ROUTING_NODE closest[DHT_K];
    uint32_t count = RoutingTable_FindClosest(&node->routing_table,
                                               chunk_hash, closest, DHT_K);

    uint32_t sent = 0;
    for (uint32_t i = 0; i < count && sent < DHT_ALPHA; i++)
    {
        if (DhtNode_SendFindValue(node, closest[i].addr, closest[i].addr_type,
                                   closest[i].port, chunk_hash))
        {
            sent++;
        }
    }

    // Return 0 for now — caller should poll and check value store later
    return 0;
}
