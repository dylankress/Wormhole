//
// transfer_state.h
// Persist and resume partial transfer state.
// by Dylan Kress
//

#pragma once

#include "common.h"

// Save the received-chunks bitfield to ~/.wormhole/transfers/<hash>.state
// File format: [4B total_chunks][ceil(total_chunks/8) bytes bitmask]
// Bitmask bit=1 means chunk is received.
BOOLEAN TransferState_Save(const uint8_t manifest_hash[32],
                           const BOOLEAN *chunks_received, uint32_t total_chunks);

// Load a previously saved state. Allocates *chunks_received_out (caller must free).
// Returns FALSE if no state file exists or on error.
BOOLEAN TransferState_Load(const uint8_t manifest_hash[32],
                           BOOLEAN **chunks_received_out, uint32_t *total_chunks_out);

// Delete the state file for a completed transfer.
void TransferState_Delete(const uint8_t manifest_hash[32]);
