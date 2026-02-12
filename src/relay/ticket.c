//
// ticket.c
// Wormhole - Ticket Display and Parsing
// by Dylan Kress
//

#include "ticket.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void format_file_size(uint64_t bytes, char* buffer, size_t buffer_size) {
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%llu bytes", (unsigned long long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

void Ticket_PrintForSharing(const char* ticket, const char* filename, uint64_t file_size) {
    if (!ticket || !filename) {
        return;
    }
    
    char size_str[64];
    format_file_size(file_size, size_str, sizeof(size_str));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                    WORMHOLE FILE TRANSFER                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║                                                           ║\n");
    printf("║  Your ticket:                                             ║\n");
    printf("║                                                           ║\n");
    printf("║      %s%*s║\n", ticket, (int)(51 - strlen(ticket)), "");
    printf("║                                                           ║\n");
    printf("║  File: %-50s║\n", filename);
    printf("║  Size: %-50s║\n", size_str);
    printf("║                                                           ║\n");
    printf("║  Share this ticket with the receiver. They can download  ║\n");
    printf("║  the file by running:                                    ║\n");
    printf("║                                                           ║\n");
    printf("║      wormhole receive %s%*s║\n", ticket, (int)(30 - strlen(ticket)), "");
    printf("║                                                           ║\n");
    printf("║  Waiting for receiver to connect...                      ║\n");
    printf("║                                                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

bool Ticket_Validate(const char* ticket) {
    if (!ticket) {
        return false;
    }
    
    size_t len = strlen(ticket);
    
    // Expected format: "N-word-word" (minimum ~10 chars, max ~30 chars)
    if (len < 5 || len > 50) {
        return false;
    }
    
    // Check format: digit-word-word
    int dash_count = 0;
    bool first_char_digit = isdigit(ticket[0]);
    
    for (size_t i = 0; i < len; i++) {
        if (ticket[i] == '-') {
            dash_count++;
        } else if (!isalnum(ticket[i])) {
            return false;  // Invalid character
        }
    }
    
    // Should have exactly 2 dashes and start with a digit
    return (dash_count == 2 && first_char_digit);
}

void Ticket_PrintReceiving(const char* ticket) {
    if (!ticket) {
        return;
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                    WORMHOLE FILE TRANSFER                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║                                                           ║\n");
    printf("║  Connecting with ticket:                                 ║\n");
    printf("║                                                           ║\n");
    printf("║      %s%*s║\n", ticket, (int)(51 - strlen(ticket)), "");
    printf("║                                                           ║\n");
    printf("║  Looking up sender...                                    ║\n");
    printf("║                                                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}
