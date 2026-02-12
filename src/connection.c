//
// connection.c
// by Dylan Kress
//

#include "connection.h"
#include "stream.h"

// Global MsQuic state (defined in wormhole.c)
extern const QUIC_API_TABLE *MsQuic;
extern HQUIC ServerConfiguration;
extern HQUIC ClientConfiguration;

// Server: Track connection state
typedef struct {
	BOOLEAN active;
} SERVER_CONNECTION_CONTEXT;

// Client: Track file to send and connection status
typedef struct {
	char *file_path;
	BOOLEAN connected;
	HANDLE connect_event;
	HANDLE transfer_complete_event;  // Signals when file transfer is done
} CLIENT_CONNECTION_CONTEXT;



// ServerListenerCallback - Handle incoming connection attempts
QUIC_STATUS QUIC_API ServerListenerCallback(
    HQUIC Listener,
    void* Context,
    QUIC_LISTENER_EVENT* Event)
{
	UNREFERENCED_PARAMETER(Listener);
	UNREFERENCED_PARAMETER(Context);

	QUIC_STATUS Status = QUIC_STATUS_NOT_SUPPORTED;

	switch (Event->Type) {
	case QUIC_LISTENER_EVENT_NEW_CONNECTION:

		// A client is trying to connect
		LOG("[listener] New connection attempt\n");
		
		// Allocate context for this connection
		SERVER_CONNECTION_CONTEXT* conn_ctx = (SERVER_CONNECTION_CONTEXT*)malloc(sizeof(SERVER_CONNECTION_CONTEXT));
		if (conn_ctx == NULL) {
			LOG("[listener] Failed to allocate connection context\n");
			return QUIC_STATUS_OUT_OF_MEMORY;
		}
		conn_ctx->active = TRUE;
		
		// Attach our connection callback to handle events
		MsQuic->SetCallbackHandler(
			Event->NEW_CONNECTION.Connection,
			(void*)ServerConnectionCallback,
			conn_ctx);
		
		// Provide the server configuration (includes certificate)
		Status = MsQuic->ConnectionSetConfiguration(
			Event->NEW_CONNECTION.Connection,
			ServerConfiguration);
		
		if (QUIC_FAILED(Status)) {
			LOG("[listener] ConnectionSetConfiguration failed: 0x%x\n", Status);
			free(conn_ctx);
			return Status;
		}
		
		LOG("[listener] Connection accepted\n");
		return Status;
		
	default:
		break;
	}

	return Status;
}



// ServerConnectionCallback - Handle server-side connection events
QUIC_STATUS QUIC_API ServerConnectionCallback(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event)
{
	SERVER_CONNECTION_CONTEXT* ctx = (SERVER_CONNECTION_CONTEXT*)Context;

	switch (Event->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		//
		// The handshake has completed successfully
		//
		LOG("[conn][%p] Connected - waiting for file stream\n", Connection);
		break;
		
	case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		//
		// The client opened a stream to send us a file
		//
		LOG("[stream][%p] Peer started stream (incoming file)\n", 
			   Event->PEER_STREAM_STARTED.Stream);
		
		// Allocate RECEIVE_CONTEXT for this incoming file transfer
		RECEIVE_CONTEXT *recv_ctx = (RECEIVE_CONTEXT*)malloc(sizeof(RECEIVE_CONTEXT));
		if (recv_ctx == NULL) {
			LOG_ERROR("[stream] ERROR: Failed to allocate RECEIVE_CONTEXT\n");
			MsQuic->StreamShutdown(Event->PEER_STREAM_STARTED.Stream, 
								   QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
			break;
		}
		
		// Initialize RECEIVE_CONTEXT
		memset(recv_ctx, 0, sizeof(RECEIVE_CONTEXT));
		recv_ctx->file_handle = NULL;
		recv_ctx->total_file_size = 0;
		recv_ctx->filename_length = 0;
		recv_ctx->filename = NULL;
		recv_ctx->bytes_received = 0;
		recv_ctx->header_buffer = NULL;
		recv_ctx->header_bytes_received = 0;
		recv_ctx->header_complete = FALSE;
		QueryPerformanceCounter(&recv_ctx->start_time);  // Record start time
		
		LOG("[stream][%p] RECEIVE_CONTEXT allocated and initialized\n", 
			   Event->PEER_STREAM_STARTED.Stream);
		
		// Attach callback with context
		MsQuic->SetCallbackHandler(
			Event->PEER_STREAM_STARTED.Stream,
			(void*)ServerStreamCallback,
			recv_ctx);
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		//
		// The connection is being shut down by the transport (QUIC layer)
		// This is expected - usually due to idle timeout after transfer completes
		//
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
			LOG("[conn][%p] Successfully shut down on idle\n", Connection);
		} else {
			LOG("[conn][%p] Shut down by transport: 0x%x\n", 
				   Connection, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
		}
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		//
		// The peer (client) explicitly closed the connection
		//
		LOG("[conn][%p] Shut down by peer: 0x%llx\n", 
			   Connection, 
			   (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		//
		// The connection shutdown is complete - safe to cleanup
		//
		LOG("[conn][%p] Shutdown complete - cleaning up\n", Connection);
		
		// Free the connection context we allocated in ServerListenerCallback
		if (ctx != NULL) {
			free(ctx);
		}
		
		// Close the connection handle
		MsQuic->ConnectionClose(Connection);
		break;
		
	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		//
		// Server received a resumption ticket (0-RTT support)
		// We don't use this in Phase 1, but MsQuic might send it
		//
		LOG("[conn][%p] Resumption ticket received (ignored)\n", Connection);
		break;
		
	default:
		break;
	}

	return QUIC_STATUS_SUCCESS;
}



// ClientConnectionCallback - Handle client-side connection events
QUIC_STATUS QUIC_API ClientConnectionCallback(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event)
{
	CLIENT_CONNECTION_CONTEXT* ctx = (CLIENT_CONNECTION_CONTEXT*)Context;

	switch (Event->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		//
		// Successfully connected to server - time to send the file!
		//
		LOG("[conn][%p] Connected to server - starting file transfer\n", Connection);
		
		if (ctx != NULL && ctx->file_path != NULL) {
			ctx->connected = TRUE;
			if (ctx->connect_event != NULL) {
				SetEvent(ctx->connect_event);  // Signal main thread that connection is ready
			}
			SendFile(Connection, ctx->file_path, ctx->transfer_complete_event);
		} else {
			LOG("[conn][%p] ERROR: No file path in context\n", Connection);
		}
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		//
		// Connection shut down by transport layer
		//
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
			LOG("[conn][%p] Successfully shut down on idle\n", Connection);
		} else {
			LOG("[conn][%p] Shut down by transport: 0x%x\n",
				   Connection, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
		}
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		//
		// Server closed the connection
		//
		LOG("[conn][%p] Shut down by server: 0x%llx\n",
			   Connection,
			   (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		break;
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		//
		// Connection shutdown complete - cleanup time
		//
		LOG("[conn][%p] Shutdown complete - cleaning up\n", Connection);
		
		// Free the context
		if (ctx != NULL) {
			if (ctx->file_path != NULL) {
				free(ctx->file_path);
			}
			if (ctx->connect_event != NULL) {
				CloseHandle(ctx->connect_event);
			}
			free(ctx);
		}
		
		// Close the connection handle
		// NOTE: In wormhole.c, connection is closed manually after RunClient returns
		if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
			MsQuic->ConnectionClose(Connection);
		}
		break;
		
	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		//
		// Received a resumption ticket from server (for 0-RTT)
		//
		LOG("[conn][%p] Resumption ticket received (%u bytes)\n",
			   Connection,
			   Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
		break;
		
	default:
		break;
	}

	return QUIC_STATUS_SUCCESS;
}
