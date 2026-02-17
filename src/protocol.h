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

// Chunk replication messages (P2P storage, Stream 0: bidirectional)
#define CTRL_MSG_CHUNK_STORE_REQUEST   0x05  // "Please store this chunk"
#define CTRL_MSG_CHUNK_STORE_ACK       0x06  // "I stored it"
#define CTRL_MSG_CHUNK_QUERY           0x07  // "Do you have this chunk?"
#define CTRL_MSG_CHUNK_QUERY_RESPONSE  0x08  // "Yes/No + chunk data if yes"

// Proof-of-storage verification (P2P storage, Stream 0: bidirectional)
#define CTRL_MSG_PROOF_CHALLENGE       0x09  // "Prove you have this chunk" [32B hash][32B seed]
#define CTRL_MSG_PROOF_RESPONSE        0x0A  // "Here's the proof" [32B hash][32B proof]

// Target replication factor for P2P storage
#define REPLICATION_TARGET 3

// Control message framing: [1B type][4B payload_length][payload]
#define CTRL_HEADER_SIZE 5

// Data frame (Stream 1: unidirectional sender->receiver)
// [4B chunk_index][32B chunk_hash][4B chunk_data_size][data]
#define DATA_FRAME_HEADER_SIZE (4 + 32 + 4)  // 40 bytes
