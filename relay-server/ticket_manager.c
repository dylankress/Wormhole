#define _GNU_SOURCE
//
// ticket_manager.c
// Wormhole Relay Server - Ticket Generation and Management
// by Dylan Kress
//

#include "ticket_manager.h"
#include "relay_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <pthread.h>
#endif

// Load EFF wordlist from file
static bool load_wordlist(TICKET_MANAGER* manager, const char* wordlist_path) {
    FILE* f = fopen(wordlist_path, "r");
    if (!f) {
        fprintf(stderr, "[TicketManager] Failed to open wordlist: %s\n", wordlist_path);
        return false;
    }
    
    // Count lines
    uint32_t line_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line_count++;
    }
    
    if (line_count == 0) {
        fprintf(stderr, "[TicketManager] Wordlist is empty\n");
        fclose(f);
        return false;
    }
    
    // Allocate wordlist
    manager->wordlist = (char**)malloc(line_count * sizeof(char*));
    if (!manager->wordlist) {
        fprintf(stderr, "[TicketManager] Failed to allocate wordlist\n");
        fclose(f);
        return false;
    }
    
    // Read words
    rewind(f);
    uint32_t index = 0;
    while (fgets(line, sizeof(line), f) && index < line_count) {
        // EFF wordlist format: "11111\tabacus\n"
        // We only need the word part (after tab)
        char* word = strchr(line, '\t');
        if (word) {
            word++;  // Skip tab
            
            // Remove newline
            char* newline = strchr(word, '\n');
            if (newline) {
                *newline = '\0';
            }
            
            manager->wordlist[index] = strdup(word);
            if (!manager->wordlist[index]) {
                fprintf(stderr, "[TicketManager] Failed to allocate word\n");
                // Cleanup partial wordlist
                for (uint32_t i = 0; i < index; i++) {
                    free(manager->wordlist[i]);
                }
                free(manager->wordlist);
                manager->wordlist = NULL;
                fclose(f);
                return false;
            }
            index++;
        }
    }
    
    fclose(f);
    manager->wordlist_size = index;
    
    printf("[TicketManager] Loaded %u words from wordlist\n", manager->wordlist_size);
    return true;
}

// Generate random number (0 to max-1)
static uint32_t random_uint32(uint32_t max) {
#ifdef _WIN32
    // Windows: Use CryptGenRandom for better randomness
    static HCRYPTPROV hProvider = 0;
    if (hProvider == 0) {
        CryptAcquireContext(&hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    }
    
    uint32_t value;
    CryptGenRandom(hProvider, sizeof(value), (BYTE*)&value);
    return value % max;
#else
    // POSIX: Use /dev/urandom
    static FILE* urandom = NULL;
    if (!urandom) {
        urandom = fopen("/dev/urandom", "rb");
    }
    
    uint32_t value;
    fread(&value, sizeof(value), 1, urandom);
    return value % max;
#endif
}

bool TicketManager_Init(TICKET_MANAGER* manager, uint32_t max_tickets, const char* wordlist_path) {
    if (!manager || max_tickets == 0 || !wordlist_path) {
        return false;
    }
    
    manager->tickets = (TICKET_INFO*)calloc(max_tickets, sizeof(TICKET_INFO));
    if (!manager->tickets) {
        return false;
    }
    
    manager->capacity = max_tickets;
    manager->count = 0;
    manager->wordlist = NULL;
    manager->wordlist_size = 0;
    
#ifdef _WIN32
    InitializeCriticalSection(&manager->lock);
#else
    pthread_mutex_init(&manager->lock, NULL);
#endif
    
    // Load wordlist
    if (!load_wordlist(manager, wordlist_path)) {
        free(manager->tickets);
        return false;
    }
    
    printf("[TicketManager] Initialized with capacity %u\n", max_tickets);
    return true;
}

void TicketManager_Cleanup(TICKET_MANAGER* manager) {
    if (!manager) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    // Free tickets
    if (manager->tickets) {
        free(manager->tickets);
        manager->tickets = NULL;
    }
    
    // Free wordlist
    if (manager->wordlist) {
        for (uint32_t i = 0; i < manager->wordlist_size; i++) {
            free(manager->wordlist[i]);
        }
        free(manager->wordlist);
        manager->wordlist = NULL;
    }
    
    manager->capacity = 0;
    manager->count = 0;
    manager->wordlist_size = 0;
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
    DeleteCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
    pthread_mutex_destroy(&manager->lock);
#endif
    
    printf("[TicketManager] Cleaned up\n");
}

const char* TicketManager_GenerateTicket(
    TICKET_MANAGER* manager,
    const uint8_t sender_peer_id[32],
    uint64_t file_size,
    const char* filename,
    char* ticket_buffer,
    uint32_t buffer_size
) {
    if (!manager || !sender_peer_id || !filename || !ticket_buffer || buffer_size < TICKET_FORMAT_LENGTH) {
        return NULL;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    // Check capacity
    if (manager->count >= manager->capacity) {
        fprintf(stderr, "[TicketManager] Ticket capacity full (%u/%u)\n",
                manager->count, manager->capacity);
#ifdef _WIN32
        LeaveCriticalSection(&manager->lock);
#else
        pthread_mutex_unlock(&manager->lock);
#endif
        return NULL;
    }
    
    // Generate ticket: "N-word-word"
    uint32_t digit = random_uint32(10);
    uint32_t word1_idx = random_uint32(manager->wordlist_size);
    uint32_t word2_idx = random_uint32(manager->wordlist_size);
    
    snprintf(ticket_buffer, buffer_size, "%u-%s-%s",
             digit,
             manager->wordlist[word1_idx],
             manager->wordlist[word2_idx]);
    
    // Check for collision (extremely unlikely)
    for (uint32_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->tickets[i].ticket_string, ticket_buffer) == 0) {
            fprintf(stderr, "[TicketManager] Ticket collision (very rare!), regenerating...\n");
            // Retry with different random words
            word1_idx = random_uint32(manager->wordlist_size);
            word2_idx = random_uint32(manager->wordlist_size);
            snprintf(ticket_buffer, buffer_size, "%u-%s-%s",
                     digit,
                     manager->wordlist[word1_idx],
                     manager->wordlist[word2_idx]);
        }
    }
    
    // Store ticket metadata
    TICKET_INFO* ticket = &manager->tickets[manager->count];
    strncpy(ticket->ticket_string, ticket_buffer, TICKET_FORMAT_LENGTH - 1);
    ticket->ticket_string[TICKET_FORMAT_LENGTH - 1] = '\0';
    memcpy(ticket->sender_peer_id, sender_peer_id, 32);
    ticket->file_size = file_size;
    strncpy(ticket->filename, filename, sizeof(ticket->filename) - 1);
    ticket->filename[sizeof(ticket->filename) - 1] = '\0';
    ticket->created_at = time(NULL);
    ticket->expires_at = ticket->created_at + TICKET_EXPIRY_SECONDS;
    
    manager->count++;
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
#endif
    
    printf("[TicketManager] Generated ticket '%s' for file '%s' (%llu bytes, expires in %d seconds)\n",
           ticket_buffer, filename, (unsigned long long)file_size, TICKET_EXPIRY_SECONDS);
    
    return ticket_buffer;
}

TICKET_INFO* TicketManager_LookupTicket(TICKET_MANAGER* manager, const char* ticket) {
    if (!manager || !ticket) {
        return NULL;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    time_t now = time(NULL);
    
    for (uint32_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->tickets[i].ticket_string, ticket) == 0) {
            // Check if expired
            if (now > manager->tickets[i].expires_at) {
                printf("[TicketManager] Ticket '%s' has expired\n", ticket);
#ifdef _WIN32
                LeaveCriticalSection(&manager->lock);
#else
                pthread_mutex_unlock(&manager->lock);
#endif
                return NULL;
            }
            
            TICKET_INFO* info = &manager->tickets[i];
            
#ifdef _WIN32
            LeaveCriticalSection(&manager->lock);
#else
            pthread_mutex_unlock(&manager->lock);
#endif
            
            printf("[TicketManager] Found ticket '%s' (expires in %ld seconds)\n",
                   ticket, (long)(info->expires_at - now));
            return info;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
#endif
    
    printf("[TicketManager] Ticket '%s' not found\n", ticket);
    return NULL;
}

uint32_t TicketManager_RemoveExpiredTickets(TICKET_MANAGER* manager, time_t current_time) {
    if (!manager) {
        return 0;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    uint32_t removed = 0;
    uint32_t write_idx = 0;
    
    // Compact array by removing expired tickets
    for (uint32_t read_idx = 0; read_idx < manager->count; read_idx++) {
        if (current_time <= manager->tickets[read_idx].expires_at) {
            // Keep this ticket
            if (write_idx != read_idx) {
                manager->tickets[write_idx] = manager->tickets[read_idx];
            }
            write_idx++;
        } else {
            // Remove this ticket
            printf("[TicketManager] Removing expired ticket '%s'\n",
                   manager->tickets[read_idx].ticket_string);
            removed++;
        }
    }
    
    manager->count = write_idx;
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
#endif
    
    if (removed > 0) {
        printf("[TicketManager] Removed %u expired tickets (active: %u)\n",
               removed, manager->count);
    }
    
    return removed;
}

bool TicketManager_RemoveTicket(TICKET_MANAGER* manager, const char* ticket) {
    if (!manager || !ticket) {
        return false;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    for (uint32_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->tickets[i].ticket_string, ticket) == 0) {
            // Remove by shifting array
            for (uint32_t j = i; j < manager->count - 1; j++) {
                manager->tickets[j] = manager->tickets[j + 1];
            }
            manager->count--;
            
#ifdef _WIN32
            LeaveCriticalSection(&manager->lock);
#else
            pthread_mutex_unlock(&manager->lock);
#endif
            
            printf("[TicketManager] Removed ticket '%s'\n", ticket);
            return true;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
#endif
    
    return false;
}

void TicketManager_GetStats(TICKET_MANAGER* manager, uint32_t* active_tickets, uint32_t* capacity) {
    if (!manager) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&manager->lock);
#else
    pthread_mutex_lock(&manager->lock);
#endif
    
    if (active_tickets) {
        *active_tickets = manager->count;
    }
    if (capacity) {
        *capacity = manager->capacity;
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&manager->lock);
#else
    pthread_mutex_unlock(&manager->lock);
#endif
}
