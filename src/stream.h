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

#define MAX_CHUNKS_IN_FLIGHT 16
#define SEND_POOL_SIZE 32  // Pre-allocated send buffer slots

//=============================================================================
// Progress callback (optional — for daemon-mediated transfers)
//=============================================================================

// Called when transfer progress changes. Return FALSE to cancel the transfer.
typedef BOOLEAN (*StreamProgressCallback)(
    uint32_t chunks_done, uint32_t total_chunks,
    uint64_t bytes_done, uint64_t bytes_total,
    double speed_bps,       // Instantaneous speed in bytes/sec
    double eta_seconds,     // Estimated time remaining
    void *user_context
);

//=============================================================================
// Pre-allocated send buffer pool (eliminates per-chunk malloc/free)
//=============================================================================

typedef struct {
    uint8_t     data[WH_CHUNK_SIZE + DATA_FRAME_HEADER_SIZE];
    QUIC_BUFFER quic_buf;
    BOOLEAN     in_use;
} SEND_BUFFER_SLOT;

//=============================================================================
// Send-side context (sender opens control stream, waits for MANIFEST_REQUEST,
// then opens data stream and sends chunks)
//=============================================================================

typedef struct {
    FILE           *file_handle;
    FILE_MANIFEST  *manifest;
    uint32_t        next_chunk_to_send;
    uint32_t        chunks_in_flight;       // current pipelined chunk count
    BOOLEAN         manifest_sent;
    BOOLEAN         all_chunks_sent;
    BOOLEAN         transfer_complete_received;
    HQUIC           connection;             // parent QUIC connection
    HQUIC           control_stream;
    HQUIC           data_stream;
    WH_EVENT        transfer_complete_event;
    double          start_time;

    // Progress tracking
    uint64_t        bytes_sent;             // bytes of actually sent chunks (excludes skipped)
    uint32_t        chunks_sent_count;      // count of actually sent chunks (excludes skipped)
    uint32_t        total_needed_chunks;    // total chunks the receiver needs (for resume progress)
    uint64_t        total_needed_bytes;     // total bytes the receiver needs (for resume progress)
    double          last_progress_time;
    uint64_t        last_progress_bytes;

    // Adaptive pipelining (Phase B)
    uint64_t        ideal_send_bytes;       // from IDEAL_SEND_BUFFER_SIZE event (0 = use default)
    uint64_t        bytes_in_flight;        // actual bytes currently queued in MsQuic

    // Pre-allocated send buffer pool (Phase C)
    SEND_BUFFER_SLOT *send_pool;            // SEND_POOL_SIZE slots

    // Multi-file (v2 manifest) support
    char           *base_dir_path;          // directory path for v2 (NULL for v1)
    uint32_t        current_file_index;     // currently open file index (UINT32_MAX = none)

    // Resumable transfer: bitmask from CHUNK_REQUEST (NULL = send all)
    BOOLEAN        *chunks_needed;
    BOOLEAN         chunk_request_received;

    // Accumulation buffer for control stream receives
    uint8_t        *ctrl_recv_buffer;
    size_t          ctrl_recv_used;
    size_t          ctrl_recv_capacity;

    // Points to server context's transfer_complete flag (wormhole.c)
    BOOLEAN        *transfer_complete_flag;

    // Progress callback (NULL = use built-in PrintProgressBar)
    StreamProgressCallback progress_cb;
    void                  *progress_cb_ctx;

    // Atomic stream shutdown tracking
    volatile int32_t streams_shutdown;
    volatile int32_t expected_streams;
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
    WH_EVENT        transfer_complete_event;
    double          start_time;

    // Progress tracking
    uint64_t        bytes_received;
    double          last_progress_time;
    uint64_t        last_progress_bytes;

    // Periodic state save tracking
    uint32_t        last_save_chunk_count;
    double          last_state_save_time;

    // Multi-file (v2 manifest) support
    uint32_t        current_file_index;     // index of currently open file (UINT32_MAX = none)

    BOOLEAN         transfer_complete_pending; // Set when TRANSFER_COMPLETE sent, awaiting SEND_COMPLETE

    void           *user_context;           // RECEIVE_CLIENT_CONTEXT from wormhole.c

    // Progress callback (NULL = use built-in PrintProgressBar)
    StreamProgressCallback progress_cb;
    void                  *progress_cb_ctx;

    // Atomic stream shutdown tracking
    volatile int32_t streams_shutdown;
    volatile int32_t expected_streams;
} CHUNK_RECEIVE_CONTEXT;

//=============================================================================
// Public API
//=============================================================================

// Send a file using the chunk protocol.
// Opens control stream (bidirectional) on Connection, waits for MANIFEST_REQUEST,
// then sends manifest + chunks. Signals transfer_complete_event when done.
void ChunkSendFile(HQUIC Connection, const char *file_path,
                   FILE_MANIFEST *manifest, WH_EVENT transfer_complete_event,
                   BOOLEAN *transfer_complete_flag);

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
