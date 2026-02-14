//
// wormhole.c
// by Dylan Kress
//

#include "common.h"
#include "protocol.h"
#include "connection.h"
#include "stream.h"
#include "file_io.h"
#include "crypto.h"
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"
#include "relay/ticket.h"
#include "relay/connection_manager.h"

// Global MsQuic state - accessible via extern in other files
const QUIC_API_TABLE *MsQuic = NULL;
HQUIC Registration = NULL;
HQUIC ServerConfiguration = NULL;
HQUIC ClientConfiguration = NULL;

// Keep-alive event for server (prevents exit while listening)
HANDLE ServerShutdownEvent = NULL;

// Relay configuration
#define DEFAULT_RELAY_HOST "wormholerelay.com"  // WSL IP (change to "wormholerelay.com" for production)
#define DEFAULT_RELAY_PORT 443              // Change to 443 for production

//=============================================================================
// Forward Declarations
//=============================================================================

static BOOLEAN InitializeMsQuic(void);
static void CleanupMsQuic(void);
static BOOLEAN ServerLoadConfiguration(void);
static BOOLEAN ClientLoadConfiguration(void);
static BOOLEAN RunServer(void);
static BOOLEAN RunClient(const char *target_host, const char *file_path);
static int cmd_send(const char *filepath);
static int cmd_receive(const char *ticket);
static void PrintUsage(void);

// Send/Receive command callbacks
static QUIC_STATUS QUIC_API ServerListenerCallback_Send(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event);
static QUIC_STATUS QUIC_API ServerConnectionCallback_Send(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
static QUIC_STATUS QUIC_API ClientConnectionCallback_Receive(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
static QUIC_STATUS QUIC_API ReceiveStreamCallback_Wrapper(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event);

//=============================================================================
// MsQuic Initialization & Cleanup
//=============================================================================

// InitializeMsQuic - Opens MsQuic library and creates registration
static BOOLEAN InitializeMsQuic(void)
{
	QUIC_STATUS status;

	LOG("[init] Opening MsQuic library...\n");

	// Step 1: Open MsQuic library (loads msquic.dll and gets API table)
	status = MsQuicOpen2(&MsQuic);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[init] ERROR: MsQuicOpen2 failed: 0x%x\n", status);
		return FALSE;
	}

	LOG("[init] MsQuic library opened successfully\n");

	// Step 2: Create registration (required for all QUIC operations)
	const QUIC_REGISTRATION_CONFIG reg_config = {
		"wormhole",  // AppName
		QUIC_EXECUTION_PROFILE_LOW_LATENCY  // Optimize for low latency
	};

	status = MsQuic->RegistrationOpen(&reg_config, &Registration);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[init] ERROR: RegistrationOpen failed: 0x%x\n", status);
		MsQuicClose(MsQuic);
		MsQuic = NULL;
		return FALSE;
	}

	LOG("[init] Registration created successfully\n");
	return TRUE;
}

// CleanupMsQuic - Closes registration and MsQuic library
static void CleanupMsQuic(void)
{
	LOG("[cleanup] Closing MsQuic resources...\n");

	if (ServerConfiguration != NULL)
	{
		MsQuic->ConfigurationClose(ServerConfiguration);
		ServerConfiguration = NULL;
	}

	if (ClientConfiguration != NULL)
	{
		MsQuic->ConfigurationClose(ClientConfiguration);
		ClientConfiguration = NULL;
	}

	if (Registration != NULL)
	{
		MsQuic->RegistrationClose(Registration);
		Registration = NULL;
	}

	if (MsQuic != NULL)
	{
		MsQuicClose(MsQuic);
		MsQuic = NULL;
	}

	LOG("[cleanup] MsQuic resources closed\n");
}

//=============================================================================
// Configuration Loading (Server & Client)
//=============================================================================

// ServerLoadConfiguration - Creates server configuration with TLS certificate
static BOOLEAN ServerLoadConfiguration(void)
{
	QUIC_STATUS status;

	LOG("[server] Loading configuration...\n");

	// Step 1: Generate or retrieve self-signed certificate
	char thumbprint[41];
	if (!GenerateSelfSignedCert(thumbprint, sizeof(thumbprint)))
	{
		LOG_ERROR("[server] ERROR: Failed to generate/retrieve certificate\n");
		return FALSE;
	}

	LOG("[server] Using certificate thumbprint: %s\n", thumbprint);

	// Step 2: Create credential configuration (tells MsQuic which cert to use)
	QUIC_CERTIFICATE_HASH cert_hash;
	memset(&cert_hash, 0, sizeof(cert_hash));

	// Convert thumbprint hex string to binary (20 bytes)
	for (int i = 0; i < 20; i++)
	{
		char hex_byte[3] = { thumbprint[i * 2], thumbprint[i * 2 + 1], 0 };
		cert_hash.ShaHash[i] = (uint8_t)strtoul(hex_byte, NULL, 16);
	}

	QUIC_CERTIFICATE_HASH_STORE cert_hash_store;
	memset(&cert_hash_store, 0, sizeof(cert_hash_store));
	cert_hash_store.Flags = QUIC_CERTIFICATE_HASH_STORE_FLAG_NONE;
	memcpy(cert_hash_store.ShaHash, cert_hash.ShaHash, sizeof(cert_hash.ShaHash));
	strcpy_s(cert_hash_store.StoreName, sizeof(cert_hash_store.StoreName), "My");

	QUIC_CREDENTIAL_CONFIG cred_config;
	memset(&cred_config, 0, sizeof(cred_config));
	cred_config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
	cred_config.CertificateHashStore = &cert_hash_store;
	cred_config.Flags = QUIC_CREDENTIAL_FLAG_NONE;

	// Step 3: Create QUIC settings
	QUIC_SETTINGS settings = { 0 };
	settings.IdleTimeoutMs = 300000;  // 300 seconds (5 minutes) - increased for large file transfers
	settings.IsSet.IdleTimeoutMs = TRUE;
	settings.DisconnectTimeoutMs = 300000;  // 300 seconds disconnect timeout
	settings.IsSet.DisconnectTimeoutMs = TRUE;
	settings.KeepAliveIntervalMs = 10000;  // Send keep-alive every 10 seconds
	settings.IsSet.KeepAliveIntervalMs = TRUE;
	settings.ServerResumptionLevel = QUIC_SERVER_RESUME_ONLY;
	settings.IsSet.ServerResumptionLevel = TRUE;
	settings.PeerBidiStreamCount = 1;  // Allow 1 bidirectional stream
	settings.IsSet.PeerBidiStreamCount = TRUE;

	// Flow control settings for large file transfers (CRITICAL for throughput!)
	// Based on Iroh's proven high-performance configuration
	// Receive window: How much data WE can buffer from peer
	settings.StreamRecvWindowDefault = 16777216;  // 16 MB (2^24, MUST be power of 2)
	settings.IsSet.StreamRecvWindowDefault = TRUE;
	
	// Send buffer: Enable buffering for better pipelining
	settings.SendBufferingEnabled = TRUE;  // Enable send buffering (allows multiple chunks in flight)
	settings.IsSet.SendBufferingEnabled = TRUE;
	
	// Connection-wide flow control
	settings.ConnFlowControlWindow = 67108864;  // 64 MB (2^26, 4x stream window)
	settings.IsSet.ConnFlowControlWindow = TRUE;
	
	// Initial window size (how much can be sent before first ACK)
	settings.InitialWindowPackets = 10;  // 10 packets initially (default: 10, but explicit)
	settings.IsSet.InitialWindowPackets = TRUE;

	// DISABLE congestion control for LAN testing (no backoff on packet loss)
	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_MAX;
	settings.IsSet.CongestionControlAlgorithm = TRUE;

	// EXPERIMENT 2.3: Force conservative MTU to bypass PMTUD issues
	settings.MinimumMtu = 1200;  // Force conservative MTU
	settings.IsSet.MinimumMtu = TRUE;
	settings.MaximumMtu = 1200;  // Prevent PMTUD
	settings.IsSet.MaximumMtu = TRUE;

	LOG("[server] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=10pkts, CongestionControl=DISABLED, MTU=1200\n");

	// Step 4: Create configuration
	QUIC_BUFFER alpn_buffer;
	alpn_buffer.Buffer = (uint8_t*)WORMHOLE_ALPN;
	alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

	status = MsQuic->ConfigurationOpen(
		Registration,
		&alpn_buffer,
		1,  // AlpnBufferCount
		&settings,
		sizeof(settings),
		NULL,  // Context
		&ServerConfiguration
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[server] ERROR: ConfigurationOpen failed: 0x%x\n", status);
		return FALSE;
	}

	// Step 5: Load credentials (associates cert with configuration)
	status = MsQuic->ConfigurationLoadCredential(ServerConfiguration, &cred_config);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[server] ERROR: ConfigurationLoadCredential failed: 0x%x\n", status);
		MsQuic->ConfigurationClose(ServerConfiguration);
		ServerConfiguration = NULL;
		return FALSE;
	}

	LOG("[server] Configuration loaded successfully\n");
	return TRUE;
}

// ClientLoadConfiguration - Creates client configuration (unsecure for Phase 1)
static BOOLEAN ClientLoadConfiguration(void)
{
	QUIC_STATUS status;

	LOG("[client] Loading configuration...\n");

	// Step 1: Create credential configuration (NO certificate validation for Phase 1)
	QUIC_CREDENTIAL_CONFIG cred_config;
	memset(&cred_config, 0, sizeof(cred_config));
	cred_config.Type = QUIC_CREDENTIAL_TYPE_NONE;
	cred_config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

	// Step 2: Create QUIC settings
	QUIC_SETTINGS settings = { 0 };
	settings.IdleTimeoutMs = 300000;  // 300 seconds (5 minutes) - increased for large file transfers
	settings.IsSet.IdleTimeoutMs = TRUE;
	settings.DisconnectTimeoutMs = 300000;  // 300 seconds disconnect timeout
	settings.IsSet.DisconnectTimeoutMs = TRUE;
	settings.KeepAliveIntervalMs = 10000;  // Send keep-alive every 10 seconds
	settings.IsSet.KeepAliveIntervalMs = TRUE;

	// Flow control settings for large file transfers (CRITICAL for throughput!)
	// Based on Iroh's proven high-performance configuration
	settings.StreamRecvWindowDefault = 16777216;  // 16 MB (2^24, MUST be power of 2)
	settings.IsSet.StreamRecvWindowDefault = TRUE;

	settings.SendBufferingEnabled = TRUE;  // Enable send buffering (allows multiple chunks in flight)
	settings.IsSet.SendBufferingEnabled = TRUE;

	settings.ConnFlowControlWindow = 67108864;  // 64 MB (2^26, 4x stream window)
	settings.IsSet.ConnFlowControlWindow = TRUE;

	settings.InitialWindowPackets = 10;
	settings.IsSet.InitialWindowPackets = TRUE;

	// DISABLE congestion control for LAN testing (no backoff on packet loss)
	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_MAX;
	settings.IsSet.CongestionControlAlgorithm = TRUE;

	// EXPERIMENT 2.3: Force conservative MTU to bypass PMTUD issues
	settings.MinimumMtu = 1200;  // Force conservative MTU
	settings.IsSet.MinimumMtu = TRUE;
	settings.MaximumMtu = 1200;  // Prevent PMTUD
	settings.IsSet.MaximumMtu = TRUE;

	// Allow peer (server) to create bidirectional streams
	settings.PeerBidiStreamCount = 10;  // Allow up to 10 bidirectional streams from peer
	settings.IsSet.PeerBidiStreamCount = TRUE;
	settings.PeerUnidiStreamCount = 10;  // Allow up to 10 unidirectional streams from peer
	settings.IsSet.PeerUnidiStreamCount = TRUE;

	LOG("[client] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=10pkts, CongestionControl=DISABLED, MTU=1200, PeerStreams=10\n");

	// Step 3: Create configuration
	QUIC_BUFFER alpn_buffer;
	alpn_buffer.Buffer = (uint8_t*)WORMHOLE_ALPN;
	alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

	status = MsQuic->ConfigurationOpen(
		Registration,
		&alpn_buffer,
		1,  // AlpnBufferCount
		&settings,
		sizeof(settings),
		NULL,  // Context
		&ClientConfiguration
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[client] ERROR: ConfigurationOpen failed: 0x%x\n", status);
		return FALSE;
	}

	// Step 4: Load credentials
	status = MsQuic->ConfigurationLoadCredential(ClientConfiguration, &cred_config);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[client] ERROR: ConfigurationLoadCredential failed: 0x%x\n", status);
		MsQuic->ConfigurationClose(ClientConfiguration);
		ClientConfiguration = NULL;
		return FALSE;
	}

	LOG("[client] Configuration loaded successfully (unsecure mode)\n");
	return TRUE;
}

//=============================================================================
// Server & Client Runners
//=============================================================================

// RunServer - Creates listener and waits for incoming connections
static BOOLEAN RunServer(void)
{
	QUIC_STATUS status;
	HQUIC listener = NULL;

	LOG("[server] Starting server on port %d...\n", WORMHOLE_DEFAULT_PORT);

	// Create shutdown event (manual-reset event)
	ServerShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ServerShutdownEvent == NULL)
	{
		LOG_ERROR("[server] ERROR: Failed to create shutdown event\n");
		return FALSE;
	}

	// Create listener
	status = MsQuic->ListenerOpen(
		Registration,
		ServerListenerCallback,
		NULL,  // Context
		&listener
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[server] ERROR: ListenerOpen failed: 0x%x\n", status);
		CloseHandle(ServerShutdownEvent);
		return FALSE;
	}

	// Start listening on all interfaces, default port
	QUIC_ADDR addr = { 0 };
	QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);  // IPv4 + IPv6
	QuicAddrSetPort(&addr, WORMHOLE_DEFAULT_PORT);

	QUIC_BUFFER alpn_buffer;
	alpn_buffer.Buffer = (uint8_t*)WORMHOLE_ALPN;
	alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

	status = MsQuic->ListenerStart(listener, &alpn_buffer, 1, &addr);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[server] ERROR: ListenerStart failed: 0x%x\n", status);
		MsQuic->ListenerClose(listener);
		CloseHandle(ServerShutdownEvent);
		return FALSE;
	}

	LOG("[server] Listening on port %d (press Ctrl+C to stop)\n", WORMHOLE_DEFAULT_PORT);
	LOG("[server] Waiting for incoming file transfers...\n\n");

	// Wait for shutdown signal (Ctrl+C sets this event)
	WaitForSingleObject(ServerShutdownEvent, INFINITE);

	LOG("\n[server] Shutting down...\n");

	// Stop listener
	MsQuic->ListenerStop(listener);
	MsQuic->ListenerClose(listener);
	CloseHandle(ServerShutdownEvent);

	return TRUE;
}

// RunClient - Connects to server and sends file
static BOOLEAN RunClient(const char *target_host, const char *file_path)
{
	QUIC_STATUS status;
	HQUIC connection = NULL;

	LOG("[client] Connecting to %s:%d...\n", target_host, WORMHOLE_DEFAULT_PORT);

	// Validate file exists
	if (!FileExists(file_path))
	{
		LOG_ERROR("[client] ERROR: File does not exist: %s\n", file_path);
		return FALSE;
	}

	// Create connection context
	typedef struct {
		char *file_path;
		BOOLEAN connected;
		HANDLE connect_event;
		HANDLE transfer_complete_event;
	} CLIENT_CONNECTION_CONTEXT;

	CLIENT_CONNECTION_CONTEXT *conn_ctx = (CLIENT_CONNECTION_CONTEXT*)malloc(sizeof(CLIENT_CONNECTION_CONTEXT));
	if (conn_ctx == NULL)
	{
		LOG_ERROR("[client] ERROR: Failed to allocate connection context\n");
		return FALSE;
	}

	conn_ctx->file_path = _strdup(file_path);
	conn_ctx->connected = FALSE;
	conn_ctx->connect_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	conn_ctx->transfer_complete_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (conn_ctx->connect_event == NULL || conn_ctx->transfer_complete_event == NULL || conn_ctx->file_path == NULL)
	{
		LOG_ERROR("[client] ERROR: Failed to allocate resources\n");
		if (conn_ctx->file_path) free(conn_ctx->file_path);
		if (conn_ctx->connect_event) CloseHandle(conn_ctx->connect_event);
		if (conn_ctx->transfer_complete_event) CloseHandle(conn_ctx->transfer_complete_event);
		free(conn_ctx);
		return FALSE;
	}

	// Create connection
	status = MsQuic->ConnectionOpen(
		Registration,
		ClientConnectionCallback,
		conn_ctx,
		&connection
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[client] ERROR: ConnectionOpen failed: 0x%x\n", status);
		free(conn_ctx->file_path);
		CloseHandle(conn_ctx->connect_event);
		CloseHandle(conn_ctx->transfer_complete_event);
		free(conn_ctx);
		return FALSE;
	}

	// Start connection
	status = MsQuic->ConnectionStart(
		connection,
		ClientConfiguration,
		QUIC_ADDRESS_FAMILY_UNSPEC,
		target_host,
		WORMHOLE_DEFAULT_PORT
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("[client] ERROR: ConnectionStart failed: 0x%x\n", status);
		MsQuic->ConnectionClose(connection);
		free(conn_ctx->file_path);
		CloseHandle(conn_ctx->connect_event);
		CloseHandle(conn_ctx->transfer_complete_event);
		free(conn_ctx);
		return FALSE;
	}

	LOG("[client] Connection initiated, waiting for handshake...\n");

	// Wait for connection to complete (or fail)
	// The ClientConnectionCallback will signal this event when CONNECTED
	DWORD wait_result = WaitForSingleObject(conn_ctx->connect_event, 10000);  // 10 second timeout

	if (wait_result != WAIT_OBJECT_0)
	{
		LOG_ERROR("[client] ERROR: Connection timeout or failed\n");
		MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
		MsQuic->ConnectionClose(connection);
		free(conn_ctx->file_path);
		CloseHandle(conn_ctx->connect_event);
		CloseHandle(conn_ctx->transfer_complete_event);
		free(conn_ctx);
		return FALSE;
	}

	LOG("[client] Connected! Sending file...\n");

	// SendFile will be called from ClientConnectionCallback when CONNECTED event fires
	// Wait for transfer to complete (or timeout after 120 seconds for large files)
	LOG("[client] Waiting for transfer to complete...\n");
	wait_result = WaitForSingleObject(conn_ctx->transfer_complete_event, 120000);  // 120 second timeout

	if (wait_result != WAIT_OBJECT_0)
	{
		LOG_ERROR("[client] WARNING: Transfer timeout or didn't complete\n");
	}
	else
	{
		LOG("[client] Transfer complete!\n");
	}

	LOG("[client] Closing connection...\n");

	// Gracefully shut down connection
	// Note: The callback will clean up conn_ctx when SHUTDOWN_COMPLETE fires
	MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
	MsQuic->ConnectionClose(connection);

	LOG("[client] Done!\n");
	return TRUE;
}

//=============================================================================
// New CLI Commands (Send/Receive)
//=============================================================================

// Global state for relay callbacks
static char g_ticket[64] = {0};
static BOOLEAN g_ticket_ready = FALSE;
static BOOLEAN g_peer_info_ready = FALSE;
static uint8_t g_peer_id[32] = {0};
static ENDPOINT g_peer_endpoints[MAX_ENDPOINTS] = {0};
static uint16_t g_peer_endpoint_count = 0;

// Reflected public IP (from relay server's REGISTERED message)
static BOOLEAN g_reflected_addr_ready = FALSE;
static uint8_t g_reflected_addr_type = 0;
static uint8_t g_reflected_addr[16] = {0};
static uint16_t g_reflected_port = 0;

// Relay callbacks
static void on_relay_connected(void* context, uint64_t session_id,
                               const uint8_t observed_addr[16],
                               uint8_t observed_addr_type, uint16_t observed_port)
{
	LOG("\n[Relay] Connected to relay server (session %llu)\n", (unsigned long long)session_id);
	
	// Store reflected address for later use (hole punching)
	// If observed address is IPv4-mapped IPv6, convert to pure IPv4
	static const uint8_t v4mapped_prefix[12] = {0,0,0,0, 0,0,0,0, 0,0,0xff,0xff};
	if (observed_addr_type == 0x06 && memcmp(observed_addr, v4mapped_prefix, 12) == 0)
	{
		g_reflected_addr_type = 0x04;
		memcpy(g_reflected_addr, &observed_addr[12], 4);
	}
	else
	{
		g_reflected_addr_type = observed_addr_type;
		memcpy(g_reflected_addr, observed_addr, 16);
	}
	g_reflected_port = ntohs(observed_port);  // Convert from network to host order
	g_reflected_addr_ready = TRUE;

	// Display public IP using Endpoint_ToString for consistent formatting
	ENDPOINT temp_ep;
	temp_ep.addr_type = observed_addr_type;
	memcpy(temp_ep.addr, observed_addr, 16);
	temp_ep.port = ntohs(observed_port);  // Convert from network to host order

	char ip_str[INET6_ADDRSTRLEN];
	uint16_t port_display;
	if (Endpoint_ToString(&temp_ep, ip_str, sizeof(ip_str), &port_display))
	{
		LOG("[Relay] Your public IP (as seen by relay): %s:%u\n", ip_str, port_display);
	}
	else
	{
		LOG("[Relay] Your public IP (as seen by relay): [parse error]:%u\n", ntohs(observed_port));
	}
}

static void on_ticket_created(void* context, const char* ticket)
{
	strncpy_s(g_ticket, sizeof(g_ticket), ticket, _TRUNCATE);
	g_ticket_ready = TRUE;
}

static void on_peer_info(void* context, const uint8_t peer_id[32],
                        const ENDPOINT* endpoints, uint16_t endpoint_count)
{
	memcpy(g_peer_id, peer_id, 32);
	uint16_t count = endpoint_count;
	if (count > MAX_ENDPOINTS) count = MAX_ENDPOINTS;
	memcpy(g_peer_endpoints, endpoints, count * sizeof(ENDPOINT));
	g_peer_endpoint_count = count;
	g_peer_info_ready = TRUE;

	LOG("\n[Relay] Found sender with %u endpoints\n", count);
}

static void on_relay_disconnected(void* context)
{
	LOG("\n[Relay] Disconnected from relay\n");
}

//=============================================================================
// Send Command - QUIC Server Callbacks
//=============================================================================

// Context for sender's QUIC server
typedef struct {
	const char* filepath;
	BOOLEAN receiver_connected;
	BOOLEAN transfer_started;
	HANDLE receiver_connect_event;
	HANDLE transfer_done_event;
} SEND_SERVER_CONTEXT;

// Server listener callback for cmd_send
static QUIC_STATUS QUIC_API ServerListenerCallback_Send(
	HQUIC Listener,
	void* Context,
	QUIC_LISTENER_EVENT* Event)
{
	SEND_SERVER_CONTEXT* ctx = (SEND_SERVER_CONTEXT*)Context;
	
	switch (Event->Type)
	{
		case QUIC_LISTENER_EVENT_NEW_CONNECTION:
		{
			if (ctx->receiver_connected) {
				LOG("[Send] Rejecting extra connection (already have receiver)\n");
				return QUIC_STATUS_CONNECTION_REFUSED;
			}

			LOG("[Send] Receiver connecting...\n");

			// Set connection callback
			MsQuic->SetCallbackHandler(
				Event->NEW_CONNECTION.Connection,
				(void*)ServerConnectionCallback_Send,
				Context
			);
			
			// Accept connection with server configuration
			QUIC_STATUS status = MsQuic->ConnectionSetConfiguration(
				Event->NEW_CONNECTION.Connection,
				ServerConfiguration
			);
			
			if (QUIC_FAILED(status))
			{
				LOG_ERROR("[Send] Failed to set connection configuration: 0x%x\n", status);
				return status;
			}
			
			ctx->receiver_connected = TRUE;
			SetEvent(ctx->receiver_connect_event);
			
			return QUIC_STATUS_SUCCESS;
		}
		
		default:
			return QUIC_STATUS_SUCCESS;
	}
}

// Server connection callback for cmd_send
static QUIC_STATUS QUIC_API ServerConnectionCallback_Send(
	HQUIC Connection,
	void* Context,
	QUIC_CONNECTION_EVENT* Event)
{
	SEND_SERVER_CONTEXT* ctx = (SEND_SERVER_CONTEXT*)Context;
	
	switch (Event->Type)
	{
		case QUIC_CONNECTION_EVENT_CONNECTED:
		{
			if (ctx->transfer_started) {
				LOG("[Send] Ignoring duplicate connected event\n");
				return QUIC_STATUS_SUCCESS;
			}
			ctx->transfer_started = TRUE;

			LOG("[Send] Receiver connected! Starting file transfer...\n");

			// Start sending file
			SendFile(Connection, ctx->filepath, ctx->transfer_done_event);

			return QUIC_STATUS_SUCCESS;
		}
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
	{
		LOG("[Send] Connection shutdown complete\n");
		// Connection already closed by main loop or MsQuic, don't close again
		return QUIC_STATUS_SUCCESS;
	}
		
		case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		{
			// We don't expect streams from receiver (sender initiates)
			return QUIC_STATUS_SUCCESS;
		}
		
		default:
			return QUIC_STATUS_SUCCESS;
	}
}

//=============================================================================
// Receive Command - QUIC Client Callbacks
//=============================================================================

// Context for receiver's QUIC client  
typedef struct {
	BOOLEAN connected;
	HANDLE connect_event;
	HANDLE transfer_done_event;
	char downloads_path[MAX_PATH];
} RECEIVE_CLIENT_CONTEXT;

// Custom stream callback wrapper for cmd_receive that handles Downloads folder path
static QUIC_STATUS QUIC_API ReceiveStreamCallback_Wrapper(
	HQUIC Stream,
	void* Context,
	QUIC_STREAM_EVENT* Event)
{
	RECEIVE_CONTEXT* recv_ctx = (RECEIVE_CONTEXT*)Context;
	RECEIVE_CLIENT_CONTEXT* client_ctx = (RECEIVE_CLIENT_CONTEXT*)recv_ctx->user_context;
	
	// Just pass through RECEIVE events - don't modify filename yet
	if (Event->Type == QUIC_STREAM_EVENT_RECEIVE)
	{
		return ServerStreamCallback(Stream, Context, Event);
	}
	
	// Handle PEER_SEND_SHUTDOWN to move completed file to Downloads
	if (Event->Type == QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN)
	{
		// Store original filename before ServerStreamCallback processes shutdown
		char original_filename[MAX_PATH] = {0};
		if (recv_ctx->filename)
		{
			strncpy_s(original_filename, sizeof(original_filename), recv_ctx->filename, _TRUNCATE);
		}
		
		// Call ServerStreamCallback to finish transfer, close file, and rename .partial to final
		QUIC_STATUS result = ServerStreamCallback(Stream, Context, Event);
		
		// After successful transfer, move file from current directory to Downloads
		if (original_filename[0] && recv_ctx->bytes_received == recv_ctx->total_file_size)
		{
			// ServerStreamCallback renamed .partial to final, so file is now at: ./<filename>
			// Check both possible locations (with and without .partial, in case rename failed)
			char source_file[MAX_PATH];
			snprintf(source_file, sizeof(source_file), "%s", original_filename);
			
			if (!FileExists(source_file))
			{
				// Maybe rename failed, try .partial
				snprintf(source_file, sizeof(source_file), "%s.partial", original_filename);
			}
			
			if (FileExists(source_file))
			{
				// Get unique path in Downloads folder
				char downloads_path[MAX_PATH];
				if (GetUniqueFilename(client_ctx->downloads_path, original_filename, downloads_path, sizeof(downloads_path)))
				{
					LOG("[Receive] Moving file to Downloads: %s -> %s\n", source_file, downloads_path);
					
					// Use MoveFileA (Windows API) which is more reliable than rename()
					if (MoveFileA(source_file, downloads_path))
					{
						LOG("[Receive] ✅ File saved to Downloads: %s\n", downloads_path);
					}
					else
					{
						DWORD error = GetLastError();
						LOG_ERROR("[Receive] ⚠️ Warning: Could not move file to Downloads (error: %lu)\n", error);
						LOG_ERROR("[Receive] File remains at: %s\n", source_file);
					}
				}
			}
			else
			{
				LOG_ERROR("[Receive] ⚠️ Warning: Cannot find transferred file: %s\n", original_filename);
			}
		}
		
		return result;
	}
	
	// Handle SHUTDOWN_COMPLETE to signal transfer done
	if (Event->Type == QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE)
	{
		SetEvent(client_ctx->transfer_done_event);
	}
	
	// Call the original ServerStreamCallback for other events
	return ServerStreamCallback(Stream, Context, Event);
}

// Client connection callback for cmd_receive
static QUIC_STATUS QUIC_API ClientConnectionCallback_Receive(
	HQUIC Connection,
	void* Context,
	QUIC_CONNECTION_EVENT* Event)
{
	RECEIVE_CLIENT_CONTEXT* ctx = (RECEIVE_CLIENT_CONTEXT*)Context;
	
	switch (Event->Type)
	{
		case QUIC_CONNECTION_EVENT_CONNECTED:
		{
			LOG("[Receive] Connected to sender!\n");
			ctx->connected = TRUE;
			SetEvent(ctx->connect_event);
			return QUIC_STATUS_SUCCESS;
		}
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
	{
		LOG("[Receive] Connection shutdown complete\n");
		// Connection already closed by main loop or MsQuic, don't close again
		return QUIC_STATUS_SUCCESS;
	}
		
		case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		{
			// Sender is starting file transfer stream
			LOG("[Receive] File transfer stream started...\n");
			
			// Create receive context
			RECEIVE_CONTEXT* recv_ctx = (RECEIVE_CONTEXT*)malloc(sizeof(RECEIVE_CONTEXT));
			if (!recv_ctx)
			{
				return QUIC_STATUS_OUT_OF_MEMORY;
			}
			
			memset(recv_ctx, 0, sizeof(RECEIVE_CONTEXT));
			// Allocate buffer for header (min size + max filename)
			recv_ctx->header_buffer = (uint8_t*)malloc(HEADER_MIN_SIZE + MAX_FILENAME_LENGTH);
			recv_ctx->user_context = ctx;  // Store client context for Downloads path
			QueryPerformanceCounter(&recv_ctx->start_time);  // Record start time for throughput calculation
			
			// Set stream callback wrapper to receive file
			MsQuic->SetCallbackHandler(
				Event->PEER_STREAM_STARTED.Stream,
				(void*)ReceiveStreamCallback_Wrapper,
				recv_ctx
			);
			
			return QUIC_STATUS_SUCCESS;
		}
		
		default:
			LOG("[Receive] Unhandled connection event type: %d\n", Event->Type);
			return QUIC_STATUS_SUCCESS;
	}
}

// Send UDP hole punch probes to all of a peer's endpoints.
// Uses the relay client's socket so probes share the same NAT mapping as relay traffic.
static void send_hole_punch_probes(RELAY_CLIENT* relay_client,
                                    const ENDPOINT* peer_endpoints,
                                    uint16_t peer_endpoint_count)
{
	static int round_num = 0;
	int sock_fd = RelayClient_GetSocket(relay_client);
	if (sock_fd < 0) return;

	// Log the local port we're sending probes from (first round only)
	if (round_num == 0)
	{
		struct sockaddr_storage local_addr;
		socklen_t local_len = sizeof(local_addr);
		if (getsockname(sock_fd, (struct sockaddr*)&local_addr, &local_len) == 0)
		{
			uint16_t local_port = 0;
			if (local_addr.ss_family == AF_INET)
				local_port = ntohs(((struct sockaddr_in*)&local_addr)->sin_port);
			else if (local_addr.ss_family == AF_INET6)
				local_port = ntohs(((struct sockaddr_in6*)&local_addr)->sin6_port);
			LOG("[HolePunch] Probes sending from local port %u (socket fd=%d)\n",
				local_port, sock_fd);
		}
	}
	round_num++;

	// Simple probe packet - 4 byte magic header
	// These packets will be ignored by the receiver's QUIC stack (not valid QUIC)
	// but they open holes in the sender's NAT for the receiver's IP
	uint8_t probe[] = { 'W', 'H', 'P', 'K' };  // Wormhole Hole Punch Knock

	for (uint16_t i = 0; i < peer_endpoint_count; i++)
	{
		const ENDPOINT* ep = &peer_endpoints[i];
		if (ep->priority >= 200) continue;  // Skip relay endpoints — they don't understand probe packets
		struct sockaddr_storage dest_addr;
		socklen_t dest_len = 0;

		if (ep->addr_type == 0x04)
		{
			struct sockaddr_in* addr4 = (struct sockaddr_in*)&dest_addr;
			memset(addr4, 0, sizeof(*addr4));
			addr4->sin_family = AF_INET;
			memcpy(&addr4->sin_addr, ep->addr, 4);
			addr4->sin_port = htons(ep->port);  // ep->port is host order, sendto needs network order
			dest_len = sizeof(struct sockaddr_in);
		}
		else if (ep->addr_type == 0x06)
		{
			// Check for IPv4-mapped IPv6 (::ffff:x.x.x.x) — downgrade to IPv4
			// so probes work on our AF_INET relay socket
			static const uint8_t v4mapped_prefix[12] = {0,0,0,0, 0,0,0,0, 0,0,0xff,0xff};
			if (memcmp(ep->addr, v4mapped_prefix, 12) == 0)
			{
				struct sockaddr_in* addr4 = (struct sockaddr_in*)&dest_addr;
				memset(addr4, 0, sizeof(*addr4));
				addr4->sin_family = AF_INET;
				memcpy(&addr4->sin_addr, &ep->addr[12], 4);
				addr4->sin_port = htons(ep->port);
				dest_len = sizeof(struct sockaddr_in);
			}
			else
			{
				// Pure IPv6 — can't send on our IPv4 socket, skip
				continue;
			}
		}
		else
		{
			continue;
		}

		int sent = sendto(sock_fd, (const char*)probe, sizeof(probe), 0,
		                  (struct sockaddr*)&dest_addr, dest_len);

		// Log each probe on round 1 for diagnostics
		if (round_num == 1)
		{
			char ep_ip[INET6_ADDRSTRLEN];
			Endpoint_ToString(ep, ep_ip, sizeof(ep_ip), &(uint16_t){0});
			LOG("[HolePunch] Round %d: probe -> EP[%u] %s:%u (priority=%u) %s\n",
				round_num, i, ep_ip, ep->port, ep->priority,
				sent > 0 ? "sent" : "FAILED");
		}
	}
}

//=============================================================================
// Parallel Connection Race (used by cmd_receive)
//=============================================================================

typedef struct PARALLEL_CONNECT_CTX PARALLEL_CONNECT_CTX;

typedef struct {
	uint16_t index;
	PARALLEL_CONNECT_CTX* shared;
} PER_CONN_CTX;

struct PARALLEL_CONNECT_CTX {
	RECEIVE_CLIENT_CONTEXT* client_ctx;
	HQUIC connections[MAX_ENDPOINTS];
	PER_CONN_CTX per_conn[MAX_ENDPOINTS];
	uint16_t count;
	HQUIC winning_connection;
	HANDLE race_event;
	volatile LONG race_won;
	uint16_t winning_index;
};

// Connection callback for parallel race phase.
// First connection to complete QUIC handshake wins; losers are abandoned.
static QUIC_STATUS QUIC_API RaceConnectionCallback(
	HQUIC Connection,
	void* Context,
	QUIC_CONNECTION_EVENT* Event)
{
	PER_CONN_CTX* ctx = (PER_CONN_CTX*)Context;
	PARALLEL_CONNECT_CTX* shared = ctx->shared;

	switch (Event->Type)
	{
		case QUIC_CONNECTION_EVENT_CONNECTED:
		{
			if (InterlockedCompareExchange(&shared->race_won, 1, 0) == 0)
			{
				LOG("[Receive] Endpoint %u connected first - winner!\n", ctx->index);
				shared->winning_connection = Connection;
				shared->winning_index = ctx->index;

				// Switch to the real file-transfer callback.
				// MsQuic serializes events per-connection, so PEER_STREAM_STARTED
				// will use the new handler after this returns.
				MsQuic->SetCallbackHandler(
					Connection,
					(void*)ClientConnectionCallback_Receive,
					shared->client_ctx);

				shared->client_ctx->connected = TRUE;
				SetEvent(shared->race_event);
			}
			else
			{
				LOG("[Receive] Endpoint %u connected but lost the race\n", ctx->index);
			}
			return QUIC_STATUS_SUCCESS;
		}

		case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		{
			LOG("[Receive] Endpoint %u connection failed\n", ctx->index);
			return QUIC_STATUS_SUCCESS;
		}

		case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
			return QUIC_STATUS_SUCCESS;

		default:
			return QUIC_STATUS_SUCCESS;
	}
}

// cmd_send - Send a file and get a ticket
static int cmd_send(const char* filepath)
{
	LOG("\n═══════════════════════════════════════\n");
	LOG("  WORMHOLE SEND\n");
	LOG("═══════════════════════════════════════\n\n");
	
	// Check if file exists
	if (!FileExists(filepath))
	{
		LOG_ERROR("Error: File not found: %s\n", filepath);
		return 1;
	}
	
	uint64_t filesize = 0;
	if (!GetWormholeFileSize(filepath, &filesize))
	{
		LOG_ERROR("Error: Failed to get file size\n");
		return 1;
	}
	
	char* filename = NULL;
	uint32_t filename_len = 0;
	ExtractFilename(filepath, &filename, &filename_len);
	if (!filename)
	{
		LOG_ERROR("Error: Failed to extract filename\n");
		return 1;
	}
	
	LOG("[Send] File: %s\n", filename);
	LOG("[Send] Size: %llu bytes\n\n", (unsigned long long)filesize);
	
#ifdef _WIN32
	// Initialize Winsock on Windows
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		LOG_ERROR("Error: Failed to initialize Winsock\n");
		free(filename);
		return 1;
	}
#endif
	
	// Initialize PeerID (Ed25519 keypair)
	if (!PeerID_Init())
	{
		LOG_ERROR("Error: Failed to initialize cryptography\n");
#ifdef _WIN32
		WSACleanup();
#endif
		free(filename);
		return 1;
	}
	
	// Load or generate identity
	KEYPAIR keypair;
	char* identity_path = PeerID_GetDefaultPath();
	if (!PeerID_LoadOrGenerate(&keypair, identity_path))
	{
		LOG_ERROR("Error: Failed to load identity\n");
		free(identity_path);
		return 1;
	}
	free(identity_path);
	
	// Create relay client
	RELAY_CLIENT_CONFIG relay_config;
	relay_config.relay_host = DEFAULT_RELAY_HOST;
	relay_config.relay_port = DEFAULT_RELAY_PORT;
	relay_config.local_port = WORMHOLE_DEFAULT_PORT;  // Bind to QUIC port for consistent NAT mapping
	relay_config.keypair = &keypair;
	relay_config.on_connected = on_relay_connected;
	relay_config.on_ticket_created = on_ticket_created;
	relay_config.on_peer_info = on_peer_info;
	relay_config.on_disconnected = on_relay_disconnected;
	relay_config.callback_context = NULL;

	RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
	if (!relay_client)
	{
		LOG_ERROR("Error: Failed to create relay client\n");
#ifdef _WIN32
		WSACleanup();
#endif
		free(filename);
		return 1;
	}

	// Discover endpoints
	LOG("[Send] Discovering network endpoints...\n");
	ENDPOINT endpoints[MAX_ENDPOINTS];
	uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);
	
	if (endpoint_count == 0)
	{
		LOG("[Send] Warning: No endpoints found, using placeholder\n");
		memset(&endpoints[0], 0, sizeof(ENDPOINT));
		endpoints[0].addr_type = 0x04;
		endpoints[0].addr[0] = 127;
		endpoints[0].addr[3] = 1;
		endpoints[0].port = WORMHOLE_DEFAULT_PORT;
		endpoint_count = 1;
	}
	else
	{
		// Set port for all discovered endpoints
		for (uint16_t i = 0; i < endpoint_count; i++)
		{
			endpoints[i].port = WORMHOLE_DEFAULT_PORT;
		}
	}
	
	// Register with relay (with retry for UDP packet loss)
	LOG("[Send] Registering with relay server...\n");
	
	const int MAX_REGISTER_ATTEMPTS = 3;
	BOOLEAN registered = FALSE;
	
	for (int attempt = 0; attempt < MAX_REGISTER_ATTEMPTS && !registered; attempt++)
	{
		if (attempt > 0)
		{
			LOG("[Send] Registration attempt %d/%d...\n", attempt + 1, MAX_REGISTER_ATTEMPTS);
		}
		
		if (!RelayClient_Register(relay_client, endpoints, endpoint_count))
		{
			LOG_ERROR("Error: Failed to send REGISTER message\n");
			continue;
		}
		
		// Wait for registration response (5 seconds per attempt)
		for (int i = 0; i < 50; i++)
		{
			if (RelayClient_IsConnected(relay_client))
			{
				registered = TRUE;
				break;
			}
			RelayClient_Poll(relay_client, 100);
		}
	}
	
	if (!registered)
	{
		LOG_ERROR("Error: Failed to connect to relay after %d attempts (timeout)\n", MAX_REGISTER_ATTEMPTS);
		RelayClient_Destroy(relay_client);
		free(filename);
		return 1;
	}
	
	// Add reflected public IP as connection endpoint (for hole punching)
	if (g_reflected_addr_ready && endpoint_count < MAX_ENDPOINTS)
	{
		// Create public IP endpoint using our QUIC listener port
		ENDPOINT public_ep;
		public_ep.addr_type = g_reflected_addr_type;
		memcpy(public_ep.addr, g_reflected_addr, 16);
		public_ep.port = WORMHOLE_DEFAULT_PORT;  // Host byte order, consistent with LAN endpoints
		public_ep.priority = 100;  // Lower priority than LAN (0) but higher than relay (200)
		
		// Debug: Log port creation
		LOG("[Send] [DEBUG] Public endpoint created:\n");
		LOG("[Send] [DEBUG]   Port (host order): %u\n", WORMHOLE_DEFAULT_PORT);
		LOG("[Send] [DEBUG]   Port (network order): 0x%04x\n", htons(WORMHOLE_DEFAULT_PORT));
		LOG("[Send] [DEBUG]   Expected: 0x11D7 (4567 in network byte order)\n");
		
		endpoints[endpoint_count++] = public_ep;
		
		// Log the public IP endpoint for debugging
		char ip_str[INET6_ADDRSTRLEN];
		uint16_t port;
		if (Endpoint_ToString(&public_ep, ip_str, sizeof(ip_str), &port))
		{
			LOG("[Send] Adding public IP endpoint: %s:%u (priority %u)\n",
				ip_str, port, public_ep.priority);
		}
		
		// Re-register with updated endpoint list (now includes public IP)
		LOG("[Send] Re-registering with public IP endpoint...\n");
		
		// Debug: Log all endpoints being registered
		LOG("[Send] [DEBUG] Re-registering with %u endpoints:\n", endpoint_count);
		for (uint16_t i = 0; i < endpoint_count; i++)
		{
			char ep_ip_str[INET6_ADDRSTRLEN];
			uint16_t ep_port;
			if (Endpoint_ToString(&endpoints[i], ep_ip_str, sizeof(ep_ip_str), &ep_port))
			{
				LOG("[Send] [DEBUG]   EP[%u]: %s:%u (priority=%u, port_raw=0x%04x)\n",
					i, ep_ip_str, ep_port, endpoints[i].priority, endpoints[i].port);
			}
		}
		
		if (!RelayClient_Register(relay_client, endpoints, endpoint_count))
		{
			LOG("[Send] Warning: Failed to re-register (continuing with original endpoints)\n");
			endpoint_count--;  // Revert endpoint addition
		}
		else
		{
			// Wait for re-registration to complete
			for (int i = 0; i < 50; i++)
			{
				if (RelayClient_IsConnected(relay_client)) break;
				RelayClient_Poll(relay_client, 100);
			}
			
			if (!RelayClient_IsConnected(relay_client))
			{
				LOG("[Send] Warning: Re-registration timed out (continuing anyway)\n");
			}
		}
	}
	
	// Create ticket
	LOG("[Send] Creating transfer ticket...\n");
	if (!RelayClient_CreateTicket(relay_client, filesize, filename))
	{
		LOG_ERROR("Error: Failed to create ticket\n");
		RelayClient_Destroy(relay_client);
		free(filename);
		return 1;
	}
	
	// Wait for ticket
	for (int i = 0; i < 50 && !g_ticket_ready; i++)
	{
		RelayClient_Poll(relay_client, 100);
	}
	
	if (!g_ticket_ready)
	{
		LOG_ERROR("Error: Failed to receive ticket (timeout)\n");
		RelayClient_Destroy(relay_client);
		free(filename);
		return 1;
	}
	
	// Display ticket
	Ticket_PrintForSharing(g_ticket, filename, filesize);
	
	LOG("\n[Send] Waiting for receiver (timeout: 30 minutes)...\n\n");

	// Phase 1: Keep relay open - wait for receiver to look up ticket.
	// The relay will send us PEER_INFO with the receiver's endpoints,
	// which we need for hole-punch probing from port 4567.
	const int timeout_ms = 1800000;  // 30 minutes
	const int poll_interval_ms = 100;
	const int keepalive_interval = 10000 / poll_interval_ms;  // Send keepalive every 10 seconds
	const int probe_interval = 200 / poll_interval_ms;        // Send hole punch probes every 200ms
	const int max_probe_rounds = 5;                            // 5 rounds * 200ms = 1 second of probing (fast transition to QUIC)

	BOOLEAN hole_punching = FALSE;
	int probe_rounds = 0;

	LOG("[Send] Phase 1: Waiting for receiver on relay (hole punch probing)...\n");

	for (int i = 0; i < (timeout_ms / poll_interval_ms); i++)
	{
		// Poll relay for messages (keepalive responses, PEER_INFO about receiver)
		RelayClient_Poll(relay_client, poll_interval_ms);

		// Send keepalive every 10 seconds
		if (i % keepalive_interval == 0 && i > 0)
		{
			RelayClient_SendKeepalive(relay_client);
		}

		// Check if we received PEER_INFO about the receiver (start hole punching)
		if (g_peer_info_ready && !hole_punching)
		{
			LOG("[Send] Receiver found! Starting hole punch probing to %u endpoints...\n",
				g_peer_endpoint_count);
			hole_punching = TRUE;
			probe_rounds = 0;

			// Send first round of probes immediately
			send_hole_punch_probes(relay_client, g_peer_endpoints, g_peer_endpoint_count);
			probe_rounds++;
		}

		// Continue sending probes at regular intervals
		if (hole_punching && probe_rounds < max_probe_rounds && (i % probe_interval == 0))
		{
			send_hole_punch_probes(relay_client, g_peer_endpoints, g_peer_endpoint_count);
			probe_rounds++;

			if (probe_rounds % 10 == 0)
			{
				LOG("[Send] Hole punch: sent %d probe rounds (%d endpoints per round)\n",
					probe_rounds, g_peer_endpoint_count);
			}
		}

		// After probing is complete, move to Phase 2 (QUIC listener)
		if (hole_punching && probe_rounds >= max_probe_rounds)
		{
			break;
		}
	}

	// Phase 2: Close relay to free port 4567, then start QUIC listener.
	// NAT mappings persist briefly (~30-120s) after socket close.
	LOG("[Send] Phase 2: Closing relay, starting QUIC listener on port %d...\n", WORMHOLE_DEFAULT_PORT);
	RelayClient_SendGoodbye(relay_client, 0x00);  // 0x00 = upgrading to direct
	RelayClient_Destroy(relay_client);
	relay_client = NULL;

	// Initialize MsQuic for file transfer
	if (!InitializeMsQuic())
	{
		LOG_ERROR("Error: Failed to initialize MsQuic\n");
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Load server configuration
	if (!ServerLoadConfiguration())
	{
		LOG_ERROR("Error: Failed to load QUIC server configuration\n");
		CleanupMsQuic();
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Create server context
	SEND_SERVER_CONTEXT server_ctx;
	server_ctx.filepath = filepath;
	server_ctx.receiver_connected = FALSE;
	server_ctx.transfer_started = FALSE;
	server_ctx.receiver_connect_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	server_ctx.transfer_done_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (!server_ctx.receiver_connect_event || !server_ctx.transfer_done_event)
	{
		LOG_ERROR("Error: Failed to create events\n");
		CleanupMsQuic();
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Create QUIC listener
	HQUIC listener = NULL;
	QUIC_STATUS status = MsQuic->ListenerOpen(
		Registration,
		ServerListenerCallback_Send,
		&server_ctx,
		&listener
	);

	if (QUIC_FAILED(status))
	{
		LOG_ERROR("Error: Failed to create QUIC listener: 0x%x\n", status);
		CloseHandle(server_ctx.receiver_connect_event);
		CloseHandle(server_ctx.transfer_done_event);
		CleanupMsQuic();
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Start listening on port 4567
	QUIC_ADDR addr = {0};
	QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
	QuicAddrSetPort(&addr, WORMHOLE_DEFAULT_PORT);

	QUIC_BUFFER alpn_buffer;
	alpn_buffer.Buffer = (uint8_t*)WORMHOLE_ALPN;
	alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

	status = MsQuic->ListenerStart(listener, &alpn_buffer, 1, &addr);
	if (QUIC_FAILED(status))
	{
		LOG_ERROR("Error: Failed to start QUIC listener: 0x%x\n", status);
		MsQuic->ListenerClose(listener);
		CloseHandle(server_ctx.receiver_connect_event);
		CloseHandle(server_ctx.transfer_done_event);
		CleanupMsQuic();
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	LOG("[Send] QUIC server listening on port %d\n", WORMHOLE_DEFAULT_PORT);

	// Phase 3: Wait for receiver to connect via QUIC
	BOOLEAN transfer_completed = FALSE;

	for (int i = 0; i < (timeout_ms / poll_interval_ms); i++)
	{
		Sleep(poll_interval_ms);

		// Check if receiver connected via QUIC
		if (server_ctx.receiver_connected)
		{
			LOG("[Send] Receiver connected! Transferring file...\n");

			// Wait for transfer to complete (with timeout)
			// 60 minutes to support large files (5GB @ 50 Mbps = ~13 minutes)
			DWORD wait_result = WaitForSingleObject(server_ctx.transfer_done_event, 3600000);

			if (wait_result == WAIT_OBJECT_0)
			{
				LOG("\n[Send] File transfer complete!\n");
				transfer_completed = TRUE;
			}
			else
			{
				LOG_ERROR("\n[Send] File transfer timed out or failed\n");
			}

			break;
		}
	}

	if (!transfer_completed && !server_ctx.receiver_connected)
	{
		LOG("\n[Send] Timeout: No receiver connected within 30 minutes\n");
	}

	// Cleanup
	MsQuic->ListenerStop(listener);
	MsQuic->ListenerClose(listener);
	CloseHandle(server_ctx.receiver_connect_event);
	CloseHandle(server_ctx.transfer_done_event);
	CleanupMsQuic();

	free(filename);
	
#ifdef _WIN32
	WSACleanup();
#endif
	
	LOG("\n[Send] Session ended\n");
	return transfer_completed ? 0 : 1;
}

// cmd_receive - Receive a file using a ticket
static int cmd_receive(const char* ticket)
{
	LOG("\n═══════════════════════════════════════\n");
	LOG("  WORMHOLE RECEIVE\n");
	LOG("═══════════════════════════════════════\n\n");
	
	// Validate ticket
	if (!Ticket_Validate(ticket))
	{
		LOG_ERROR("Error: Invalid ticket format\n");
		LOG("Expected format: N-word-word (e.g., 7-guitar-battery)\n");
		return 1;
	}
	
	Ticket_PrintReceiving(ticket);
	
#ifdef _WIN32
	// Initialize Winsock on Windows
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		LOG_ERROR("Error: Failed to initialize Winsock\n");
		return 1;
	}
#endif
	
	// Initialize PeerID
	if (!PeerID_Init())
	{
		LOG_ERROR("Error: Failed to initialize cryptography\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	
	// Load or generate identity
	KEYPAIR keypair;
	char* identity_path = PeerID_GetDefaultPath();
	if (!PeerID_LoadOrGenerate(&keypair, identity_path))
	{
		LOG_ERROR("Error: Failed to load identity\n");
		free(identity_path);
		return 1;
	}
	free(identity_path);
	
	// Create relay client
	RELAY_CLIENT_CONFIG relay_config;
	relay_config.relay_host = DEFAULT_RELAY_HOST;
	relay_config.relay_port = DEFAULT_RELAY_PORT;
	relay_config.local_port = WORMHOLE_DEFAULT_PORT;  // Bind to QUIC port for consistent NAT mapping
	relay_config.keypair = &keypair;
	relay_config.on_connected = on_relay_connected;
	relay_config.on_ticket_created = on_ticket_created;
	relay_config.on_peer_info = on_peer_info;
	relay_config.on_disconnected = on_relay_disconnected;
	relay_config.callback_context = NULL;

	RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
	if (!relay_client)
	{
		LOG_ERROR("Error: Failed to create relay client\n");
		return 1;
	}

	// Discover endpoints
	LOG("[Receive] Discovering network endpoints...\n");
	ENDPOINT endpoints[MAX_ENDPOINTS];
	uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);
	
	if (endpoint_count == 0)
	{
		memset(&endpoints[0], 0, sizeof(ENDPOINT));
		endpoints[0].addr_type = 0x04;
		endpoints[0].addr[0] = 127;
		endpoints[0].addr[3] = 1;
		endpoints[0].port = WORMHOLE_DEFAULT_PORT;
		endpoint_count = 1;
	}
	else
	{
		// Set port for all discovered endpoints
		for (uint16_t i = 0; i < endpoint_count; i++)
		{
			endpoints[i].port = WORMHOLE_DEFAULT_PORT;
		}
	}
	
	// Register with relay (with retry for UDP packet loss)
	LOG("[Receive] Registering with relay server...\n");
	
	const int MAX_REGISTER_ATTEMPTS = 3;
	BOOLEAN registered = FALSE;
	
	for (int attempt = 0; attempt < MAX_REGISTER_ATTEMPTS && !registered; attempt++)
	{
		if (attempt > 0)
		{
			LOG("[Receive] Registration attempt %d/%d...\n", attempt + 1, MAX_REGISTER_ATTEMPTS);
		}
		
		if (!RelayClient_Register(relay_client, endpoints, endpoint_count))
		{
			LOG_ERROR("Error: Failed to send REGISTER message\n");
			continue;
		}
		
		// Wait for registration response (5 seconds per attempt)
		for (int i = 0; i < 50; i++)
		{
			if (RelayClient_IsConnected(relay_client))
			{
				registered = TRUE;
				break;
			}
			RelayClient_Poll(relay_client, 100);
		}
	}
	
	if (!registered)
	{
		LOG_ERROR("Error: Failed to connect to relay after %d attempts (timeout)\n", MAX_REGISTER_ATTEMPTS);
		RelayClient_Destroy(relay_client);
		return 1;
	}
	
	// Lookup sender
	LOG("[Receive] Looking up sender...\n");
	if (!RelayClient_LookupTicket(relay_client, ticket))
	{
		LOG_ERROR("Error: Failed to lookup ticket\n");
		RelayClient_Destroy(relay_client);
		return 1;
	}
	
	// Wait for peer info (15 seconds timeout: 150 iterations × 100ms)
	for (int i = 0; i < 150 && !g_peer_info_ready; i++)
	{
		RelayClient_Poll(relay_client, 100);
	}
	
	if (!g_peer_info_ready)
	{
		LOG_ERROR("Error: Ticket not found or expired\n");
		RelayClient_Destroy(relay_client);
		return 1;
	}
	
	// Print peer info
	char peer_id_hex[65];
	PeerID_ToHex(g_peer_id, peer_id_hex);
	LOG("\n[Receive] Sender PeerID: %s\n", peer_id_hex);
	LOG("[Receive] Sender has %u endpoints\n\n", g_peer_endpoint_count);
	
	// Debug: Log all received endpoints
	LOG("[Receive] [DEBUG] Received %u endpoints from relay:\n", g_peer_endpoint_count);
	for (uint16_t i = 0; i < g_peer_endpoint_count; i++)
	{
		char ep_ip_str[INET6_ADDRSTRLEN];
		uint16_t ep_port;
		if (Endpoint_ToString(&g_peer_endpoints[i], ep_ip_str, sizeof(ep_ip_str), &ep_port))
		{
			LOG("[Receive] [DEBUG]   EP[%u]: %s:%u (priority=%u, port_raw=0x%04x)\n",
				i, ep_ip_str, ep_port, g_peer_endpoints[i].priority, g_peer_endpoints[i].port);
		}
	}
	
	// Send hole-punch probes to sender's endpoints before closing relay.
	// Send multiple rounds (matching sender's 5 rounds × 200ms) to handle packet loss
	// and give the sender time to start probing back.
	LOG("[Receive] Sending hole-punch probes to sender's %u endpoints (5 rounds)...\n",
		g_peer_endpoint_count);
	for (int probe_round = 0; probe_round < 5; probe_round++)
	{
		send_hole_punch_probes(relay_client, g_peer_endpoints, g_peer_endpoint_count);
		if (probe_round < 4)
			Sleep(200);  // 200ms between rounds, skip sleep after last round
	}
	LOG("[Receive] Hole-punch probing complete (5 rounds sent)\n");

	// Close relay client to free port 4567 for MsQuic.
	// The NAT mapping persists briefly (~30-120s) after socket close,
	// giving MsQuic time to reuse the same port for QUIC connections.
	LOG("[Receive] Closing relay connection (freeing port for QUIC)...\n");
	RelayClient_SendGoodbye(relay_client, 0x00);  // 0x00 = upgrading to direct
	RelayClient_Destroy(relay_client);
	relay_client = NULL;

	// Get Downloads folder path
	char downloads_path[MAX_PATH];
	if (!GetDownloadsPath(downloads_path, sizeof(downloads_path)))
	{
		LOG_ERROR("Error: Failed to get Downloads folder path\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	LOG("[Receive] Files will be saved to: %s\n\n", downloads_path);

	// Initialize MsQuic
	if (!InitializeMsQuic())
	{
		LOG_ERROR("Error: Failed to initialize MsQuic\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Load client configuration
	if (!ClientLoadConfiguration())
	{
		LOG_ERROR("Error: Failed to load QUIC client configuration\n");
		CleanupMsQuic();
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	
	// Create client context
	RECEIVE_CLIENT_CONTEXT client_ctx;
	client_ctx.connected = FALSE;
	client_ctx.connect_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	client_ctx.transfer_done_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	strncpy_s(client_ctx.downloads_path, sizeof(client_ctx.downloads_path), downloads_path, _TRUNCATE);
	
	if (!client_ctx.connect_event || !client_ctx.transfer_done_event)
	{
		LOG_ERROR("Error: Failed to create events\n");
		CleanupMsQuic();
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	
	// =========================================================================
	// Parallel Connection Race (with retries)
	// Try ALL endpoints simultaneously. If all fail, wait and retry.
	// The sender may need a few seconds to transition from relay to QUIC listener.
	// =========================================================================
	HQUIC connection = NULL;
	BOOLEAN connected = FALSE;

	const int max_race_attempts = 4;      // Up to 4 attempts
	const int race_timeout_ms = 5000;     // 5 seconds per attempt
	const int retry_delay_ms = 3000;      // 3 seconds between retries

	for (int attempt = 0; attempt < max_race_attempts && !connected; attempt++)
	{
		if (attempt > 0)
		{
			LOG("[Receive] Retrying connection (attempt %d/%d) in %d seconds...\n",
				attempt + 1, max_race_attempts, retry_delay_ms / 1000);
			Sleep(retry_delay_ms);
		}

		LOG("[Receive] Starting parallel connection race to %u endpoints (attempt %d/%d)...\n",
			g_peer_endpoint_count, attempt + 1, max_race_attempts);

		PARALLEL_CONNECT_CTX par_ctx;
		memset(&par_ctx, 0, sizeof(par_ctx));
		par_ctx.client_ctx = &client_ctx;
		par_ctx.race_event = CreateEvent(NULL, TRUE, FALSE, NULL);  // Manual-reset
		par_ctx.race_won = 0;
		par_ctx.count = 0;

		if (!par_ctx.race_event)
		{
			LOG_ERROR("Error: Failed to create race event\n");
			break;
		}

		// Open and start ALL connections simultaneously
		for (uint16_t i = 0; i < g_peer_endpoint_count; i++)
		{
			const ENDPOINT* ep = &g_peer_endpoints[i];

			char ip_str[INET6_ADDRSTRLEN];
			uint16_t port;
			if (!Endpoint_ToString(ep, ip_str, sizeof(ip_str), &port))
			{
				par_ctx.connections[i] = NULL;
				continue;
			}

			LOG("[Receive]   [%u] %s:%u (priority %u) - starting...\n",
				i, ip_str, port, ep->priority);

			par_ctx.per_conn[i].index = i;
			par_ctx.per_conn[i].shared = &par_ctx;

			QUIC_STATUS status = MsQuic->ConnectionOpen(
				Registration,
				RaceConnectionCallback,
				&par_ctx.per_conn[i],
				&par_ctx.connections[i]
			);

			if (QUIC_FAILED(status))
			{
				LOG("[Receive]   [%u] Failed to open connection: 0x%x\n", i, status);
				par_ctx.connections[i] = NULL;
				continue;
			}

			// Only bind to port 4567 for public IP / relay endpoints where
			// NAT hole punching requires reusing the relay's NAT mapping.
			// LAN (priority 0) and IPv6 (priority 75) don't traverse NAT.
			if (ep->priority >= 100) {
				QUIC_ADDR local_addr = {0};
				QuicAddrSetFamily(&local_addr,
					ep->addr_type == 0x06 ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET);
				QuicAddrSetPort(&local_addr, WORMHOLE_DEFAULT_PORT);
				QUIC_STATUS bind_status = MsQuic->SetParam(
					par_ctx.connections[i],
					QUIC_PARAM_CONN_LOCAL_ADDRESS,
					sizeof(local_addr),
					&local_addr);
				if (QUIC_FAILED(bind_status)) {
					LOG("[Receive]   [%u] QUIC could not bind to port %d (0x%x), using ephemeral port\n",
						i, WORMHOLE_DEFAULT_PORT, bind_status);
				} else {
					LOG("[Receive]   [%u] QUIC bound to local port %d (reusing NAT mapping)\n",
						i, WORMHOLE_DEFAULT_PORT);
				}
			}

			// Start connection (non-blocking - result delivered via RaceConnectionCallback)
			status = MsQuic->ConnectionStart(
				par_ctx.connections[i],
				ClientConfiguration,
				ep->addr_type == 0x06 ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET,
				ip_str,
				port
			);

			if (QUIC_FAILED(status))
			{
				LOG("[Receive]   [%u] Failed to start connection: 0x%x\n", i, status);
				MsQuic->ConnectionClose(par_ctx.connections[i]);
				par_ctx.connections[i] = NULL;
				continue;
			}

			par_ctx.count++;
		}

		if (par_ctx.count == 0)
		{
			LOG_ERROR("[Receive] Error: No connections could be started\n");
			CloseHandle(par_ctx.race_event);
			break;
		}

		LOG("[Receive] All %u connections started, waiting for first success (%ds timeout)...\n",
			par_ctx.count, race_timeout_ms / 1000);

		// Wait for ANY connection to succeed
		{
			DWORD wait_result = WaitForSingleObject(par_ctx.race_event, race_timeout_ms);

			if (wait_result == WAIT_OBJECT_0 && par_ctx.winning_connection != NULL)
			{
				connection = par_ctx.winning_connection;
				connected = TRUE;

				char win_ip[INET6_ADDRSTRLEN];
				uint16_t win_port;
				Endpoint_ToString(&g_peer_endpoints[par_ctx.winning_index],
					win_ip, sizeof(win_ip), &win_port);
				LOG("[Receive] Connected via endpoint %u: %s:%u (priority %u)\n",
					par_ctx.winning_index, win_ip, win_port,
					g_peer_endpoints[par_ctx.winning_index].priority);
			}
		}

		// Clean up all non-winning connections
		for (uint16_t i = 0; i < g_peer_endpoint_count; i++)
		{
			if (par_ctx.connections[i] && par_ctx.connections[i] != connection)
			{
				MsQuic->ConnectionShutdown(
					par_ctx.connections[i],
					QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
				MsQuic->ConnectionClose(par_ctx.connections[i]);
			}
		}

		CloseHandle(par_ctx.race_event);

		if (!connected && attempt < max_race_attempts - 1)
		{
			LOG("[Receive] Attempt %d failed, sender may still be starting QUIC listener...\n",
				attempt + 1);
		}
	}
	
	if (!connected)
	{
		// Count endpoint types attempted for detailed error message
		uint16_t lan_count = 0, ipv6_count = 0, public_count = 0, relay_count = 0;
		for (uint16_t i = 0; i < g_peer_endpoint_count; i++)
		{
			if (g_peer_endpoints[i].priority == 0)
			{
				lan_count++;
			}
			else if (g_peer_endpoints[i].priority == 75)
			{
				ipv6_count++;
			}
			else if (g_peer_endpoints[i].priority == 100)
			{
				public_count++;
			}
			else if (g_peer_endpoints[i].priority >= 200)
			{
				relay_count++;
			}
		}
		
		LOG("\n");
		LOG("╔══════════════════════════════════════════════════════════╗\n");
		LOG("║  CONNECTION FAILED - No Direct Path Available           ║\n");
		LOG("╠══════════════════════════════════════════════════════════╣\n");
		LOG("║                                                          ║\n");
		LOG("║  Attempted %u connection methods:                        ║\n", g_peer_endpoint_count);
		if (lan_count > 0)
			LOG("║   • %u LAN endpoints (same network)         - FAILED    ║\n", lan_count);
		if (ipv6_count > 0)
			LOG("║   • %u IPv6 endpoints                       - FAILED    ║\n", ipv6_count);
		if (public_count > 0)
			LOG("║   • %u Public IP hole punch attempts        - FAILED    ║\n", public_count);
		if (relay_count > 0)
			LOG("║   • %u Relay forwarding attempts            - FAILED    ║\n", relay_count);
		LOG("║                                                          ║\n");
		LOG("║  Possible causes:                                        ║\n");
		LOG("║   • Both peers behind Symmetric NAT (restrictive)       ║\n");
		LOG("║   • Firewall blocking UDP connections                   ║\n");
		LOG("║   • Mobile carrier blocking peer-to-peer traffic        ║\n");
		LOG("║                                                          ║\n");
		LOG("║  Solutions:                                              ║\n");
		LOG("║   • Try from a different network                        ║\n");
		LOG("║   • Use a VPN with port forwarding enabled              ║\n");
		LOG("║   • Relay forwarding (coming soon)                      ║\n");
		LOG("║                                                          ║\n");
		LOG("╚══════════════════════════════════════════════════════════╝\n");
		
		CloseHandle(client_ctx.connect_event);
		CloseHandle(client_ctx.transfer_done_event);
		CleanupMsQuic();
		if (relay_client)
		{
			RelayClient_Destroy(relay_client);
		}
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	
	LOG("╔═══════════════════════════════════════════════════════════╗\n");
	LOG("║  CONNECTION ESTABLISHED                                   ║\n");
	LOG("║  Waiting for file transfer...                             ║\n");
	LOG("╚═══════════════════════════════════════════════════════════╝\n\n");
	
	// Wait for file transfer to complete (60 minute timeout for large files)
	DWORD wait_result = WaitForSingleObject(client_ctx.transfer_done_event, 3600000);
	
	BOOLEAN transfer_completed = FALSE;
	if (wait_result == WAIT_OBJECT_0)
	{
		LOG("\n[Receive] ✅ File transfer complete!\n");
		transfer_completed = TRUE;
	}
	else
	{
		LOG_ERROR("\n[Receive] ⚠️ File transfer timed out or failed\n");
	}
	
	// Cleanup
	if (connection)
	{
		MsQuic->ConnectionClose(connection);
	}
	CloseHandle(client_ctx.connect_event);
	CloseHandle(client_ctx.transfer_done_event);
	CleanupMsQuic();
	
	if (relay_client)
	{
		RelayClient_SendGoodbye(relay_client, transfer_completed ? 0x02 : 0x01);
		RelayClient_Destroy(relay_client);
	}
	
#ifdef _WIN32
	WSACleanup();
#endif
	
	LOG("\n[Receive] Session ended\n");
	return transfer_completed ? 0 : 1;
}

//=============================================================================
// Command-Line Parsing & Usage
//=============================================================================

static void PrintUsage(void)
{
	LOG("\nWormhole - Secure P2P File Transfer\n\n");
	LOG("Usage:\n");
	LOG("  wormhole.exe send <filename>      Send a file and get a ticket\n");
	LOG("  wormhole.exe receive <ticket>     Receive a file using a ticket\n");
	LOG("  wormhole.exe --help               Show this help message\n\n");
	LOG("Examples:\n");
	LOG("  wormhole.exe send document.pdf\n");
	LOG("  wormhole.exe receive 7-guitar-battery\n\n");
	LOG("Legacy commands (Phase 1 - for testing):\n");
	LOG("  wormhole.exe -server                             Start server mode\n");
	LOG("  wormhole.exe -client -target:<host> -file:<path> Client mode\n\n");
}

//=============================================================================
// Main Entry Point
//=============================================================================

int main(int argc, char *argv[])
{
	BOOLEAN success = FALSE;
	BOOLEAN is_server = FALSE;
	BOOLEAN is_client = FALSE;
	char *target_host = NULL;
	char *file_path = NULL;

	LOG("=== Wormhole - Secure P2P File Transfer ===\n\n");

	// Check for no arguments or help
	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}

	// Check for new send/receive commands first
	if (strcmp(argv[1], "send") == 0)
	{
		if (argc < 3)
		{
			LOG_ERROR("ERROR: Missing filename\n");
			PrintUsage();
			return 1;
		}
		return cmd_send(argv[2]);
	}
	else if (strcmp(argv[1], "receive") == 0)
	{
		if (argc < 3)
		{
			LOG_ERROR("ERROR: Missing ticket\n");
			PrintUsage();
			return 1;
		}
		return cmd_receive(argv[2]);
	}
	else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
	{
		PrintUsage();
		return 0;
	}

	// Legacy command-line parsing (Phase 1 compatibility)
	for (int i = 1; i < argc; i++)
	{
		if (_stricmp(argv[i], "-server") == 0)
		{
			is_server = TRUE;
		}
		else if (_stricmp(argv[i], "-client") == 0)
		{
			is_client = TRUE;
		}
		else if (strncmp(argv[i], "-target:", 8) == 0)
		{
			target_host = argv[i] + 8;
		}
		else if (strncmp(argv[i], "-file:", 6) == 0)
		{
			file_path = argv[i] + 6;
		}
	}

	// Validate legacy arguments
	if (!is_server && !is_client)
	{
		LOG_ERROR("ERROR: Unknown command or must specify either -server or -client\n\n");
		PrintUsage();
		return 1;
	}

	if (is_server && is_client)
	{
		LOG_ERROR("ERROR: Cannot specify both -server and -client\n\n");
		PrintUsage();
		return 1;
	}

	if (is_client && (target_host == NULL || file_path == NULL))
	{
		LOG_ERROR("ERROR: Client mode requires -target:<host> and -file:<path>\n\n");
		PrintUsage();
		return 1;
	}

	// Initialize MsQuic
	if (!InitializeMsQuic())
	{
		LOG_ERROR("ERROR: Failed to initialize MsQuic\n");
		return 1;
	}

	// Run server or client (legacy mode)
	if (is_server)
	{
		if (ServerLoadConfiguration())
		{
			success = RunServer();
		}
	}
	else  // is_client
	{
		if (ClientLoadConfiguration())
		{
			success = RunClient(target_host, file_path);
		}
	}

	// Cleanup
	CleanupMsQuic();

	LOG("\n=== Wormhole - Exit ===\n");
	return success ? 0 : 1;
}
