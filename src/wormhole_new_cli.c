//
// wormhole_new_cli.c  
// New CLI with send/receive commands (Phase 2 integration demo)
// by Dylan Kress
//

#include "common.h"
#include "protocol.h"
#include "connection.h"
#include "stream.h"
#include "file_io.h"
#include "crypto.h"
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"
#include "relay/ticket.h"
#include "relay/connection_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global MsQuic state
extern const QUIC_API_TABLE *MsQuic;
extern HQUIC Registration;
extern HQUIC ServerConfiguration;
extern HQUIC ClientConfiguration;

// Relay configuration
#define DEFAULT_RELAY_HOST "localhost"  // Change to "wormholerelay.com" for production
#define DEFAULT_RELAY_PORT 8080         // Change to 443 for production

// Global state for relay callbacks
static char g_ticket[64] = {0};
static bool g_ticket_ready = false;
static bool g_peer_info_ready = false;
static uint8_t g_peer_id[32] = {0};
static ENDPOINT g_peer_endpoints[MAX_ENDPOINTS] = {0};
static uint16_t g_peer_endpoint_count = 0;

//=============================================================================
// Relay Callbacks
//=============================================================================

static void on_relay_connected(void* context, uint64_t session_id,
                               const uint8_t observed_addr[16],
                               uint8_t observed_addr_type, uint16_t observed_port) {
    printf("\n[Relay] Connected to relay server (session %llu)\n", (unsigned long long)session_id);
    printf("[Relay] Your public IP (as seen by relay): ");
    
    if (observed_addr_type == 0x04) {
        printf("%u.%u.%u.%u:%u\n", observed_addr[0], observed_addr[1],
               observed_addr[2], observed_addr[3], observed_port);
    } else {
        printf("[IPv6]:%u\n", observed_port);
    }
}

static void on_ticket_created(void* context, const char* ticket) {
    strncpy(g_ticket, ticket, sizeof(g_ticket) - 1);
    g_ticket_ready = true;
}

static void on_peer_info(void* context, const uint8_t peer_id[32],
                        const ENDPOINT* endpoints, uint16_t endpoint_count) {
    memcpy(g_peer_id, peer_id, 32);
    memcpy(g_peer_endpoints, endpoints, endpoint_count * sizeof(ENDPOINT));
    g_peer_endpoint_count = endpoint_count;
    g_peer_info_ready = true;
    
    printf("\n[Relay] Found sender with %u endpoints\n", endpoint_count);
}

static void on_relay_disconnected(void* context) {
    printf("\n[Relay] Disconnected from relay\n");
}

//=============================================================================
// Send Mode (Sender)
//=============================================================================

static int cmd_send(const char* filepath) {
    printf("\n═══════════════════════════════════════\n");
    printf("  WORMHOLE SEND\n");
    printf("═══════════════════════════════════════\n\n");
    
    // Check if file exists
    if (!FileExists(filepath)) {
        printf("Error: File not found: %s\n", filepath);
        return 1;
    }
    
    uint64_t filesize = GetWormholeFileSize(filepath);
    const char* filename = ExtractFilename(filepath);
    
    printf("[Send] File: %s\n", filename);
    printf("[Send] Size: %llu bytes\n\n", (unsigned long long)filesize);
    
    // Initialize PeerID (Ed25519 keypair)
    if (!PeerID_Init()) {
        printf("Error: Failed to initialize cryptography\n");
        return 1;
    }
    
    // Load or generate identity
    KEYPAIR keypair;
    char* identity_path = PeerID_GetDefaultPath();
    if (!PeerID_LoadOrGenerate(&keypair, identity_path)) {
        printf("Error: Failed to load identity\n");
        free(identity_path);
        return 1;
    }
    free(identity_path);
    
    // Create relay client
    RELAY_CLIENT_CONFIG relay_config = {
        .relay_host = DEFAULT_RELAY_HOST,
        .relay_port = DEFAULT_RELAY_PORT,
        .keypair = &keypair,
        .on_connected = on_relay_connected,
        .on_ticket_created = on_ticket_created,
        .on_peer_info = on_peer_info,
        .on_disconnected = on_relay_disconnected,
        .callback_context = NULL
    };
    
    RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
    if (!relay_client) {
        printf("Error: Failed to create relay client\n");
        return 1;
    }
    
    // Discover endpoints
    printf("[Send] Discovering network endpoints...\n");
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);
    
    if (endpoint_count == 0) {
        printf("[Send] Warning: No endpoints found, using placeholder\n");
        memset(&endpoints[0], 0, sizeof(ENDPOINT));
        endpoints[0].addr_type = 0x04;
        endpoints[0].addr[0] = 127;
        endpoints[0].addr[3] = 1;
        endpoints[0].port = WORMHOLE_DEFAULT_PORT;
        endpoint_count = 1;
    }
    
    // Register with relay
    printf("[Send] Registering with relay server...\n");
    if (!RelayClient_Register(relay_client, endpoints, endpoint_count)) {
        printf("Error: Failed to register with relay\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Wait for registration
    for (int i = 0; i < 50; i++) {
        if (RelayClient_IsConnected(relay_client)) break;
        RelayClient_Poll(relay_client, 100);
    }
    
    if (!RelayClient_IsConnected(relay_client)) {
        printf("Error: Failed to connect to relay (timeout)\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Create ticket
    printf("[Send] Creating transfer ticket...\n");
    if (!RelayClient_CreateTicket(relay_client, filesize, filename)) {
        printf("Error: Failed to create ticket\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Wait for ticket
    for (int i = 0; i < 50 && !g_ticket_ready; i++) {
        RelayClient_Poll(relay_client, 100);
    }
    
    if (!g_ticket_ready) {
        printf("Error: Failed to receive ticket (timeout)\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Display ticket
    Ticket_PrintForSharing(g_ticket, filename, filesize);
    
    // TODO: Wait for receiver to connect and transfer file
    printf("\n[Send] Waiting for receiver (press Ctrl+C to cancel)...\n");
    printf("[Send] NOTE: Full file transfer integration is in progress\n\n");
    
    // Keep relay connection alive
    for (int i = 0; i < 600; i++) {  // 60 seconds max
        RelayClient_Poll(relay_client, 100);
        if (i % 100 == 0) {
            RelayClient_SendKeepalive(relay_client);
        }
    }
    
    // Cleanup
    RelayClient_SendGoodbye(relay_client, 0x02);  // Transfer complete
    RelayClient_Destroy(relay_client);
    
    printf("\n[Send] Session ended\n");
    return 0;
}

//=============================================================================
// Receive Mode (Receiver)
//=============================================================================

static int cmd_receive(const char* ticket) {
    printf("\n═══════════════════════════════════════\n");
    printf("  WORMHOLE RECEIVE\n");
    printf("═══════════════════════════════════════\n\n");
    
    // Validate ticket
    if (!Ticket_Validate(ticket)) {
        printf("Error: Invalid ticket format\n");
        printf("Expected format: N-word-word (e.g., 7-guitar-battery)\n");
        return 1;
    }
    
    Ticket_PrintReceiving(ticket);
    
    // Initialize PeerID
    if (!PeerID_Init()) {
        printf("Error: Failed to initialize cryptography\n");
        return 1;
    }
    
    // Load or generate identity
    KEYPAIR keypair;
    char* identity_path = PeerID_GetDefaultPath();
    if (!PeerID_LoadOrGenerate(&keypair, identity_path)) {
        printf("Error: Failed to load identity\n");
        free(identity_path);
        return 1;
    }
    free(identity_path);
    
    // Create relay client
    RELAY_CLIENT_CONFIG relay_config = {
        .relay_host = DEFAULT_RELAY_HOST,
        .relay_port = DEFAULT_RELAY_PORT,
        .keypair = &keypair,
        .on_connected = on_relay_connected,
        .on_ticket_created = on_ticket_created,
        .on_peer_info = on_peer_info,
        .on_disconnected = on_relay_disconnected,
        .callback_context = NULL
    };
    
    RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
    if (!relay_client) {
        printf("Error: Failed to create relay client\n");
        return 1;
    }
    
    // Discover endpoints
    printf("[Receive] Discovering network endpoints...\n");
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);
    
    if (endpoint_count == 0) {
        memset(&endpoints[0], 0, sizeof(ENDPOINT));
        endpoints[0].addr_type = 0x04;
        endpoints[0].addr[0] = 127;
        endpoints[0].addr[3] = 1;
        endpoints[0].port = WORMHOLE_DEFAULT_PORT;
        endpoint_count = 1;
    }
    
    // Register with relay
    printf("[Receive] Registering with relay server...\n");
    if (!RelayClient_Register(relay_client, endpoints, endpoint_count)) {
        printf("Error: Failed to register with relay\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Wait for registration
    for (int i = 0; i < 50; i++) {
        if (RelayClient_IsConnected(relay_client)) break;
        RelayClient_Poll(relay_client, 100);
    }
    
    if (!RelayClient_IsConnected(relay_client)) {
        printf("Error: Failed to connect to relay (timeout)\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Lookup sender
    printf("[Receive] Looking up sender...\n");
    if (!RelayClient_LookupTicket(relay_client, ticket)) {
        printf("Error: Failed to lookup ticket\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Wait for peer info
    for (int i = 0; i < 50 && !g_peer_info_ready; i++) {
        RelayClient_Poll(relay_client, 100);
    }
    
    if (!g_peer_info_ready) {
        printf("Error: Ticket not found or expired\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Print peer info
    char peer_id_hex[65];
    PeerID_ToHex(g_peer_id, peer_id_hex);
    printf("\n[Receive] Sender PeerID: %s\n", peer_id_hex);
    printf("[Receive] Sender has %u endpoints\n", g_peer_endpoint_count);
    
    // Create connection manager
    CONNECTION_MANAGER* conn_mgr = ConnectionManager_Create();
    if (!conn_mgr) {
        printf("Error: Failed to create connection manager\n");
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Start connection attempts
    printf("[Receive] Attempting connections to sender...\n");
    if (!ConnectionManager_ConnectToPeer(conn_mgr, g_peer_id, g_peer_endpoints,
                                         g_peer_endpoint_count, relay_client)) {
        printf("Error: Failed to start connection attempts\n");
        ConnectionManager_Destroy(conn_mgr);
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Wait for connection
    CONN_RESULT result = ConnectionManager_Poll(conn_mgr, 5000);
    if (result != CONN_RESULT_SUCCESS) {
        printf("Error: Failed to connect to sender\n");
        ConnectionManager_Destroy(conn_mgr);
        RelayClient_Destroy(relay_client);
        return 1;
    }
    
    // Print connection info
    CONN_TYPE conn_type = ConnectionManager_GetConnectionType(conn_mgr);
    const char* conn_type_str = "Unknown";
    switch (conn_type) {
        case CONN_TYPE_DIRECT_LAN: conn_type_str = "Direct (LAN)"; break;
        case CONN_TYPE_DIRECT_PUBLIC: conn_type_str = "Direct (Public IP)"; break;
        case CONN_TYPE_DIRECT_IPV6: conn_type_str = "Direct (IPv6)"; break;
        case CONN_TYPE_RELAY_FORWARDED: conn_type_str = "Relay (Forwarded)"; break;
    }
    
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  CONNECTION ESTABLISHED                                   ║\n");
    printf("║  Type: %-50s║\n", conn_type_str);
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    // TODO: Receive file using QUIC
    printf("[Receive] NOTE: Full file transfer integration is in progress\n");
    printf("[Receive] File download would start here...\n\n");
    
    // Cleanup
    ConnectionManager_Destroy(conn_mgr);
    RelayClient_SendGoodbye(relay_client, 0x02);
    RelayClient_Destroy(relay_client);
    
    printf("[Receive] Session ended\n");
    return 0;
}

//=============================================================================
// Main & CLI
//=============================================================================

static void print_usage(void) {
    printf("\nWormhole - Secure P2P File Transfer\n\n");
    printf("Usage:\n");
    printf("  wormhole send <filename>      Send a file and get a ticket\n");
    printf("  wormhole receive <ticket>     Receive a file using a ticket\n");
    printf("  wormhole --help               Show this help message\n\n");
    printf("Examples:\n");
    printf("  wormhole send document.pdf\n");
    printf("  wormhole receive 7-guitar-battery\n\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage();
        return 0;
    }
    else if (strcmp(command, "send") == 0) {
        if (argc < 3) {
            printf("Error: Missing filename\n");
            print_usage();
            return 1;
        }
        return cmd_send(argv[2]);
    }
    else if (strcmp(command, "receive") == 0) {
        if (argc < 3) {
            printf("Error: Missing ticket\n");
            print_usage();
            return 1;
        }
        return cmd_receive(argv[2]);
    }
    else {
        printf("Error: Unknown command: %s\n", command);
        print_usage();
        return 1;
    }
    
    return 0;
}
