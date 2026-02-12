//
// ticket.h
// Wormhole - Ticket Display and Parsing
// by Dylan Kress
//

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pretty-print ticket for sharing (with instructions)
// ticket: Ticket string (e.g., "7-guitar-battery")
// filename: Name of file being sent
// file_size: Size of file in bytes
void Ticket_PrintForSharing(const char* ticket, const char* filename, uint64_t file_size);

// Validate ticket format
// ticket: Ticket string to validate
// Returns: true if valid format, false otherwise
bool Ticket_Validate(const char* ticket);

// Parse and display received ticket (for receiver UI)
void Ticket_PrintReceiving(const char* ticket);
