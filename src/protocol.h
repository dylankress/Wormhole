//
// protocol.h
// Wormhole chunk-based transfer protocol constants.
// by Dylan Kress
//

#pragma once

#include "common.h"

// ALPN identifier for Wormhole protocol
#define WORMHOLE_ALPN "wormhole"

// Default UDP port (4567 for development, use 443 in production)
#define WORMHOLE_DEFAULT_PORT 4567

// Content-addressed chunk size (256KB)
#define CHUNK_SIZE (256 * 1024)

// Control messages (Stream 0: bidirectional)
#define CTRL_MSG_MANIFEST_REQUEST    0x01
#define CTRL_MSG_MANIFEST_RESPONSE   0x02
#define CTRL_MSG_CHUNK_REQUEST       0x03
#define CTRL_MSG_TRANSFER_COMPLETE   0x04

// Control message framing: [1B type][4B payload_length][payload]
#define CTRL_HEADER_SIZE 5

// Data frame (Stream 1: unidirectional sender->receiver)
// [4B chunk_index][32B chunk_hash][4B chunk_data_size][data]
#define DATA_FRAME_HEADER_SIZE (4 + 32 + 4)  // 40 bytes
