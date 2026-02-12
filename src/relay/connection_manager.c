//
// connection_manager.c
// Wormhole - Multi-Path Connection Manager (Simplified)
// by Dylan Kress
//
// NOTE: This is a simplified stub for Phase 2 integration.
// Full implementation with MsQuic multi-path racing would be ~500 lines.

#include "connection_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct CONNECTION_MANAGER {
    uint8_t peer_id[32];
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count;
    RELAY_CLIENT* relay_client;
    
    CONN_TYPE connection_type;
    ENDPOINT connected_endpoint;
    bool connected;
    bool using_relay;
};

CONNECTION_MANAGER* ConnectionManager_Create(void) {
    CONNECTION_MANAGER* mgr = (CONNECTION_MANAGER*)calloc(1, sizeof(CONNECTION_MANAGER));
    if (!mgr) {
        return NULL;
    }
    
    printf("[ConnectionManager] Created\n");
    return mgr;
}

void ConnectionManager_Destroy(CONNECTION_MANAGER* mgr) {
    if (!mgr) {
        return;
    }
    
    free(mgr);
    printf("[ConnectionManager] Destroyed\n");
}

bool ConnectionManager_ConnectToPeer(CONNECTION_MANAGER* mgr,
                                     const uint8_t peer_id[32],
                                     const ENDPOINT* endpoints,
                                     uint16_t endpoint_count,
                                     RELAY_CLIENT* relay_client) {
    if (!mgr || !peer_id || !endpoints || endpoint_count == 0) {
        return false;
    }
    
    // Store peer info
    memcpy(mgr->peer_id, peer_id, 32);
    memcpy(mgr->endpoints, endpoints, endpoint_count * sizeof(ENDPOINT));
    mgr->endpoint_count = endpoint_count;
    mgr->relay_client = relay_client;
    
    printf("[ConnectionManager] Starting connection attempts to peer (%u endpoints)\n", endpoint_count);
    
    // TODO: In full implementation, this would:
    // 1. Launch parallel threads for each endpoint
    // 2. Attempt QUIC connections simultaneously
    // 3. First successful connection wins
    // 4. Monitor for better paths in background
    // 5. Upgrade from relay to direct if better path found
    
    // For now, we'll simulate choosing the best endpoint
    // Priority: 0 (LAN) > 75 (IPv6) > 100 (reflected) > 200 (relay)
    
    int best_idx = 0;
    for (int i = 1; i < endpoint_count; i++) {
        if (endpoints[i].priority < endpoints[best_idx].priority) {
            best_idx = i;
        }
    }
    
    mgr->connected_endpoint = endpoints[best_idx];
    mgr->connected = true;
    mgr->using_relay = false;
    
    // Determine connection type based on priority
    if (mgr->connected_endpoint.priority == 0) {
        mgr->connection_type = CONN_TYPE_DIRECT_LAN;
    } else if (mgr->connected_endpoint.priority == 75) {
        mgr->connection_type = CONN_TYPE_DIRECT_IPV6;
    } else if (mgr->connected_endpoint.priority == 100) {
        mgr->connection_type = CONN_TYPE_DIRECT_PUBLIC;
    } else {
        mgr->connection_type = CONN_TYPE_RELAY_FORWARDED;
        mgr->using_relay = true;
    }
    
    printf("[ConnectionManager] Simulated connection via endpoint (priority %u)\n",
           mgr->connected_endpoint.priority);
    
    return true;
}

CONN_RESULT ConnectionManager_Poll(CONNECTION_MANAGER* mgr, int timeout_ms) {
    if (!mgr) {
        return CONN_RESULT_FAILED;
    }
    
    // TODO: In full implementation, poll all parallel connection attempts
    // For now, we simulate immediate success
    
    (void)timeout_ms;  // Unused in stub
    
    if (mgr->connected) {
        return CONN_RESULT_SUCCESS;
    }
    
    return CONN_RESULT_IN_PROGRESS;
}

CONN_TYPE ConnectionManager_GetConnectionType(CONNECTION_MANAGER* mgr) {
    if (!mgr) {
        return CONN_TYPE_RELAY_FORWARDED;
    }
    return mgr->connection_type;
}

bool ConnectionManager_GetConnectedEndpoint(CONNECTION_MANAGER* mgr, ENDPOINT* endpoint_out) {
    if (!mgr || !endpoint_out || !mgr->connected) {
        return false;
    }
    
    *endpoint_out = mgr->connected_endpoint;
    return true;
}

bool ConnectionManager_IsUsingRelay(CONNECTION_MANAGER* mgr) {
    if (!mgr) {
        return false;
    }
    return mgr->using_relay;
}
