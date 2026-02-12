//
// connection_manager.h
// Wormhole - Multi-Path Connection Manager
// by Dylan Kress
//

#pragma once

#include "relay_client.h"
#include "../../relay-server/relay_protocol.h"
#include <stdint.h>
#include <stdbool.h>

// Connection attempt result
typedef enum {
    CONN_RESULT_SUCCESS,
    CONN_RESULT_IN_PROGRESS,
    CONN_RESULT_FAILED,
    CONN_RESULT_TIMEOUT
} CONN_RESULT;

// Connection type
typedef enum {
    CONN_TYPE_DIRECT_LAN,      // Direct connection to LAN address
    CONN_TYPE_DIRECT_PUBLIC,   // Direct connection to public IP
    CONN_TYPE_DIRECT_IPV6,     // Direct connection to IPv6
    CONN_TYPE_RELAY_FORWARDED  // Forwarded through relay server
} CONN_TYPE;

// Connection manager (tracks multiple parallel connection attempts)
typedef struct CONNECTION_MANAGER CONNECTION_MANAGER;

// Create connection manager
// Returns: Allocated manager, or NULL on failure
CONNECTION_MANAGER* ConnectionManager_Create(void);

// Destroy connection manager
void ConnectionManager_Destroy(CONNECTION_MANAGER* mgr);

// Start connection attempts to all peer endpoints in parallel
// peer_id: Target peer's public key
// endpoints: Array of peer endpoints
// endpoint_count: Number of endpoints
// relay_client: Relay client (for fallback forwarding)
// Returns: true if attempts started, false on failure
bool ConnectionManager_ConnectToPeer(CONNECTION_MANAGER* mgr,
                                     const uint8_t peer_id[32],
                                     const ENDPOINT* endpoints,
                                     uint16_t endpoint_count,
                                     RELAY_CLIENT* relay_client);

// Poll for connection results (non-blocking)
// timeout_ms: Timeout in milliseconds
// Returns: Connection result status
CONN_RESULT ConnectionManager_Poll(CONNECTION_MANAGER* mgr, int timeout_ms);

// Get the type of connection that succeeded
CONN_TYPE ConnectionManager_GetConnectionType(CONNECTION_MANAGER* mgr);

// Get connected endpoint info (after success)
bool ConnectionManager_GetConnectedEndpoint(CONNECTION_MANAGER* mgr, ENDPOINT* endpoint_out);

// Check if relay forwarding is being used
bool ConnectionManager_IsUsingRelay(CONNECTION_MANAGER* mgr);
