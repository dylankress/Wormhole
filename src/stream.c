//
// stream.c
// Chunk-based two-stream transfer protocol implementation.
// by Dylan Kress
//
// Transfer flow:
//   1. Sender opens bidirectional control stream (Stream 0)
//   2. Receiver sends MANIFEST_REQUEST on control stream
//   3. Sender replies with MANIFEST_RESPONSE (serialized manifest)
//   4. Sender opens unidirectional data stream (Stream 1), sends chunks
//   5. Each chunk: [4B index][32B hash][4B size][data]
//   6. After last chunk: FIN on data stream
//   7. Receiver verifies all chunks, sends TRANSFER_COMPLETE on control stream
//   8. Sender signals completion event
//

#include "stream.h"
#include "wire_format.h"
#include "manifest.h"
#include "chunk_store.h"
#include "transfer_state.h"
#include <blake3.h>
#include <errno.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static BOOLEAN SendControlMessage(HQUIC Stream, uint8_t msg_type,
                                  const uint8_t *payload, uint32_t payload_len);
static BOOLEAN SendNextDataChunk(CHUNK_SEND_CONTEXT *ctx);
static void ProcessReceiverControlData(CHUNK_RECEIVE_CONTEXT *ctx);
static void ProcessReceiverDataFrames(CHUNK_RECEIVE_CONTEXT *ctx);
static void CleanupSendContext(CHUNK_SEND_CONTEXT *ctx);
static void CleanupReceiveContext(CHUNK_RECEIVE_CONTEXT *ctx);

//=============================================================================
// Progress bar helper
//=============================================================================

#define PROGRESS_BAR_WIDTH 30

// Render a live progress bar using \r to overwrite the line.
// Uses printf directly (not LOG) to avoid timestamp prefix conflicting with \r.
static void PrintProgressBar(
    uint32_t chunks_done, uint32_t total_chunks,
    uint64_t bytes_transferred, uint64_t total_bytes,
    double *start_time,
    double *last_progress_time,
    uint64_t *last_progress_bytes,
    const char *label)
{
    if (total_chunks == 0) return;

    BOOLEAN is_complete = (chunks_done == total_chunks);

    double now = WH_TIMER_NOW();

    // Throttle: only update every 100ms (unless transfer is complete)
    if (!is_complete && *last_progress_time != 0.0)
    {
        double since_last = now - *last_progress_time;
        if (since_last < 0.1) return;
    }

    // Calculate instantaneous speed (bytes/sec) from last interval
    double speed = 0.0;
    if (*last_progress_time != 0.0)
    {
        double interval = now - *last_progress_time;
        if (interval > 0.0)
            speed = (double)(bytes_transferred - *last_progress_bytes) / interval;
    }
    else
    {
        // First update — use overall speed
        double elapsed = now - *start_time;
        if (elapsed > 0.0)
            speed = (double)bytes_transferred / elapsed;
    }

    // ETA based on overall average speed (smoother than instantaneous)
    double eta_seconds = 0.0;
    if (!is_complete)
    {
        double overall = now - *start_time;
        if (overall > 0.0)
        {
            double avg_speed = (double)bytes_transferred / overall;
            if (avg_speed > 0.0)
                eta_seconds = (double)(total_bytes - bytes_transferred) / avg_speed;
        }
    }

    // Update last progress markers
    *last_progress_time = now;
    *last_progress_bytes = bytes_transferred;

    // Build progress bar: [=====>          ]
    double pct = (chunks_done * 100.0) / total_chunks;
    int filled = (int)(pct * PROGRESS_BAR_WIDTH / 100.0);
    if (filled > PROGRESS_BAR_WIDTH) filled = PROGRESS_BAR_WIDTH;

    char bar[PROGRESS_BAR_WIDTH + 1];
    memset(bar, ' ', PROGRESS_BAR_WIDTH);
    bar[PROGRESS_BAR_WIDTH] = '\0';
    for (int i = 0; i < filled; i++) bar[i] = '=';
    if (filled < PROGRESS_BAR_WIDTH && !is_complete) bar[filled] = '>';

    // Format speed
    char speed_str[32];
    if (speed >= 1024.0 * 1024.0 * 1024.0)
        snprintf(speed_str, sizeof(speed_str), "%.1f GB/s", speed / (1024.0 * 1024.0 * 1024.0));
    else if (speed >= 1024.0 * 1024.0)
        snprintf(speed_str, sizeof(speed_str), "%.1f MB/s", speed / (1024.0 * 1024.0));
    else if (speed >= 1024.0)
        snprintf(speed_str, sizeof(speed_str), "%.1f KB/s", speed / 1024.0);
    else
        snprintf(speed_str, sizeof(speed_str), "%.0f B/s", speed);

    // Format ETA
    char eta_str[32];
    if (is_complete)
        snprintf(eta_str, sizeof(eta_str), "done");
    else if (eta_seconds < 60.0)
        snprintf(eta_str, sizeof(eta_str), "ETA: %ds", (int)eta_seconds);
    else if (eta_seconds < 3600.0)
        snprintf(eta_str, sizeof(eta_str), "ETA: %dm %ds",
                 (int)(eta_seconds / 60), ((int)eta_seconds) % 60);
    else
        snprintf(eta_str, sizeof(eta_str), "ETA: %dh %dm",
                 (int)(eta_seconds / 3600), ((int)eta_seconds % 3600) / 60);

    // Print progress bar with \r (no newline until complete)
    printf("\r%s [%s] %5.1f%% | %s | %s     ", label, bar, pct, speed_str, eta_str);

    if (is_complete)
        printf("\n");

    fflush(stdout);
}

//=============================================================================
// Multi-file (v2 manifest) helpers
//=============================================================================

#ifdef _WIN32
#include <direct.h>
#endif

// Ensure a directory path exists (creating parent directories as needed).
static BOOLEAN EnsureDirPath(const char *path)
{
    char tmp[MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return FALSE;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++)
    {
#ifdef _WIN32
        if (tmp[i] == '\\' || tmp[i] == '/')
        {
            tmp[i] = '\0';
            CreateDirectoryA(tmp, NULL);  // OK if exists
            tmp[i] = '\\';
        }
#else
        if (tmp[i] == '/')
        {
            tmp[i] = '\0';
            mkdir(tmp, 0755);  // OK if exists
            tmp[i] = '/';
        }
#endif
    }

#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
    return TRUE;
}

// Find which FILE_ENTRY a chunk_index belongs to.
// Returns NULL if not found (v1 manifest or invalid index).
static const FILE_ENTRY *FindFileEntryForChunk(const FILE_MANIFEST *m, uint32_t chunk_index)
{
    if (!m || m->version < WH_MANIFEST_VERSION_2 || !m->files) return NULL;

    for (uint32_t i = 0; i < m->file_count; i++)
    {
        if (chunk_index >= m->files[i].chunk_start &&
            chunk_index < m->files[i].chunk_start + m->files[i].chunk_count)
        {
            return &m->files[i];
        }
    }
    return NULL;
}

// Validate a relative path from a manifest entry.
// Rejects path traversal (".."), absolute paths, and null bytes.
// Returns 0 for valid, -1 for invalid.
static int ValidateRelativePath(const char *path, uint16_t path_len)
{
    if (!path || path_len == 0) return -1;

    // Reject null bytes within path
    for (uint16_t i = 0; i < path_len; i++)
    {
        if (path[i] == '\0') return -1;
    }

    // Reject absolute paths (/, \, or drive letter like C:)
    if (path[0] == '/' || path[0] == '\\') return -1;
    if (path_len >= 2 && path[1] == ':') return -1;

    // Reject ".." path components
    for (uint16_t i = 0; i < path_len; i++)
    {
        // Check if we're at the start of a path component
        if (i == 0 || path[i - 1] == '/' || path[i - 1] == '\\')
        {
            if (i + 1 < path_len && path[i] == '.' && path[i + 1] == '.')
            {
                // ".." at end of path, or followed by separator
                if (i + 2 == path_len || path[i + 2] == '/' || path[i + 2] == '\\')
                    return -1;
            }
        }
    }

    return 0;
}

// Defense-in-depth: verify that a resolved path stays within the expected directory.
// Uses realpath() (POSIX) or GetFullPathNameA() (Windows) to resolve symlinks and ".." components.
// For files that don't exist yet, resolves the parent directory instead.
// Returns TRUE if path is safe, FALSE if it escapes the expected directory.
static BOOLEAN VerifyPathWithinDir(const char *full_path, const char *expected_dir)
{
#ifdef _WIN32
    char resolved_dir[MAX_PATH];
    char resolved_path[MAX_PATH];

    // GetFullPathNameA resolves . and .. even for non-existent files
    if (GetFullPathNameA(expected_dir, MAX_PATH, resolved_dir, NULL) == 0)
        return FALSE;
    if (GetFullPathNameA(full_path, MAX_PATH, resolved_path, NULL) == 0)
        return FALSE;

    // Ensure trailing backslash on dir for prefix matching
    size_t dir_len = strlen(resolved_dir);
    if (dir_len > 0 && resolved_dir[dir_len - 1] != '\\')
    {
        if (dir_len + 1 < MAX_PATH)
        {
            resolved_dir[dir_len] = '\\';
            resolved_dir[dir_len + 1] = '\0';
            dir_len++;
        }
    }

    // Case-insensitive prefix check on Windows
    return (_strnicmp(resolved_path, resolved_dir, dir_len) == 0) ? TRUE : FALSE;
#else
    char resolved_dir[MAX_PATH];
    char resolved_path[MAX_PATH];

    // Resolve the expected output directory
    if (!realpath(expected_dir, resolved_dir))
        return FALSE;

    // Try to resolve the full path — may fail if file doesn't exist yet
    if (!realpath(full_path, resolved_path))
    {
        // File doesn't exist: resolve parent directory, then re-append filename
        char parent_copy[MAX_PATH];
        snprintf(parent_copy, sizeof(parent_copy), "%s", full_path);
        char *last_slash = strrchr(parent_copy, '/');
        if (!last_slash)
            return FALSE;
        *last_slash = '\0';
        const char *filename = last_slash + 1;

        if (!realpath(parent_copy, resolved_path))
            return FALSE;

        // Re-append the filename so the prefix check covers the full path
        size_t rp_len = strlen(resolved_path);
        snprintf(resolved_path + rp_len, sizeof(resolved_path) - rp_len,
                 "/%s", filename);
    }

    // Ensure trailing slash on dir for prefix matching
    size_t dir_len = strlen(resolved_dir);
    if (dir_len > 0 && resolved_dir[dir_len - 1] != '/')
    {
        if (dir_len + 1 < MAX_PATH)
        {
            resolved_dir[dir_len] = '/';
            resolved_dir[dir_len + 1] = '\0';
            dir_len++;
        }
    }

    return (strncmp(resolved_path, resolved_dir, dir_len) == 0) ? TRUE : FALSE;
#endif
}

// For v2 manifests: ensure the correct file is open in ctx->file_handle.
// Returns TRUE if file_handle is ready for writing the given chunk.
static BOOLEAN OpenChunkOutputFile(CHUNK_RECEIVE_CONTEXT *ctx, uint32_t chunk_index)
{
    if (ctx->manifest->version < WH_MANIFEST_VERSION_2)
        return (ctx->file_handle != NULL);  // v1: single file already open

    // Find which file this chunk belongs to
    uint32_t file_idx = UINT32_MAX;
    for (uint32_t i = 0; i < ctx->manifest->file_count; i++)
    {
        if (chunk_index >= ctx->manifest->files[i].chunk_start &&
            chunk_index < ctx->manifest->files[i].chunk_start + ctx->manifest->files[i].chunk_count)
        {
            file_idx = i;
            break;
        }
    }
    if (file_idx == UINT32_MAX) return FALSE;

    // Validate path (reject path traversal, absolute paths, null bytes)
    {
        const FILE_ENTRY *fe_val = &ctx->manifest->files[file_idx];
        if (ValidateRelativePath(fe_val->relative_path, fe_val->path_length) != 0)
        {
            LOG_ERROR("[RecvData] ERROR: Rejected unsafe path in manifest: %s\n",
                      fe_val->relative_path);
            return FALSE;
        }
    }

    // Already have the right file open?
    if (ctx->file_handle && ctx->current_file_index == file_idx)
        return TRUE;

    // Close current file if different
    if (ctx->file_handle)
    {
        fflush(ctx->file_handle);
        CloseFile(ctx->file_handle);
        ctx->file_handle = NULL;
    }

    // Build full path: output_path/<relative_path> (with OS separators)
    const FILE_ENTRY *fe = &ctx->manifest->files[file_idx];
    char full_path[MAX_PATH];

#ifdef _WIN32
    snprintf(full_path, sizeof(full_path), "%s\\", ctx->output_path);
    size_t base_len = strlen(full_path);
    size_t rem = sizeof(full_path) - base_len;
    if (fe->path_length < rem)
    {
        memcpy(full_path + base_len, fe->relative_path, fe->path_length);
        full_path[base_len + fe->path_length] = '\0';
        for (size_t k = base_len; full_path[k]; k++)
        {
            if (full_path[k] == '/') full_path[k] = '\\';
        }
    }
    else
    {
        return FALSE;
    }
#else
    snprintf(full_path, sizeof(full_path), "%s/%s", ctx->output_path, fe->relative_path);
#endif

    // Ensure parent directory exists
    char parent[MAX_PATH];
    memcpy(parent, full_path, sizeof(parent));
    // Find last separator
    char *last_sep = NULL;
    for (char *p = parent; *p; p++)
    {
#ifdef _WIN32
        if (*p == '\\' || *p == '/') last_sep = p;
#else
        if (*p == '/') last_sep = p;
#endif
    }
    if (last_sep)
    {
        *last_sep = '\0';
        EnsureDirPath(parent);
    }

    // Defense-in-depth: verify resolved path stays within output directory
    if (!VerifyPathWithinDir(full_path, ctx->output_path))
    {
        LOG_ERROR("[RecvData] ERROR: Path traversal detected, resolved path escapes output dir: %s\n",
                  full_path);
        return FALSE;
    }

    // Open file for writing (create or overwrite)
    ctx->file_handle = fopen(full_path, "r+b");
    if (!ctx->file_handle)
        ctx->file_handle = fopen(full_path, "wb");

    if (!ctx->file_handle)
    {
        LOG_ERROR("[RecvData] ERROR: Cannot open %s for writing\n", full_path);
        return FALSE;
    }

    ctx->current_file_index = file_idx;
    return TRUE;
}

// For v2 manifests: ensure the correct file is open in ctx->file_handle for reading.
// Returns TRUE if file_handle is ready for reading the given chunk.
static BOOLEAN OpenChunkInputFile(CHUNK_SEND_CONTEXT *ctx, uint32_t chunk_index)
{
    if (ctx->manifest->version < WH_MANIFEST_VERSION_2)
        return (ctx->file_handle != NULL);  // v1: single file already open

    // Find which file this chunk belongs to
    const FILE_ENTRY *fe = FindFileEntryForChunk(ctx->manifest, chunk_index);
    if (!fe) return FALSE;

    uint32_t file_idx = (uint32_t)(fe - ctx->manifest->files);

    // Already have the right file open?
    if (ctx->file_handle && ctx->current_file_index == file_idx)
        return TRUE;

    // Close current file if different
    if (ctx->file_handle)
    {
        CloseFile(ctx->file_handle);
        ctx->file_handle = NULL;
    }

    // Build full path: base_dir_path + OS separator + relative_path
    char full_path[MAX_PATH];

#ifdef _WIN32
    snprintf(full_path, sizeof(full_path), "%s\\", ctx->base_dir_path);
    size_t base_len = strlen(full_path);
    size_t rem = sizeof(full_path) - base_len;
    if (fe->path_length < rem)
    {
        memcpy(full_path + base_len, fe->relative_path, fe->path_length);
        full_path[base_len + fe->path_length] = '\0';
        for (size_t k = base_len; full_path[k]; k++)
        {
            if (full_path[k] == '/') full_path[k] = '\\';
        }
    }
    else
    {
        return FALSE;
    }
#else
    snprintf(full_path, sizeof(full_path), "%s/%s", ctx->base_dir_path, fe->relative_path);
#endif

    if (!OpenFileForRead(full_path, &ctx->file_handle))
    {
        LOG_ERROR("[SendData] ERROR: Cannot open %s for reading\n", full_path);
        return FALSE;
    }

    ctx->current_file_index = file_idx;
    return TRUE;
}

//=============================================================================
// Buffer accumulation helper
//=============================================================================

static BOOLEAN AccumulateBuffer(uint8_t **buf, size_t *used, size_t *capacity,
                                const uint8_t *data, size_t data_len)
{
    size_t needed = *used + data_len;
    if (needed > *capacity)
    {
        size_t new_cap = (*capacity == 0) ? 65536 : *capacity;
        while (new_cap < needed) new_cap *= 2;
        uint8_t *new_buf = (uint8_t *)realloc(*buf, new_cap);
        if (!new_buf) return FALSE;
        *buf = new_buf;
        *capacity = new_cap;
    }
    memcpy(*buf + *used, data, data_len);
    *used += data_len;
    return TRUE;
}

// Remove consumed bytes from front of buffer
static void ConsumeBuffer(uint8_t *buf, size_t *used, size_t consumed)
{
    if (consumed >= *used)
    {
        *used = 0;
    }
    else
    {
        memmove(buf, buf + consumed, *used - consumed);
        *used -= consumed;
    }
}

//=============================================================================
// Control message send helper
//=============================================================================

// Send a control message: [1B type][4B payload_length][payload]
// Buffer is heap-allocated and freed in SEND_COMPLETE callback.
static BOOLEAN SendControlMessage(HQUIC Stream, uint8_t msg_type,
                                  const uint8_t *payload, uint32_t payload_len)
{
    if (payload_len > MAX_CTRL_PAYLOAD)
    {
        LOG_ERROR("[stream] ERROR: Control message payload too large: %u\n", payload_len);
        return FALSE;
    }
    size_t frame_size = (size_t)CTRL_HEADER_SIZE + payload_len;
    uint8_t *buffer = (uint8_t *)malloc(frame_size);
    if (!buffer) return FALSE;

    buffer[0] = msg_type;
    WriteUint32LE(buffer + 1, payload_len);
    if (payload && payload_len > 0)
    {
        memcpy(buffer + CTRL_HEADER_SIZE, payload, payload_len);
    }

    QUIC_BUFFER *quic_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
    if (!quic_buf)
    {
        free(buffer);
        return FALSE;
    }

    quic_buf->Buffer = buffer;
    quic_buf->Length = frame_size;

    QUIC_STATUS status = MsQuic->StreamSend(Stream, quic_buf, 1,
                                            QUIC_SEND_FLAG_NONE, quic_buf);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[stream] ERROR: StreamSend control message failed: 0x%x\n", status);
        free(buffer);
        free(quic_buf);
        return FALSE;
    }

    return TRUE;
}

//=============================================================================
// Adaptive pipelining helper
//=============================================================================

// Decide whether we should queue another chunk. Uses MsQuic's BDP-based
// IDEAL_SEND_BUFFER_SIZE when available, falls back to MAX_CHUNKS_IN_FLIGHT.
static inline BOOLEAN ShouldSendMore(CHUNK_SEND_CONTEXT *ctx)
{
    if (ctx->all_chunks_sent || ctx->transfer_complete_received) return FALSE;
    // Safety cap: never exceed 4x the static limit regardless
    if (ctx->chunks_in_flight >= MAX_CHUNKS_IN_FLIGHT * 4) return FALSE;
    if (ctx->ideal_send_bytes > 0)
        return ctx->bytes_in_flight < ctx->ideal_send_bytes;
    return ctx->chunks_in_flight < MAX_CHUNKS_IN_FLIGHT;  // fallback
}

//=============================================================================
// Data chunk send
//=============================================================================

// Send the next chunk on the data stream.
// Frame: [4B chunk_index][32B hash][4B data_size][data]
static BOOLEAN SendNextDataChunk(CHUNK_SEND_CONTEXT *ctx)
{
    if (!ctx || !ctx->manifest || !ctx->data_stream)
        return FALSE;
    // v1 requires file_handle pre-opened; v2+ opens files on demand
    if (ctx->manifest->version < WH_MANIFEST_VERSION_2 && !ctx->file_handle)
        return FALSE;

    // Skip chunks the receiver already has (resumable transfer)
    if (ctx->chunk_request_received && ctx->chunks_needed)
    {
        while (ctx->next_chunk_to_send < ctx->manifest->chunk_count &&
               !ctx->chunks_needed[ctx->next_chunk_to_send])
        {
            ctx->next_chunk_to_send++;
        }
    }

    if (ctx->next_chunk_to_send >= ctx->manifest->chunk_count)
    {
        // All chunks queued
        ctx->all_chunks_sent = TRUE;
        // Send zero-length FIN so receiver knows no more data is coming
        if (ctx->data_stream)
        {
            MsQuic->StreamSend(ctx->data_stream, NULL, 0, QUIC_SEND_FLAG_FIN, NULL);
            LOG("[stream] All chunks skipped/sent, FIN sent on data stream\n");
        }
        return TRUE;
    }

    uint32_t idx = ctx->next_chunk_to_send;
    CHUNK_INFO *ci = &ctx->manifest->chunks[idx];
    uint32_t data_size = ci->chunk_size;

    // Open the correct file for this chunk (v2 switches files on demand)
    if (!OpenChunkInputFile(ctx, idx))
    {
        LOG_ERROR("[stream] ERROR: Cannot open input file for chunk %u\n", idx);
        return FALSE;
    }

    // Seek to chunk position (per-file offset for v2, absolute for v1)
    uint64_t offset;
    if (ctx->manifest->version >= WH_MANIFEST_VERSION_2)
    {
        const FILE_ENTRY *fe = FindFileEntryForChunk(ctx->manifest, idx);
        offset = (uint64_t)(idx - fe->chunk_start) * WH_CHUNK_SIZE;
    }
    else
    {
        offset = (uint64_t)idx * WH_CHUNK_SIZE;
    }
#ifdef _WIN32
    if (_fseeki64(ctx->file_handle, (__int64)offset, SEEK_SET) != 0)
    {
        LOG_ERROR("[stream] ERROR: Failed to seek to offset %llu for chunk %u\n",
                  (unsigned long long)offset, idx);
        return FALSE;
    }
#else
    if (fseeko(ctx->file_handle, (off_t)offset, SEEK_SET) != 0)
    {
        LOG_ERROR("[stream] ERROR: Failed to seek to offset %llu for chunk %u\n",
                  (unsigned long long)offset, idx);
        return FALSE;
    }
#endif

    // Find free slot in pre-allocated pool
    uint32_t frame_size = DATA_FRAME_HEADER_SIZE + data_size;
    SEND_BUFFER_SLOT *slot = NULL;
    if (ctx->send_pool)
    {
        for (int i = 0; i < SEND_POOL_SIZE; i++)
        {
            if (!ctx->send_pool[i].in_use) { slot = &ctx->send_pool[i]; break; }
        }
    }
    if (!slot) return FALSE;  // all slots in use

    // Write frame header + data directly into pool slot
    uint8_t *p = slot->data;
    WriteUint32LE(p, idx);              p += 4;
    memcpy(p, ci->hash, WH_HASH_SIZE); p += WH_HASH_SIZE;
    WriteUint32LE(p, data_size);        p += 4;

    // Read chunk data from file
    size_t bytes_read = 0;
    if (!ReadFileChunk(ctx->file_handle, p, data_size, &bytes_read) ||
        bytes_read != data_size)
    {
        LOG_ERROR("[stream] ERROR: Failed to read chunk %u from file\n", idx);
        return FALSE;
    }

    slot->quic_buf.Buffer = slot->data;
    slot->quic_buf.Length = frame_size;
    slot->in_use = TRUE;

    // Check if this is the last chunk
    BOOLEAN is_last = (idx == ctx->manifest->chunk_count - 1);
    QUIC_SEND_FLAGS flags = is_last ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;

    QUIC_STATUS status = MsQuic->StreamSend(ctx->data_stream, &slot->quic_buf, 1,
                                            flags, slot);  // ClientContext = slot
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[stream] ERROR: StreamSend data chunk %u failed: 0x%x\n", idx, status);
        slot->in_use = FALSE;
        return FALSE;
    }

    ctx->next_chunk_to_send++;
    ctx->chunks_in_flight++;
    ctx->bytes_in_flight += frame_size;
    ctx->bytes_sent += data_size;
    ctx->chunks_sent_count++;

    // Live progress bar — use only needed chunks/bytes for accurate resume progress
    {
        uint32_t progress_total_chunks = ctx->total_needed_chunks > 0
            ? ctx->total_needed_chunks : ctx->manifest->chunk_count;
        uint64_t progress_total_bytes = ctx->total_needed_bytes > 0
            ? ctx->total_needed_bytes : ctx->manifest->file_size;

        if (ctx->progress_cb) {
            // Calculate speed for callback
            double now = WH_TIMER_NOW();
            double speed = 0.0;
            if (ctx->last_progress_time != 0.0) {
                double interval = now - ctx->last_progress_time;
                if (interval > 0.0)
                    speed = (double)(ctx->bytes_sent - ctx->last_progress_bytes) / interval;
            }
            double eta = 0.0;
            double elapsed = now - ctx->start_time;
            if (elapsed > 0.0 && ctx->bytes_sent > 0) {
                double avg = (double)ctx->bytes_sent / elapsed;
                if (avg > 0.0)
                    eta = (double)(progress_total_bytes - ctx->bytes_sent) / avg;
            }
            ctx->progress_cb(ctx->chunks_sent_count, progress_total_chunks,
                             ctx->bytes_sent, progress_total_bytes,
                             speed, eta, ctx->progress_cb_ctx);
        } else {
            PrintProgressBar(ctx->chunks_sent_count, progress_total_chunks,
                             ctx->bytes_sent, progress_total_bytes,
                             &ctx->start_time, &ctx->last_progress_time,
                             &ctx->last_progress_bytes, "Sending");
        }
    }

    if (is_last)
    {
        ctx->all_chunks_sent = TRUE;
        LOG("[stream] Last chunk %u queued (FIN set)\n", idx);
    }

    return TRUE;
}

//=============================================================================
// Sender Callbacks
//=============================================================================

// SenderControlStreamCallback — handles MANIFEST_REQUEST and TRANSFER_COMPLETE
QUIC_STATUS QUIC_API SenderControlStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    CHUNK_SEND_CONTEXT *ctx = (CHUNK_SEND_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        LOG("[SenderCtrl] START_COMPLETE (status: 0x%x)\n", Event->START_COMPLETE.Status);
        break;

    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // Accumulate control data from receiver
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (!AccumulateBuffer(&ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used,
                                  &ctx->ctrl_recv_capacity, buf->Buffer, buf->Length))
            {
                LOG_ERROR("[SenderCtrl] ERROR: Failed to accumulate control data\n");
                MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                return QUIC_STATUS_SUCCESS;
            }
        }

        // Try to parse complete control messages
        while (ctx->ctrl_recv_used >= CTRL_HEADER_SIZE)
        {
            uint8_t msg_type = ctx->ctrl_recv_buffer[0];
            uint32_t payload_len = ReadUint32LE(ctx->ctrl_recv_buffer + 1);

            if (payload_len > MAX_CTRL_PAYLOAD)
            {
                LOG_ERROR("[SenderCtrl] ERROR: Payload too large: %u\n", payload_len);
                ctx->ctrl_recv_used = 0;
                break;
            }

            size_t frame_size = (size_t)CTRL_HEADER_SIZE + payload_len;

            if (ctx->ctrl_recv_used < frame_size) break;  // need more data

            switch (msg_type)
            {
            case CTRL_MSG_MANIFEST_REQUEST:
            {
                LOG("[SenderCtrl] Received MANIFEST_REQUEST\n");

                // Serialize and send manifest
                size_t manifest_size = 0;
                uint8_t *manifest_data = Manifest_Serialize(ctx->manifest, &manifest_size);
                if (!manifest_data)
                {
                    LOG_ERROR("[SenderCtrl] ERROR: Failed to serialize manifest\n");
                    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return QUIC_STATUS_SUCCESS;
                }

                if (!SendControlMessage(Stream, CTRL_MSG_MANIFEST_RESPONSE,
                                        manifest_data, (uint32_t)manifest_size))
                {
                    LOG_ERROR("[SenderCtrl] ERROR: Failed to send MANIFEST_RESPONSE\n");
                    free(manifest_data);
                    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return QUIC_STATUS_SUCCESS;
                }

                free(manifest_data);
                ctx->manifest_sent = TRUE;
                LOG("[SenderCtrl] Sent MANIFEST_RESPONSE (%zu bytes)\n", manifest_size);
                LOG("[SenderCtrl] Waiting for CHUNK_REQUEST before sending data...\n");

                break;
            }

            case CTRL_MSG_CHUNK_REQUEST:
            {
                // Receiver tells us which chunks it needs (after cache recovery).
                // Format: [4B total_chunks][ceil(total/8) bytes bitmask] bit=1 = needed
                const uint8_t *payload = ctx->ctrl_recv_buffer + CTRL_HEADER_SIZE;
                if (payload_len < 4) break;

                uint32_t req_total = ReadUint32LE(payload);
                uint32_t bitmask_bytes = (req_total + 7) / 8;
                if (payload_len != 4 + bitmask_bytes || req_total != ctx->manifest->chunk_count)
                {
                    LOG_ERROR("[SenderCtrl] ERROR: Invalid CHUNK_REQUEST payload\n");
                    break;
                }

                ctx->chunks_needed = (BOOLEAN *)calloc(req_total, sizeof(BOOLEAN));
                if (!ctx->chunks_needed) break;

                const uint8_t *bitmask = payload + 4;
                uint32_t needed_count = 0;
                for (uint32_t i = 0; i < req_total; i++)
                {
                    if (bitmask[i / 8] & (1 << (i % 8)))
                    {
                        ctx->chunks_needed[i] = TRUE;
                        needed_count++;
                    }
                }

                ctx->chunk_request_received = TRUE;
                ctx->total_needed_chunks = needed_count;

                // Compute total bytes needed (for accurate resume progress bar)
                ctx->total_needed_bytes = 0;
                for (uint32_t i = 0; i < req_total; i++)
                {
                    if (ctx->chunks_needed[i])
                        ctx->total_needed_bytes += ctx->manifest->chunks[i].chunk_size;
                }

                LOG("[SenderCtrl] Received CHUNK_REQUEST: %u/%u chunks needed\n",
                    needed_count, req_total);

                // Open data stream (unidirectional) and start sending requested chunks
                QUIC_STATUS status = MsQuic->StreamOpen(
                    ctx->connection,
                    QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,
                    SenderDataStreamCallback,
                    ctx,
                    &ctx->data_stream);

                if (QUIC_FAILED(status))
                {
                    LOG_ERROR("[SenderCtrl] ERROR: Failed to open data stream: 0x%x\n", status);
                    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return QUIC_STATUS_SUCCESS;
                }

                status = MsQuic->StreamStart(ctx->data_stream, QUIC_STREAM_START_FLAG_NONE);
                if (QUIC_FAILED(status))
                {
                    LOG_ERROR("[SenderCtrl] ERROR: Failed to start data stream: 0x%x\n", status);
                    MsQuic->StreamClose(ctx->data_stream);
                    ctx->data_stream = NULL;
                    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return QUIC_STATUS_SUCCESS;
                }

                LOG("[SenderCtrl] Data stream opened, starting chunk send...\n");
                ctx->expected_streams = 2;  // Now both control + data
                ctx->start_time = WH_TIMER_NOW();

                // Pipeline initial chunks (adaptive: fill to ideal or fallback limit)
                while (ShouldSendMore(ctx))
                {
                    if (!SendNextDataChunk(ctx))
                    {
                        if (!ctx->all_chunks_sent)
                            LOG_ERROR("[SenderCtrl] ERROR: Failed to send initial chunk\n");
                        break;
                    }
                }

                break;
            }

            case CTRL_MSG_TRANSFER_COMPLETE:
            {
                LOG("[SenderCtrl] Received TRANSFER_COMPLETE\n");
                ctx->transfer_complete_received = TRUE;
                if (ctx->transfer_complete_flag) {
                    *ctx->transfer_complete_flag = TRUE;
                }

                // Abort data stream — receiver is done, no point sending more chunks
                if (ctx->data_stream)
                {
                    MsQuic->StreamShutdown(ctx->data_stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
                }

                // Report transfer stats
                double end_time = WH_TIMER_NOW();
                double duration = end_time - ctx->start_time;
                double throughput = (ctx->manifest->file_size / 1024.0) / duration;

                LOG("[SenderCtrl] Transfer complete: %llu bytes in %.2f sec (%.1f KB/s)\n",
                    (unsigned long long)ctx->manifest->file_size, duration, throughput);

                // Report 100% completion to transfer manager (critical for "all from cache" case
                // where no data chunks were sent and progress callback was never called)
                if (ctx->progress_cb) {
                    ctx->progress_cb(ctx->manifest->chunk_count, ctx->manifest->chunk_count,
                                     ctx->manifest->file_size, ctx->manifest->file_size,
                                     0.0, 0.0, ctx->progress_cb_ctx);
                }

                // Signal completion
                if (ctx->transfer_complete_event)
                {
                    WH_EVENT_SET(ctx->transfer_complete_event);
                }
                break;
            }

            default:
                LOG("[SenderCtrl] Unknown control message type: 0x%02x\n", msg_type);
                break;
            }

            ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
        }

        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        // Free the QUIC_BUFFER + data allocated in SendControlMessage
        if (Event->SEND_COMPLETE.ClientContext)
        {
            QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
            free(sent->Buffer);
            free(sent);
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        LOG("[SenderCtrl] Receiver closed their send direction\n");
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[SenderCtrl] SHUTDOWN_COMPLETE\n");
        ctx->control_stream = NULL;
        {
            int32_t prev = WH_ATOMIC_ADD(&ctx->streams_shutdown, 1);
            if (prev + 1 >= ctx->expected_streams)
            {
                CleanupSendContext(ctx);
            }
        }
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// SenderDataStreamCallback — handles SEND_COMPLETE for chunk pipelining
QUIC_STATUS QUIC_API SenderDataStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    CHUNK_SEND_CONTEXT *ctx = (CHUNK_SEND_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        LOG("[SenderData] START_COMPLETE (status: 0x%x)\n", Event->START_COMPLETE.Status);
        break;

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        // Release pool slot (no free needed — pre-allocated)
        if (Event->SEND_COMPLETE.ClientContext)
        {
            SEND_BUFFER_SLOT *slot = (SEND_BUFFER_SLOT *)Event->SEND_COMPLETE.ClientContext;
            if (ctx) ctx->bytes_in_flight -= slot->quic_buf.Length;
            slot->in_use = FALSE;
        }

        if (ctx && ctx->chunks_in_flight > 0)
            ctx->chunks_in_flight--;

        if (Event->SEND_COMPLETE.Canceled)
        {
            LOG_ERROR("[SenderData] Send canceled, aborting\n");
            break;
        }

        // Pipeline next chunk using adaptive check
        while (ctx && ShouldSendMore(ctx))
        {
            if (!SendNextDataChunk(ctx))
            {
                if (!ctx->all_chunks_sent)
                {
                    LOG_ERROR("[SenderData] ERROR: SendNextDataChunk failed\n");
                    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
                }
                break;
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        LOG("[SenderData] Peer aborted receive, shutting down data stream\n");
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        break;

    case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
        if (ctx)
        {
            ctx->ideal_send_bytes = Event->IDEAL_SEND_BUFFER_SIZE.ByteCount;
            LOG("[SenderData] Ideal send buffer: %llu bytes\n",
                (unsigned long long)ctx->ideal_send_bytes);
            // Fill up to the new ideal if we have room
            while (ctx && ShouldSendMore(ctx))
            {
                if (!SendNextDataChunk(ctx)) break;
            }
        }
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[SenderData] SHUTDOWN_COMPLETE\n");
        ctx->data_stream = NULL;
        {
            int32_t prev = WH_ATOMIC_ADD(&ctx->streams_shutdown, 1);
            if (prev + 1 >= ctx->expected_streams)
            {
                CleanupSendContext(ctx);
            }
        }
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Receiver Callbacks
//=============================================================================

// ReceiverControlStreamCallback — handles MANIFEST_RESPONSE
QUIC_STATUS QUIC_API ReceiverControlStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    CHUNK_RECEIVE_CONTEXT *ctx = (CHUNK_RECEIVE_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        LOG("[RecvCtrl] START_COMPLETE\n");
        ctx->expected_streams = 1;  // Control stream
        break;

    case QUIC_STREAM_EVENT_RECEIVE:
    {
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (!AccumulateBuffer(&ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used,
                                  &ctx->ctrl_recv_capacity, buf->Buffer, buf->Length))
            {
                LOG_ERROR("[RecvCtrl] ERROR: Failed to accumulate control data\n");
                MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                return QUIC_STATUS_SUCCESS;
            }
        }

        ProcessReceiverControlData(ctx);
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        if (Event->SEND_COMPLETE.ClientContext)
        {
            QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
            free(sent->Buffer);
            free(sent);
        }
        // Signal completion once TRANSFER_COMPLETE has been transmitted
        if (ctx->transfer_complete_pending && ctx->transfer_complete_event)
        {
            ctx->transfer_complete_pending = FALSE;
            WH_EVENT_SET(ctx->transfer_complete_event);
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        LOG("[RecvCtrl] Sender closed their send direction on control stream\n");
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[RecvCtrl] SHUTDOWN_COMPLETE\n");
        ctx->control_stream = NULL;
        {
            int32_t prev = WH_ATOMIC_ADD(&ctx->streams_shutdown, 1);
            if (prev + 1 >= ctx->expected_streams)
            {
                CleanupReceiveContext(ctx);
            }
        }
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// ReceiverDataStreamCallback — receives and verifies chunk data
QUIC_STATUS QUIC_API ReceiverDataStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    CHUNK_RECEIVE_CONTEXT *ctx = (CHUNK_RECEIVE_CONTEXT *)Context;

    // Data stream accepted — we now have 2 active streams
    ctx->expected_streams = 2;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (!AccumulateBuffer(&ctx->data_recv_buffer, &ctx->data_recv_used,
                                  &ctx->data_recv_capacity, buf->Buffer, buf->Length))
            {
                LOG_ERROR("[RecvData] ERROR: Failed to accumulate data\n");
                MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                return QUIC_STATUS_SUCCESS;
            }
        }

        ProcessReceiverDataFrames(ctx);
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    {
        LOG("[RecvData] Sender finished sending data (FIN received)\n");

        // Process any remaining buffered data
        if (ctx->data_recv_used > 0)
        {
            ProcessReceiverDataFrames(ctx);
        }

        // Check if transfer is complete
        if (ctx->manifest && ctx->chunks_received_count == ctx->total_chunks)
        {
            LOG("[RecvData] All %u chunks received successfully!\n", ctx->total_chunks);

            // Close current file handle
            if (ctx->file_handle)
            {
                CloseFile(ctx->file_handle);
                ctx->file_handle = NULL;
            }

            if (ctx->manifest->version >= WH_MANIFEST_VERSION_2)
            {
                // V2+: files were written directly to output_path directory
                LOG("[RecvData] Directory saved: %s\n", ctx->output_path);
            }
            else if (ctx->partial_path && ctx->output_path)
            {
                // V1: rename .partial to final
                if (FileExists(ctx->output_path))
                {
                    remove(ctx->output_path);
                }

                if (rename(ctx->partial_path, ctx->output_path) != 0)
                {
                    LOG_ERROR("[RecvData] ERROR: Failed to rename %s -> %s (errno: %d)\n",
                              ctx->partial_path, ctx->output_path, errno);
                }
                else
                {
                    LOG("[RecvData] File saved: %s\n", ctx->output_path);
                }
            }

            // Delete state file — transfer is complete
            TransferState_Delete(ctx->manifest->manifest_hash);

            // Report transfer stats
            double end_time = WH_TIMER_NOW();
            double duration = end_time - ctx->start_time;
            double throughput = (ctx->manifest->file_size / 1024.0) / duration;

            LOG("[RecvData] Transfer: %llu bytes in %.2f sec (%.1f KB/s)\n",
                (unsigned long long)ctx->manifest->file_size, duration, throughput);

            // Send TRANSFER_COMPLETE to sender
            if (ctx->control_stream)
            {
                SendControlMessage(ctx->control_stream, CTRL_MSG_TRANSFER_COMPLETE, NULL, 0);
                LOG("[RecvData] Sent TRANSFER_COMPLETE to sender\n");
                ctx->transfer_complete_pending = TRUE;
            }
            else if (ctx->transfer_complete_event)
            {
                WH_EVENT_SET(ctx->transfer_complete_event);
            }
        }
        else if (ctx->manifest)
        {
            LOG_ERROR("[RecvData] WARNING: FIN received but only %u/%u chunks received\n",
                      ctx->chunks_received_count, ctx->total_chunks);

            // Save state immediately for crash safety (cleanup also saves, but this is earlier)
            if (ctx->chunks_received && ctx->chunks_received_count > 0)
            {
                TransferState_Save(ctx->manifest->manifest_hash,
                                   ctx->chunks_received, ctx->total_chunks);
                LOG("[RecvData] Saved transfer state (%u/%u) on incomplete FIN\n",
                    ctx->chunks_received_count, ctx->total_chunks);
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[RecvData] SHUTDOWN_COMPLETE\n");
        ctx->data_stream = NULL;
        {
            int32_t prev = WH_ATOMIC_ADD(&ctx->streams_shutdown, 1);
            if (prev + 1 >= ctx->expected_streams)
            {
                CleanupReceiveContext(ctx);
            }
        }
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Receiver processing helpers
//=============================================================================

static void ProcessReceiverControlData(CHUNK_RECEIVE_CONTEXT *ctx)
{
    while (ctx->ctrl_recv_used >= CTRL_HEADER_SIZE)
    {
        uint8_t msg_type = ctx->ctrl_recv_buffer[0];
        uint32_t payload_len = ReadUint32LE(ctx->ctrl_recv_buffer + 1);

        if (payload_len > MAX_CTRL_PAYLOAD)
        {
            LOG_ERROR("[RecvCtrl] ERROR: Payload too large: %u\n", payload_len);
            ctx->ctrl_recv_used = 0;
            break;
        }

        size_t frame_size = (size_t)CTRL_HEADER_SIZE + payload_len;

        if (ctx->ctrl_recv_used < frame_size) break;

        switch (msg_type)
        {
        case CTRL_MSG_MANIFEST_RESPONSE:
        {
            LOG("[RecvCtrl] Received MANIFEST_RESPONSE (%u bytes payload)\n", payload_len);

            const uint8_t *manifest_data = ctx->ctrl_recv_buffer + CTRL_HEADER_SIZE;
            ctx->manifest = Manifest_Deserialize(manifest_data, payload_len);

            if (!ctx->manifest)
            {
                LOG_ERROR("[RecvCtrl] ERROR: Failed to deserialize manifest\n");
                ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                return;
            }

            ctx->total_chunks = ctx->manifest->chunk_count;
            LOG("[RecvCtrl] Manifest: file=%s, size=%llu, chunks=%u\n",
                ctx->manifest->filename,
                (unsigned long long)ctx->manifest->file_size,
                ctx->total_chunks);

            // Allocate chunk tracking array
            if (ctx->total_chunks > 0)
            {
                ctx->chunks_received = (BOOLEAN *)calloc(ctx->total_chunks, sizeof(BOOLEAN));
                if (!ctx->chunks_received)
                {
                    LOG_ERROR("[RecvCtrl] ERROR: Failed to allocate chunk tracking\n");
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return;
                }
            }

            // Build output path
            if (ctx->output_path) free(ctx->output_path);
            if (ctx->partial_path) free(ctx->partial_path);
            ctx->partial_path = NULL;
            ctx->current_file_index = UINT32_MAX;

            BOOLEAN is_multifile = (ctx->manifest->version >= WH_MANIFEST_VERSION_2);

            if (is_multifile)
            {
                // V2: output_path is a directory
                if (ctx->downloads_path[0])
                {
                    char dir_path[260];
                    snprintf(dir_path, sizeof(dir_path), "%s"
#ifdef _WIN32
                             "\\"
#else
                             "/"
#endif
                             "%s", ctx->downloads_path, ctx->manifest->filename);
                    ctx->output_path = _strdup(dir_path);
                }
                else
                {
                    ctx->output_path = _strdup(ctx->manifest->filename);
                }

                // Create the output directory
                if (!ctx->output_path)
                {
                    LOG_ERROR("[RecvCtrl] ERROR: Failed to allocate output path\n");
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return;
                }
                EnsureDirPath(ctx->output_path);

                LOG("[RecvCtrl] Multi-file transfer: directory=%s, files=%u\n",
                    ctx->output_path, ctx->manifest->file_count);
            }
            else
            {
                // V1: output_path is a file
                if (ctx->downloads_path[0])
                {
                    char unique_path[260];
                    if (GetUniqueFilename(ctx->downloads_path, ctx->manifest->filename,
                                          unique_path, sizeof(unique_path)))
                    {
                        ctx->output_path = _strdup(unique_path);
                    }
                    else
                    {
                        ctx->output_path = _strdup(ctx->manifest->filename);
                    }
                }
                else
                {
                    ctx->output_path = _strdup(ctx->manifest->filename);
                }

                if (!ctx->output_path)
                {
                    LOG_ERROR("[RecvCtrl] ERROR: Failed to allocate output path\n");
                    ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                    return;
                }

                // Create .partial path (v1 only)
                size_t plen = strlen(ctx->output_path) + 9;
                ctx->partial_path = (char *)malloc(plen);
                if (ctx->partial_path)
                    snprintf(ctx->partial_path, plen, "%s.partial", ctx->output_path);
            }

            // Initialize chunk store for this transfer
            ChunkStore_Init();

            // --- Resumable transfer: check for saved state ---
            BOOLEAN resuming = FALSE;
            BOOLEAN *saved_received = NULL;
            uint32_t saved_total = 0;

            if (TransferState_Load(ctx->manifest->manifest_hash,
                                   &saved_received, &saved_total) &&
                saved_total == ctx->total_chunks)
            {
                resuming = TRUE;
                for (uint32_t i = 0; i < ctx->total_chunks; i++)
                {
                    if (saved_received[i])
                    {
                        ctx->chunks_received[i] = TRUE;
                        ctx->chunks_received_count++;
                        ctx->bytes_received += ctx->manifest->chunks[i].chunk_size;
                    }
                }
                free(saved_received);
                LOG("[RecvCtrl] Loaded saved state: %u/%u chunks from previous transfer\n",
                    ctx->chunks_received_count, ctx->total_chunks);
            }

            // Open output file(s) for writing
            if (!is_multifile)
            {
                // V1: open single .partial file
                if (ctx->partial_path)
                {
                    if (resuming && FileExists(ctx->partial_path))
                    {
                        ctx->file_handle = fopen(ctx->partial_path, "r+b");
                        if (!ctx->file_handle)
                        {
                            LOG_ERROR("[RecvCtrl] ERROR: Cannot reopen %s for resume\n",
                                      ctx->partial_path);
                            ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                            return;
                        }
                    }
                    else
                    {
                        if (!OpenFileForWrite(ctx->partial_path, &ctx->file_handle))
                        {
                            LOG_ERROR("[RecvCtrl] ERROR: Cannot open %s for writing\n",
                                      ctx->partial_path);
                            ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                            return;
                        }
                    }
                }
            }
            // V2: files are opened on demand in OpenChunkOutputFile()

            // Check ChunkStore for additional chunks we already have
            {
                uint8_t *store_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
                if (store_buf)
                {
                    uint32_t store_recovered = 0;
                    for (uint32_t i = 0; i < ctx->total_chunks; i++)
                    {
                        if (ctx->chunks_received[i]) continue;
                        if (!ChunkStore_Has(ctx->manifest->chunks[i].hash)) continue;

                        uint32_t chunk_size = 0;
                        if (ChunkStore_Get(ctx->manifest->chunks[i].hash,
                                           store_buf, &chunk_size))
                        {
                            if (is_multifile)
                            {
                                // V2: open correct file on demand
                                if (!OpenChunkOutputFile(ctx, i)) continue;
                                const FILE_ENTRY *fe = FindFileEntryForChunk(ctx->manifest, i);
                                if (!fe) continue;
                                uint64_t off_in_file = (uint64_t)(i - fe->chunk_start) * WH_CHUNK_SIZE;
#ifdef _WIN32
                                if (_fseeki64(ctx->file_handle, (__int64)off_in_file, SEEK_SET) != 0)
                                {
                                    LOG_ERROR("[RecvCtrl] ERROR: Failed to seek to offset %llu for store chunk %u\n",
                                              (unsigned long long)off_in_file, i);
                                    continue;
                                }
#else
                                if (fseeko(ctx->file_handle, (off_t)off_in_file, SEEK_SET) != 0)
                                {
                                    LOG_ERROR("[RecvCtrl] ERROR: Failed to seek to offset %llu for store chunk %u\n",
                                              (unsigned long long)off_in_file, i);
                                    continue;
                                }
#endif
                            }
                            else
                            {
                                // V1: single file, absolute offset
                                uint64_t file_offset = (uint64_t)i * WH_CHUNK_SIZE;
#ifdef _WIN32
                                if (_fseeki64(ctx->file_handle, (__int64)file_offset, SEEK_SET) != 0)
                                {
                                    LOG_ERROR("[RecvCtrl] ERROR: Failed to seek to offset %llu for store chunk %u\n",
                                              (unsigned long long)file_offset, i);
                                    continue;
                                }
#else
                                if (fseeko(ctx->file_handle, (off_t)file_offset, SEEK_SET) != 0)
                                {
                                    LOG_ERROR("[RecvCtrl] ERROR: Failed to seek to offset %llu for store chunk %u\n",
                                              (unsigned long long)file_offset, i);
                                    continue;
                                }
#endif
                            }

                            if (fwrite(store_buf, 1, chunk_size, ctx->file_handle) == chunk_size)
                            {
                                ctx->chunks_received[i] = TRUE;
                                ctx->chunks_received_count++;
                                ctx->bytes_received += chunk_size;
                                store_recovered++;
                            }
                        }
                    }
                    free(store_buf);

                    if (store_recovered > 0)
                    {
                        if (ctx->file_handle) fflush(ctx->file_handle);
                        LOG("[RecvCtrl] Recovered %u chunks from local store\n", store_recovered);
                    }
                }
            }

            ctx->start_time = WH_TIMER_NOW();
            ctx->last_state_save_time = ctx->start_time;
            ctx->last_save_chunk_count = ctx->chunks_received_count;

            // If all chunks already available, complete immediately
            if (ctx->chunks_received_count == ctx->total_chunks)
            {
                LOG("[RecvCtrl] All %u chunks recovered from cache!\n", ctx->total_chunks);

                if (ctx->file_handle)
                {
                    CloseFile(ctx->file_handle);
                    ctx->file_handle = NULL;
                }

                if (is_multifile)
                {
                    LOG("[RecvCtrl] Directory saved: %s\n", ctx->output_path);
                }
                else if (ctx->partial_path && ctx->output_path)
                {
                    if (FileExists(ctx->output_path)) remove(ctx->output_path);
                    if (rename(ctx->partial_path, ctx->output_path) != 0)
                        LOG_ERROR("[RecvCtrl] ERROR: rename failed: %s -> %s\n",
                                  ctx->partial_path, ctx->output_path);
                    else
                        LOG("[RecvCtrl] File saved: %s\n", ctx->output_path);
                }

                TransferState_Delete(ctx->manifest->manifest_hash);

                // Report 100% completion to transfer manager (progress callback was never
                // called since all chunks came from local store, not the network)
                if (ctx->progress_cb) {
                    ctx->progress_cb(ctx->chunks_received_count, ctx->total_chunks,
                                     ctx->manifest->file_size, ctx->manifest->file_size,
                                     0.0, 0.0, ctx->progress_cb_ctx);
                }

                if (ctx->control_stream)
                {
                    SendControlMessage(ctx->control_stream, CTRL_MSG_TRANSFER_COMPLETE, NULL, 0);
                    ctx->transfer_complete_pending = TRUE;
                }
                else if (ctx->transfer_complete_event)
                {
                    WH_EVENT_SET(ctx->transfer_complete_event);
                }
                break;
            }

            // Send CHUNK_REQUEST to tell sender which chunks we need
            // (always sent — sender waits for this before opening data stream)
            {
                uint32_t bitmask_bytes = (ctx->total_chunks + 7) / 8;
                uint32_t req_payload_len = 4 + bitmask_bytes;
                uint8_t *req_payload = (uint8_t *)calloc(1, req_payload_len);
                if (req_payload)
                {
                    WriteUint32LE(req_payload, ctx->total_chunks);
                    for (uint32_t i = 0; i < ctx->total_chunks; i++)
                    {
                        if (!ctx->chunks_received[i])  // bit=1 means NEEDED
                            req_payload[4 + i / 8] |= (1 << (i % 8));
                    }
                    SendControlMessage(ctx->control_stream, CTRL_MSG_CHUNK_REQUEST,
                                       req_payload, req_payload_len);
                    LOG("[RecvCtrl] Sent CHUNK_REQUEST: %u/%u cached, need %u\n",
                        ctx->chunks_received_count, ctx->total_chunks,
                        ctx->total_chunks - ctx->chunks_received_count);
                    free(req_payload);
                }
            }

            LOG("[RecvCtrl] Ready to receive %u chunks\n",
                ctx->total_chunks - ctx->chunks_received_count);
            break;
        }

        default:
            LOG("[RecvCtrl] Unknown control message type: 0x%02x\n", msg_type);
            break;
        }

        ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
    }
}

static void ProcessReceiverDataFrames(CHUNK_RECEIVE_CONTEXT *ctx)
{
    if (!ctx->manifest) return;
    // V1 requires file_handle to be pre-opened; V2+ opens files on demand
    if (ctx->manifest->version < WH_MANIFEST_VERSION_2 && !ctx->file_handle) return;

    while (ctx->data_recv_used >= DATA_FRAME_HEADER_SIZE)
    {
        // Parse frame header
        uint32_t chunk_index = ReadUint32LE(ctx->data_recv_buffer);
        // hash is at offset 4
        uint32_t data_size = ReadUint32LE(ctx->data_recv_buffer + 4 + WH_HASH_SIZE);

        if (data_size > WH_CHUNK_SIZE)
        {
            LOG_ERROR("[RecvData] Invalid data_size %u (max %u)\n", data_size, WH_CHUNK_SIZE);
            ctx->data_recv_used = 0;
            break;
        }

        size_t frame_size = (size_t)DATA_FRAME_HEADER_SIZE + data_size;
        if (ctx->data_recv_used < frame_size) break;  // need more data

        const uint8_t *chunk_hash = ctx->data_recv_buffer + 4;
        const uint8_t *chunk_data = ctx->data_recv_buffer + DATA_FRAME_HEADER_SIZE;

        // Validate chunk index
        if (chunk_index >= ctx->total_chunks)
        {
            LOG_ERROR("[RecvData] ERROR: Invalid chunk index %u (max: %u)\n",
                      chunk_index, ctx->total_chunks - 1);
            ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
            continue;
        }

        // Verify Blake3 hash
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, chunk_data, data_size);
        uint8_t computed_hash[WH_HASH_SIZE];
        blake3_hasher_finalize(&hasher, computed_hash, WH_HASH_SIZE);

        if (memcmp(computed_hash, chunk_hash, WH_HASH_SIZE) != 0)
        {
            LOG_ERROR("[RecvData] ERROR: Hash mismatch for chunk %u\n", chunk_index);
            ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
            continue;
        }

        // Also verify against manifest
        if (memcmp(computed_hash, ctx->manifest->chunks[chunk_index].hash, WH_HASH_SIZE) != 0)
        {
            LOG_ERROR("[RecvData] ERROR: Chunk %u hash doesn't match manifest\n", chunk_index);
            ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
            continue;
        }

        // Write chunk to file at correct offset
        if (ctx->manifest->version >= WH_MANIFEST_VERSION_2)
        {
            // V2+: open the correct file for this chunk
            if (!OpenChunkOutputFile(ctx, chunk_index))
            {
                LOG_ERROR("[RecvData] ERROR: Cannot open output file for chunk %u\n", chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
            const FILE_ENTRY *fe = FindFileEntryForChunk(ctx->manifest, chunk_index);
            if (!fe)
            {
                LOG_ERROR("[RecvData] ERROR: No file entry for chunk %u\n", chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
            uint64_t file_offset = (uint64_t)(chunk_index - fe->chunk_start) * WH_CHUNK_SIZE;
#ifdef _WIN32
            if (_fseeki64(ctx->file_handle, (__int64)file_offset, SEEK_SET) != 0)
            {
                LOG_ERROR("[RecvData] ERROR: Failed to seek to offset %llu for chunk %u\n",
                          (unsigned long long)file_offset, chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
#else
            if (fseeko(ctx->file_handle, (off_t)file_offset, SEEK_SET) != 0)
            {
                LOG_ERROR("[RecvData] ERROR: Failed to seek to offset %llu for chunk %u\n",
                          (unsigned long long)file_offset, chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
#endif
        }
        else
        {
            // V1: single file, absolute offset
            uint64_t file_offset = (uint64_t)chunk_index * WH_CHUNK_SIZE;
#ifdef _WIN32
            if (_fseeki64(ctx->file_handle, (__int64)file_offset, SEEK_SET) != 0)
            {
                LOG_ERROR("[RecvData] ERROR: Failed to seek to offset %llu for chunk %u\n",
                          (unsigned long long)file_offset, chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
#else
            if (fseeko(ctx->file_handle, (off_t)file_offset, SEEK_SET) != 0)
            {
                LOG_ERROR("[RecvData] ERROR: Failed to seek to offset %llu for chunk %u\n",
                          (unsigned long long)file_offset, chunk_index);
                ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
                continue;
            }
#endif
        }

        if (!WriteFileChunkWithRetry(ctx->file_handle, chunk_data, data_size))
        {
            LOG_ERROR("[RecvData] ERROR: Failed to write chunk %u to file\n", chunk_index);
            ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
            continue;
        }

        // Store chunk in content-addressed store
        ChunkStore_Put(computed_hash, chunk_data, data_size);

        // Mark as received
        if (!ctx->chunks_received[chunk_index])
        {
            ctx->chunks_received[chunk_index] = TRUE;
            ctx->chunks_received_count++;
            ctx->bytes_received += data_size;
        }

        // Live progress bar
        if (ctx->progress_cb) {
            double now = WH_TIMER_NOW();
            double speed = 0.0;
            if (ctx->last_progress_time != 0.0) {
                double interval = now - ctx->last_progress_time;
                if (interval > 0.0)
                    speed = (double)(ctx->bytes_received - ctx->last_progress_bytes) / interval;
            }
            double eta = 0.0;
            double elapsed = now - ctx->start_time;
            if (elapsed > 0.0 && ctx->bytes_received > 0) {
                double avg = (double)ctx->bytes_received / elapsed;
                if (avg > 0.0)
                    eta = (double)(ctx->manifest->file_size - ctx->bytes_received) / avg;
            }
            ctx->progress_cb(ctx->chunks_received_count, ctx->total_chunks,
                             ctx->bytes_received, ctx->manifest->file_size,
                             speed, eta, ctx->progress_cb_ctx);
        } else {
            PrintProgressBar(ctx->chunks_received_count, ctx->total_chunks,
                             ctx->bytes_received, ctx->manifest->file_size,
                             &ctx->start_time, &ctx->last_progress_time,
                             &ctx->last_progress_bytes, "Receiving");
        }

        // Periodic state save (every 100 chunks or 5 seconds)
        if (ctx->chunks_received_count < ctx->total_chunks)
        {
            BOOLEAN should_save = (ctx->chunks_received_count - ctx->last_save_chunk_count >= 100);
            if (!should_save)
            {
                double now = WH_TIMER_NOW();
                double elapsed = now - ctx->last_state_save_time;
                should_save = (elapsed >= 5.0);
            }
            if (should_save)
            {
                TransferState_Save(ctx->manifest->manifest_hash,
                                   ctx->chunks_received, ctx->total_chunks);
                ctx->last_save_chunk_count = ctx->chunks_received_count;
                ctx->last_state_save_time = WH_TIMER_NOW();
            }
        }

        ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
    }
}

//=============================================================================
// Public API
//=============================================================================

void ChunkSendFile(HQUIC Connection, const char *file_path,
                   FILE_MANIFEST *manifest, WH_EVENT transfer_complete_event,
                   BOOLEAN *transfer_complete_flag,
                   StreamProgressCallback progress_cb, void *progress_cb_ctx)
{
    QUIC_STATUS status;

    LOG("[ChunkSendFile] Starting chunk-based transfer: %s (%u chunks)\n",
        file_path, manifest->chunk_count);

    // Allocate send context
    CHUNK_SEND_CONTEXT *ctx = (CHUNK_SEND_CONTEXT *)calloc(1, sizeof(CHUNK_SEND_CONTEXT));
    if (!ctx)
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Failed to allocate send context\n");
        return;
    }

    ctx->manifest = manifest;
    ctx->connection = Connection;
    ctx->transfer_complete_event = transfer_complete_event;
    ctx->transfer_complete_flag = transfer_complete_flag;
    ctx->progress_cb = progress_cb;
    ctx->progress_cb_ctx = progress_cb_ctx;
    ctx->current_file_index = UINT32_MAX;
    ctx->expected_streams = 1;  // Control stream only initially

    // Pre-allocate send buffer pool
    ctx->send_pool = (SEND_BUFFER_SLOT *)calloc(SEND_POOL_SIZE, sizeof(SEND_BUFFER_SLOT));
    if (!ctx->send_pool)
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Failed to allocate send buffer pool\n");
        free(ctx);
        return;
    }

    if (manifest->version >= WH_MANIFEST_VERSION_2)
    {
        // V2+ (directory): defer file opening to SendNextDataChunk via OpenChunkInputFile
        ctx->base_dir_path = _strdup(file_path);
        if (!ctx->base_dir_path)
        {
            LOG_ERROR("[ChunkSendFile] ERROR: Failed to duplicate directory path\n");
            free(ctx->send_pool);
            free(ctx);
            return;
        }
        ctx->file_handle = NULL;
        LOG("[ChunkSendFile] Directory transfer (v2 manifest): %s\n", file_path);
    }
    else
    {
        // V1 (single file): open immediately
        if (!OpenFileForRead(file_path, &ctx->file_handle))
        {
            LOG_ERROR("[ChunkSendFile] ERROR: Cannot open file: %s\n", file_path);
            free(ctx->send_pool);
            free(ctx);
            return;
        }
    }

    // Open control stream (bidirectional)
    status = MsQuic->StreamOpen(
        Connection,
        QUIC_STREAM_OPEN_FLAG_NONE,  // bidirectional
        SenderControlStreamCallback,
        ctx,
        &ctx->control_stream);

    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Failed to open control stream: 0x%x\n", status);
        CloseFile(ctx->file_handle);
        free(ctx->base_dir_path);
        free(ctx->send_pool);
        free(ctx);
        return;
    }

    status = MsQuic->StreamStart(ctx->control_stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Failed to start control stream: 0x%x\n", status);
        MsQuic->StreamClose(ctx->control_stream);
        CloseFile(ctx->file_handle);
        free(ctx->base_dir_path);
        free(ctx->send_pool);
        free(ctx);
        return;
    }

    LOG("[ChunkSendFile] Control stream opened, waiting for MANIFEST_REQUEST...\n");
    // Transfer continues asynchronously via callbacks
}

//=============================================================================
// Cleanup helpers
//=============================================================================

static void CleanupSendContext(CHUNK_SEND_CONTEXT *ctx)
{
    if (!ctx) return;
    LOG("[stream] Cleaning up CHUNK_SEND_CONTEXT\n");
    if (ctx->file_handle) CloseFile(ctx->file_handle);
    if (ctx->base_dir_path) free(ctx->base_dir_path);
    if (ctx->ctrl_recv_buffer) free(ctx->ctrl_recv_buffer);
    if (ctx->chunks_needed) free(ctx->chunks_needed);
    if (ctx->send_pool) free(ctx->send_pool);
    // Note: manifest is owned by caller (wormhole.c), not freed here
    free(ctx);
}

static void CleanupReceiveContext(CHUNK_RECEIVE_CONTEXT *ctx)
{
    if (!ctx) return;
    LOG("[stream] Cleaning up CHUNK_RECEIVE_CONTEXT\n");

    // Save transfer state if transfer was in progress (not complete)
    if (ctx->manifest && ctx->chunks_received && ctx->total_chunks > 0
        && ctx->chunks_received_count < ctx->total_chunks
        && ctx->chunks_received_count > 0)
    {
        TransferState_Save(ctx->manifest->manifest_hash,
                           ctx->chunks_received, ctx->total_chunks);
        LOG("[stream] Saved transfer state (%u/%u chunks) for resume\n",
            ctx->chunks_received_count, ctx->total_chunks);
    }

    if (ctx->file_handle) CloseFile(ctx->file_handle);
    if (ctx->manifest) Manifest_Destroy(ctx->manifest);
    if (ctx->chunks_received) free(ctx->chunks_received);
    if (ctx->ctrl_recv_buffer) free(ctx->ctrl_recv_buffer);
    if (ctx->data_recv_buffer) free(ctx->data_recv_buffer);
    if (ctx->output_path) free(ctx->output_path);
    if (ctx->partial_path) free(ctx->partial_path);
    free(ctx);
}
