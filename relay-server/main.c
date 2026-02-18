//
// main.c
// Wormhole Relay Server - Entry Point
// by Dylan Kress
//

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Global server instance (for signal handler)
static RELAY_SERVER* g_server = NULL;

// Async-signal-safe shutdown flag
static volatile sig_atomic_t g_shutdown_requested = 0;

// Signal handler for graceful shutdown — only sets a flag (async-signal-safe)
#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_shutdown_requested = 1;
        if (g_server) {
            g_server->running = false;
        }
        return TRUE;
    }
    return FALSE;
}
#else
void signal_handler(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
    if (g_server) {
        g_server->running = false;
    }
}
#endif

void print_usage(const char* program_name) {
    printf("Wormhole Relay Server\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -p, --port <port>         UDP port to listen on (default: 443)\n");
    printf("  -w, --wordlist <path>     Path to EFF wordlist (default: eff_large_wordlist.txt)\n");
    printf("  --max-peers <num>         Maximum concurrent peers (default: 10000)\n");
    printf("  --max-tickets <num>       Maximum active tickets (default: 5000)\n");
    printf("  --public-addr <ip>        Public IP address for relay fallback endpoint\n");
    printf("  -h, --help                Show this help message\n");
    printf("\n");
}

int main(int argc, char* argv[]) {
    // Default configuration
    SERVER_CONFIG config;
    config.port = 443;
    config.max_peers = 10000;
    config.max_tickets = 5000;
    config.wordlist_path = "eff_large_wordlist.txt";
    config.public_addr = NULL;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                config.port = (uint16_t)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--wordlist") == 0) {
            if (i + 1 < argc) {
                config.wordlist_path = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--max-peers") == 0) {
            if (i + 1 < argc) {
                config.max_peers = (uint32_t)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--max-tickets") == 0) {
            if (i + 1 < argc) {
                config.max_tickets = (uint32_t)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--public-addr") == 0) {
            if (i + 1 < argc) {
                config.public_addr = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Print banner
    printf("===============================================\n");
    printf("  Wormhole Relay Server v0.1.0\n");
    printf("  Port: %u\n", config.port);
    printf("  Max Peers: %u\n", config.max_peers);
    printf("  Max Tickets: %u\n", config.max_tickets);
    printf("  Wordlist: %s\n", config.wordlist_path);
    printf("  Public Addr: %s\n", config.public_addr ? config.public_addr : "(not set - relay fallback disabled)");
    printf("===============================================\n\n");
    
    // Initialize server
    RELAY_SERVER server;
    g_server = &server;
    
    if (!RelayServer_Init(&server, &config)) {
        fprintf(stderr, "Failed to initialize relay server\n");
        return 1;
    }
    
    // Set up signal handlers
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
    
    printf("[Main] Press Ctrl+C to stop server\n\n");
    
    // Run server (blocks until stopped)
    int result = RelayServer_Run(&server);

    // Print shutdown message (outside signal handler, safe to call printf)
    if (g_shutdown_requested) {
        printf("\n[Main] Caught shutdown signal, cleaning up...\n");
    }

    // Print final statistics
    RelayServer_PrintStats(&server);
    
    // Cleanup
    RelayServer_Cleanup(&server);
    
    printf("[Main] Server exited with code %d\n", result);
    return result;
}
