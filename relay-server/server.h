//
// server.h
// Wormhole Relay Server - Main UDP Server Loop
// by Dylan Kress
//

#pragma once
#ifndef _WIN32
#include <pthread.h>
#endif

#include "peer_registry.h"
#include "ticket_manager.h"
#include "rate_limiter.h"
#include <stdint.h>
#include <stdbool.h>

// Server configuration
typedef struct {
    uint16_t port;                    // UDP port to listen on (default 443)
    uint32_t max_peers;               // Maximum concurrent peers
    uint32_t max_tickets;             // Maximum active tickets
    const char* wordlist_path;        // Path to EFF wordlist
} SERVER_CONFIG;

// Server state
typedef struct {
    int socket_fd;                    // UDP socket
    bool running;                     // Server running flag
    
    PEER_REGISTRY peer_registry;     // Connected peers
    TICKET_MANAGER ticket_manager;   // Active tickets
    RATE_LIMITER rate_limiter;       // Rate limiting
    
    // Statistics
    uint64_t total_packets_received;
    uint64_t total_packets_sent;
    uint64_t total_bytes_forwarded;
    
#ifdef _WIN32
    CRITICAL_SECTION stats_lock;
#else
    pthread_mutex_t stats_lock;
#endif
} RELAY_SERVER;

// Initialize relay server
// Returns: true on success, false on failure
bool RelayServer_Init(RELAY_SERVER* server, const SERVER_CONFIG* config);

// Start server main loop (blocks until stopped)
// Returns: 0 on clean shutdown, non-zero on error
int RelayServer_Run(RELAY_SERVER* server);

// Stop server (call from signal handler)
void RelayServer_Stop(RELAY_SERVER* server);

// Cleanup server resources
void RelayServer_Cleanup(RELAY_SERVER* server);

// Print server statistics
void RelayServer_PrintStats(RELAY_SERVER* server);
