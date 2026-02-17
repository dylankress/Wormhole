#define _GNU_SOURCE
//
// relay_client.c
// Wormhole - Relay Client (Protocol Communication)
// by Dylan Kress
//

#include "relay_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;  // Windows doesn't have ssize_t
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#endif

#define MAX_PACKET_SIZE 65536

struct RELAY_CLIENT {
    int socket_fd;
    struct sockaddr_storage relay_addr;
    socklen_t relay_addr_len;
    
    KEYPAIR keypair;
    uint64_t session_id;
    bool connected;
    
    // Callbacks
    RelayConnectedCallback on_connected;
    RelayTicketCreatedCallback on_ticket_created;
    RelayPeerInfoCallback on_peer_info;
    RelayDisconnectedCallback on_disconnected;
    RelayPeersFoundCallback on_peers_found;
    void* callback_context;
};

RELAY_CLIENT* RelayClient_Create(const RELAY_CLIENT_CONFIG* config) {
    if (!config || !config->relay_host || !config->keypair) {
        return NULL;
    }
    
    RELAY_CLIENT* client = (RELAY_CLIENT*)calloc(1, sizeof(RELAY_CLIENT));
    if (!client) {
        return NULL;
    }
    
    // Copy keypair
    memcpy(&client->keypair, config->keypair, sizeof(KEYPAIR));
    
    // Copy callbacks
    client->on_connected = config->on_connected;
    client->on_ticket_created = config->on_ticket_created;
    client->on_peer_info = config->on_peer_info;
    client->on_disconnected = config->on_disconnected;
    client->on_peers_found = config->on_peers_found;
    client->callback_context = config->callback_context;
    
    // Resolve relay server address first
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // Force IPv4 for now (dual-stack issues)
    hints.ai_socktype = SOCK_DGRAM;
    
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", config->relay_port);
    
    if (getaddrinfo(config->relay_host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "[RelayClient] Failed to resolve relay server: %s\n", config->relay_host);
        free(client);
        return NULL;
    }
    
    // Create UDP socket with the same family as resolved address
    client->socket_fd = socket(result->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    if (client->socket_fd < 0) {
        fprintf(stderr, "[RelayClient] Failed to create socket\n");
        freeaddrinfo(result);
        free(client);
        return NULL;
    }

    // Bind to local port if specified (for consistent NAT mapping with QUIC listener)
    if (config->local_port > 0) {
        int reuse = 1;
        setsockopt(client->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse));

        if (result->ai_family == AF_INET) {
            struct sockaddr_in bind_addr;
            memset(&bind_addr, 0, sizeof(bind_addr));
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_addr.s_addr = INADDR_ANY;
            bind_addr.sin_port = htons(config->local_port);

            if (bind(client->socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
#ifdef _WIN32
                fprintf(stderr, "[RelayClient] Warning: Failed to bind to port %u (WSA error %d), using ephemeral\n",
                        config->local_port, WSAGetLastError());
#else
                fprintf(stderr, "[RelayClient] Warning: Failed to bind to port %u, using ephemeral\n",
                        config->local_port);
#endif
            } else {
                printf("[RelayClient] Bound to local port %u (SO_REUSEADDR)\n", config->local_port);
            }
        } else {
            struct sockaddr_in6 bind_addr;
            memset(&bind_addr, 0, sizeof(bind_addr));
            bind_addr.sin6_family = AF_INET6;
            bind_addr.sin6_addr = in6addr_any;
            bind_addr.sin6_port = htons(config->local_port);

            if (bind(client->socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
#ifdef _WIN32
                fprintf(stderr, "[RelayClient] Warning: Failed to bind to port %u (WSA error %d), using ephemeral\n",
                        config->local_port, WSAGetLastError());
#else
                fprintf(stderr, "[RelayClient] Warning: Failed to bind to port %u, using ephemeral\n",
                        config->local_port);
#endif
            } else {
                printf("[RelayClient] Bound to local port %u (SO_REUSEADDR)\n", config->local_port);
            }
        }
    }

    // Save relay address
    memcpy(&client->relay_addr, result->ai_addr, result->ai_addrlen);
    client->relay_addr_len = result->ai_addrlen;
    
    printf("[RelayClient] Created client for %s:%u (%s)\n", config->relay_host, config->relay_port,
           result->ai_family == AF_INET ? "IPv4" : "IPv6");
    
    freeaddrinfo(result);
    return client;
}

void RelayClient_Destroy(RELAY_CLIENT* client) {
    if (!client) {
        return;
    }
    
    if (client->connected) {
        RelayClient_SendGoodbye(client, 0x01);  // Reason: error/disconnect
    }
    
#ifdef _WIN32
    closesocket(client->socket_fd);
#else
    close(client->socket_fd);
#endif
    
    // Zero out keypair
    sodium_memzero(&client->keypair, sizeof(KEYPAIR));
    
    free(client);
    printf("[RelayClient] Destroyed\n");
}

static bool send_to_relay(RELAY_CLIENT* client, const void* data, size_t len) {
#ifdef _WIN32
    ssize_t sent = sendto(client->socket_fd, (const char*)data, (int)len, 0,
                         (struct sockaddr*)&client->relay_addr, client->relay_addr_len);
#else
    ssize_t sent = sendto(client->socket_fd, data, len, 0,
                         (struct sockaddr*)&client->relay_addr, client->relay_addr_len);
#endif
    
    if (sent != (ssize_t)len) {
#ifdef _WIN32
        int err = WSAGetLastError();
        fprintf(stderr, "[RelayClient] Failed to send to relay (sent=%d/%zu bytes, WSAError=%d)\n", 
                (int)sent, len, err);
#else
        fprintf(stderr, "[RelayClient] Failed to send to relay (%zd/%zu bytes)\n", sent, len);
#endif
        return false;
    }
    
    return true;
}

bool RelayClient_Register(RELAY_CLIENT* client, const ENDPOINT* endpoints, uint16_t endpoint_count) {
    if (!client || !endpoints || endpoint_count == 0 || endpoint_count > MAX_ENDPOINTS) {
        return false;
    }
    
    // Build REGISTER message
    size_t message_size = sizeof(RegisterMsg) + (endpoint_count * sizeof(ENDPOINT));
    uint8_t* message_buf = (uint8_t*)malloc(message_size);
    if (!message_buf) {
        return false;
    }
    
    RegisterMsg* msg = (RegisterMsg*)message_buf;
    msg->message_type = RELAY_MSG_REGISTER;
    memcpy(msg->peer_id, client->keypair.public_key, PEER_ID_SIZE);
    msg->timestamp = (uint64_t)time(NULL);
    msg->endpoint_count = endpoint_count;
    
    // Copy endpoints
    memcpy(message_buf + sizeof(RegisterMsg), endpoints, endpoint_count * sizeof(ENDPOINT));
    
    // Sign the message: peer_id || timestamp || blake2b(endpoints)
    uint8_t endpoints_hash[crypto_generichash_BYTES];
    crypto_generichash(endpoints_hash, sizeof(endpoints_hash),
                      (const uint8_t*)endpoints, endpoint_count * sizeof(ENDPOINT),
                      NULL, 0);
    
    uint8_t signed_data[PEER_ID_SIZE + 8 + crypto_generichash_BYTES];
    memcpy(signed_data, msg->peer_id, PEER_ID_SIZE);
    memcpy(signed_data + PEER_ID_SIZE, &msg->timestamp, 8);
    memcpy(signed_data + PEER_ID_SIZE + 8, endpoints_hash, crypto_generichash_BYTES);
    
    if (!PeerID_Sign(&client->keypair, signed_data, sizeof(signed_data), msg->signature)) {
        fprintf(stderr, "[RelayClient] Failed to sign REGISTER message\n");
        free(message_buf);
        return false;
    }
    
    // Send to relay
    bool success = send_to_relay(client, message_buf, message_size);
    free(message_buf);
    
    if (success) {
        printf("[RelayClient] Sent REGISTER (%u endpoints)\n", endpoint_count);
    }
    
    return success;
}

bool RelayClient_CreateTicket(RELAY_CLIENT* client, uint64_t file_size, const char* filename) {
    if (!client || !filename || !client->connected) {
        return false;
    }
    
    size_t filename_len = strlen(filename);
    if (filename_len > MAX_FILENAME_LENGTH) {
        filename_len = MAX_FILENAME_LENGTH;
    }
    
    size_t message_size = sizeof(CreateTicketMsg) + filename_len;
    uint8_t* message_buf = (uint8_t*)malloc(message_size);
    if (!message_buf) {
        return false;
    }
    
    CreateTicketMsg* msg = (CreateTicketMsg*)message_buf;
    msg->message_type = RELAY_MSG_CREATE_TICKET;
    msg->session_id = client->session_id;
    msg->file_size = file_size;
    msg->filename_length = (uint16_t)filename_len;
    memcpy(message_buf + sizeof(CreateTicketMsg), filename, filename_len);
    
    bool success = send_to_relay(client, message_buf, message_size);
    free(message_buf);
    
    if (success) {
        printf("[RelayClient] Sent CREATE_TICKET (file: %s, size: %llu bytes)\n",
               filename, (unsigned long long)file_size);
    }
    
    return success;
}

bool RelayClient_LookupTicket(RELAY_CLIENT* client, const char* ticket) {
    if (!client || !ticket || !client->connected) {
        return false;
    }
    
    size_t ticket_len = strlen(ticket);
    if (ticket_len > MAX_TICKET_LENGTH) {
        ticket_len = MAX_TICKET_LENGTH;
    }
    
    size_t message_size = sizeof(LookupMsg) + ticket_len;
    uint8_t* message_buf = (uint8_t*)malloc(message_size);
    if (!message_buf) {
        return false;
    }
    
    LookupMsg* msg = (LookupMsg*)message_buf;
    msg->message_type = RELAY_MSG_LOOKUP;
    msg->session_id = client->session_id;
    msg->ticket_length = (uint8_t)ticket_len;
    memcpy(message_buf + sizeof(LookupMsg), ticket, ticket_len);
    
    bool success = send_to_relay(client, message_buf, message_size);
    free(message_buf);
    
    if (success) {
        printf("[RelayClient] Sent LOOKUP (ticket: %s)\n", ticket);
    }
    
    return success;
}

bool RelayClient_SendKeepalive(RELAY_CLIENT* client) {
    if (!client || !client->connected) {
        return false;
    }
    
    KeepaliveMsg msg;
    msg.message_type = RELAY_MSG_KEEPALIVE;
    msg.timestamp = (uint64_t)time(NULL);
    
    return send_to_relay(client, &msg, sizeof(msg));
}

bool RelayClient_SendGoodbye(RELAY_CLIENT* client, uint8_t reason) {
    if (!client) {
        return false;
    }
    
    GoodbyeMsg msg;
    msg.message_type = RELAY_MSG_GOODBYE;
    msg.reason = reason;
    
    bool success = send_to_relay(client, &msg, sizeof(msg));
    
    if (success) {
        const char* reason_str = "unknown";
        if (reason == 0x00) reason_str = "upgraded to direct";
        else if (reason == 0x01) reason_str = "error";
        else if (reason == 0x02) reason_str = "transfer complete";
        
        printf("[RelayClient] Sent GOODBYE (reason: %s)\n", reason_str);
        client->connected = false;
        
        if (client->on_disconnected) {
            client->on_disconnected(client->callback_context);
        }
    }
    
    return success;
}

bool RelayClient_FindPeers(RELAY_CLIENT* client, uint16_t max_peers) {
    if (!client || !client->connected) {
        return false;
    }

    FindPeersMsg msg;
    msg.message_type = RELAY_MSG_FIND_PEERS;
    msg.session_id = client->session_id;
    msg.max_peers = max_peers;

    bool success = send_to_relay(client, &msg, sizeof(msg));

    if (success) {
        printf("[RelayClient] Sent FIND_PEERS (max %u)\n", max_peers);
    }

    return success;
}

bool RelayClient_ForwardPacket(RELAY_CLIENT* client, const uint8_t dest_peer_id[32],
                               const uint8_t* payload, uint16_t payload_len) {
    if (!client || !dest_peer_id || !payload || !client->connected) {
        return false;
    }
    
    if (payload_len > MAX_PAYLOAD_LENGTH) {
        fprintf(stderr, "[RelayClient] Payload too large (%u > %u)\n",
                payload_len, MAX_PAYLOAD_LENGTH);
        return false;
    }
    
    size_t message_size = sizeof(ForwardMsg) + payload_len;
    uint8_t* message_buf = (uint8_t*)malloc(message_size);
    if (!message_buf) {
        return false;
    }
    
    ForwardMsg* msg = (ForwardMsg*)message_buf;
    msg->message_type = RELAY_MSG_FORWARD;
    memcpy(msg->dest_peer_id, dest_peer_id, 32);
    msg->payload_length = payload_len;
    memcpy(message_buf + sizeof(ForwardMsg), payload, payload_len);
    
    bool success = send_to_relay(client, message_buf, message_size);
    free(message_buf);
    
    return success;
}

bool RelayClient_Poll(RELAY_CLIENT* client, int timeout_ms) {
    if (!client) {
        return false;
    }
    
    // Wait for incoming data
#ifdef _WIN32
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket_fd, &readfds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ready = select(client->socket_fd + 1, &readfds, NULL, NULL, timeout_ms >= 0 ? &tv : NULL);
#else
    struct pollfd pfd;
    pfd.fd = client->socket_fd;
    pfd.events = POLLIN;
    
    int ready = poll(&pfd, 1, timeout_ms);
#endif
    
    if (ready <= 0) {
        return false;  // Timeout or error
    }
    
    // Receive message
    uint8_t buffer[MAX_PACKET_SIZE];
    struct sockaddr_storage from_addr;
    socklen_t from_len = sizeof(from_addr);
    
#ifdef _WIN32
    ssize_t recv_len = recvfrom(client->socket_fd, (char*)buffer, (int)sizeof(buffer), 0,
                                (struct sockaddr*)&from_addr, &from_len);
#else
    ssize_t recv_len = recvfrom(client->socket_fd, (char*)buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&from_addr, &from_len);
#endif
    
    if (recv_len <= 0) {
        return false;
    }
    
    // Parse message type
    if (recv_len < 1) {
        return false;
    }
    
    uint8_t msg_type = buffer[0];
    
    // Handle message
    switch (msg_type) {
        case RELAY_MSG_REGISTERED: {
            if (recv_len < (ssize_t)sizeof(RegisteredMsg)) {
                fprintf(stderr, "[RelayClient] REGISTERED message too short\n");
                break;
            }
            
            RegisteredMsg* msg = (RegisteredMsg*)buffer;
            
            if (msg->status != 0x00) {
                fprintf(stderr, "[RelayClient] Registration failed (status: 0x%02X)\n", msg->status);
                break;
            }
            
            client->session_id = msg->session_id;
            client->connected = true;
            
            printf("[RelayClient] REGISTERED (session %llu)\n", (unsigned long long)client->session_id);
            
            if (client->on_connected) {
                client->on_connected(client->callback_context, msg->session_id,
                                    msg->observed_addr, msg->observed_addr_type, msg->observed_port);
            }
            break;
        }
        
        case RELAY_MSG_TICKET_CREATED: {
            if (recv_len < (ssize_t)sizeof(TicketCreatedMsg)) {
                fprintf(stderr, "[RelayClient] TICKET_CREATED message too short\n");
                break;
            }
            
            TicketCreatedMsg* msg = (TicketCreatedMsg*)buffer;
            
            if (recv_len < (ssize_t)(sizeof(TicketCreatedMsg) + msg->ticket_length)) {
                fprintf(stderr, "[RelayClient] TICKET_CREATED message truncated\n");
                break;
            }
            
            char ticket[MAX_TICKET_LENGTH + 1];
            size_t ticket_len = (msg->ticket_length < MAX_TICKET_LENGTH) ? msg->ticket_length : MAX_TICKET_LENGTH;
            memcpy(ticket, buffer + sizeof(TicketCreatedMsg), ticket_len);
            ticket[ticket_len] = '\0';
            
            printf("[RelayClient] TICKET_CREATED: %s\n", ticket);
            
            if (client->on_ticket_created) {
                client->on_ticket_created(client->callback_context, ticket);
            }
            break;
        }
        
        case RELAY_MSG_PEER_INFO: {
            if (recv_len < (ssize_t)sizeof(PeerInfoMsg)) {
                fprintf(stderr, "[RelayClient] PEER_INFO message too short\n");
                break;
            }
            
            PeerInfoMsg* msg = (PeerInfoMsg*)buffer;
            
            size_t expected_len = sizeof(PeerInfoMsg) + (msg->endpoint_count * sizeof(ENDPOINT));
            if (recv_len < (ssize_t)expected_len) {
                fprintf(stderr, "[RelayClient] PEER_INFO message truncated\n");
                break;
            }
            
            const ENDPOINT* endpoints = (const ENDPOINT*)(buffer + sizeof(PeerInfoMsg));

            uint16_t ep_count = msg->endpoint_count;
            if (ep_count > MAX_ENDPOINTS) {
                printf("[RelayClient] Warning: PEER_INFO has %u endpoints, capping to %u\n",
                       ep_count, MAX_ENDPOINTS);
                ep_count = MAX_ENDPOINTS;
            }

            printf("[RelayClient] PEER_INFO (%u endpoints)\n", ep_count);

            if (client->on_peer_info) {
                client->on_peer_info(client->callback_context, msg->peer_id, endpoints, ep_count);
            }
            break;
        }
        
        case RELAY_MSG_KEEPALIVE: {
            // Keepalive received from relay server - no need to echo back
            // Just acknowledge silently to avoid ping-pong packet loops
            break;
        }

        case RELAY_MSG_PEERS_FOUND: {
            if (recv_len < (ssize_t)sizeof(PeersFoundMsg)) {
                fprintf(stderr, "[RelayClient] PEERS_FOUND message too short\n");
                break;
            }

            PeersFoundMsg* msg = (PeersFoundMsg*)buffer;
            uint16_t peer_count = msg->peer_count;

            if (peer_count > MAX_FIND_PEERS) {
                printf("[RelayClient] Warning: PEERS_FOUND has %u peers, capping to %u\n",
                       peer_count, MAX_FIND_PEERS);
                peer_count = MAX_FIND_PEERS;
            }

            printf("[RelayClient] PEERS_FOUND (%u peers)\n", peer_count);

            // Parse variable-length peer entries
            DISCOVERED_PEER* peers = NULL;
            if (peer_count > 0) {
                peers = (DISCOVERED_PEER*)calloc(peer_count, sizeof(DISCOVERED_PEER));
                if (!peers) {
                    fprintf(stderr, "[RelayClient] Out of memory for PEERS_FOUND\n");
                    break;
                }

                const uint8_t* read_ptr = buffer + sizeof(PeersFoundMsg);
                const uint8_t* end_ptr = buffer + recv_len;
                uint16_t parsed = 0;

                for (uint16_t i = 0; i < peer_count; i++) {
                    // Need at least peer_id(32) + endpoint_count(2)
                    if (read_ptr + 34 > end_ptr) {
                        fprintf(stderr, "[RelayClient] PEERS_FOUND truncated at peer %u\n", i);
                        break;
                    }

                    memcpy(peers[i].peer_id, read_ptr, 32);
                    read_ptr += 32;

                    uint16_t ep_count;
                    memcpy(&ep_count, read_ptr, 2);
                    read_ptr += 2;

                    if (ep_count > MAX_ENDPOINTS) {
                        ep_count = MAX_ENDPOINTS;
                    }

                    size_t ep_bytes = ep_count * sizeof(ENDPOINT);
                    if (read_ptr + ep_bytes > end_ptr) {
                        fprintf(stderr, "[RelayClient] PEERS_FOUND truncated at peer %u endpoints\n", i);
                        break;
                    }

                    memcpy(peers[i].endpoints, read_ptr, ep_bytes);
                    peers[i].endpoint_count = ep_count;
                    read_ptr += ep_bytes;
                    parsed++;
                }

                if (client->on_peers_found && parsed > 0) {
                    client->on_peers_found(client->callback_context, peers, parsed);
                }

                free(peers);
            } else {
                if (client->on_peers_found) {
                    client->on_peers_found(client->callback_context, NULL, 0);
                }
            }
            break;
        }

        default:
            fprintf(stderr, "[RelayClient] Unknown message type: 0x%02X\n", msg_type);
            break;
    }
    
    return true;
}

uint64_t RelayClient_GetSessionID(RELAY_CLIENT* client) {
    return client ? client->session_id : 0;
}

bool RelayClient_IsConnected(RELAY_CLIENT* client) {
    return client ? client->connected : false;
}

int RelayClient_GetSocket(RELAY_CLIENT* client) {
    return client ? client->socket_fd : -1;
}

bool RelayClient_GetRelayAddr(RELAY_CLIENT* client, struct sockaddr_storage* addr, socklen_t* addr_len) {
    if (!client || !addr || !addr_len) {
        return false;
    }
    memcpy(addr, &client->relay_addr, client->relay_addr_len);
    *addr_len = client->relay_addr_len;
    return true;
}
