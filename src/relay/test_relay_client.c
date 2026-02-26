//
// test_relay_client.c
// Manual integration test for relay client components (not part of unit test suite).
// Build and run manually when testing relay connectivity:
//   gcc -o test_relay_client test_relay_client.c peer_id.c relay_client.c discovery.c ticket.c
//       -I../../deps/blake3 -lsodium -lpthread
//   ./test_relay_client send <relay-host> <filename>
//   ./test_relay_client receive <relay-host> <ticket>
// by Dylan Kress
//

#include "peer_id.h"
#include "relay_client.h"
#include "discovery.h"
#include "ticket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global state for callbacks
static char g_received_ticket[64] = {0};
static bool g_ticket_received = false;
static bool g_connected = false;

// Callback when connected to relay
static void on_connected(void* context, uint64_t session_id,
                        const uint8_t observed_addr[16],
                        uint8_t observed_addr_type, uint16_t observed_port) {
    printf("\n[Callback] Connected to relay (session %llu)\n", (unsigned long long)session_id);
    printf("[Callback] Observed address type: 0x%02X, port: %u\n", observed_addr_type, observed_port);
    g_connected = true;
}

// Callback when ticket is created
static void on_ticket_created(void* context, const char* ticket) {
    printf("\n[Callback] Ticket created: %s\n", ticket);
    strncpy(g_received_ticket, ticket, sizeof(g_received_ticket) - 1);
    g_ticket_received = true;
}

// Callback when peer info is received
static void on_peer_info(void* context, const uint8_t peer_id[32],
                        const ENDPOINT* endpoints, uint16_t endpoint_count) {
    printf("\n[Callback] Received peer info\n");
    printf("[Callback] Peer has %u endpoints\n", endpoint_count);
    
    char peer_id_hex[65];
    PeerID_ToHex(peer_id, peer_id_hex);
    printf("[Callback] Peer ID: %s\n", peer_id_hex);
}

// Callback when disconnected
static void on_disconnected(void* context) {
    printf("\n[Callback] Disconnected from relay\n");
    g_connected = false;
}

int main(int argc, char* argv[]) {
    printf("Wormhole Relay Client Test\n\n");
    
    // Parse command line
    if (argc < 3) {
        printf("Usage:\n");
        printf("  %s send <relay-host> <filename>\n", argv[0]);
        printf("  %s receive <relay-host> <ticket>\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* relay_host = argv[2];
    
    // Initialize libsodium
    if (!PeerID_Init()) {
        fprintf(stderr, "Failed to initialize PeerID\n");
        return 1;
    }
    
    // Load or generate keypair
    KEYPAIR keypair;
    char* identity_path = PeerID_GetDefaultPath();
    if (!identity_path) {
        fprintf(stderr, "Failed to get identity path\n");
        return 1;
    }
    
    printf("[Test] Identity file: %s\n", identity_path);
    
    if (!PeerID_LoadOrGenerate(&keypair, identity_path)) {
        fprintf(stderr, "Failed to load/generate keypair\n");
        free(identity_path);
        return 1;
    }
    free(identity_path);
    
    // Print our peer ID
    char peer_id_hex[65];
    PeerID_ToHex(keypair.public_key, peer_id_hex);
    printf("[Test] Our Peer ID: %s\n\n", peer_id_hex);
    
    // Create relay client
    RELAY_CLIENT_CONFIG config = {
        .relay_host = relay_host,
        .relay_port = 8080,  // Use our test server port
        .keypair = &keypair,
        .on_connected = on_connected,
        .on_ticket_created = on_ticket_created,
        .on_peer_info = on_peer_info,
        .on_disconnected = on_disconnected,
        .callback_context = NULL
    };
    
    RELAY_CLIENT* client = RelayClient_Create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create relay client\n");
        return 1;
    }
    
    // Discover endpoints
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);
    
    if (endpoint_count == 0) {
        printf("[Test] Warning: No endpoints discovered, using placeholder\n");
        // Add a placeholder endpoint
        memset(&endpoints[0], 0, sizeof(ENDPOINT));
        endpoints[0].addr_type = 0x04;
        endpoints[0].addr[0] = 127;
        endpoints[0].addr[1] = 0;
        endpoints[0].addr[2] = 0;
        endpoints[0].addr[3] = 1;
        endpoints[0].port = 4567;
        endpoints[0].priority = 0;
        endpoint_count = 1;
    }
    
    // Register with relay
    printf("[Test] Registering with relay...\n");
    if (!RelayClient_Register(client, endpoints, endpoint_count)) {
        fprintf(stderr, "Failed to register with relay\n");
        RelayClient_Destroy(client);
        return 1;
    }
    
    // Wait for registration response
    printf("[Test] Waiting for registration response...\n");
    for (int i = 0; i < 50 && !g_connected; i++) {
        RelayClient_Poll(client, 100);  // 100ms timeout
    }
    
    if (!g_connected) {
        fprintf(stderr, "Failed to connect to relay (timeout)\n");
        RelayClient_Destroy(client);
        return 1;
    }
    
    // Mode-specific behavior
    if (strcmp(mode, "send") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s send <relay-host> <filename>\n", argv[0]);
            RelayClient_Destroy(client);
            return 1;
        }
        
        const char* filename = argv[3];
        uint64_t file_size = 12345;  // Placeholder
        
        printf("\n[Test] Creating ticket for file: %s\n", filename);
        if (!RelayClient_CreateTicket(client, file_size, filename)) {
            fprintf(stderr, "Failed to create ticket\n");
            RelayClient_Destroy(client);
            return 1;
        }
        
        // Wait for ticket response
        printf("[Test] Waiting for ticket...\n");
        for (int i = 0; i < 50 && !g_ticket_received; i++) {
            RelayClient_Poll(client, 100);
        }
        
        if (!g_ticket_received) {
            fprintf(stderr, "Failed to receive ticket (timeout)\n");
            RelayClient_Destroy(client);
            return 1;
        }
        
        // Display ticket
        Ticket_PrintForSharing(g_received_ticket, filename, file_size);
        
        printf("\n[Test] Press Ctrl+C to exit\n");
        while (1) {
            RelayClient_Poll(client, 1000);
        }
        
    } else if (strcmp(mode, "receive") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s receive <relay-host> <ticket>\n", argv[0]);
            RelayClient_Destroy(client);
            return 1;
        }
        
        const char* ticket = argv[3];
        
        if (!Ticket_Validate(ticket)) {
            fprintf(stderr, "Invalid ticket format: %s\n", ticket);
            RelayClient_Destroy(client);
            return 1;
        }
        
        Ticket_PrintReceiving(ticket);
        
        printf("[Test] Looking up sender...\n");
        if (!RelayClient_LookupTicket(client, ticket)) {
            fprintf(stderr, "Failed to lookup ticket\n");
            RelayClient_Destroy(client);
            return 1;
        }
        
        // Wait for peer info
        printf("[Test] Waiting for peer info...\n");
        for (int i = 0; i < 50; i++) {
            RelayClient_Poll(client, 100);
        }
        
        printf("\n[Test] Test complete\n");
        
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        RelayClient_Destroy(client);
        return 1;
    }
    
    // Cleanup
    RelayClient_Destroy(client);
    
    return 0;
}
