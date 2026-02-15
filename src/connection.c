//
// connection.c
// QUIC connection callbacks for legacy -server/-client mode.
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
	CHUNK_RECEIVE_CONTEXT *recv_ctx;  // for chunk protocol
} SERVER_CONNECTION_CONTEXT;

// Client: Track file to send and connection status
typedef struct {
	char *file_path;
	FILE_MANIFEST *manifest;
	BOOLEAN connected;
	HANDLE connect_event;
	HANDLE transfer_complete_event;
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
		conn_ctx->recv_ctx = NULL;

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



// ServerConnectionCallback - Handle server-side connection events (chunk protocol)
QUIC_STATUS QUIC_API ServerConnectionCallback(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event)
{
	SERVER_CONNECTION_CONTEXT* ctx = (SERVER_CONNECTION_CONTEXT*)Context;

	switch (Event->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		LOG("[conn][%p] Connected - waiting for file stream\n", Connection);
		break;

	case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
	{
		HQUIC stream = Event->PEER_STREAM_STARTED.Stream;
		BOOLEAN is_bidi = !(Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL);

		if (is_bidi)
		{
			// Bidirectional stream = control stream
			LOG("[conn][%p] Control stream started (bidirectional)\n", Connection);

			CHUNK_RECEIVE_CONTEXT *recv_ctx = (CHUNK_RECEIVE_CONTEXT *)calloc(1, sizeof(CHUNK_RECEIVE_CONTEXT));
			if (!recv_ctx) {
				LOG_ERROR("[conn] ERROR: Failed to allocate CHUNK_RECEIVE_CONTEXT\n");
				MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
				break;
			}

			recv_ctx->control_stream = stream;
			ctx->recv_ctx = recv_ctx;

			MsQuic->SetCallbackHandler(stream, (void*)ReceiverControlStreamCallback, recv_ctx);

			// Send MANIFEST_REQUEST
			uint8_t *buffer = (uint8_t *)malloc(CTRL_HEADER_SIZE);
			if (buffer)
			{
				buffer[0] = CTRL_MSG_MANIFEST_REQUEST;
				buffer[1] = 0; buffer[2] = 0; buffer[3] = 0; buffer[4] = 0;

				QUIC_BUFFER *quic_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
				if (quic_buf)
				{
					quic_buf->Buffer = buffer;
					quic_buf->Length = CTRL_HEADER_SIZE;
					QUIC_STATUS status = MsQuic->StreamSend(stream, quic_buf, 1,
						QUIC_SEND_FLAG_NONE, quic_buf);
					if (QUIC_FAILED(status))
					{
						free(buffer);
						free(quic_buf);
					}
				}
				else
				{
					free(buffer);
				}
			}
		}
		else
		{
			// Unidirectional stream = data stream
			LOG("[conn][%p] Data stream started (unidirectional)\n", Connection);
			if (ctx->recv_ctx)
			{
				ctx->recv_ctx->data_stream = stream;
				MsQuic->SetCallbackHandler(stream, (void*)ReceiverDataStreamCallback, ctx->recv_ctx);
			}
			else
			{
				MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
			}
		}
		break;
	}

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
			LOG("[conn][%p] Successfully shut down on idle\n", Connection);
		} else {
			LOG("[conn][%p] Shut down by transport: 0x%x\n",
				   Connection, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
		}
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		LOG("[conn][%p] Shut down by peer: 0x%llx\n",
			   Connection,
			   (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		LOG("[conn][%p] Shutdown complete - cleaning up\n", Connection);
		if (ctx != NULL) {
			free(ctx);
		}
		MsQuic->ConnectionClose(Connection);
		break;

	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
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
		LOG("[conn][%p] Connected to server - starting chunk-based file transfer\n", Connection);

		if (ctx != NULL && ctx->file_path != NULL && ctx->manifest != NULL) {
			ctx->connected = TRUE;
			if (ctx->connect_event != NULL) {
				SetEvent(ctx->connect_event);
			}
			ChunkSendFile(Connection, ctx->file_path, ctx->manifest, ctx->transfer_complete_event);
		} else {
			LOG("[conn][%p] ERROR: No file path or manifest in context\n", Connection);
		}
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
			LOG("[conn][%p] Successfully shut down on idle\n", Connection);
		} else {
			LOG("[conn][%p] Shut down by transport: 0x%x\n",
				   Connection, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
		}
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		LOG("[conn][%p] Shut down by server: 0x%llx\n",
			   Connection,
			   (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		LOG("[conn][%p] Shutdown complete - cleaning up\n", Connection);
		if (ctx != NULL) {
			if (ctx->file_path != NULL) {
				free(ctx->file_path);
			}
			if (ctx->connect_event != NULL) {
				CloseHandle(ctx->connect_event);
			}
			free(ctx);
		}
		if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
			MsQuic->ConnectionClose(Connection);
		}
		break;

	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		LOG("[conn][%p] Resumption ticket received (%u bytes)\n",
			   Connection,
			   Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
		break;

	default:
		break;
	}

	return QUIC_STATUS_SUCCESS;
}
