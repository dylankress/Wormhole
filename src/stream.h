//
// stream.h
// Chunk-based two-stream transfer protocol over QUIC.
// by Dylan Kress
//

#pragma once

#include "common.h"
#include "protocol.h"
#include "file_io.h"
#include "manifest.h"
#include "chunk_store.h"

// External reference to global MsQuic API table (defined in wormhole.c)
extern const QUIC_API_TABLE *MsQuic;

//=============================================================================
// Send-side context (sender opens control stream, waits for MANIFEST_REQUEST,
// then opens data stream and sends chunks)
//=============================================================================

typedef struct {
    FILE           *file_handle;
    FILE_MANIFEST  *manifest;
    uint32_t        next_chunk_to_send;
    uint32_t        chunks_in_flight;       // max ~3 pipelined
    BOOLEAN         manifest_sent;
    BOOLEAN         all_chunks_sent;
    BOOLEAN         transfer_complete_received;
    HQUIC           connection;             // parent QUIC connection
    HQUIC           control_stream;
    HQUIC           data_stream;
    HANDLE          transfer_complete_event;
    LARGE_INTEGER   start_time;

    // Accumulation buffer for control stream receives
    uint8_t        *ctrl_recv_buffer;
    size_t          ctrl_recv_used;
    size_t          ctrl_recv_capacity;
} CHUNK_SEND_CONTEXT;

//=============================================================================
// Receive-side context (receiver gets control stream from sender,
// sends MANIFEST_REQUEST, receives manifest and data chunks)
//=============================================================================

typedef struct {
    FILE_MANIFEST  *manifest;
    BOOLEAN        *chunks_received;        // bitfield per chunk
    uint32_t        chunks_received_count;
    uint32_t        total_chunks;

    // Accumulation buffers
    uint8_t        *ctrl_recv_buffer;
    size_t          ctrl_recv_used;
    size_t          ctrl_recv_capacity;

    uint8_t        *data_recv_buffer;
    size_t          data_recv_used;
    size_t          data_recv_capacity;

    FILE           *file_handle;
    char           *output_path;            // final output path (in Downloads)
    char           *partial_path;           // .partial temp file path
    char            downloads_path[260];    // Downloads folder path (MAX_PATH)

    HQUIC           control_stream;
    HQUIC           data_stream;
    HANDLE          transfer_complete_event;
    LARGE_INTEGER   start_time;

    void           *user_context;           // RECEIVE_CLIENT_CONTEXT from wormhole.c
} CHUNK_RECEIVE_CONTEXT;

//=============================================================================
// Public API
//=============================================================================

// Send a file using the chunk protocol.
// Opens control stream (bidirectional) on Connection, waits for MANIFEST_REQUEST,
// then sends manifest + chunks. Signals transfer_complete_event when done.
void ChunkSendFile(HQUIC Connection, const char *file_path,
                   FILE_MANIFEST *manifest, HANDLE transfer_complete_event);

//=============================================================================
// Stream Callbacks (attached by wormhole.c / connection callbacks)
//=============================================================================

// Sender callbacks
QUIC_STATUS QUIC_API SenderControlStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

QUIC_STATUS QUIC_API SenderDataStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

// Receiver callbacks
QUIC_STATUS QUIC_API ReceiverControlStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

QUIC_STATUS QUIC_API ReceiverDataStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);
