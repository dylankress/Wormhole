//
// stream.c
// by Dylan Kress
//

#include "stream.h"
#include <errno.h>

//=============================================================================
// Static Helper Functions (Endianness & Wire Protocol)
//=============================================================================

static void WriteUint64LE(uint8_t *buffer, uint64_t value)
{
	buffer[0] = (uint8_t)(value); // Bits 0-7
	buffer[1] = (uint8_t)(value >> 8); // Bits 8-15
	buffer[2] = (uint8_t)(value >> 16); // Bits 16-23
	buffer[3] = (uint8_t)(value >> 24); // Bits 24-31
	buffer[4] = (uint8_t)(value >> 32); // Bits 32-39
	buffer[5] = (uint8_t)(value >> 40); // Bits 40-47
	buffer[6] = (uint8_t)(value >> 48); // Bits 48-55
	buffer[7] = (uint8_t)(value >> 56); // Bits 56-63
}

static uint64_t ReadUint64LE(const uint8_t *buffer)
{
	return (uint64_t)buffer[0] |
		((uint64_t)buffer[1] << 8) |
		((uint64_t)buffer[2] << 16) |
		((uint64_t)buffer[3] << 24) |
		((uint64_t)buffer[4] << 32) |
		((uint64_t)buffer[5] << 40) |
		((uint64_t)buffer[6] << 48) |
		((uint64_t)buffer[7] << 56);
}

static void WriteUint32LE(uint8_t *buffer, uint32_t value)
{
	buffer[0] = (uint8_t)(value); // Bits 0-7
	buffer[1] = (uint8_t)(value >> 8); // Bits 8-15
	buffer[2] = (uint8_t)(value >> 16); // Bits 16-23
	buffer[3] = (uint8_t)(value >> 24); // Bits 24-31
}

static uint32_t ReadUint32LE(const uint8_t *buffer)
{
	return (uint32_t)buffer[0] |
		((uint32_t)buffer[1] << 8) |
		((uint32_t)buffer[2] << 16) |
		((uint32_t)buffer[3] << 24);
}

//=============================================================================
// Forward Declarations
//=============================================================================

static BOOLEAN SendHeaderAndFirstChunk(HQUIC Stream, SEND_CONTEXT *send_ctx);
static BOOLEAN SendNextChunk(HQUIC Stream, SEND_CONTEXT *send_ctx);
static size_t ParseHeader(RECEIVE_CONTEXT *recv_ctx, const uint8_t *data, size_t data_length);

//=============================================================================
// QUIC Stream Callbacks
//=============================================================================

// ClientStreamCallback - Handles async stream events for file sending
// Events: START_COMPLETE, SEND_COMPLETE, PEER_SEND_SHUTDOWN, PEER_SEND_ABORTED, SHUTDOWN_COMPLETE
QUIC_STATUS QUIC_API ClientStreamCallback(
    HQUIC Stream,
    void *Context,
    QUIC_STREAM_EVENT *Event)
{
	SEND_CONTEXT *send_ctx = (SEND_CONTEXT*)Context;

	switch (Event->Type)
	{
	case QUIC_STREAM_EVENT_START_COMPLETE:
		LOG("[ClientStreamCallback] START_COMPLETE (status: 0x%x)\n", Event->START_COMPLETE.Status);
		if (QUIC_FAILED(Event->START_COMPLETE.Status))
		{
			LOG_ERROR("[ClientStreamCallback] ERROR: Stream start failed\n");
			// Cleanup will happen in SHUTDOWN_COMPLETE
		}
		break;

	case QUIC_STREAM_EVENT_SEND_COMPLETE:
	{
		BOOLEAN canceled = Event->SEND_COMPLETE.Canceled;
		
		// Get timestamp for debugging gaps
		SYSTEMTIME st;
		GetLocalTime(&st);
		
		LOG("[ClientStreamCallback] SEND_COMPLETE at %02d:%02d:%02d.%03d (canceled: %d, in_flight: %u)\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			canceled, send_ctx ? send_ctx->chunks_in_flight : 0);

		// Free the buffer and QUIC_BUFFER struct that were allocated in SendHeaderAndFirstChunk/SendNextChunk
		// The ClientContext pointer holds the QUIC_BUFFER we passed to StreamSend
		if (Event->SEND_COMPLETE.ClientContext != NULL)
		{
			QUIC_BUFFER *sent_buffer = (QUIC_BUFFER*)Event->SEND_COMPLETE.ClientContext;
			free(sent_buffer->Buffer);  // Free the actual data buffer
			free(sent_buffer);          // Free the QUIC_BUFFER struct
		}

		// Decrement chunks in flight counter
		if (send_ctx != NULL && send_ctx->chunks_in_flight > 0)
		{
			send_ctx->chunks_in_flight--;
		}

		// If send was canceled (e.g., due to error), don't send more data
		if (canceled)
		{
			LOG_ERROR("[ClientStreamCallback] Send was canceled, aborting transfer\n");
			MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
			break;
		}

		// Send next chunk to keep the pipe full (maintain 2-3 chunks in flight)
		// Only send if we have fewer than 3 chunks pending to avoid overwhelming the buffer
		if (send_ctx != NULL && 
		    send_ctx->bytes_sent < send_ctx->total_file_size &&
		    send_ctx->chunks_in_flight < 3)  // Limit to 3 concurrent chunks max
		{
			LOG("[ClientStreamCallback] Sending next chunk (in_flight: %u, sent: %llu/%llu)\n",
			    send_ctx->chunks_in_flight,
			    (unsigned long long)send_ctx->bytes_sent,
			    (unsigned long long)send_ctx->total_file_size);
			
			if (!SendNextChunk(Stream, send_ctx))
			{
				LOG_ERROR("[ClientStreamCallback] ERROR: SendNextChunk failed, aborting\n");
				MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
			}
		}
		else if (send_ctx != NULL && send_ctx->chunks_in_flight >= 3)
		{
			LOG("[ClientStreamCallback] Skipping send (already have %u chunks in flight)\n",
			    send_ctx->chunks_in_flight);
		}

		// Check if all data has been sent
		if (send_ctx != NULL && send_ctx->bytes_sent >= send_ctx->total_file_size)
		{
			LOG("[ClientStreamCallback] All data queued for sending (%llu bytes), waiting for final ACKs\n",
				(unsigned long long)send_ctx->bytes_sent);
			// Note: Stream will close gracefully after FIN is acknowledged
		}
		break;
	}

	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		LOG("[ClientStreamCallback] PEER_SEND_SHUTDOWN (server closed their send)\n");
		// Server shouldn't be sending data in Phase 1, but acknowledge receipt
		break;

	case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
		LOG("[ClientStreamCallback] PEER_SEND_ABORTED (error code: %llu)\n",
			(unsigned long long)Event->PEER_SEND_ABORTED.ErrorCode);
		LOG_ERROR("[ClientStreamCallback] Server aborted the stream\n");
		break;

	case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
		LOG("[ClientStreamCallback] PEER_RECEIVE_ABORTED (error code: %llu)\n",
			(unsigned long long)Event->PEER_RECEIVE_ABORTED.ErrorCode);
		LOG_ERROR("[ClientStreamCallback] Server aborted receiving, shutting down send\n");
		MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
		break;

	case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
		LOG("[ClientStreamCallback] SEND_SHUTDOWN_COMPLETE (graceful: %d)\n",
			Event->SEND_SHUTDOWN_COMPLETE.Graceful);
		// Our send direction is fully shut down
		// Now gracefully shut down the receive direction
		MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
		break;

	case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
		LOG("[ClientStreamCallback] SHUTDOWN_COMPLETE (cleanup time)\n");

		// Calculate and report transfer duration
		if (send_ctx != NULL)
		{
			LARGE_INTEGER end_time, frequency;
			QueryPerformanceCounter(&end_time);
			QueryPerformanceFrequency(&frequency);
			
			double duration_seconds = (double)(end_time.QuadPart - send_ctx->start_time.QuadPart) / frequency.QuadPart;
			double throughput_kbps = (send_ctx->bytes_sent / 1024.0) / duration_seconds;
			
			LOG("[ClientStreamCallback] Transfer complete: %llu bytes in %.2f seconds (%.1f KB/s)\n",
				(unsigned long long)send_ctx->bytes_sent, duration_seconds, throughput_kbps);
		}

		// Signal transfer complete event (if set)
		if (send_ctx != NULL && send_ctx->transfer_complete_event != NULL)
		{
			LOG("[ClientStreamCallback] Signaling transfer complete\n");
			SetEvent(send_ctx->transfer_complete_event);
		}

		// Clean up SEND_CONTEXT
		if (send_ctx != NULL)
		{
			if (send_ctx->file_handle != NULL)
			{
				CloseFile(send_ctx->file_handle);
			}
			if (send_ctx->filename != NULL)
			{
				free(send_ctx->filename);
			}
			free(send_ctx);
		}

		// Close the stream handle (final cleanup)
		MsQuic->StreamClose(Stream);
		LOG("[ClientStreamCallback] Stream closed and context freed\n");
		break;

	case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
	{
		uint64_t ideal_bytes = Event->IDEAL_SEND_BUFFER_SIZE.ByteCount;
		LOG("[ClientStreamCallback] IDEAL_SEND_BUFFER_SIZE: %llu bytes (%.2f KB)\n", 
			(unsigned long long)ideal_bytes, ideal_bytes / 1024.0);
		
		// Log whether we SHOULD be sending more based on this value
		if (send_ctx != NULL) {
			LOG("[ClientStreamCallback]   Current state: sent=%llu/%llu, in_flight=%u\n",
				(unsigned long long)send_ctx->bytes_sent, (unsigned long long)send_ctx->total_file_size, 
				send_ctx->chunks_in_flight);
			
			if (ideal_bytes == 0) {
				LOG("[ClientStreamCallback]   WARNING: Ideal buffer is ZERO - backpressure active!\n");
			} else if (ideal_bytes < CHUNK_SIZE) {
				LOG("[ClientStreamCallback]   WARNING: Ideal buffer (%llu) < chunk size (%u) - may need to wait\n",
					(unsigned long long)ideal_bytes, CHUNK_SIZE);
			}
		}
		break;
	}

	default:
		LOG("[ClientStreamCallback] Unhandled event type: %d\n", Event->Type);
		break;
	}

	return QUIC_STATUS_SUCCESS;
}

// ServerStreamCallback - Handles async stream events for file receiving
// Events: START_COMPLETE, RECEIVE, PEER_SEND_SHUTDOWN, PEER_SEND_ABORTED, SHUTDOWN_COMPLETE
QUIC_STATUS QUIC_API ServerStreamCallback(
    HQUIC Stream,
    void *Context,
    QUIC_STREAM_EVENT *Event)
{
	RECEIVE_CONTEXT *recv_ctx = (RECEIVE_CONTEXT*)Context;

	switch (Event->Type)
	{
	case QUIC_STREAM_EVENT_START_COMPLETE:
		LOG("[ServerStreamCallback] START_COMPLETE (status: 0x%x)\n", Event->START_COMPLETE.Status);
		break;

	case QUIC_STREAM_EVENT_RECEIVE:
	{
		static int receive_count = 0;
		receive_count++;
		
		LOG("[ServerStreamCallback] RECEIVE #%d (%u buffers, %llu total bytes)\n",
			receive_count,
			Event->RECEIVE.BufferCount,
			(unsigned long long)Event->RECEIVE.TotalBufferLength);

		// Process all received buffers
		for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
		{
			const QUIC_BUFFER *buffer = &Event->RECEIVE.Buffers[i];
			const uint8_t *data = buffer->Buffer;
			uint32_t data_length = buffer->Length;

			LOG("[ServerStreamCallback] Processing buffer %u: %u bytes\n", i, data_length);

			// If header not yet complete, try to parse it
			if (!recv_ctx->header_complete)
			{
				size_t header_bytes_consumed = ParseHeader(recv_ctx, data, data_length);
				if (header_bytes_consumed == 0)
				{
					LOG_ERROR("[ServerStreamCallback] ERROR: Failed to parse header\n");
					MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
					return QUIC_STATUS_SUCCESS;
				}

				// If header is still not complete after parsing, continue to next buffer
				if (!recv_ctx->header_complete)
				{
					continue;
				}

				// Header just completed! Open file for writing
				LOG("[ServerStreamCallback] Header complete: filename=%s, size=%llu bytes\n",
					recv_ctx->filename, (unsigned long long)recv_ctx->total_file_size);

				// Create output path with .partial extension
				char output_path[512];
				snprintf(output_path, sizeof(output_path), "%s.partial", recv_ctx->filename);

				if (!OpenFileForWrite(output_path, &recv_ctx->file_handle))
				{
					LOG_ERROR("[ServerStreamCallback] ERROR: Cannot open file for writing: %s\n",
						output_path);
					MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
					return QUIC_STATUS_SUCCESS;
				}

				LOG("[ServerStreamCallback] Opened file for writing: %s\n", output_path);

				// Calculate how much of this buffer is file data (after header)
				size_t file_data_offset = header_bytes_consumed;
				size_t file_data_length = data_length - file_data_offset;

				// Write the file data portion of this buffer
				if (file_data_length > 0)
				{
					if (!WriteFileChunkWithRetry(recv_ctx->file_handle, data + file_data_offset, file_data_length))
					{
						LOG_ERROR("[ServerStreamCallback] ERROR: Failed to write file data\n");
						MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
						return QUIC_STATUS_SUCCESS;
					}

					recv_ctx->bytes_received += file_data_length;
					double percent_complete = (recv_ctx->bytes_received * 100.0) / recv_ctx->total_file_size;
					LOG("[ServerStreamCallback] Wrote %zu bytes of file data (total: %llu/%llu, %.2f%% complete)\n",
						file_data_length,
						(unsigned long long)recv_ctx->bytes_received,
						(unsigned long long)recv_ctx->total_file_size,
						percent_complete);
				}
			}
			else
			{
				// Header already complete, this is pure file data
				if (!WriteFileChunkWithRetry(recv_ctx->file_handle, data, data_length))
				{
					LOG_ERROR("[ServerStreamCallback] ERROR: Failed to write file data\n");
					MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
					return QUIC_STATUS_SUCCESS;
				}

				recv_ctx->bytes_received += data_length;
				double percent_complete = (recv_ctx->bytes_received * 100.0) / recv_ctx->total_file_size;
				LOG("[ServerStreamCallback] Wrote %u bytes of file data (total: %llu/%llu, %.2f%% complete)\n",
					data_length,
					(unsigned long long)recv_ctx->bytes_received,
					(unsigned long long)recv_ctx->total_file_size,
					percent_complete);
			}
		}

		break;
	}

	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		LOG("[ServerStreamCallback] PEER_SEND_SHUTDOWN (client finished sending)\n");

		// Verify we received the complete file
		if (recv_ctx->bytes_received != recv_ctx->total_file_size)
		{
			LOG_ERROR("[ServerStreamCallback] ERROR: Incomplete file transfer (%llu/%llu bytes)\n",
				(unsigned long long)recv_ctx->bytes_received,
				(unsigned long long)recv_ctx->total_file_size);
			MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
		}
		else
		{
			// Calculate transfer duration and throughput
			LARGE_INTEGER end_time, frequency;
			QueryPerformanceCounter(&end_time);
			QueryPerformanceFrequency(&frequency);
			
			double duration_seconds = (double)(end_time.QuadPart - recv_ctx->start_time.QuadPart) / frequency.QuadPart;
			double throughput_kbps = (recv_ctx->bytes_received / 1024.0) / duration_seconds;
			
			LOG("[ServerStreamCallback] File transfer complete! Received %llu bytes in %.2f seconds (%.1f KB/s)\n",
				(unsigned long long)recv_ctx->bytes_received, duration_seconds, throughput_kbps);

			// Close the .partial file
			if (recv_ctx->file_handle != NULL)
			{
				CloseFile(recv_ctx->file_handle);
				recv_ctx->file_handle = NULL;
			}

			// Rename from .partial to final filename
			char partial_path[512];
			snprintf(partial_path, sizeof(partial_path), "%s.partial", recv_ctx->filename);

			LOG("[ServerStreamCallback] Renaming %s -> %s\n", partial_path, recv_ctx->filename);

			// On Windows, rename() fails if destination exists - delete it first
			if (FileExists(recv_ctx->filename))
			{
				LOG("[ServerStreamCallback] Destination file exists, deleting...\n");
				if (remove(recv_ctx->filename) != 0)
				{
					LOG_ERROR("[ServerStreamCallback] WARNING: Failed to delete existing file (errno: %d)\n", errno);
					// Continue anyway - maybe we can still rename
				}
			}

			if (rename(partial_path, recv_ctx->filename) != 0)
			{
				LOG_ERROR("[ServerStreamCallback] ERROR: Failed to rename file (errno: %d)\n", errno);
				LOG_ERROR("[ServerStreamCallback] File saved as: %s (rename failed)\n", partial_path);
			}
			else
			{
				LOG("[ServerStreamCallback] SUCCESS! File saved as: %s\n", recv_ctx->filename);
			}

			// Gracefully shut down our send direction (we don't send anything in Phase 1, but good practice)
			MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
		}
		break;

	case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
		LOG("[ServerStreamCallback] PEER_SEND_ABORTED (error code: %llu)\n",
			(unsigned long long)Event->PEER_SEND_ABORTED.ErrorCode);
		LOG_ERROR("[ServerStreamCallback] Client aborted the stream, keeping .partial file\n");
		break;

	case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
		LOG("[ServerStreamCallback] SEND_SHUTDOWN_COMPLETE\n");
		break;

	case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
		LOG("[ServerStreamCallback] SHUTDOWN_COMPLETE (cleanup time)\n");

		// Clean up RECEIVE_CONTEXT
		if (recv_ctx != NULL)
		{
			if (recv_ctx->file_handle != NULL)
			{
				CloseFile(recv_ctx->file_handle);
			}
			if (recv_ctx->filename != NULL)
			{
				free(recv_ctx->filename);
			}
			if (recv_ctx->header_buffer != NULL)
			{
				free(recv_ctx->header_buffer);
			}
			free(recv_ctx);
		}

		// Close the stream handle
		MsQuic->StreamClose(Stream);
		LOG("[ServerStreamCallback] Stream closed and context freed\n");
		break;

	default:
		LOG("[ServerStreamCallback] Unhandled event type: %d\n", Event->Type);
		break;
	}

	return QUIC_STATUS_SUCCESS;
}

// ParseHeader - Incrementally parses the wire protocol header from received data
// Handles case where header arrives split across multiple RECEIVE events
// Returns: Number of bytes consumed from 'data', or 0 on error
static size_t ParseHeader(RECEIVE_CONTEXT *recv_ctx, const uint8_t *data, size_t data_length)
{
	size_t bytes_consumed = 0;

	// Allocate header buffer on first call
	if (recv_ctx->header_buffer == NULL)
	{
		recv_ctx->header_buffer = (uint8_t*)malloc(HEADER_MIN_SIZE + 256);  // +256 for max filename
		if (recv_ctx->header_buffer == NULL)
		{
			LOG_ERROR("[ParseHeader] ERROR: Failed to allocate header buffer\n");
			return 0;
		}
	}

	// Step 1: Accumulate minimum header (file_size + filename_length = 12 bytes)
	if (recv_ctx->header_bytes_received < 12)
	{
		size_t bytes_needed = 12 - recv_ctx->header_bytes_received;
		size_t bytes_to_copy = (data_length < bytes_needed) ? data_length : bytes_needed;

		memcpy(recv_ctx->header_buffer + recv_ctx->header_bytes_received, data, bytes_to_copy);
		recv_ctx->header_bytes_received += bytes_to_copy;
		bytes_consumed += bytes_to_copy;

		LOG("[ParseHeader] Accumulated %zu bytes of minimum header (have: %zu/12)\n",
			bytes_to_copy, recv_ctx->header_bytes_received);

		// If we now have 12 bytes, parse file_size and filename_length
		if (recv_ctx->header_bytes_received == 12)
		{
			recv_ctx->total_file_size = ReadUint64LE(recv_ctx->header_buffer);
			recv_ctx->filename_length = ReadUint32LE(recv_ctx->header_buffer + 8);

			LOG("[ParseHeader] Parsed header fields: file_size=%llu, filename_length=%u\n",
				(unsigned long long)recv_ctx->total_file_size, recv_ctx->filename_length);

			// Validate filename_length
			if (recv_ctx->filename_length == 0 || recv_ctx->filename_length > 255)
			{
				LOG_ERROR("[ParseHeader] ERROR: Invalid filename_length: %u\n",
					recv_ctx->filename_length);
				return 0;
			}
		}
		else
		{
			// Still need more data for minimum header
			return bytes_consumed;
		}
	}

	// Step 2: Accumulate filename (if not already complete)
	if (recv_ctx->filename_length > 0 && !recv_ctx->header_complete)
	{
		size_t full_header_size = 12 + recv_ctx->filename_length;
		size_t bytes_needed = full_header_size - recv_ctx->header_bytes_received;
		size_t bytes_available = data_length - bytes_consumed;
		size_t bytes_to_copy = (bytes_available < bytes_needed) ? bytes_available : bytes_needed;

		memcpy(recv_ctx->header_buffer + recv_ctx->header_bytes_received, 
			   data + bytes_consumed, bytes_to_copy);
		recv_ctx->header_bytes_received += bytes_to_copy;
		bytes_consumed += bytes_to_copy;

		LOG("[ParseHeader] Accumulated %zu bytes of filename (have: %zu/%zu total)\n",
			bytes_to_copy, recv_ctx->header_bytes_received, full_header_size);

		// Check if header is now complete
		if (recv_ctx->header_bytes_received >= full_header_size)
		{
			// Extract filename
			recv_ctx->filename = (char*)malloc(recv_ctx->filename_length + 1);
			if (recv_ctx->filename == NULL)
			{
				LOG_ERROR("[ParseHeader] ERROR: Failed to allocate filename buffer\n");
				return 0;
			}

			memcpy(recv_ctx->filename, recv_ctx->header_buffer + 12, recv_ctx->filename_length);
			recv_ctx->filename[recv_ctx->filename_length] = '\0';

			LOG("[ParseHeader] Header complete! filename=%s (consumed %zu bytes total)\n",
				recv_ctx->filename, bytes_consumed);

			recv_ctx->header_complete = TRUE;

			// Free header buffer (no longer needed)
			free(recv_ctx->header_buffer);
			recv_ctx->header_buffer = NULL;
		}
	}

	return bytes_consumed;
}

//=============================================================================
// Public API Functions
//=============================================================================

// SendFile - Main entry point for sending a file over a QUIC connection
// Creates a stream, allocates context, and initiates the file transfer
void SendFile(HQUIC Connection, const char* file_path, HANDLE transfer_complete_event)
{
	QUIC_STATUS status;
	HQUIC stream = NULL;
	SEND_CONTEXT *send_ctx = NULL;

	LOG("[SendFile] Starting file transfer: %s\n", file_path);

	// Step 1: Validate file exists and get metadata
	if (!FileExists(file_path))
	{
		LOG_ERROR("[SendFile] ERROR: File does not exist: %s\n", file_path);
		return;
	}

	uint64_t file_size = 0;
	if (!GetWormholeFileSize(file_path, &file_size) || file_size == 0)
	{
		LOG_ERROR("[SendFile] ERROR: Cannot get file size for: %s\n", file_path);
		return;
	}

	char *filename = NULL;
	uint32_t filename_length = 0;
	ExtractFilename(file_path, &filename, &filename_length);
	if (filename == NULL)
	{
		LOG_ERROR("[SendFile] ERROR: Cannot extract filename from path: %s\n", file_path);
		return;
	}

	LOG("[SendFile] File size: %llu bytes, Filename: %s\n", 
		(unsigned long long)file_size, filename);

	// Step 2: Open file for reading
	FILE *file_handle = NULL;
	if (!OpenFileForRead(file_path, &file_handle))
	{
		LOG_ERROR("[SendFile] ERROR: Cannot open file for reading: %s\n", file_path);
		free(filename);
		return;
	}

	// Step 3: Allocate and initialize SEND_CONTEXT
	send_ctx = (SEND_CONTEXT*)malloc(sizeof(SEND_CONTEXT));
	if (send_ctx == NULL)
	{
		LOG_ERROR("[SendFile] ERROR: Failed to allocate SEND_CONTEXT\n");
		CloseFile(file_handle);
		return;
	}

	send_ctx->file_handle = file_handle;
	send_ctx->total_file_size = file_size;
	send_ctx->bytes_sent = 0;
	send_ctx->chunks_in_flight = 0;
	send_ctx->filename_length = filename_length;
	send_ctx->filename = filename;  // Transfer ownership to context
	send_ctx->transfer_complete_event = transfer_complete_event;
	QueryPerformanceCounter(&send_ctx->start_time);  // Record start time

	// Step 4: Open a new QUIC stream
	status = MsQuic->StreamOpen(
		Connection,
		QUIC_STREAM_OPEN_FLAG_NONE,
		ClientStreamCallback,
		send_ctx,  // Pass context to callback
		&stream
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[SendFile] ERROR: StreamOpen failed: 0x%x\n", status);
		CloseFile(file_handle);
		free(send_ctx->filename);
		free(send_ctx);
		return;
	}

	LOG("[SendFile] Stream opened successfully\n");

	// Step 5: Start the stream (enables sending)
	status = MsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[SendFile] ERROR: StreamStart failed: 0x%x\n", status);
		MsQuic->StreamClose(stream);
		CloseFile(file_handle);
		free(send_ctx->filename);
		free(send_ctx);
		return;
	}

	LOG("[SendFile] Stream started, sending header and first chunk...\n");

	// Step 6: Send header + first chunk
	if (!SendHeaderAndFirstChunk(stream, send_ctx))
	{
		LOG_ERROR("[SendFile] ERROR: Failed to send header and first chunk\n");
		MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
		MsQuic->StreamClose(stream);
		CloseFile(file_handle);
		free(send_ctx->filename);
		free(send_ctx);
		return;
	}

	// Pipeline 2 additional chunks upfront to keep the pipe full during ramp-up
	// This works with the increased flow control window (1MB) to maximize throughput
	LOG("[SendFile] Initial chunk sent, pipelining additional chunks...\n");
	int chunks_pipelined = 0;
	for (int i = 0; i < 2 && send_ctx->bytes_sent < send_ctx->total_file_size; i++)
	{
		if (!SendNextChunk(stream, send_ctx))
		{
			LOG_ERROR("[SendFile] ERROR: Failed to pipeline chunk %d\n", i+2);
			break;
		}
		chunks_pipelined++;
	}
	LOG("[SendFile] Pipelined %d additional chunks (total in flight: %u, sent: %llu/%llu bytes)\n", 
	    chunks_pipelined, send_ctx->chunks_in_flight,
	    (unsigned long long)send_ctx->bytes_sent, (unsigned long long)send_ctx->total_file_size);
	// Note: Stream cleanup happens in ClientStreamCallback when transfer completes
}

// SendHeaderAndFirstChunk - Constructs and sends the wire protocol header + first data chunk
// Returns TRUE on success, FALSE on failure
static BOOLEAN SendHeaderAndFirstChunk(HQUIC Stream, SEND_CONTEXT *send_ctx)
{
	QUIC_STATUS status;
	uint8_t *buffer = NULL;
	size_t buffer_size;
	size_t header_size;
	size_t data_to_read;
	size_t bytes_read;

	// Calculate header size: 8 (file_size) + 4 (filename_len) + filename_len
	header_size = 8 + 4 + send_ctx->filename_length;

	// Calculate how much file data fits in first chunk
	// Ensure we don't exceed CHUNK_SIZE total
	if (header_size >= CHUNK_SIZE)
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: Header size (%zu) exceeds CHUNK_SIZE (%d)\n",
			header_size, CHUNK_SIZE);
		return FALSE;
	}

	data_to_read = CHUNK_SIZE - header_size;
	if (data_to_read > send_ctx->total_file_size)
	{
		data_to_read = (size_t)send_ctx->total_file_size;  // Don't read more than file size
	}

	buffer_size = header_size + data_to_read;

	LOG("[SendHeaderAndFirstChunk] Header: %zu bytes, Data: %zu bytes, Total: %zu bytes\n",
		header_size, data_to_read, buffer_size);

	// Allocate buffer for header + data
	buffer = (uint8_t*)malloc(buffer_size);
	if (buffer == NULL)
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: Failed to allocate buffer (%zu bytes)\n",
			buffer_size);
		return FALSE;
	}

	// Step 1: Write header to buffer
	uint8_t *write_ptr = buffer;

	// Write file_size (8 bytes, little-endian)
	WriteUint64LE(write_ptr, send_ctx->total_file_size);
	write_ptr += 8;

	// Write filename_length (4 bytes, little-endian)
	WriteUint32LE(write_ptr, send_ctx->filename_length);
	write_ptr += 4;

	// Write filename (no null terminator)
	memcpy(write_ptr, send_ctx->filename, send_ctx->filename_length);
	write_ptr += send_ctx->filename_length;

	// Step 2: Read first chunk of file data
	if (!ReadFileChunk(send_ctx->file_handle, write_ptr, data_to_read, &bytes_read))
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: ReadFileChunk failed\n");
		free(buffer);
		return FALSE;
	}
	if (bytes_read != data_to_read)
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: Short read (expected %zu, got %zu)\n",
			data_to_read, bytes_read);
		free(buffer);
		return FALSE;
	}

	send_ctx->bytes_sent += bytes_read;

	LOG("[SendHeaderAndFirstChunk] Header constructed, %zu bytes of file data read (total sent: %llu/%llu)\n",
		bytes_read, (unsigned long long)send_ctx->bytes_sent, (unsigned long long)send_ctx->total_file_size);

	// Step 3: Send buffer via QUIC stream
	QUIC_BUFFER *quic_buffer = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER));
	if (quic_buffer == NULL)
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: Failed to allocate QUIC_BUFFER\n");
		free(buffer);
		return FALSE;
	}

	quic_buffer->Buffer = buffer;
	quic_buffer->Length = (uint32_t)buffer_size;

	// Determine if this is the final send (file fits in one chunk)
	QUIC_SEND_FLAGS send_flags = QUIC_SEND_FLAG_NONE;
	if (send_ctx->bytes_sent >= send_ctx->total_file_size)
	{
		send_flags = QUIC_SEND_FLAG_FIN;  // Signal end of stream
		LOG("[SendHeaderAndFirstChunk] File fits in one chunk, setting FIN flag\n");
	}

	LOG("[StreamSend] About to call StreamSend: buffer=%p, length=%u, flags=0x%x\n",
		quic_buffer->Buffer, quic_buffer->Length, send_flags);
	
	status = MsQuic->StreamSend(Stream, quic_buffer, 1, send_flags, quic_buffer);
	
	LOG("[StreamSend] StreamSend returned: status=0x%x (%s)\n", 
		status, QUIC_SUCCEEDED(status) ? "SUCCESS" : "FAILED");
	
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[SendHeaderAndFirstChunk] ERROR: StreamSend failed: 0x%x\n", status);
		free(quic_buffer);
		free(buffer);
		return FALSE;
	}

	send_ctx->chunks_in_flight++;
	LOG("[SendHeaderAndFirstChunk] StreamSend successful (flags: 0x%x), chunks_in_flight=%u\n", 
		send_flags, send_ctx->chunks_in_flight);

	// Note: Buffer will be freed in ClientStreamCallback when SEND_COMPLETE event fires
	return TRUE;
}

// SendNextChunk - Sends the next CHUNK_SIZE bytes of file data
// Returns TRUE on success, FALSE on failure
static BOOLEAN SendNextChunk(HQUIC Stream, SEND_CONTEXT *send_ctx)
{
	QUIC_STATUS status;
	uint8_t *buffer = NULL;
	size_t bytes_remaining;
	size_t bytes_to_read;
	size_t bytes_read;

	// Calculate how much data is left to send
	bytes_remaining = (size_t)(send_ctx->total_file_size - send_ctx->bytes_sent);
	if (bytes_remaining == 0)
	{
		LOG("[SendNextChunk] No more data to send, transfer complete\n");
		return TRUE;
	}

	bytes_to_read = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;

	LOG("[SendNextChunk] Sending next chunk: %zu bytes (remaining: %zu)\n",
		bytes_to_read, bytes_remaining);

	// Allocate buffer for this chunk
	buffer = (uint8_t*)malloc(bytes_to_read);
	if (buffer == NULL)
	{
		LOG_ERROR("[SendNextChunk] ERROR: Failed to allocate buffer (%zu bytes)\n",
			bytes_to_read);
		return FALSE;
	}

	// Read file data
	if (!ReadFileChunk(send_ctx->file_handle, buffer, bytes_to_read, &bytes_read))
	{
		LOG_ERROR("[SendNextChunk] ERROR: ReadFileChunk failed\n");
		free(buffer);
		return FALSE;
	}
	if (bytes_read != bytes_to_read)
	{
		LOG_ERROR("[SendNextChunk] ERROR: Short read (expected %zu, got %zu)\n",
			bytes_to_read, bytes_read);
		free(buffer);
		return FALSE;
	}

	send_ctx->bytes_sent += bytes_read;

	LOG("[SendNextChunk] Read %zu bytes from file (total sent: %llu/%llu)\n",
		bytes_read, (unsigned long long)send_ctx->bytes_sent, (unsigned long long)send_ctx->total_file_size);

	// Prepare QUIC buffer
	QUIC_BUFFER *quic_buffer = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER));
	if (quic_buffer == NULL)
	{
		LOG_ERROR("[SendNextChunk] ERROR: Failed to allocate QUIC_BUFFER\n");
		free(buffer);
		return FALSE;
	}

	quic_buffer->Buffer = buffer;
	quic_buffer->Length = (uint32_t)bytes_read;

	// Determine if this is the final chunk
	QUIC_SEND_FLAGS send_flags = QUIC_SEND_FLAG_NONE;
	if (send_ctx->bytes_sent >= send_ctx->total_file_size)
	{
		send_flags = QUIC_SEND_FLAG_FIN;  // Signal end of stream
		LOG("[SendNextChunk] Final chunk, setting FIN flag\n");
	}

	// Send the chunk
	LOG("[StreamSend] About to call StreamSend: buffer=%p, length=%u, flags=0x%x\n",
		quic_buffer->Buffer, quic_buffer->Length, send_flags);
	
	status = MsQuic->StreamSend(Stream, quic_buffer, 1, send_flags, quic_buffer);
	
	LOG("[StreamSend] StreamSend returned: status=0x%x (%s)\n", 
		status, QUIC_SUCCEEDED(status) ? "SUCCESS" : "FAILED");
	
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[SendNextChunk] ERROR: StreamSend failed: 0x%x\n", status);
		free(quic_buffer);
		free(buffer);
		return FALSE;
	}

	send_ctx->chunks_in_flight++;
	LOG("[SendNextChunk] StreamSend successful (flags: 0x%x), chunks_in_flight=%u\n", 
		send_flags, send_ctx->chunks_in_flight);

	// Note: Buffer will be freed in ClientStreamCallback when SEND_COMPLETE event fires
	return TRUE;
}
