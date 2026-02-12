//
// server.c
// Wormhole Relay Server - Main UDP Server Loop
// by Dylan Kress
//

#include "server.h"
#include "relay_protocol.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#endif

#define MAX_PACKET_SIZE 65536

// Forward declarations for message handlers
static void handle_register(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                           const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_create_ticket(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                                const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_lookup(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                         const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_forward(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                          const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_keepalive(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                            const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_goodbye(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                          const struct sockaddr* client_addr, socklen_t addr_len);

// Send UDP packet
static bool send_packet(int socket_fd, const void* data, size_t len,
                       const struct sockaddr* dest_addr, socklen_t addr_len) {
    ssize_t sent = sendto(socket_fd, data, len, 0, dest_addr, addr_len);
    if (sent != (ssize_t)len) {
        fprintf(stderr, "[Server] Failed to send packet (%zd/%zu bytes)\n", sent, len);
        return false;
    }
    return true;
}

bool RelayServer_Init(RELAY_SERVER* server, const SERVER_CONFIG* config) {
    if (!server || !config) {
        return false;
    }
    
    memset(server, 0, sizeof(RELAY_SERVER));
    
    // Initialize libsodium
    if (!Crypto_Init()) {
        fprintf(stderr, "[Server] Failed to initialize cryptography\n");
        return false;
    }
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "[Server] WSAStartup failed\n");
        Crypto_Cleanup();
        return false;
    }
#endif
    
    // Create UDP socket
    server->socket_fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (server->socket_fd < 0) {
        fprintf(stderr, "[Server] Failed to create socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    
    // Enable dual-stack (IPv4 + IPv6)
    int ipv6only = 0;
    setsockopt(server->socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&ipv6only, sizeof(ipv6only));
    
    // Bind to port
    struct sockaddr_in6 bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin6_family = AF_INET6;
    bind_addr.sin6_addr = in6addr_any;
    bind_addr.sin6_port = htons(config->port);
    
    if (bind(server->socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        fprintf(stderr, "[Server] Failed to bind to port %u\n", config->port);
#ifdef _WIN32
        closesocket(server->socket_fd);
        WSACleanup();
#else
        close(server->socket_fd);
#endif
        return false;
    }
    
    // Initialize subsystems
    if (!PeerRegistry_Init(&server->peer_registry, config->max_peers)) {
        fprintf(stderr, "[Server] Failed to initialize peer registry\n");
#ifdef _WIN32
        closesocket(server->socket_fd);
        WSACleanup();
#else
        close(server->socket_fd);
#endif
        return false;
    }
    
    if (!TicketManager_Init(&server->ticket_manager, config->max_tickets, config->wordlist_path)) {
        fprintf(stderr, "[Server] Failed to initialize ticket manager\n");
        PeerRegistry_Cleanup(&server->peer_registry);
#ifdef _WIN32
        closesocket(server->socket_fd);
        WSACleanup();
#else
        close(server->socket_fd);
#endif
        return false;
    }
    
    if (!RateLimiter_Init(&server->rate_limiter, 1024)) {
        fprintf(stderr, "[Server] Failed to initialize rate limiter\n");
        TicketManager_Cleanup(&server->ticket_manager);
        PeerRegistry_Cleanup(&server->peer_registry);
#ifdef _WIN32
        closesocket(server->socket_fd);
        WSACleanup();
#else
        close(server->socket_fd);
#endif
        return false;
    }
    
    server->running = true;
    server->total_packets_received = 0;
    server->total_packets_sent = 0;
    server->total_bytes_forwarded = 0;
    
#ifdef _WIN32
    InitializeCriticalSection(&server->stats_lock);
#else
    pthread_mutex_init(&server->stats_lock, NULL);
#endif
    
    printf("[Server] Initialized on port %u (max peers: %u, max tickets: %u)\n",
           config->port, config->max_peers, config->max_tickets);
    return true;
}

void RelayServer_Stop(RELAY_SERVER* server) {
    if (server) {
        server->running = false;
        printf("[Server] Stopping...\n");
    }
}

void RelayServer_Cleanup(RELAY_SERVER* server) {
    if (!server) {
        return;
    }
    
    server->running = false;
    
    RateLimiter_Cleanup(&server->rate_limiter);
    TicketManager_Cleanup(&server->ticket_manager);
    PeerRegistry_Cleanup(&server->peer_registry);
    
#ifdef _WIN32
    closesocket(server->socket_fd);
    DeleteCriticalSection(&server->stats_lock);
    WSACleanup();
#else
    close(server->socket_fd);
    pthread_mutex_destroy(&server->stats_lock);
#endif
    
    Crypto_Cleanup();
    
    printf("[Server] Cleaned up\n");
}

int RelayServer_Run(RELAY_SERVER* server) {
    if (!server) {
        return -1;
    }
    
    uint8_t packet_buffer[MAX_PACKET_SIZE];
    struct sockaddr_storage client_addr;
    socklen_t addr_len;
    
    time_t last_cleanup = time(NULL);
    
    printf("[Server] Running (listening for packets)...\n");
    
    while (server->running) {
        addr_len = sizeof(client_addr);
        
        // Receive packet
        ssize_t recv_len = recvfrom(server->socket_fd, (char*)packet_buffer, MAX_PACKET_SIZE, 0,
                                    (struct sockaddr*)&client_addr, &addr_len);
        
        if (recv_len <= 0) {
            if (recv_len < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEINTR) continue;  // Interrupted, try again
#else
                if (errno == EINTR) continue;
#endif
                fprintf(stderr, "[Server] recvfrom error\n");
            }
            continue;
        }
        
        // Update stats
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_received++;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
        
        // Rate limiting check
        if (!RateLimiter_CheckAndUpdate(&server->rate_limiter, (struct sockaddr*)&client_addr, addr_len)) {
            printf("[Server] Rate limit exceeded for client, dropping packet\n");
            continue;
        }
        
        // Parse message type
        if (recv_len < 1) {
            fprintf(stderr, "[Server] Packet too short (no message type)\n");
            continue;
        }
        
        uint8_t msg_type = packet_buffer[0];
        
        // Dispatch to handler
        switch (msg_type) {
            case RELAY_MSG_REGISTER:
                handle_register(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            case RELAY_MSG_CREATE_TICKET:
                handle_create_ticket(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            case RELAY_MSG_LOOKUP:
                handle_lookup(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            case RELAY_MSG_FORWARD:
                handle_forward(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            case RELAY_MSG_KEEPALIVE:
                handle_keepalive(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            case RELAY_MSG_GOODBYE:
                handle_goodbye(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            default:
                fprintf(stderr, "[Server] Unknown message type: 0x%02X\n", msg_type);
                break;
        }
        
        // Periodic cleanup (every 30 seconds)
        time_t now = time(NULL);
        if (now - last_cleanup > 30) {
            PeerRegistry_RemoveStalePeers(&server->peer_registry, now);
            TicketManager_RemoveExpiredTickets(&server->ticket_manager, now);
            RateLimiter_RemoveStaleEntries(&server->rate_limiter, now);
            last_cleanup = now;
        }
    }
    
    printf("[Server] Stopped\n");
    return 0;
}

void RelayServer_PrintStats(RELAY_SERVER* server) {
    if (!server) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&server->stats_lock);
#else
    pthread_mutex_lock(&server->stats_lock);
#endif
    
    uint32_t peer_count, ticket_count, tracked_ips;
    PeerRegistry_GetStats(&server->peer_registry, &peer_count, NULL);
    TicketManager_GetStats(&server->ticket_manager, &ticket_count, NULL);
    RateLimiter_GetStats(&server->rate_limiter, &tracked_ips, NULL);
    
    printf("\n========== Relay Server Statistics ==========\n");
    printf("Packets received:  %llu\n", (unsigned long long)server->total_packets_received);
    printf("Packets sent:      %llu\n", (unsigned long long)server->total_packets_sent);
    printf("Bytes forwarded:   %llu\n", (unsigned long long)server->total_bytes_forwarded);
    printf("Active peers:      %u\n", peer_count);
    printf("Active tickets:    %u\n", ticket_count);
    printf("Tracked IPs:       %u\n", tracked_ips);
    printf("=============================================\n\n");
    
#ifdef _WIN32
    LeaveCriticalSection(&server->stats_lock);
#else
    pthread_mutex_unlock(&server->stats_lock);
#endif
}

// ============================================================================
// Message Handlers
// ============================================================================

static void handle_register(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                           const struct sockaddr* client_addr, socklen_t addr_len) {
    printf("[Server] REGISTER from client\n");
    
    // Parse message
    if (len < sizeof(RegisterMsg)) {
        fprintf(stderr, "[Server] REGISTER message too short\n");
        return;
    }
    
    const RegisterMsg* msg = (const RegisterMsg*)packet;
    
    // Parse endpoints
    size_t header_size = sizeof(RegisterMsg);
    size_t endpoints_size = msg->endpoint_count * sizeof(ENDPOINT);
    
    if (len < header_size + endpoints_size) {
        fprintf(stderr, "[Server] REGISTER message truncated (missing endpoints)\n");
        return;
    }
    
    const ENDPOINT* endpoints = (const ENDPOINT*)(packet + header_size);
    
    // Verify Ed25519 signature
    if (!Crypto_VerifyRegisterSignature(msg->peer_id, msg->signature, msg->timestamp,
                                        endpoints, msg->endpoint_count)) {
        fprintf(stderr, "[Server] REGISTER signature verification failed\n");
        
        // Send error response
        RegisteredMsg response;
        response.message_type = RELAY_MSG_REGISTERED;
        response.status = 0x01;  // Error
        response.session_id = 0;
        
        send_packet(server->socket_fd, &response, sizeof(response), client_addr, addr_len);
        return;
    }
    
    // Register peer
    uint64_t session_id = PeerRegistry_RegisterPeer(
        &server->peer_registry,
        msg->peer_id,
        client_addr,
        addr_len,
        endpoints,
        msg->endpoint_count
    );
    
    if (session_id == 0) {
        fprintf(stderr, "[Server] Failed to register peer\n");
        
        // Send error response
        RegisteredMsg response;
        response.message_type = RELAY_MSG_REGISTERED;
        response.status = 0x01;  // Error
        response.session_id = 0;
        
        send_packet(server->socket_fd, &response, sizeof(response), client_addr, addr_len);
        return;
    }
    
    // Extract observed address (NAT reflection)
    RegisteredMsg response;
    memset(&response, 0, sizeof(response));
    response.message_type = RELAY_MSG_REGISTERED;
    response.status = 0x00;  // Success
    response.session_id = session_id;
    
    if (client_addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr4 = (const struct sockaddr_in*)client_addr;
        response.observed_addr_type = 0x04;
        memcpy(response.observed_addr, &addr4->sin_addr, 4);
        response.observed_port = addr4->sin_port;
    } else if (client_addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)client_addr;
        response.observed_addr_type = 0x06;
        memcpy(response.observed_addr, &addr6->sin6_addr, 16);
        response.observed_port = addr6->sin6_port;
    }
    
    // Send response
    if (send_packet(server->socket_fd, &response, sizeof(response), client_addr, addr_len)) {
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_sent++;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
        
        printf("[Server] REGISTERED sent (session %llu)\n", (unsigned long long)session_id);
    }
}

static void handle_create_ticket(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                                const struct sockaddr* client_addr, socklen_t addr_len) {
    printf("[Server] CREATE_TICKET from client\n");
    
    // Parse message
    if (len < sizeof(CreateTicketMsg)) {
        fprintf(stderr, "[Server] CREATE_TICKET message too short\n");
        return;
    }
    
    const CreateTicketMsg* msg = (const CreateTicketMsg*)packet;
    
    // Extract filename
    size_t header_size = sizeof(CreateTicketMsg);
    if (len < header_size + msg->filename_length) {
        fprintf(stderr, "[Server] CREATE_TICKET message truncated (missing filename)\n");
        return;
    }
    
    const char* filename_data = (const char*)(packet + header_size);
    char filename[MAX_FILENAME_LENGTH + 1];
    size_t filename_len = (msg->filename_length < MAX_FILENAME_LENGTH) ? msg->filename_length : MAX_FILENAME_LENGTH;
    memcpy(filename, filename_data, filename_len);
    filename[filename_len] = '\0';
    
    // Find peer by session ID
    PEER_ENTRY* peer = PeerRegistry_FindBySessionID(&server->peer_registry, msg->session_id);
    if (!peer) {
        fprintf(stderr, "[Server] CREATE_TICKET: Invalid session ID %llu\n",
                (unsigned long long)msg->session_id);
        return;
    }
    
    // Generate ticket
    char ticket[TICKET_FORMAT_LENGTH];
    const char* ticket_str = TicketManager_GenerateTicket(
        &server->ticket_manager,
        peer->peer_id,
        msg->file_size,
        filename,
        ticket,
        sizeof(ticket)
    );
    
    if (!ticket_str) {
        fprintf(stderr, "[Server] Failed to generate ticket\n");
        return;
    }
    
    // Associate ticket with peer
    PeerRegistry_SetTicket(&server->peer_registry, msg->session_id, ticket_str);
    
    // Send response
    size_t ticket_len = strlen(ticket_str);
    size_t response_size = sizeof(TicketCreatedMsg) + ticket_len;
    uint8_t* response_buffer = (uint8_t*)malloc(response_size);
    
    if (!response_buffer) {
        fprintf(stderr, "[Server] Failed to allocate response buffer\n");
        return;
    }
    
    TicketCreatedMsg* response = (TicketCreatedMsg*)response_buffer;
    response->message_type = RELAY_MSG_TICKET_CREATED;
    response->ticket_length = (uint8_t)ticket_len;
    memcpy(response_buffer + sizeof(TicketCreatedMsg), ticket_str, ticket_len);
    
    if (send_packet(server->socket_fd, response_buffer, response_size, client_addr, addr_len)) {
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_sent++;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
        
        printf("[Server] TICKET_CREATED sent: %s\n", ticket_str);
    }
    
    free(response_buffer);
}

static void handle_lookup(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                         const struct sockaddr* client_addr, socklen_t addr_len) {
    printf("[Server] LOOKUP from client\n");
    
    // Parse message
    if (len < sizeof(LookupMsg)) {
        fprintf(stderr, "[Server] LOOKUP message too short\n");
        return;
    }
    
    const LookupMsg* msg = (const LookupMsg*)packet;
    
    // Extract ticket
    size_t header_size = sizeof(LookupMsg);
    if (len < header_size + msg->ticket_length) {
        fprintf(stderr, "[Server] LOOKUP message truncated (missing ticket)\n");
        return;
    }
    
    const char* ticket_data = (const char*)(packet + header_size);
    char ticket[MAX_TICKET_LENGTH + 1];
    size_t ticket_len = (msg->ticket_length < MAX_TICKET_LENGTH) ? msg->ticket_length : MAX_TICKET_LENGTH;
    memcpy(ticket, ticket_data, ticket_len);
    ticket[ticket_len] = '\0';
    
    // Lookup ticket
    TICKET_INFO* ticket_info = TicketManager_LookupTicket(&server->ticket_manager, ticket);
    if (!ticket_info) {
        fprintf(stderr, "[Server] Ticket not found or expired: %s\n", ticket);
        return;
    }
    
    // Find sender peer
    PEER_ENTRY* sender = PeerRegistry_FindByPeerID(&server->peer_registry, ticket_info->sender_peer_id);
    if (!sender) {
        fprintf(stderr, "[Server] Sender peer not found for ticket: %s\n", ticket);
        return;
    }
    
    // Send PEER_INFO to receiver
    size_t response_size = sizeof(PeerInfoMsg) + sender->endpoint_count * sizeof(ENDPOINT);
    uint8_t* response_buffer = (uint8_t*)malloc(response_size);
    
    if (!response_buffer) {
        fprintf(stderr, "[Server] Failed to allocate response buffer\n");
        return;
    }
    
    PeerInfoMsg* response = (PeerInfoMsg*)response_buffer;
    response->message_type = RELAY_MSG_PEER_INFO;
    memcpy(response->peer_id, sender->peer_id, 32);
    response->endpoint_count = sender->endpoint_count;
    memcpy(response_buffer + sizeof(PeerInfoMsg), sender->endpoints,
           sender->endpoint_count * sizeof(ENDPOINT));
    
    if (send_packet(server->socket_fd, response_buffer, response_size, client_addr, addr_len)) {
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_sent++;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
        
        printf("[Server] PEER_INFO sent (%u endpoints)\n", sender->endpoint_count);
    }
    
    free(response_buffer);
}

static void handle_forward(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                          const struct sockaddr* client_addr, socklen_t addr_len) {
    // Parse message
    if (len < sizeof(ForwardMsg)) {
        fprintf(stderr, "[Server] FORWARD message too short\n");
        return;
    }
    
    const ForwardMsg* msg = (const ForwardMsg*)packet;
    
    // Find destination peer
    PEER_ENTRY* dest = PeerRegistry_FindByPeerID(&server->peer_registry, msg->dest_peer_id);
    if (!dest) {
        fprintf(stderr, "[Server] FORWARD: Destination peer not found\n");
        return;
    }
    
    // Extract payload
    size_t header_size = sizeof(ForwardMsg);
    if (len < header_size + msg->payload_length) {
        fprintf(stderr, "[Server] FORWARD message truncated (missing payload)\n");
        return;
    }
    
    const uint8_t* payload = packet + header_size;
    
    // Forward to destination
    if (send_packet(server->socket_fd, payload, msg->payload_length,
                   (struct sockaddr*)&dest->socket_addr, dest->addr_len)) {
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_sent++;
        server->total_bytes_forwarded += msg->payload_length;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
        
        printf("[Server] FORWARD: %u bytes to peer\n", msg->payload_length);
    }
}

static void handle_keepalive(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                            const struct sockaddr* client_addr, socklen_t addr_len) {
    // Just echo back the keepalive
    if (send_packet(server->socket_fd, packet, len, client_addr, addr_len)) {
#ifdef _WIN32
        EnterCriticalSection(&server->stats_lock);
#else
        pthread_mutex_lock(&server->stats_lock);
#endif
        server->total_packets_sent++;
#ifdef _WIN32
        LeaveCriticalSection(&server->stats_lock);
#else
        pthread_mutex_unlock(&server->stats_lock);
#endif
    }
}

static void handle_goodbye(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                          const struct sockaddr* client_addr, socklen_t addr_len) {
    if (len < sizeof(GoodbyeMsg)) {
        return;
    }
    
    const GoodbyeMsg* msg = (const GoodbyeMsg*)packet;
    
    const char* reason_str = "unknown";
    if (msg->reason == 0x00) reason_str = "upgraded to direct";
    else if (msg->reason == 0x01) reason_str = "error";
    else if (msg->reason == 0x02) reason_str = "transfer complete";
    
    printf("[Server] GOODBYE from client (reason: %s)\n", reason_str);
}
