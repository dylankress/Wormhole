//
// protocol.h
// by Dylan Kress
//

#pragma once

#include "common.h"

// ALPN identifier for Wormhole protocol
#define WORMHOLE_ALPN "wormhole"

// Default UDP port (4567 for development, use 443 in production)
#define WORMHOLE_DEFAULT_PORT 4567

// Chunk size for file transfers (64KB)
#define CHUNK_SIZE (64 * 1024)

// Minimum header size (file_size + filename_length)
#define HEADER_MIN_SIZE 12

// Wire Protocol Format (bytes on the wire):
//
// [8 bytes] uint64_t file_size (little-endian)
// [4 bytes] uint32_t filename_length (little-endian)
// [N bytes] filename (UTF-8, no null terminator)
// [remaining] file data chunks

