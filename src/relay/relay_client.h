//
// relay_client.h
// Wormhole - Relay Client (Protocol Communication)
// by Dylan Kress
//

#pragma once

#include "peer_id.h"
#include "../../relay-server/relay_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Forward declarations
typedef struct RELAY_CLIENT RELAY_CLIENT;

// Relay client callbacks
typedef void (*RelayConnectedCallback)(void* context, uint64_t session_id,
                                       const uint8_t observed_addr[16],
                                       uint8_t observed_addr_type, uint16_t observed_port);
typedef void (*RelayTicketCreatedCallback)(void* context, const char* ticket);
typedef void (*RelayPeerInfoCallback)(void* context, const uint8_t peer_id[32],
                                     const ENDPOINT* endpoints, uint16_t endpoint_count);
typedef void (*RelayDisconnectedCallback)(void* context);

// Relay client configuration
typedef struct {
    const char* relay_host;           // Relay server hostname (e.g., "wormholerelay.com")
    uint16_t relay_port;              // Relay server port (default: 443)
    KEYPAIR* keypair;                 // Our Ed25519 keypair
    
    // Callbacks
    RelayConnectedCallback on_connected;
    RelayTicketCreatedCallback on_ticket_created;
    RelayPeerInfoCallback on_peer_info;
    RelayDisconnectedCallback on_disconnected;
    void* callback_context;           // User context for callbacks
} RELAY_CLIENT_CONFIG;

// Create relay client
// Returns: Allocated RELAY_CLIENT, or NULL on failure (caller must free with RelayClient_Destroy)
RELAY_CLIENT* RelayClient_Create(const RELAY_CLIENT_CONFIG* config);

// Destroy relay client and free resources
void RelayClient_Destroy(RELAY_CLIENT* client);

// Connect to relay server and register
// endpoints: Array of our connection endpoints (LAN, public, IPv6, etc.)
// endpoint_count: Number of endpoints
// Returns: true if registration sent successfully, false on failure
bool RelayClient_Register(RELAY_CLIENT* client, const ENDPOINT* endpoints, uint16_t endpoint_count);

// Request a ticket for file transfer (sender only)
// Returns: true if request sent successfully, false on failure
bool RelayClient_CreateTicket(RELAY_CLIENT* client, uint64_t file_size, const char* filename);

// Lookup peer by ticket (receiver only)
// Returns: true if lookup sent successfully, false on failure
bool RelayClient_LookupTicket(RELAY_CLIENT* client, const char* ticket);

// Send keepalive to relay
// Returns: true if sent successfully, false on failure
bool RelayClient_SendKeepalive(RELAY_CLIENT* client);

// Send goodbye to relay (graceful disconnect)
// reason: 0x00=upgraded to direct, 0x01=error, 0x02=transfer complete
// Returns: true if sent successfully, false on failure
bool RelayClient_SendGoodbye(RELAY_CLIENT* client, uint8_t reason);

// Forward a packet through relay (fallback when direct connection fails)
// dest_peer_id: Destination peer's public key
// payload: Encrypted QUIC packet
// payload_len: Length of payload
// Returns: true if sent successfully, false on failure
bool RelayClient_ForwardPacket(RELAY_CLIENT* client, const uint8_t dest_peer_id[32],
                               const uint8_t* payload, uint16_t payload_len);

// Process incoming messages from relay (call this in a loop or polling thread)
// timeout_ms: Timeout in milliseconds (0 = non-blocking, -1 = blocking)
// Returns: true if message processed, false on timeout/error
bool RelayClient_Poll(RELAY_CLIENT* client, int timeout_ms);

// Get current session ID (0 if not registered)
uint64_t RelayClient_GetSessionID(RELAY_CLIENT* client);

// Check if client is connected to relay
bool RelayClient_IsConnected(RELAY_CLIENT* client);
