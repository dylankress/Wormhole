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
    uint32_t frame_size = CTRL_HEADER_SIZE + payload_len;
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
// Data chunk send
//=============================================================================

// Send the next chunk on the data stream.
// Frame: [4B chunk_index][32B hash][4B data_size][data]
static BOOLEAN SendNextDataChunk(CHUNK_SEND_CONTEXT *ctx)
{
    if (!ctx || !ctx->manifest || !ctx->file_handle || !ctx->data_stream)
        return FALSE;

    if (ctx->next_chunk_to_send >= ctx->manifest->chunk_count)
    {
        // All chunks queued
        ctx->all_chunks_sent = TRUE;
        return TRUE;
    }

    uint32_t idx = ctx->next_chunk_to_send;
    CHUNK_INFO *ci = &ctx->manifest->chunks[idx];
    uint32_t data_size = ci->chunk_size;

    // Seek to chunk position
    uint64_t offset = (uint64_t)idx * WH_CHUNK_SIZE;
#ifdef _WIN32
    _fseeki64(ctx->file_handle, (__int64)offset, SEEK_SET);
#else
    fseeko(ctx->file_handle, (off_t)offset, SEEK_SET);
#endif

    // Allocate frame buffer: header + data
    uint32_t frame_size = DATA_FRAME_HEADER_SIZE + data_size;
    uint8_t *buffer = (uint8_t *)malloc(frame_size);
    if (!buffer) return FALSE;

    // Write frame header
    uint8_t *p = buffer;
    WriteUint32LE(p, idx);              p += 4;
    memcpy(p, ci->hash, WH_HASH_SIZE); p += WH_HASH_SIZE;
    WriteUint32LE(p, data_size);        p += 4;

    // Read chunk data from file
    size_t bytes_read = 0;
    if (!ReadFileChunk(ctx->file_handle, p, data_size, &bytes_read) ||
        bytes_read != data_size)
    {
        LOG_ERROR("[stream] ERROR: Failed to read chunk %u from file\n", idx);
        free(buffer);
        return FALSE;
    }

    QUIC_BUFFER *quic_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
    if (!quic_buf)
    {
        free(buffer);
        return FALSE;
    }

    quic_buf->Buffer = buffer;
    quic_buf->Length = frame_size;

    // Check if this is the last chunk
    BOOLEAN is_last = (idx == ctx->manifest->chunk_count - 1);
    QUIC_SEND_FLAGS flags = is_last ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;

    QUIC_STATUS status = MsQuic->StreamSend(ctx->data_stream, quic_buf, 1,
                                            flags, quic_buf);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[stream] ERROR: StreamSend data chunk %u failed: 0x%x\n", idx, status);
        free(buffer);
        free(quic_buf);
        return FALSE;
    }

    ctx->next_chunk_to_send++;
    ctx->chunks_in_flight++;

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
            size_t frame_size = CTRL_HEADER_SIZE + payload_len;

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

                // Open data stream (unidirectional) and start sending chunks
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
                QueryPerformanceCounter(&ctx->start_time);

                // Pipeline initial chunks (up to 3)
                for (int k = 0; k < 3 && !ctx->all_chunks_sent; k++)
                {
                    if (!SendNextDataChunk(ctx))
                    {
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

                // Report transfer stats
                LARGE_INTEGER end_time, frequency;
                QueryPerformanceCounter(&end_time);
                QueryPerformanceFrequency(&frequency);
                double duration = (double)(end_time.QuadPart - ctx->start_time.QuadPart) /
                                  frequency.QuadPart;
                double throughput = (ctx->manifest->file_size / 1024.0) / duration;

                LOG("[SenderCtrl] Transfer complete: %llu bytes in %.2f sec (%.1f KB/s)\n",
                    (unsigned long long)ctx->manifest->file_size, duration, throughput);

                // Signal completion
                if (ctx->transfer_complete_event)
                {
                    SetEvent(ctx->transfer_complete_event);
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
        // Control stream cleanup — don't free ctx here since data stream may still be active.
        // ctx is freed when the last stream shuts down.
        if (ctx && !ctx->data_stream)
        {
            CleanupSendContext(ctx);
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
        // Free sent buffer
        if (Event->SEND_COMPLETE.ClientContext)
        {
            QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
            free(sent->Buffer);
            free(sent);
        }

        if (ctx && ctx->chunks_in_flight > 0)
            ctx->chunks_in_flight--;

        if (Event->SEND_COMPLETE.Canceled)
        {
            LOG_ERROR("[SenderData] Send canceled, aborting\n");
            break;
        }

        // Pipeline next chunk if room
        if (ctx && !ctx->all_chunks_sent && ctx->chunks_in_flight < 3)
        {
            if (!SendNextDataChunk(ctx))
            {
                LOG_ERROR("[SenderData] ERROR: SendNextDataChunk failed\n");
                MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[SenderData] SHUTDOWN_COMPLETE\n");
        ctx->data_stream = NULL;
        // If control stream is also gone, cleanup
        if (ctx && !ctx->control_stream)
        {
            CleanupSendContext(ctx);
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
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        LOG("[RecvCtrl] Sender closed their send direction on control stream\n");
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[RecvCtrl] SHUTDOWN_COMPLETE\n");
        ctx->control_stream = NULL;
        if (ctx && !ctx->data_stream)
        {
            CleanupReceiveContext(ctx);
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

            // Close .partial file
            if (ctx->file_handle)
            {
                CloseFile(ctx->file_handle);
                ctx->file_handle = NULL;
            }

            // Rename .partial to final
            if (ctx->partial_path && ctx->output_path)
            {
                // Delete target if it exists (rename fails on Windows otherwise)
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

            // Report transfer stats
            LARGE_INTEGER end_time, frequency;
            QueryPerformanceCounter(&end_time);
            QueryPerformanceFrequency(&frequency);
            double duration = (double)(end_time.QuadPart - ctx->start_time.QuadPart) /
                              frequency.QuadPart;
            double throughput = (ctx->manifest->file_size / 1024.0) / duration;

            LOG("[RecvData] Transfer: %llu bytes in %.2f sec (%.1f KB/s)\n",
                (unsigned long long)ctx->manifest->file_size, duration, throughput);

            // Send TRANSFER_COMPLETE to sender
            if (ctx->control_stream)
            {
                SendControlMessage(ctx->control_stream, CTRL_MSG_TRANSFER_COMPLETE, NULL, 0);
                LOG("[RecvData] Sent TRANSFER_COMPLETE to sender\n");
            }

            // Signal completion
            if (ctx->transfer_complete_event)
            {
                SetEvent(ctx->transfer_complete_event);
            }
        }
        else if (ctx->manifest)
        {
            LOG_ERROR("[RecvData] WARNING: FIN received but only %u/%u chunks received\n",
                      ctx->chunks_received_count, ctx->total_chunks);
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        LOG("[RecvData] SHUTDOWN_COMPLETE\n");
        ctx->data_stream = NULL;
        if (ctx && !ctx->control_stream)
        {
            CleanupReceiveContext(ctx);
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
        size_t frame_size = CTRL_HEADER_SIZE + payload_len;

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

            // Build output path in Downloads folder
            if (ctx->output_path) free(ctx->output_path);
            if (ctx->partial_path) free(ctx->partial_path);

            if (ctx->downloads_path[0])
            {
                // Build unique path in Downloads folder
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

            // Create .partial path
            size_t plen = strlen(ctx->output_path) + 9;  // ".partial\0"
            ctx->partial_path = (char *)malloc(plen);
            if (ctx->partial_path)
            {
                snprintf(ctx->partial_path, plen, "%s.partial", ctx->output_path);
            }

            // Open .partial file for writing
            if (ctx->partial_path && !OpenFileForWrite(ctx->partial_path, &ctx->file_handle))
            {
                LOG_ERROR("[RecvCtrl] ERROR: Cannot open %s for writing\n", ctx->partial_path);
                ConsumeBuffer(ctx->ctrl_recv_buffer, &ctx->ctrl_recv_used, frame_size);
                return;
            }

            // Initialize chunk store for this transfer
            ChunkStore_Init();

            QueryPerformanceCounter(&ctx->start_time);
            LOG("[RecvCtrl] Ready to receive %u chunks\n", ctx->total_chunks);
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
    if (!ctx->manifest || !ctx->file_handle) return;

    while (ctx->data_recv_used >= DATA_FRAME_HEADER_SIZE)
    {
        // Parse frame header
        uint32_t chunk_index = ReadUint32LE(ctx->data_recv_buffer);
        // hash is at offset 4
        uint32_t data_size = ReadUint32LE(ctx->data_recv_buffer + 4 + WH_HASH_SIZE);

        size_t frame_size = DATA_FRAME_HEADER_SIZE + data_size;
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
        uint64_t file_offset = (uint64_t)chunk_index * WH_CHUNK_SIZE;
#ifdef _WIN32
        _fseeki64(ctx->file_handle, (__int64)file_offset, SEEK_SET);
#else
        fseeko(ctx->file_handle, (off_t)file_offset, SEEK_SET);
#endif

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
        }

        // Progress logging
        double pct = (ctx->chunks_received_count * 100.0) / ctx->total_chunks;
        if (ctx->chunks_received_count % 10 == 0 ||
            ctx->chunks_received_count == ctx->total_chunks)
        {
            LOG("[RecvData] Chunk %u/%u received (%.1f%%)\n",
                ctx->chunks_received_count, ctx->total_chunks, pct);
        }

        ConsumeBuffer(ctx->data_recv_buffer, &ctx->data_recv_used, frame_size);
    }
}

//=============================================================================
// Public API
//=============================================================================

void ChunkSendFile(HQUIC Connection, const char *file_path,
                   FILE_MANIFEST *manifest, HANDLE transfer_complete_event)
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

    // Open file for reading
    if (!OpenFileForRead(file_path, &ctx->file_handle))
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Cannot open file: %s\n", file_path);
        free(ctx);
        return;
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
        free(ctx);
        return;
    }

    status = MsQuic->StreamStart(ctx->control_stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[ChunkSendFile] ERROR: Failed to start control stream: 0x%x\n", status);
        MsQuic->StreamClose(ctx->control_stream);
        CloseFile(ctx->file_handle);
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
    if (ctx->ctrl_recv_buffer) free(ctx->ctrl_recv_buffer);
    // Note: manifest is owned by caller (wormhole.c), not freed here
    free(ctx);
}

static void CleanupReceiveContext(CHUNK_RECEIVE_CONTEXT *ctx)
{
    if (!ctx) return;
    LOG("[stream] Cleaning up CHUNK_RECEIVE_CONTEXT\n");
    if (ctx->file_handle) CloseFile(ctx->file_handle);
    if (ctx->manifest) Manifest_Destroy(ctx->manifest);
    if (ctx->chunks_received) free(ctx->chunks_received);
    if (ctx->ctrl_recv_buffer) free(ctx->ctrl_recv_buffer);
    if (ctx->data_recv_buffer) free(ctx->data_recv_buffer);
    if (ctx->output_path) free(ctx->output_path);
    if (ctx->partial_path) free(ctx->partial_path);
    free(ctx);
}
