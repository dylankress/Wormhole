//
// stream.h
// by Dylan Kress
//

#pragma once

#include "common.h"
#include "protocol.h"
#include "file_io.h"

// External reference to global MsQuic API table (defined in wormhole.c)
extern const QUIC_API_TABLE *MsQuic;

// Context for sending files (client side)
typedef struct {
    FILE *file_handle;
    uint64_t total_file_size;
    uint64_t bytes_sent;
    char *filename;
    uint32_t filename_length;
    uint32_t chunks_in_flight;  // Track number of chunks sent but not yet acknowledged
    HANDLE transfer_complete_event;  // Signaled when transfer is complete
    LARGE_INTEGER start_time;  // Transfer start time (Windows high-res timer)
} SEND_CONTEXT;

// Context for receiving files (server side)
typedef struct {
    FILE *file_handle;
    uint64_t total_file_size;
    uint32_t filename_length;
    char *filename;
    uint64_t bytes_received;
    uint8_t *header_buffer;
    size_t header_bytes_received;
    BOOLEAN header_complete;
    LARGE_INTEGER start_time;  // Transfer start time (Windows high-res timer)
    void* user_context;  // Optional user context (e.g., for Downloads folder path)
} RECEIVE_CONTEXT;

// Stream callback (implemented in Part 5)
QUIC_STATUS QUIC_API ServerStreamCallback(
	HQUIC Stream,
	void *Context,
	QUIC_STREAM_EVENT *Event
);

// File sending (implemented in Part 4)
void SendFile(HQUIC Connection, const char *file_path, HANDLE transfer_complete_event);


void TestEndiannessHelpers(void);

// Stream callbacks (to be implemented)
QUIC_STATUS QUIC_API ClientStreamCallback(
    HQUIC Stream,
    void *Context,
    QUIC_STREAM_EVENT *Event
);

QUIC_STATUS QUIC_API ServerStreamCallback(
    HQUIC Stream,
    void *Context,
    QUIC_STREAM_EVENT *Event
);
