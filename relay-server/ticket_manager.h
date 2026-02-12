//
// ticket_manager.h
// Wormhole Relay Server - Ticket Generation and Management
// by Dylan Kress
//

#pragma once
#ifndef _WIN32
#include <pthread.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Ticket format: "N-word-word" (e.g., "7-guitar-battery")
// N = random digit 0-9
// word = random word from EFF long wordlist (7776 words)

#define TICKET_FORMAT_LENGTH 32  // Max length of ticket string

// Ticket metadata
typedef struct {
    char ticket_string[TICKET_FORMAT_LENGTH];  // Full ticket (e.g., "7-guitar-battery")
    uint8_t sender_peer_id[32];                // Sender's PeerID
    uint64_t file_size;                        // File size (for receiver info)
    char filename[256];                        // Filename
    time_t created_at;                         // When ticket was created
    time_t expires_at;                         // When ticket expires
} TICKET_INFO;

// Ticket manager (stores active tickets)
typedef struct {
    TICKET_INFO* tickets;                      // Array of tickets
    uint32_t capacity;                         // Max tickets
    uint32_t count;                            // Current number of tickets
    
    char** wordlist;                           // EFF wordlist (7776 words)
    uint32_t wordlist_size;                    // Number of words loaded
    
#ifdef _WIN32
    CRITICAL_SECTION lock;                     // Thread safety
#else
    pthread_mutex_t lock;
#endif
} TICKET_MANAGER;

// Initialize ticket manager
// Returns: true on success, false on failure
bool TicketManager_Init(TICKET_MANAGER* manager, uint32_t max_tickets, const char* wordlist_path);

// Cleanup ticket manager
void TicketManager_Cleanup(TICKET_MANAGER* manager);

// Generate a new ticket
// Returns: Ticket string (in provided buffer), or NULL on failure
const char* TicketManager_GenerateTicket(
    TICKET_MANAGER* manager,
    const uint8_t sender_peer_id[32],
    uint64_t file_size,
    const char* filename,
    char* ticket_buffer,  // Buffer must be at least TICKET_FORMAT_LENGTH bytes
    uint32_t buffer_size
);

// Look up ticket information
// Returns: Pointer to TICKET_INFO, or NULL if not found or expired
TICKET_INFO* TicketManager_LookupTicket(TICKET_MANAGER* manager, const char* ticket);

// Remove expired tickets
// Returns: Number of tickets removed
uint32_t TicketManager_RemoveExpiredTickets(TICKET_MANAGER* manager, time_t current_time);

// Remove a specific ticket (after successful transfer)
// Returns: true if ticket found and removed, false otherwise
bool TicketManager_RemoveTicket(TICKET_MANAGER* manager, const char* ticket);

// Get statistics
void TicketManager_GetStats(TICKET_MANAGER* manager, uint32_t* active_tickets, uint32_t* capacity);
