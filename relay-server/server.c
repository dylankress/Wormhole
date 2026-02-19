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
#include <sodium.h>

// ============================================================================
// DHT bootstrap constants (subset of src/dht/dht_protocol.h)
// ============================================================================

#define DHT_MSG_PING                0x20
#define DHT_MSG_PONG                0x21
#define DHT_MSG_FIND_NODE           0x22
#define DHT_MSG_FIND_NODE_RESPONSE  0x23
#define DHT_MSG_FIND_VALUE          0x24
#define DHT_MSG_STORE               0x26
#define DHT_HEADER_SIZE             102
#define DHT_NODE_INFO_SIZE          51
#define DHT_SIGNATURE_OFFSET        38
#define DHT_K                       20
#define DHT_PROTOCOL_VERSION        1
#define DHT_ADDR_IPV4               0x04
#define DHT_ADDR_IPV6               0x06

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;
typedef int socklen_t;
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
static void handle_find_peers(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                              const struct sockaddr* client_addr, socklen_t addr_len);
static void handle_dht_message(RELAY_SERVER* server, const uint8_t* packet, size_t len,
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

    // Store relay identity for fallback endpoint
    server->listen_port = 0;
    server->relay_addr_type = 0;
    memset(server->relay_addr, 0, sizeof(server->relay_addr));

    if (config->public_addr) {
        struct in_addr addr4;
        struct in6_addr addr6;
        if (inet_pton(AF_INET, config->public_addr, &addr4) == 1) {
            server->relay_addr_type = 0x04;
            memcpy(server->relay_addr, &addr4, 4);
            server->listen_port = config->port;
            printf("[Server] Relay endpoint: %s:%u (IPv4)\n", config->public_addr, config->port);
        } else if (inet_pton(AF_INET6, config->public_addr, &addr6) == 1) {
            server->relay_addr_type = 0x06;
            memcpy(server->relay_addr, &addr6, 16);
            server->listen_port = config->port;
            printf("[Server] Relay endpoint: [%s]:%u (IPv6)\n", config->public_addr, config->port);
        } else {
            fprintf(stderr, "[Server] Warning: Invalid --public-addr '%s', relay endpoint disabled\n",
                    config->public_addr);
        }
    } else {
        printf("[Server] Warning: No --public-addr specified, relay fallback endpoint disabled\n");
    }

#ifdef _WIN32
    InitializeCriticalSection(&server->stats_lock);
#else
    pthread_mutex_init(&server->stats_lock, NULL);
#endif

    // Generate DHT bootstrap keypair (ephemeral, new each run)
    crypto_sign_keypair(server->dht_public_key, server->dht_secret_key);
    printf("[Server] DHT bootstrap identity generated\n");

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
    time_t last_ratelimit_cleanup = time(NULL);

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
            case RELAY_MSG_FIND_PEERS:
                handle_find_peers(server, packet_buffer, recv_len, (struct sockaddr*)&client_addr, addr_len);
                break;
            default:
                // Check for DHT messages (0x20-0x27)
                if (msg_type >= DHT_MSG_PING && msg_type <= DHT_MSG_STORE + 1) {
                    handle_dht_message(server, packet_buffer, recv_len,
                                       (struct sockaddr*)&client_addr, addr_len);
                } else {
                    fprintf(stderr, "[Server] Unknown message type: 0x%02X\n", msg_type);
                }
                break;
        }
        
        // Periodic cleanup (every 30 seconds for peers/tickets)
        time_t now = time(NULL);
        if (now - last_cleanup > 30) {
            PeerRegistry_RemoveStalePeers(&server->peer_registry, now);
            TicketManager_RemoveExpiredTickets(&server->ticket_manager, now);
            last_cleanup = now;
        }

        // Rate limiter cleanup more frequently (every 10 seconds) to bound table size
        if (now - last_ratelimit_cleanup > 10) {
            RateLimiter_RemoveStaleEntries(&server->rate_limiter, now);
            last_ratelimit_cleanup = now;
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

    // Validate endpoint count to prevent overflow
    if (msg->endpoint_count > MAX_ENDPOINTS) {
        fprintf(stderr, "[Server] REGISTER: endpoint count %u exceeds MAX_ENDPOINTS (%u)\n",
                msg->endpoint_count, MAX_ENDPOINTS);
        return;
    }

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
        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
            response.observed_addr_type = 0x04;
            memcpy(response.observed_addr, &addr6->sin6_addr.s6_addr[12], 4);
            response.observed_port = addr6->sin6_port;
        } else {
            response.observed_addr_type = 0x06;
            memcpy(response.observed_addr, &addr6->sin6_addr, 16);
            response.observed_port = addr6->sin6_port;
        }
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
    /* TODO: No sender authentication - relies on IP:port matching.
       Add Ed25519 signature verification in future. */

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

// Helper: Build an ENDPOINT from a sockaddr (extracts IP + port, assigns priority)
static bool endpoint_from_sockaddr(const struct sockaddr* addr, ENDPOINT* ep, uint8_t priority) {
    memset(ep, 0, sizeof(ENDPOINT));
    ep->priority = priority;

    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr4 = (const struct sockaddr_in*)addr;
        ep->addr_type = 0x04;
        memcpy(ep->addr, &addr4->sin_addr, 4);
        ep->port = ntohs(addr4->sin_port);  // Store in host byte order (little-endian)
        return true;
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)addr;
        // Check for IPv4-mapped IPv6 (::ffff:x.x.x.x) — extract pure IPv4
        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
            ep->addr_type = 0x04;
            memcpy(ep->addr, &addr6->sin6_addr.s6_addr[12], 4);  // Last 4 bytes = IPv4
            ep->port = ntohs(addr6->sin6_port);
            return true;
        }
        ep->addr_type = 0x06;
        memcpy(ep->addr, &addr6->sin6_addr, 16);
        ep->port = ntohs(addr6->sin6_port);  // Store in host byte order (little-endian)
        return true;
    }

    return false;
}

static void handle_lookup(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                         const struct sockaddr* client_addr, socklen_t addr_len) {
    /* TODO: No sender authentication - relies on IP:port matching.
       Add Ed25519 signature verification in future. */

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

    // Find receiver peer (by session_id from the LOOKUP message)
    PEER_ENTRY* receiver = PeerRegistry_FindBySessionID(&server->peer_registry, msg->session_id);

    // ---------------------------------------------------------------
    // Part 1: Send PEER_INFO to RECEIVER (sender's endpoints)
    //         + relay server as fallback endpoint (priority 200)
    // ---------------------------------------------------------------
    {
        uint16_t extra_endpoints = (server->listen_port > 0) ? 1 : 0;
        uint16_t total_ep_count = sender->endpoint_count + extra_endpoints;
        if (total_ep_count > MAX_ENDPOINTS) {
            printf("[Server] Warning: total endpoints %u exceeds MAX_ENDPOINTS, capping to %u\n",
                   total_ep_count, MAX_ENDPOINTS);
            total_ep_count = MAX_ENDPOINTS;
        }
        uint16_t sender_eps_to_copy = total_ep_count - extra_endpoints;
        size_t response_size = sizeof(PeerInfoMsg) + total_ep_count * sizeof(ENDPOINT);
        uint8_t* response_buffer = (uint8_t*)malloc(response_size);

        if (!response_buffer) {
            fprintf(stderr, "[Server] Failed to allocate response buffer\n");
            return;
        }

        PeerInfoMsg* response = (PeerInfoMsg*)response_buffer;
        response->message_type = RELAY_MSG_PEER_INFO;
        memcpy(response->peer_id, sender->peer_id, 32);
        response->endpoint_count = total_ep_count;

        // Copy sender's self-reported endpoints (capped to fit within total)
        ENDPOINT* ep_ptr = (ENDPOINT*)(response_buffer + sizeof(PeerInfoMsg));
        memcpy(ep_ptr, sender->endpoints, sender_eps_to_copy * sizeof(ENDPOINT));

        // Append relay server as fallback endpoint (priority 200)
        if (extra_endpoints > 0) {
            ENDPOINT* relay_ep = &ep_ptr[sender_eps_to_copy];
            memset(relay_ep, 0, sizeof(ENDPOINT));
            relay_ep->addr_type = server->relay_addr_type;
            memcpy(relay_ep->addr, server->relay_addr, sizeof(relay_ep->addr));
            relay_ep->port = server->listen_port;
            relay_ep->priority = 200;
        }

        printf("[Server] [DEBUG] Sending PEER_INFO to receiver with %u endpoints:\n", total_ep_count);
        for (uint16_t i = 0; i < total_ep_count; i++) {
            printf("[Server] [DEBUG]   EP[%u]: type=0x%02x, port=%u, priority=%u\n",
                   i, ep_ptr[i].addr_type, ep_ptr[i].port, ep_ptr[i].priority);
        }

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

            printf("[Server] PEER_INFO sent to receiver (%u endpoints)\n", total_ep_count);
        }

        free(response_buffer);
    }

    // ---------------------------------------------------------------
    // Part 2: Send PEER_INFO to SENDER (receiver's endpoints)
    //         This enables bidirectional hole punching
    // ---------------------------------------------------------------
    if (receiver) {
        // Build observed public endpoint from receiver's client_addr
        ENDPOINT observed_ep;
        bool has_observed = endpoint_from_sockaddr(client_addr, &observed_ep, 100);

        uint16_t recv_ep_count = receiver->endpoint_count;
        uint16_t extra = (has_observed ? 1 : 0) + ((server->listen_port > 0) ? 1 : 0);
        uint16_t total_ep_count = recv_ep_count + extra;
        if (total_ep_count > MAX_ENDPOINTS) {
            printf("[Server] Warning: total endpoints %u exceeds MAX_ENDPOINTS, capping to %u\n",
                   total_ep_count, MAX_ENDPOINTS);
            total_ep_count = MAX_ENDPOINTS;
        }
        // Cap receiver endpoints copied so extra endpoints fit within total
        uint16_t recv_eps_to_copy = total_ep_count - extra;
        size_t response_size = sizeof(PeerInfoMsg) + total_ep_count * sizeof(ENDPOINT);
        uint8_t* response_buffer = (uint8_t*)malloc(response_size);

        if (!response_buffer) {
            fprintf(stderr, "[Server] Failed to allocate response buffer for sender notification\n");
            return;
        }

        PeerInfoMsg* response = (PeerInfoMsg*)response_buffer;
        response->message_type = RELAY_MSG_PEER_INFO;
        memcpy(response->peer_id, receiver->peer_id, 32);
        response->endpoint_count = total_ep_count;

        // Copy receiver's self-reported endpoints (capped to fit within total)
        ENDPOINT* ep_ptr = (ENDPOINT*)(response_buffer + sizeof(PeerInfoMsg));
        memcpy(ep_ptr, receiver->endpoints, recv_eps_to_copy * sizeof(ENDPOINT));

        // Append receiver's observed public IP (NAT-mapped address) as priority 100
        uint16_t appended = 0;
        if (has_observed && (recv_eps_to_copy + appended) < total_ep_count) {
            ep_ptr[recv_eps_to_copy + appended] = observed_ep;
            appended++;
        }

        // Append relay server as fallback endpoint (priority 200)
        if (server->listen_port > 0 && (recv_eps_to_copy + appended) < total_ep_count) {
            ENDPOINT* relay_ep = &ep_ptr[recv_eps_to_copy + appended];
            memset(relay_ep, 0, sizeof(ENDPOINT));
            relay_ep->addr_type = server->relay_addr_type;
            memcpy(relay_ep->addr, server->relay_addr, sizeof(relay_ep->addr));
            relay_ep->port = server->listen_port;
            relay_ep->priority = 200;
            appended++;
        }

        printf("[Server] [DEBUG] Sending PEER_INFO to sender with %u endpoints:\n", total_ep_count);
        for (uint16_t i = 0; i < total_ep_count; i++) {
            printf("[Server] [DEBUG]   EP[%u]: type=0x%02x, port=%u, priority=%u\n",
                   i, ep_ptr[i].addr_type, ep_ptr[i].port, ep_ptr[i].priority);
        }

        if (send_packet(server->socket_fd, response_buffer, response_size,
                       (struct sockaddr*)&sender->socket_addr, sender->addr_len)) {
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

            printf("[Server] PEER_INFO sent to sender (%u endpoints, bidirectional notification)\n",
                   total_ep_count);
        }

        free(response_buffer);
    } else {
        printf("[Server] Warning: Could not find receiver peer (session %llu) for bidirectional notification\n",
               (unsigned long long)msg->session_id);
    }
}

static void handle_forward(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                          const struct sockaddr* client_addr, socklen_t addr_len) {
    /* TODO: FORWARD messages are unauthenticated - any peer can forge sender.
       Add HMAC or signature verification in future. */

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
    // Find peer by their socket address and update keepalive timestamp
    PEER_ENTRY* peer = PeerRegistry_FindByAddress(&server->peer_registry, client_addr, addr_len);
    if (peer) {
        PeerRegistry_UpdateKeepalive(&server->peer_registry, peer->session_id);
    }

    // Echo back the keepalive
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

    // Find and remove the peer from the registry immediately
    PEER_ENTRY* peer = PeerRegistry_FindByAddress(&server->peer_registry, client_addr, addr_len);
    if (peer) {
        uint64_t session_id = peer->session_id;
        if (PeerRegistry_RemovePeer(&server->peer_registry, session_id)) {
            printf("[Server] Removed peer (session %llu) on GOODBYE\n",
                   (unsigned long long)session_id);
        }
    }
}

static void handle_find_peers(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                              const struct sockaddr* client_addr, socklen_t addr_len) {
    /* TODO: No sender authentication - relies on IP:port matching.
       Add Ed25519 signature verification in future. */

    printf("[Server] FIND_PEERS from client\n");

    if (len < sizeof(FindPeersMsg)) {
        fprintf(stderr, "[Server] FIND_PEERS message too short\n");
        return;
    }

    const FindPeersMsg* msg = (const FindPeersMsg*)packet;

    // Validate session
    PEER_ENTRY* requester = PeerRegistry_FindBySessionID(&server->peer_registry, msg->session_id);
    if (!requester) {
        fprintf(stderr, "[Server] FIND_PEERS: Invalid session ID %llu\n",
                (unsigned long long)msg->session_id);
        return;
    }

    // Cap max_peers at protocol limit
    uint16_t max_peers = msg->max_peers;
    if (max_peers > MAX_FIND_PEERS) {
        max_peers = MAX_FIND_PEERS;
    }

    // Find active peers (registered > 60s, recent keepalive, exclude requester)
    PEER_ENTRY* found_peers[MAX_FIND_PEERS];
    time_t now = time(NULL);
    uint32_t peer_count = PeerRegistry_FindActivePeers(
        &server->peer_registry,
        requester->peer_id,
        now,
        5,   // min_age_sec: registered for at least 5 seconds
        found_peers,
        max_peers
    );

    // Build response: PeersFoundMsg header + per-peer { peer_id[32], endpoint_count(2), endpoints[] }
    // For each peer, inject the relay-observed public endpoint (like handle_lookup does)
    // Cap response to fit within UDP MTU (1400 bytes)
    #define MAX_UDP_PAYLOAD 1400
    size_t response_size = sizeof(PeersFoundMsg);
    uint32_t capped_peer_count = 0;
    for (uint32_t i = 0; i < peer_count; i++) {
        // Check if we need to inject an observed public endpoint for this peer
        ENDPOINT observed_ep;
        bool has_observed = endpoint_from_sockaddr(
            (const struct sockaddr*)&found_peers[i]->socket_addr, &observed_ep, 100);

        // Override port: use peer's QUIC listen port, not relay client's ephemeral port
        if (has_observed && found_peers[i]->endpoint_count > 0)
            observed_ep.port = found_peers[i]->endpoints[0].port;

        // Check for duplicate: skip if observed IP matches any self-reported endpoint
        if (has_observed) {
            for (uint16_t j = 0; j < found_peers[i]->endpoint_count; j++) {
                if (found_peers[i]->endpoints[j].addr_type == observed_ep.addr_type &&
                    memcmp(found_peers[i]->endpoints[j].addr, observed_ep.addr, 16) == 0) {
                    has_observed = false;
                    break;
                }
            }
        }

        uint16_t extra = has_observed ? 1 : 0;
        size_t entry_size = 32 + 2 + ((found_peers[i]->endpoint_count + extra) * sizeof(ENDPOINT));
        if (response_size + entry_size > MAX_UDP_PAYLOAD) {
            break;
        }
        response_size += entry_size;
        capped_peer_count++;
    }
    peer_count = capped_peer_count;

    uint8_t* response_buffer = (uint8_t*)malloc(response_size);
    if (!response_buffer) {
        fprintf(stderr, "[Server] Failed to allocate PEERS_FOUND response\n");
        return;
    }

    PeersFoundMsg* response = (PeersFoundMsg*)response_buffer;
    response->message_type = RELAY_MSG_PEERS_FOUND;
    response->peer_count = (uint16_t)peer_count;

    // Write peer entries
    uint8_t* write_ptr = response_buffer + sizeof(PeersFoundMsg);
    for (uint32_t i = 0; i < peer_count; i++) {
        PEER_ENTRY* peer = found_peers[i];

        // Build observed endpoint for this peer
        ENDPOINT observed_ep;
        bool has_observed = endpoint_from_sockaddr(
            (const struct sockaddr*)&peer->socket_addr, &observed_ep, 100);
        if (has_observed && peer->endpoint_count > 0)
            observed_ep.port = peer->endpoints[0].port;

        // Check for duplicate
        if (has_observed) {
            for (uint16_t j = 0; j < peer->endpoint_count; j++) {
                if (peer->endpoints[j].addr_type == observed_ep.addr_type &&
                    memcmp(peer->endpoints[j].addr, observed_ep.addr, 16) == 0) {
                    has_observed = false;
                    break;
                }
            }
        }

        uint16_t extra = has_observed ? 1 : 0;
        uint16_t ep_count = peer->endpoint_count + extra;

        // peer_id[32]
        memcpy(write_ptr, peer->peer_id, 32);
        write_ptr += 32;

        // endpoint_count (uint16, little-endian — packed struct on LE arch)
        memcpy(write_ptr, &ep_count, 2);
        write_ptr += 2;

        // Self-reported endpoints
        memcpy(write_ptr, peer->endpoints, peer->endpoint_count * sizeof(ENDPOINT));
        write_ptr += peer->endpoint_count * sizeof(ENDPOINT);

        // Observed public endpoint (if not duplicate)
        if (has_observed) {
            memcpy(write_ptr, &observed_ep, sizeof(ENDPOINT));
            write_ptr += sizeof(ENDPOINT);
        }
    }

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

        printf("[Server] PEERS_FOUND sent (%u peers)\n", peer_count);
    }

    free(response_buffer);
}

// ============================================================================
// DHT Bootstrap Handler
// The relay acts as a bootstrap-only DHT node: responds to PING and FIND_NODE.
// It does NOT handle STORE or FIND_VALUE (those are for full DHT nodes).
// ============================================================================

// Sign a DHT message buffer in-place using the relay's keypair.
static bool dht_sign_message(RELAY_SERVER* server, uint8_t* msg, size_t msg_len) {
    if (msg_len < DHT_HEADER_SIZE) return false;

    // Zero signature field before signing
    memset(msg + DHT_SIGNATURE_OFFSET, 0, 64);

    // Sign into separate buffer to avoid aliasing — Ed25519 reads msg twice
    // and writes R between reads, so sig output must not overlap msg input
    uint8_t sig[64];
    unsigned long long sig_len;
    if (crypto_sign_detached(sig, &sig_len,
                             msg, msg_len, server->dht_secret_key) != 0) {
        return false;
    }
    memcpy(msg + DHT_SIGNATURE_OFFSET, sig, 64);
    return true;
}

// Verify a DHT message signature. Mutates msg temporarily (zeroes + restores sig).
static bool dht_verify_message(const uint8_t sender_id[32], uint8_t* msg, size_t msg_len) {
    if (msg_len < DHT_HEADER_SIZE) return false;

    uint8_t sig[64];
    memcpy(sig, msg + DHT_SIGNATURE_OFFSET, 64);

    memset(msg + DHT_SIGNATURE_OFFSET, 0, 64);
    int result = crypto_sign_verify_detached(sig, msg, msg_len, sender_id);
    memcpy(msg + DHT_SIGNATURE_OFFSET, sig, 64);

    return (result == 0);
}

// Fill a DHT header in a response buffer.
static void dht_fill_header(RELAY_SERVER* server, uint8_t* buf, uint8_t msg_type, uint32_t txn_id) {
    buf[0] = msg_type;
    buf[1] = DHT_PROTOCOL_VERSION;
    // txn_id (little-endian at offset 2)
    buf[2] = (uint8_t)(txn_id);
    buf[3] = (uint8_t)(txn_id >> 8);
    buf[4] = (uint8_t)(txn_id >> 16);
    buf[5] = (uint8_t)(txn_id >> 24);
    // sender_id at offset 6
    memcpy(buf + 6, server->dht_public_key, 32);
    // signature at offset 38 (filled by dht_sign_message)
    memset(buf + DHT_SIGNATURE_OFFSET, 0, 64);
}

// Extract endpoint address from a PEER_ENTRY's socket_addr.
// Writes addr_type, addr[16], and port (host byte order).
static bool extract_peer_dht_addr(const PEER_ENTRY* peer, uint8_t* addr_type,
                                   uint8_t addr[16], uint16_t* port) {
    const struct sockaddr* sa = (const struct sockaddr*)&peer->socket_addr;

    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* sin4 = (const struct sockaddr_in*)sa;
        *addr_type = DHT_ADDR_IPV4;
        memset(addr, 0, 16);
        memcpy(addr, &sin4->sin_addr, 4);
        *port = ntohs(sin4->sin_port);
        return true;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)sa;
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            *addr_type = DHT_ADDR_IPV4;
            memset(addr, 0, 16);
            memcpy(addr, &sin6->sin6_addr.s6_addr[12], 4);
            *port = ntohs(sin6->sin6_port);
            return true;
        }
        *addr_type = DHT_ADDR_IPV6;
        memcpy(addr, &sin6->sin6_addr, 16);
        *port = ntohs(sin6->sin6_port);
        return true;
    }
    return false;
}

// XOR distance comparison: returns <0 if a closer to target than b.
static int xor_distance_cmp(const uint8_t a[32], const uint8_t b[32], const uint8_t target[32]) {
    for (int i = 0; i < 32; i++) {
        uint8_t da = a[i] ^ target[i];
        uint8_t db = b[i] ^ target[i];
        if (da < db) return -1;
        if (da > db) return 1;
    }
    return 0;
}

static void handle_dht_message(RELAY_SERVER* server, const uint8_t* packet, size_t len,
                               const struct sockaddr* client_addr, socklen_t addr_len) {
    if (len < DHT_HEADER_SIZE) {
        fprintf(stderr, "[Server] DHT message too short (%zu < %d)\n", len, DHT_HEADER_SIZE);
        return;
    }

    uint8_t msg_type = packet[0];

    // Extract sender_id from header (offset 6, 32 bytes)
    const uint8_t* sender_id = packet + 6;

    // Verify signature (need a mutable copy)
    uint8_t verify_buf[2048];
    if (len > sizeof(verify_buf)) {
        fprintf(stderr, "[Server] DHT message too large for verify buffer\n");
        return;
    }
    memcpy(verify_buf, packet, len);

    if (!dht_verify_message(sender_id, verify_buf, len)) {
        fprintf(stderr, "[Server] DHT signature verification failed (type 0x%02X)\n", msg_type);
        return;
    }

    // Extract txn_id (little-endian at offset 2)
    uint32_t txn_id = (uint32_t)packet[2] |
                      ((uint32_t)packet[3] << 8) |
                      ((uint32_t)packet[4] << 16) |
                      ((uint32_t)packet[5] << 24);

    switch (msg_type) {
        case DHT_MSG_PING: {
            // Respond with PONG (header only, 102 bytes)
            uint8_t pong[DHT_HEADER_SIZE];
            memset(pong, 0, sizeof(pong));
            dht_fill_header(server, pong, DHT_MSG_PONG, txn_id);

            if (dht_sign_message(server, pong, sizeof(pong))) {
                if (send_packet(server->socket_fd, pong, sizeof(pong), client_addr, addr_len)) {
                    server->total_packets_sent++;
                    printf("[Server] DHT PONG sent\n");
                }
            }
            break;
        }

        case DHT_MSG_FIND_NODE: {
            // FIND_NODE: header(102) + target_id(32) = 134 bytes
            if (len < DHT_HEADER_SIZE + 32) {
                fprintf(stderr, "[Server] DHT FIND_NODE too short\n");
                break;
            }

            const uint8_t* target_id = packet + DHT_HEADER_SIZE;

            // Get active peers from registry (exclude the requester)
            PEER_ENTRY* found_peers[MAX_FIND_PEERS];
            time_t now = time(NULL);
            uint32_t found_count = PeerRegistry_FindActivePeers(
                &server->peer_registry,
                sender_id,
                now,
                0,      // no min_age for DHT bootstrap
                found_peers,
                MAX_FIND_PEERS
            );

            // Sort by XOR distance to target (selection sort, small N)
            for (uint32_t i = 0; i < found_count && i < DHT_K; i++) {
                uint32_t best = i;
                for (uint32_t j = i + 1; j < found_count; j++) {
                    if (xor_distance_cmp(found_peers[j]->peer_id,
                                         found_peers[best]->peer_id, target_id) < 0) {
                        best = j;
                    }
                }
                if (best != i) {
                    PEER_ENTRY* tmp = found_peers[i];
                    found_peers[i] = found_peers[best];
                    found_peers[best] = tmp;
                }
            }

            // Cap at K
            uint8_t node_count = (uint8_t)(found_count < DHT_K ? found_count : DHT_K);

            // Build FIND_NODE_RESPONSE: header(102) + node_count(1) + N * DHT_NODE_INFO(51)
            size_t response_size = DHT_HEADER_SIZE + 1 + (size_t)node_count * DHT_NODE_INFO_SIZE;
            uint8_t* response = (uint8_t*)malloc(response_size);
            if (!response) {
                fprintf(stderr, "[Server] DHT: failed to allocate FIND_NODE_RESPONSE\n");
                break;
            }

            memset(response, 0, response_size);
            dht_fill_header(server, response, DHT_MSG_FIND_NODE_RESPONSE, txn_id);

            // node_count at offset 102
            response[DHT_HEADER_SIZE] = node_count;

            // Write DHT_NODE_INFO entries starting at offset 103
            uint8_t* write_ptr = response + DHT_HEADER_SIZE + 1;
            for (uint8_t i = 0; i < node_count; i++) {
                uint8_t a_type;
                uint8_t a_addr[16];
                uint16_t a_port;

                if (!extract_peer_dht_addr(found_peers[i], &a_type, a_addr, &a_port)) {
                    // Can't extract address, write peer_id with zeroed address
                    memcpy(write_ptr, found_peers[i]->peer_id, 32);
                    write_ptr += DHT_NODE_INFO_SIZE;
                    continue;
                }

                // node_id[32]
                memcpy(write_ptr, found_peers[i]->peer_id, 32);
                write_ptr += 32;
                // addr_type[1]
                *write_ptr++ = a_type;
                // addr[16]
                memcpy(write_ptr, a_addr, 16);
                write_ptr += 16;
                // port[2] (little-endian, use DHT default port 4568)
                // TODO: Let peers register their actual DHT port. 4568 is correct for peer-to-peer DHT
                // since all daemons listen on this port, but the relay uses its own port for bootstrap.
                uint16_t dht_port = 4568;
                write_ptr[0] = (uint8_t)(dht_port);
                write_ptr[1] = (uint8_t)(dht_port >> 8);
                write_ptr += 2;
            }

            if (dht_sign_message(server, response, response_size)) {
                if (send_packet(server->socket_fd, response, response_size, client_addr, addr_len)) {
                    server->total_packets_sent++;
                    printf("[Server] DHT FIND_NODE_RESPONSE sent (%u nodes)\n", node_count);
                }
            }

            free(response);
            break;
        }

        default:
            // Relay is bootstrap-only: ignore FIND_VALUE, STORE, etc.
            printf("[Server] DHT: ignoring non-bootstrap message type 0x%02X\n", msg_type);
            break;
    }
}
