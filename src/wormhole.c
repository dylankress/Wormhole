//
// wormhole.c
// by Dylan Kress
//

#include "common.h"
#include "protocol.h"
#include "wire_format.h"
#include "stream.h"
#include "file_io.h"
#include "crypto.h"
#include "chunker.h"
#include "config.h"
#include "ipc.h"
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"
#include "relay/ticket.h"
#include "relay_forwarder.h"

#ifndef _WIN32
#include <fcntl.h>
#include <arpa/inet.h>
#endif

// Global MsQuic state - accessible via extern in other files
const QUIC_API_TABLE *MsQuic = NULL;
HQUIC Registration = NULL;
HQUIC ServerConfiguration = NULL;
HQUIC ClientConfiguration = NULL;

// Relay configuration
#define DEFAULT_RELAY_HOST "wormholerelay.com"
#define DEFAULT_RELAY_PORT 443

#define WORMHOLE_VERSION "0.8.0"

// IPC pipe name (set by --daemon flag, defaults to port-derived name)
static char g_ipc_pipe[256] = {0};

//=============================================================================
// Ctrl+C Cleanup
//=============================================================================

// Forward-declare for use in ConsoleCtrlHandler
static void CleanupMsQuic(void);

// Global state for clean Ctrl+C shutdown
static volatile int32_t g_shutdown_requested = 0;
static RELAY_CLIENT* g_active_relay_client = NULL;
static HQUIC g_active_listener = NULL;
static FILE_MANIFEST* g_active_manifest = NULL;
static HQUIC g_active_connection = NULL;   // Active QUIC connection for Ctrl+C cleanup
static RELAY_FORWARDER* g_active_relay_forwarder = NULL;

#ifdef _WIN32
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type)
{
	if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT)
	{
#else
static void SignalHandler(int sig)
{
	(void)sig;
	{
#endif
		if (WH_ATOMIC_CAS(&g_shutdown_requested, 0, 1) == 0)
		{
			LOG("\n[Shutdown] Ctrl+C received, cleaning up...\n");

			// Gracefully disconnect from relay
			if (g_active_relay_client)
			{
				RelayClient_SendGoodbye(g_active_relay_client, 0x01);
				RelayClient_Destroy(g_active_relay_client);
				g_active_relay_client = NULL;
			}

			// Stop QUIC listener if active
			if (g_active_listener && MsQuic)
			{
				MsQuic->ListenerStop(g_active_listener);
				MsQuic->ListenerClose(g_active_listener);
				g_active_listener = NULL;
			}

			// Gracefully shut down active QUIC connection (sends CONNECTION_CLOSE)
			if (g_active_connection && MsQuic)
			{
				MsQuic->ConnectionShutdown(g_active_connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
				WH_SLEEP_MS(200);  // Let CONNECTION_CLOSE frame be transmitted
				g_active_connection = NULL;
			}

			// Clean up MsQuic
			CleanupMsQuic();

			// Free manifest
			if (g_active_manifest)
			{
				Manifest_Destroy(g_active_manifest);
				g_active_manifest = NULL;
			}

			// Stop relay forwarder before WSACleanup to avoid WSANOTINITIALISED
			if (g_active_relay_forwarder)
			{
				RelayForwarder_Stop(g_active_relay_forwarder);
				g_active_relay_forwarder = NULL;
			}

#ifdef _WIN32
			WSACleanup();
#endif
			LOG("[Shutdown] Cleanup complete\n");
		}
#ifdef _WIN32
		return TRUE;  // Handled
	}
	return FALSE;
}
#else
	}
}
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

static BOOLEAN InitializeMsQuic(void);
static void CleanupMsQuic(void);
static BOOLEAN ServerLoadConfiguration(void);
static BOOLEAN ClientLoadConfiguration(void);
static int cmd_send(const char *filepath);
static int cmd_receive(const char *ticket);
static int cmd_store(const char *filepath);
static int cmd_get(int argc, char *argv[]);
static int cmd_delete(const char *file_id, BOOLEAN force);
static int cmd_export_key(const char *file_id);
static int cmd_import_key(const char *file_id, const char *hex_key);
static int cmd_files(int argc, char *argv[]);
static int cmd_status(void);
static int cmd_config(int argc, char *argv[]);
static int cmd_daemon(int argc, char *argv[]);
static int cmd_peers(void);
static void PrintUsage(void);

//=============================================================================
// 0-RTT Session Ticket Helpers
//=============================================================================

static BOOLEAN SaveSessionTicket(const uint8_t *ticket, uint32_t len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home) return FALSE;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s" PATH_SEP_STR ".wormhole" PATH_SEP_STR "session_ticket", home);
    FILE *f = fopen(path, "wb");
    if (!f) return FALSE;
    fwrite(&len, sizeof(len), 1, f);
    fwrite(ticket, 1, len, f);
    fclose(f);
    return TRUE;
}

static uint8_t *LoadSessionTicket(uint32_t *out_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home) return NULL;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s" PATH_SEP_STR ".wormhole" PATH_SEP_STR "session_ticket", home);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint32_t len;
    if (fread(&len, sizeof(len), 1, f) != 1 || len > 65536) { fclose(f); return NULL; }
    uint8_t *ticket = (uint8_t *)malloc(len);
    if (!ticket || fread(ticket, 1, len, f) != len) { free(ticket); fclose(f); return NULL; }
    fclose(f);
    *out_len = len;
    return ticket;
}

// Helper: check if path is a directory
#ifndef _WIN32
#include <sys/stat.h>
#endif
static BOOLEAN IsDirectory(const char *path)
{
#ifdef _WIN32
	DWORD attrs = GetFileAttributesA(path);
	return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

// Send/Receive command callbacks
static QUIC_STATUS QUIC_API ServerListenerCallback_Send(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event);
static QUIC_STATUS QUIC_API ServerConnectionCallback_Send(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
static QUIC_STATUS QUIC_API ClientConnectionCallback_Receive(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);

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
		QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT  // Optimize for bulk file transfer
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
#ifdef _WIN32
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
#else
	char cert_path_buf[MAX_PATH];
	if (!GenerateSelfSignedCert(cert_path_buf, sizeof(cert_path_buf)))
	{
		LOG_ERROR("[server] ERROR: Failed to generate/retrieve certificate\n");
		return FALSE;
	}

	char cert_path[MAX_PATH], key_path[MAX_PATH];
	if (!Crypto_GetCertPaths(cert_path, sizeof(cert_path), key_path, sizeof(key_path)))
	{
		LOG_ERROR("[server] ERROR: Failed to determine cert paths\n");
		return FALSE;
	}

	LOG("[server] Using PEM certificate: %s\n", cert_path);

	QUIC_CERTIFICATE_FILE cert_file;
	memset(&cert_file, 0, sizeof(cert_file));
	cert_file.CertificateFile = cert_path;
	cert_file.PrivateKeyFile = key_path;

	QUIC_CREDENTIAL_CONFIG cred_config;
	memset(&cred_config, 0, sizeof(cred_config));
	cred_config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
	cred_config.CertificateFile = &cert_file;
	cred_config.Flags = QUIC_CREDENTIAL_FLAG_NONE;
#endif

	// Step 3: Create QUIC settings
	QUIC_SETTINGS settings = { 0 };
	settings.IdleTimeoutMs = 30000;   // 30 seconds — detect dead peer promptly (keepalive is 10s)
	settings.IsSet.IdleTimeoutMs = TRUE;
	settings.DisconnectTimeoutMs = 30000;  // Match idle timeout — cache recovery can block callbacks
	settings.IsSet.DisconnectTimeoutMs = TRUE;
	settings.KeepAliveIntervalMs = 10000;  // Send keep-alive every 10 seconds
	settings.IsSet.KeepAliveIntervalMs = TRUE;
	settings.ServerResumptionLevel = QUIC_SERVER_RESUME_AND_ZERORTT;
	settings.IsSet.ServerResumptionLevel = TRUE;
	settings.PeerBidiStreamCount = 1;  // Allow 1 bidirectional stream
	settings.IsSet.PeerBidiStreamCount = TRUE;

	// Flow control settings for large file transfers (CRITICAL for throughput!)
	// Based on Iroh's proven high-performance configuration
	// Receive window: How much data WE can buffer from peer
	settings.StreamRecvWindowDefault = 16777216;  // 16 MB (2^24, MUST be power of 2)
	settings.IsSet.StreamRecvWindowDefault = TRUE;
	
	// Disable send buffering — zero-copy mode. MsQuic uses our buffers directly
	// instead of copying each 256KB chunk. Safe because we free in SEND_COMPLETE.
	settings.SendBufferingEnabled = FALSE;
	settings.IsSet.SendBufferingEnabled = TRUE;
	
	// Connection-wide flow control
	settings.ConnFlowControlWindow = 67108864;  // 64 MB (2^26, 4x stream window)
	settings.IsSet.ConnFlowControlWindow = TRUE;
	
	// Initial window size — larger window for faster ramp-up on file transfers
	settings.InitialWindowPackets = 20;  // 2x default for faster start
	settings.IsSet.InitialWindowPackets = TRUE;

	// MTU settings: allow PMTUD up to standard Ethernet MTU
	settings.MinimumMtu = 1200;  // QUIC minimum (safe baseline)
	settings.IsSet.MinimumMtu = TRUE;
	settings.MaximumMtu = 1500;  // Standard Ethernet MTU, let PMTUD discover path MTU
	settings.IsSet.MaximumMtu = TRUE;

	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
	settings.IsSet.CongestionControlAlgorithm = TRUE;

	settings.EcnEnabled = TRUE;
	settings.IsSet.EcnEnabled = TRUE;

	LOG("[server] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on\n");

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
	settings.IdleTimeoutMs = 30000;   // 30 seconds — detect dead peer promptly (keepalive is 10s)
	settings.IsSet.IdleTimeoutMs = TRUE;
	settings.DisconnectTimeoutMs = 30000;  // Match idle timeout — cache recovery can block callbacks
	settings.IsSet.DisconnectTimeoutMs = TRUE;
	settings.KeepAliveIntervalMs = 10000;  // Send keep-alive every 10 seconds
	settings.IsSet.KeepAliveIntervalMs = TRUE;

	// Flow control settings for large file transfers (CRITICAL for throughput!)
	// Based on Iroh's proven high-performance configuration
	settings.StreamRecvWindowDefault = 16777216;  // 16 MB (2^24, MUST be power of 2)
	settings.IsSet.StreamRecvWindowDefault = TRUE;

	settings.SendBufferingEnabled = FALSE;  // Zero-copy: MsQuic uses our buffers directly
	settings.IsSet.SendBufferingEnabled = TRUE;

	settings.ConnFlowControlWindow = 67108864;  // 64 MB (2^26, 4x stream window)
	settings.IsSet.ConnFlowControlWindow = TRUE;

	settings.InitialWindowPackets = 20;  // 2x default for faster start
	settings.IsSet.InitialWindowPackets = TRUE;

	// MTU settings: allow PMTUD up to standard Ethernet MTU
	settings.MinimumMtu = 1200;  // QUIC minimum (safe baseline)
	settings.IsSet.MinimumMtu = TRUE;
	settings.MaximumMtu = 1500;  // Standard Ethernet MTU, let PMTUD discover path MTU
	settings.IsSet.MaximumMtu = TRUE;

	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
	settings.IsSet.CongestionControlAlgorithm = TRUE;

	settings.EcnEnabled = TRUE;
	settings.IsSet.EcnEnabled = TRUE;

	// Allow peer (server) to create bidirectional streams
	settings.PeerBidiStreamCount = 10;  // Allow up to 10 bidirectional streams from peer
	settings.IsSet.PeerBidiStreamCount = TRUE;
	settings.PeerUnidiStreamCount = 10;  // Allow up to 10 unidirectional streams from peer
	settings.IsSet.PeerUnidiStreamCount = TRUE;

	LOG("[client] Flow control: StreamRecv=16MB, ConnFlow=64MB, InitWindow=20pkts, SendBuffer=off, BBR, MTU=1200-1500, ECN=on, PeerStreams=10\n");

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
// CLI Commands (Send/Receive)
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
	FILE_MANIFEST* manifest;
	BOOLEAN receiver_connected;
	BOOLEAN transfer_started;
	BOOLEAN transfer_failed;        // Receiver disconnected without completing
	BOOLEAN peer_disconnected;      // Set by SHUTDOWN_INITIATED_BY_PEER/TRANSPORT
	BOOLEAN transfer_complete_received; // Set by stream.c when TRANSFER_COMPLETE msg arrives
	uint64_t peer_error_code;       // Error code from SHUTDOWN_INITIATED_BY_PEER (0 = graceful)
	WH_EVENT receiver_connect_event;
	WH_EVENT transfer_done_event;
	HQUIC connection;
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
			WH_EVENT_SET(ctx->receiver_connect_event);
			
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
			ctx->connection = Connection;
			g_active_connection = Connection;

			LOG("[Send] Receiver connected! Starting chunk-based file transfer...\n");

			// Start sending file using chunk protocol
			ChunkSendFile(Connection, ctx->filepath, ctx->manifest, ctx->transfer_done_event,
				&ctx->transfer_complete_received);

			return QUIC_STATUS_SUCCESS;
		}
		
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
	{
		LOG("[Send] Receiver disconnected (error: 0x%llx)\n",
			(unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		ctx->peer_error_code = Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
		ctx->peer_disconnected = TRUE;
		return QUIC_STATUS_SUCCESS;
	}

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
	{
		LOG("[Send] Connection lost (status: 0x%x)\n", Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
		ctx->peer_disconnected = TRUE;
		return QUIC_STATUS_SUCCESS;
	}

	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
	{
		LOG("[Send] Connection shutdown complete\n");
		MsQuic->ConnectionClose(Connection);
		g_active_connection = NULL;

		// If receiver disconnected (not sender-initiated cleanup), check if transfer actually completed
		if (ctx->peer_disconnected && ctx->transfer_started && !ctx->transfer_failed)
		{
			if (ctx->transfer_complete_received)
			{
				// Genuine success — TRANSFER_COMPLETE was received before connection closed
				WH_EVENT_SET(ctx->transfer_done_event);
			}
			else
			{
				// Receiver disconnected without completing (Ctrl+C or crash)
				printf("\n[sender] Receiver disconnected. Waiting for reconnection...\n");
				ctx->receiver_connected = FALSE;
				ctx->transfer_started = FALSE;
				ctx->connection = NULL;
				ctx->peer_disconnected = FALSE;
				ctx->transfer_failed = TRUE;
				WH_EVENT_SET(ctx->transfer_done_event);
			}
		}
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
	WH_EVENT connect_event;
	WH_EVENT transfer_done_event;
	char downloads_path[MAX_PATH];
	CHUNK_RECEIVE_CONTEXT *chunk_recv_ctx;
} RECEIVE_CLIENT_CONTEXT;

// Client connection callback for cmd_receive (chunk protocol)
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
			g_active_connection = Connection;
			WH_EVENT_SET(ctx->connect_event);
			return QUIC_STATUS_SUCCESS;
		}

		case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		{
			LOG("[Receive] Connection shutdown complete\n");
			return QUIC_STATUS_SUCCESS;
		}

		case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		{
			const uint8_t *ticket = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket;
			uint32_t ticket_len = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength;
			SaveSessionTicket(ticket, ticket_len);
			LOG("[client] Session ticket saved (%u bytes) for 0-RTT\n", ticket_len);
			return QUIC_STATUS_SUCCESS;
		}

		case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		{
			HQUIC stream = Event->PEER_STREAM_STARTED.Stream;
			BOOLEAN is_bidi = !(Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL);

			if (is_bidi)
			{
				// Bidirectional stream = control stream from sender
				LOG("[Receive] Control stream started (bidirectional)\n");

				// Create chunk receive context
				CHUNK_RECEIVE_CONTEXT* recv_ctx = (CHUNK_RECEIVE_CONTEXT*)calloc(1, sizeof(CHUNK_RECEIVE_CONTEXT));
				if (!recv_ctx)
				{
					return QUIC_STATUS_OUT_OF_MEMORY;
				}

				recv_ctx->control_stream = stream;
				recv_ctx->transfer_complete_event = ctx->transfer_done_event;
				recv_ctx->user_context = ctx;
				strncpy_s(recv_ctx->downloads_path, sizeof(recv_ctx->downloads_path),
					ctx->downloads_path, _TRUNCATE);
				ctx->chunk_recv_ctx = recv_ctx;

				// Attach control stream callback
				MsQuic->SetCallbackHandler(stream, (void*)ReceiverControlStreamCallback, recv_ctx);

				// Send MANIFEST_REQUEST to sender
				LOG("[Receive] Sending MANIFEST_REQUEST...\n");
				uint32_t frame_size = CTRL_HEADER_SIZE;
				uint8_t *buffer = (uint8_t *)malloc(frame_size);
				if (buffer)
				{
					buffer[0] = CTRL_MSG_MANIFEST_REQUEST;
					// payload_length = 0
					buffer[1] = 0; buffer[2] = 0; buffer[3] = 0; buffer[4] = 0;

					QUIC_BUFFER *quic_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
					if (quic_buf)
					{
						quic_buf->Buffer = buffer;
						quic_buf->Length = frame_size;

						QUIC_STATUS status = MsQuic->StreamSend(stream, quic_buf, 1,
							QUIC_SEND_FLAG_NONE, quic_buf);
						if (QUIC_FAILED(status))
						{
							LOG_ERROR("[Receive] ERROR: Failed to send MANIFEST_REQUEST: 0x%x\n", status);
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
				// Unidirectional stream = data stream from sender
				LOG("[Receive] Data stream started (unidirectional)\n");

				if (ctx->chunk_recv_ctx)
				{
					ctx->chunk_recv_ctx->data_stream = stream;
					MsQuic->SetCallbackHandler(stream, (void*)ReceiverDataStreamCallback, ctx->chunk_recv_ctx);
				}
				else
				{
					LOG_ERROR("[Receive] ERROR: Data stream started before control stream\n");
					MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
				}
			}

			return QUIC_STATUS_SUCCESS;
		}

		default:
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
	WH_EVENT race_event;
	volatile int32_t race_won;
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
			if (WH_ATOMIC_CAS(&shared->race_won, 0, 1) == 0)
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
				WH_EVENT_SET(shared->race_event);
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
	// Install Ctrl+C handler for clean shutdown
#ifdef _WIN32
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = SignalHandler;
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
	}
#endif

	LOG("\n═══════════════════════════════════════\n");
	LOG("  WORMHOLE SEND\n");
	LOG("═══════════════════════════════════════\n\n");

	BOOLEAN is_directory = IsDirectory(filepath);

	// Check if file/directory exists
	if (!is_directory && !FileExists(filepath))
	{
		LOG_ERROR("Error: File not found: %s\n", filepath);
		return 1;
	}

	FILE_MANIFEST* manifest = NULL;
	char* filename = NULL;
	uint32_t filename_len = 0;
	uint64_t filesize = 0;

	if (is_directory)
	{
		// Multi-file directory transfer (manifest version 2)
		LOG("[Send] Directory: %s\n", filepath);
		LOG("[Send] Building directory manifest...\n");
		manifest = Chunker_BuildManifestFromDirectory(filepath);
		if (!manifest)
		{
			LOG_ERROR("Error: Failed to build directory manifest\n");
			return 1;
		}
		filesize = manifest->file_size;
		LOG("[Send] Manifest v2: %u files, %u chunks, %llu bytes total (Blake3 hashed)\n",
			manifest->file_count, manifest->chunk_count,
			(unsigned long long)manifest->file_size);
		// Use directory name as the display name
		ExtractFilename(filepath, &filename, &filename_len);
	}
	else
	{
		// Single-file transfer (manifest version 1)
		filesize = 0;
		if (!GetWormholeFileSize(filepath, &filesize))
		{
			LOG_ERROR("Error: Failed to get file size\n");
			return 1;
		}

		ExtractFilename(filepath, &filename, &filename_len);
		if (!filename)
		{
			LOG_ERROR("Error: Failed to extract filename\n");
			return 1;
		}

		LOG("[Send] File: %s\n", filename);
		LOG("[Send] Size: %llu bytes\n\n", (unsigned long long)filesize);

		LOG("[Send] Building file manifest...\n");
		manifest = Chunker_BuildManifest(filepath);
		if (!manifest)
		{
			LOG_ERROR("Error: Failed to build file manifest\n");
			free(filename);
			return 1;
		}
		LOG("[Send] Manifest: %u chunks of %u bytes (Blake3 hashed)\n",
			manifest->chunk_count, manifest->chunk_size);
	}
	
#ifdef _WIN32
	// Initialize Winsock on Windows
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		LOG_ERROR("Error: Failed to initialize Winsock\n");
		Manifest_Destroy(manifest);
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
		Manifest_Destroy(manifest);
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
		Manifest_Destroy(manifest);
		free(filename);
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
	relay_config.on_peers_found = NULL;
	relay_config.callback_context = NULL;

	RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
	if (!relay_client)
	{
		LOG_ERROR("Error: Failed to create relay client\n");
#ifdef _WIN32
		WSACleanup();
#endif
		Manifest_Destroy(manifest);
		free(filename);
		return 1;
	}

	// Track active resources for Ctrl+C cleanup
	g_active_relay_client = relay_client;
	g_active_manifest = manifest;

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
		LOG("  Troubleshooting:\n");
		LOG("  - Check your internet connection\n");
		LOG("  - Verify DNS can resolve %s\n", DEFAULT_RELAY_HOST);
		LOG("  - Check if a firewall is blocking UDP port %d\n", DEFAULT_RELAY_PORT);
		LOG("  - The relay server at %s may be temporarily down\n", DEFAULT_RELAY_HOST);
		g_active_relay_client = NULL;
		RelayClient_Destroy(relay_client);
		Manifest_Destroy(manifest);
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
		Manifest_Destroy(manifest);
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
		Manifest_Destroy(manifest);
		free(filename);
		return 1;
	}
	
	// Display ticket
	Ticket_PrintForSharing(g_ticket, filename, filesize);
	printf("(ticket expires in 1 hour)\n");

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
	g_active_relay_client = NULL;

	// Initialize MsQuic for file transfer
	if (!InitializeMsQuic())
	{
		LOG_ERROR("Error: Failed to initialize MsQuic\n");
		Manifest_Destroy(manifest);
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
		Manifest_Destroy(manifest);
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// Create server context
	SEND_SERVER_CONTEXT server_ctx;
	server_ctx.filepath = filepath;
	server_ctx.manifest = manifest;
	server_ctx.receiver_connected = FALSE;
	server_ctx.transfer_started = FALSE;
	server_ctx.transfer_failed = FALSE;
	server_ctx.peer_disconnected = FALSE;
	server_ctx.peer_error_code = UINT64_MAX;
	server_ctx.connection = NULL;
	server_ctx.receiver_connect_event = WH_EVENT_CREATE();
	server_ctx.transfer_done_event = WH_EVENT_CREATE();

	if (!server_ctx.receiver_connect_event || !server_ctx.transfer_done_event)
	{
		LOG_ERROR("Error: Failed to create events\n");
		CleanupMsQuic();
		Manifest_Destroy(manifest);
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
		WH_EVENT_DESTROY(server_ctx.receiver_connect_event);
		WH_EVENT_DESTROY(server_ctx.transfer_done_event);
		CleanupMsQuic();
		Manifest_Destroy(manifest);
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
		WH_EVENT_DESTROY(server_ctx.receiver_connect_event);
		WH_EVENT_DESTROY(server_ctx.transfer_done_event);
		CleanupMsQuic();
		Manifest_Destroy(manifest);
		free(filename);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	LOG("[Send] QUIC server listening on port %d\n", WORMHOLE_DEFAULT_PORT);

	// Track listener for Ctrl+C cleanup
	g_active_listener = listener;

	// Start relay forwarder alongside QUIC listener for fallback connectivity.
	// This lets receivers behind symmetric NAT connect via relay.
	RELAY_FORWARDER* send_forwarder = NULL;
	if (g_peer_info_ready)
	{
		RELAY_FORWARDER_CONFIG fwd_cfg;
		memset(&fwd_cfg, 0, sizeof(fwd_cfg));
		fwd_cfg.relay_host = DEFAULT_RELAY_HOST;
		fwd_cfg.relay_port = DEFAULT_RELAY_PORT;
		fwd_cfg.keypair = &keypair;
		memcpy(fwd_cfg.remote_peer_id, g_peer_id, 32);
		fwd_cfg.our_endpoints = endpoints;
		fwd_cfg.our_endpoint_count = endpoint_count;
		fwd_cfg.local_quic_port = WORMHOLE_DEFAULT_PORT;

		send_forwarder = RelayForwarder_Start(&fwd_cfg);
		if (send_forwarder)
		{
			g_active_relay_forwarder = send_forwarder;
			LOG("[Send] Relay forwarder started (fallback for restricted NAT)\n");
		}
		else
		{
			LOG("[Send] Warning: Relay forwarder failed to start (direct-only mode)\n");
		}
	}

	// Phase 3: Wait for receiver to connect via QUIC
	BOOLEAN transfer_completed = FALSE;

	for (int i = 0; i < (timeout_ms / poll_interval_ms); i++)
	{
		if (g_shutdown_requested) break;
		WH_SLEEP_MS(poll_interval_ms);

		// Check if receiver connected via QUIC
		if (server_ctx.receiver_connected)
		{
			LOG("[Send] Receiver connected! Transferring file...\n");

			// Wait for transfer with periodic progress feedback (check every 5 seconds)
			const DWORD check_interval_ms = 5000;
			const DWORD max_transfer_ms = 3600000;  // 60 minutes max
			DWORD elapsed_ms = 0;

			while (elapsed_ms < max_transfer_ms && !g_shutdown_requested)
			{
				int wait_result = WH_EVENT_WAIT(server_ctx.transfer_done_event, check_interval_ms);
				if (wait_result == WH_EVENT_WAIT_OK)
				{
					if (server_ctx.transfer_failed)
					{
						// Receiver disconnected — will continue outer loop
						LOG("\n[Send] Waiting for receiver to reconnect...\n");
					}
					else
					{
						LOG("\n[Send] File transfer complete!\n");
						transfer_completed = TRUE;
					}
					break;
				}
				elapsed_ms += check_interval_ms;

				if (elapsed_ms % 30000 == 0)
				{
					LOG("[Send] Transfer in progress... (%u minutes elapsed)\n", elapsed_ms / 60000);
				}
			}

			if (transfer_completed)
			{
				break;  // Success — exit outer loop
			}
			else if (server_ctx.transfer_failed)
			{
				// Receiver disconnected — reset and wait for new receiver
				server_ctx.transfer_failed = FALSE;
				continue;  // Back to outer loop
			}
			else
			{
				// Timeout or shutdown — exit
				if (!g_shutdown_requested && elapsed_ms >= max_transfer_ms)
				{
					LOG_ERROR("\n[Send] File transfer timed out after 60 minutes\n");
					LOG("  Possible causes:\n");
					LOG("  - Network connection dropped\n");
					LOG("  - Receiver disconnected unexpectedly\n");
					LOG("  - File too large for current connection speed\n");
				}
				break;
			}
		}

		// Periodic waiting feedback (every 5 minutes)
		if (i > 0 && (i * poll_interval_ms) % 300000 == 0)
		{
			LOG("[Send] Still waiting for receiver... (%u minutes elapsed)\n",
				(i * poll_interval_ms) / 60000);
		}
	}

	if (!transfer_completed && !server_ctx.receiver_connected && !g_shutdown_requested)
	{
		LOG("\n[Send] Timeout: No receiver connected within 30 minutes\n");
		LOG("  The ticket may have expired. Try running 'wormhole send' again.\n");
	}

	// Cleanup
	g_active_listener = NULL;
	g_active_relay_client = NULL;
	g_active_manifest = NULL;

	if (send_forwarder)
	{
		if (g_active_relay_forwarder == send_forwarder)
			g_active_relay_forwarder = NULL;
		RelayForwarder_Stop(send_forwarder);
		send_forwarder = NULL;
	}

	g_active_connection = NULL;
	if (server_ctx.connection)
	{
		MsQuic->ConnectionShutdown(server_ctx.connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
		WH_SLEEP_MS(200);  // Let CONNECTION_CLOSE reach receiver
	}

	MsQuic->ListenerStop(listener);
	MsQuic->ListenerClose(listener);
	WH_EVENT_DESTROY(server_ctx.receiver_connect_event);
	WH_EVENT_DESTROY(server_ctx.transfer_done_event);
	CleanupMsQuic();

	Manifest_Destroy(manifest);
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
	// Install Ctrl+C handler for clean shutdown
#ifdef _WIN32
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = SignalHandler;
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
	}
#endif

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
	relay_config.local_port = 0;  // Ephemeral port — receiver is QUIC client, doesn't need fixed port
	relay_config.keypair = &keypair;
	relay_config.on_connected = on_relay_connected;
	relay_config.on_ticket_created = on_ticket_created;
	relay_config.on_peer_info = on_peer_info;
	relay_config.on_disconnected = on_relay_disconnected;
	relay_config.on_peers_found = NULL;
	relay_config.callback_context = NULL;

	RELAY_CLIENT* relay_client = RelayClient_Create(&relay_config);
	if (!relay_client)
	{
		LOG_ERROR("Error: Failed to create relay client\n");
		LOG("  Check your network connection and try again.\n");
		LOG("  If the problem persists, the relay server may be down.\n");
		return 1;
	}

	// Track for Ctrl+C cleanup
	g_active_relay_client = relay_client;

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
		LOG("  Troubleshooting:\n");
		LOG("  - Check your internet connection\n");
		LOG("  - Verify DNS can resolve %s\n", DEFAULT_RELAY_HOST);
		LOG("  - Check if a firewall is blocking UDP port %d\n", DEFAULT_RELAY_PORT);
		LOG("  - The relay server at %s may be temporarily down\n", DEFAULT_RELAY_HOST);
		g_active_relay_client = NULL;
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
		LOG("  - Make sure the sender is still running 'wormhole send'\n");
		LOG("  - Check that you typed the ticket correctly\n");
		LOG("  - Tickets expire after 1 hour\n");
		g_active_relay_client = NULL;
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
			WH_SLEEP_MS(200);  // 200ms between rounds, skip sleep after last round
	}
	LOG("[Receive] Hole-punch probing complete (5 rounds sent)\n");

	// Close relay client to free port 4567 for MsQuic.
	// The NAT mapping persists briefly (~30-120s) after socket close,
	// giving MsQuic time to reuse the same port for QUIC connections.
	LOG("[Receive] Closing relay connection (freeing port for QUIC)...\n");
	RelayClient_SendGoodbye(relay_client, 0x00);  // 0x00 = upgrading to direct
	RelayClient_Destroy(relay_client);
	relay_client = NULL;
	g_active_relay_client = NULL;

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
	memset(&client_ctx, 0, sizeof(client_ctx));
	client_ctx.connected = FALSE;
	client_ctx.connect_event = WH_EVENT_CREATE();
	client_ctx.transfer_done_event = WH_EVENT_CREATE();
	strncpy_s(client_ctx.downloads_path, sizeof(client_ctx.downloads_path), downloads_path, _TRUNCATE);
	client_ctx.chunk_recv_ctx = NULL;
	
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
			WH_SLEEP_MS(retry_delay_ms);
		}

		LOG("[Receive] Starting parallel connection race to %u endpoints (attempt %d/%d)...\n",
			g_peer_endpoint_count, attempt + 1, max_race_attempts);

		PARALLEL_CONNECT_CTX par_ctx;
		memset(&par_ctx, 0, sizeof(par_ctx));
		par_ctx.client_ctx = &client_ctx;
		par_ctx.race_event = WH_EVENT_CREATE();  // Manual-reset
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

			// Skip relay endpoints — they speak relay protocol, not QUIC.
			// The relay forwarder path handles QUIC-over-relay separately.
			if (ep->priority >= 200)
			{
				par_ctx.connections[i] = NULL;
				continue;
			}

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

			// Apply saved session ticket for 0-RTT resumption
			{
				uint32_t ticket_len;
				uint8_t *ticket = LoadSessionTicket(&ticket_len);
				if (ticket)
				{
					MsQuic->SetParam(par_ctx.connections[i],
						QUIC_PARAM_CONN_RESUMPTION_TICKET, ticket_len, ticket);
					free(ticket);
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
			WH_EVENT_DESTROY(par_ctx.race_event);
			break;
		}

		LOG("[Receive] All %u connections started, waiting for first success (%ds timeout)...\n",
			par_ctx.count, race_timeout_ms / 1000);

		// Wait for ANY connection to succeed
		{
			int wait_result = WH_EVENT_WAIT(par_ctx.race_event, race_timeout_ms);

			if (wait_result == WH_EVENT_WAIT_OK && par_ctx.winning_connection != NULL)
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

		WH_EVENT_DESTROY(par_ctx.race_event);

		if (!connected && attempt < max_race_attempts - 1)
		{
			LOG("[Receive] Attempt %d failed, sender may still be starting QUIC listener...\n",
				attempt + 1);
		}
	}
	
	// =========================================================================
	// Relay Forwarding Fallback
	// If direct connections failed, try tunneling QUIC through the relay.
	// =========================================================================
	RELAY_FORWARDER* recv_forwarder = NULL;

	if (!connected)
	{
		LOG("\n[Receive] Direct connections failed. Attempting relay-forwarded connection...\n");

		RELAY_FORWARDER_CONFIG fwd_cfg;
		memset(&fwd_cfg, 0, sizeof(fwd_cfg));
		fwd_cfg.relay_host = DEFAULT_RELAY_HOST;
		fwd_cfg.relay_port = DEFAULT_RELAY_PORT;
		fwd_cfg.keypair = &keypair;
		memcpy(fwd_cfg.remote_peer_id, g_peer_id, 32);
		fwd_cfg.our_endpoints = endpoints;
		fwd_cfg.our_endpoint_count = endpoint_count;
		fwd_cfg.local_quic_port = 0;  // Receiver mode: proxy provides port

		recv_forwarder = RelayForwarder_Start(&fwd_cfg);
		if (recv_forwarder)
			g_active_relay_forwarder = recv_forwarder;
		if (recv_forwarder && RelayForwarder_IsReady(recv_forwarder))
		{
			uint16_t proxy_port = RelayForwarder_GetProxyPort(recv_forwarder);
			LOG("[Receive] Relay forwarder started on 127.0.0.1:%u\n", proxy_port);

			// Connect MsQuic to the local proxy port
			HQUIC relay_conn = NULL;
			QUIC_STATUS status = MsQuic->ConnectionOpen(
				Registration,
				ClientConnectionCallback_Receive,
				&client_ctx,
				&relay_conn
			);

			if (QUIC_SUCCEEDED(status))
			{
				// Apply saved session ticket for 0-RTT
				{
					uint32_t ticket_len;
					uint8_t *ticket = LoadSessionTicket(&ticket_len);
					if (ticket)
					{
						MsQuic->SetParam(relay_conn,
							QUIC_PARAM_CONN_RESUMPTION_TICKET, ticket_len, ticket);
						free(ticket);
					}
				}

				status = MsQuic->ConnectionStart(
					relay_conn,
					ClientConfiguration,
					QUIC_ADDRESS_FAMILY_INET,
					"127.0.0.1",
					proxy_port
				);

				if (QUIC_SUCCEEDED(status))
				{
					LOG("[Receive] Connecting to sender via relay...\n");

					// Wait for connection (15 second timeout for relay path)
					int wait_result = WH_EVENT_WAIT(client_ctx.connect_event, 15000);
					if (wait_result == WH_EVENT_WAIT_OK && client_ctx.connected)
					{
						connection = relay_conn;
						connected = TRUE;
						LOG("[Receive] Connected via relay forwarding!\n");
					}
					else
					{
						LOG("[Receive] Relay-forwarded connection timed out\n");
						MsQuic->ConnectionShutdown(relay_conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
						MsQuic->ConnectionClose(relay_conn);
					}
				}
				else
				{
					LOG_ERROR("[Receive] Failed to start relay connection: 0x%x\n", status);
					MsQuic->ConnectionClose(relay_conn);
				}
			}
		}
		else
		{
			LOG("[Receive] Relay forwarder failed to start\n");
		}
	}

	if (!connected)
	{
		LOG("\n");
		LOG("╔══════════════════════════════════════════════════════════╗\n");
		LOG("║  CONNECTION FAILED                                       ║\n");
		LOG("╠══════════════════════════════════════════════════════════╣\n");
		LOG("║                                                          ║\n");
		LOG("║  All connection methods failed (direct + relay).         ║\n");
		LOG("║                                                          ║\n");
		LOG("║  Troubleshooting:                                        ║\n");
		LOG("║   - Is the sender still running 'wormhole send'?        ║\n");
		LOG("║   - Check firewall settings on both machines             ║\n");
		LOG("║   - Try from a different network                         ║\n");
		LOG("║   - Both peers may be behind restrictive NAT             ║\n");
		LOG("║                                                          ║\n");
		LOG("╚══════════════════════════════════════════════════════════╝\n");

		if (recv_forwarder)
		{
			if (g_active_relay_forwarder == recv_forwarder)
				g_active_relay_forwarder = NULL;
			RelayForwarder_Stop(recv_forwarder);
		}
		WH_EVENT_DESTROY(client_ctx.connect_event);
		WH_EVENT_DESTROY(client_ctx.transfer_done_event);
		CleanupMsQuic();
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	
	LOG("╔═══════════════════════════════════════════════════════════╗\n");
	LOG("║  CONNECTION ESTABLISHED                                   ║\n");
	LOG("║  Waiting for file transfer...                             ║\n");
	LOG("╚═══════════════════════════════════════════════════════════╝\n\n");
	
	// Wait for file transfer with periodic progress feedback
	BOOLEAN transfer_completed = FALSE;
	const DWORD check_interval_ms = 5000;
	const DWORD max_transfer_ms = 3600000;  // 60 minutes max
	DWORD elapsed_ms = 0;

	while (elapsed_ms < max_transfer_ms && !g_shutdown_requested)
	{
		int wait_result = WH_EVENT_WAIT(client_ctx.transfer_done_event, check_interval_ms);
		if (wait_result == WH_EVENT_WAIT_OK)
		{
			LOG("\n[Receive] File transfer complete!\n");
			transfer_completed = TRUE;
			break;
		}
		elapsed_ms += check_interval_ms;

		if (elapsed_ms % 30000 == 0)
		{
			LOG("[Receive] Transfer in progress... (%u minutes elapsed)\n", elapsed_ms / 60000);
		}
	}

	if (!transfer_completed && !g_shutdown_requested)
	{
		LOG_ERROR("\n[Receive] File transfer timed out or failed\n");
		LOG("  Possible causes:\n");
		LOG("  - Sender disconnected or lost network\n");
		LOG("  - Connection was interrupted by a firewall\n");
	}
	
	// Cleanup
	g_active_connection = NULL;
	if (connection)
	{
		MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
		WH_SLEEP_MS(200);  // Let CONNECTION_CLOSE reach sender
		MsQuic->ConnectionClose(connection);
	}
	WH_EVENT_DESTROY(client_ctx.connect_event);
	WH_EVENT_DESTROY(client_ctx.transfer_done_event);
	CleanupMsQuic();
	
	if (recv_forwarder)
	{
		if (g_active_relay_forwarder == recv_forwarder)
			g_active_relay_forwarder = NULL;
		RelayForwarder_Stop(recv_forwarder);
	}

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
// Thin Client Commands (communicate with wormholed daemon via IPC)
//=============================================================================

// Format a byte size as a human-readable string (e.g., "100 MB")
static void FormatSize(uint64_t bytes, char *buf, size_t buf_len)
{
	if (bytes >= 1024ULL * 1024 * 1024)
		snprintf(buf, buf_len, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
	else if (bytes >= 1024ULL * 1024)
		snprintf(buf, buf_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
	else if (bytes >= 1024)
		snprintf(buf, buf_len, "%.1f KB", (double)bytes / 1024.0);
	else
		snprintf(buf, buf_len, "%llu B", (unsigned long long)bytes);
}

//=============================================================================
// Daemon-mediated send/receive (CLI is thin client, daemon does the work)
//=============================================================================

static int cmd_send_via_daemon(const char *filepath)
{
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to daemon. Use --direct or start daemon first.\n");
		return 1;
	}

	// Subscribe to events for progress
	if (!IpcClient_Subscribe(client, IPC_EVENT_MASK_PROGRESS | IPC_EVENT_MASK_TRANSFER))
	{
		LOG_ERROR("Error: Failed to subscribe to daemon events\n");
		IpcClient_Disconnect(client);
		return 1;
	}

	// Build SEND payload: [2B path_len][filepath]
	uint32_t path_len = (uint32_t)strlen(filepath);
	uint32_t payload_size = 2 + path_len;
	uint8_t *payload = (uint8_t *)malloc(payload_size);
	if (!payload)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	WriteUint16LE(payload, (uint16_t)path_len);
	memcpy(payload + 2, filepath, path_len);

	uint8_t response[512];
	uint32_t resp_size = 0;
	uint32_t op_id = (uint32_t)time(NULL); // Simple unique ID

	BOOLEAN ok = IpcClient_SendCommandV2(client, IPC_CMD_SEND, op_id,
		payload, payload_size, response, sizeof(response), &resp_size);
	free(payload);

	if (!ok || resp_size < 1 || response[0] != IPC_STATUS_OK)
	{
		char errmsg[256] = {0};
		uint8_t status = 0;
		if (resp_size > 0)
			IpcReadErrorResponse(response, resp_size, &status, errmsg, sizeof(errmsg));
		LOG_ERROR("Error: Send failed%s%s\n", errmsg[0] ? ": " : "", errmsg);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t transfer_id = 0;
	if (resp_size >= 5)
		transfer_id = ReadUint32LE(response + 1);

	printf("Transfer started (ID: %u). Waiting for ticket...\n", transfer_id);

	// Poll for transfer events — wait for ticket, then progress, then completion
	BOOLEAN got_ticket = FALSE;
	BOOLEAN transfer_done = FALSE;
	time_t last_status_check = time(NULL);

	while (!transfer_done && !g_shutdown_requested)
	{
		uint8_t event_type = 0;
		uint32_t event_op_id = 0;
		uint8_t event_buf[512];
		uint32_t event_size = 0;

		if (IpcClient_ReadEvent(client, &event_type, &event_op_id,
			event_buf, sizeof(event_buf), &event_size, 1000))
		{
			if (event_type == IPC_EVENT_TRANSFER && event_size >= 7)
			{
				uint32_t eid = ReadUint32LE(event_buf);
				if (eid == transfer_id)
				{
					uint8_t state = event_buf[5];
					(void)event_buf[6]; // ev_status — reserved for future use

					if (state == 4) // TRANSFER_STATE_TRANSFERRING
					{
						if (!got_ticket)
						{
							// Query for ticket
							uint8_t ts_payload[4];
							WriteUint32LE(ts_payload, transfer_id);
							uint8_t ts_resp[512];
							uint32_t ts_size = 0;
							if (IpcClient_SendCommandV2(client, IPC_CMD_TRANSFER_STATUS, 0,
								ts_payload, 4, ts_resp, sizeof(ts_resp), &ts_size) &&
								ts_size > 30 && ts_resp[0] == IPC_STATUS_OK)
							{
								// Parse ticket from response
								uint32_t off = 1 + 4 + 1 + 1 + 8 + 8 + 4 + 4; // skip fixed fields
								if (off < ts_size) {
									uint8_t tl = ts_resp[off++];
									if (tl > 0 && off + tl <= ts_size) {
										char ticket[64] = {0};
										memcpy(ticket, ts_resp + off, tl);
										printf("\nTicket: %s\n\n", ticket);
										printf("On the receiving end, run:\n");
										printf("  wormhole receive %s\n\n", ticket);
										got_ticket = TRUE;
									}
								}
							}
						}
					}
					else if (state == 5 || state == 6 || state == 7)
					{
						// COMPLETED / FAILED / CANCELLED
						transfer_done = TRUE;
						if (state == 5)
							printf("\nTransfer completed successfully.\n");
						else if (state == 7)
							printf("\nTransfer cancelled.\n");
						else {
							// Parse error message
							printf("\nTransfer failed.\n");
						}
					}
				}
			}
			else if (event_type == IPC_EVENT_PROGRESS && event_op_id == transfer_id)
			{
				// Progress event — could print a progress bar here
				if (event_size >= IPC_PROGRESS_PAYLOAD_SIZE)
				{
					uint64_t bytes_done = ReadUint64LE(event_buf + 4);
					uint64_t bytes_total = ReadUint64LE(event_buf + 12);
					uint32_t chunks_done = ReadUint32LE(event_buf + 20);
					uint32_t chunks_total = ReadUint32LE(event_buf + 24);

					if (bytes_total > 0)
					{
						double pct = 100.0 * (double)bytes_done / (double)bytes_total;
						printf("\rSending: %u/%u chunks (%.1f%%)     ",
							chunks_done, chunks_total, pct);
						fflush(stdout);
						if (chunks_done == chunks_total)
							printf("\n");
					}
				}
			}
		}

		// Periodic status check if no events
		time_t now = time(NULL);
		if (now - last_status_check >= 5 && !got_ticket)
		{
			last_status_check = now;
			// Check if ticket is ready
			uint8_t ts_payload[4];
			WriteUint32LE(ts_payload, transfer_id);
			uint8_t ts_resp[512];
			uint32_t ts_size = 0;
			if (IpcClient_SendCommandV2(client, IPC_CMD_TRANSFER_STATUS, 0,
				ts_payload, 4, ts_resp, sizeof(ts_resp), &ts_size) &&
				ts_size > 30 && ts_resp[0] == IPC_STATUS_OK)
			{
				uint32_t off = 1 + 4 + 1 + 1 + 8 + 8 + 4 + 4;
				if (off < ts_size) {
					uint8_t tl = ts_resp[off++];
					if (tl > 0 && off + tl <= ts_size) {
						char ticket[64] = {0};
						memcpy(ticket, ts_resp + off, tl);
						printf("\nTicket: %s\n\n", ticket);
						printf("On the receiving end, run:\n");
						printf("  wormhole receive %s\n\n", ticket);
						got_ticket = TRUE;
					}
				}
			}
		}
	}

	IpcClient_Disconnect(client);
	return transfer_done ? 0 : 1;
}

static int cmd_receive_via_daemon(const char *ticket)
{
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to daemon. Use --direct or start daemon first.\n");
		return 1;
	}

	// Subscribe to events for progress
	if (!IpcClient_Subscribe(client, IPC_EVENT_MASK_PROGRESS | IPC_EVENT_MASK_TRANSFER))
	{
		LOG_ERROR("Error: Failed to subscribe to daemon events\n");
		IpcClient_Disconnect(client);
		return 1;
	}

	// Build RECEIVE payload: [1B ticket_len][ticket]
	uint8_t ticket_len = (uint8_t)strlen(ticket);
	uint32_t payload_size = 1 + ticket_len;
	uint8_t payload[128];
	payload[0] = ticket_len;
	memcpy(payload + 1, ticket, ticket_len);

	uint8_t response[512];
	uint32_t resp_size = 0;
	uint32_t op_id = (uint32_t)time(NULL);

	BOOLEAN ok = IpcClient_SendCommandV2(client, IPC_CMD_RECEIVE, op_id,
		payload, payload_size, response, sizeof(response), &resp_size);

	if (!ok || resp_size < 1 || response[0] != IPC_STATUS_OK)
	{
		char errmsg[256] = {0};
		uint8_t status = 0;
		if (resp_size > 0)
			IpcReadErrorResponse(response, resp_size, &status, errmsg, sizeof(errmsg));
		LOG_ERROR("Error: Receive failed%s%s\n", errmsg[0] ? ": " : "", errmsg);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t transfer_id = 0;
	if (resp_size >= 5)
		transfer_id = ReadUint32LE(response + 1);

	printf("Connecting to sender...\n");

	// Poll for transfer events
	BOOLEAN transfer_done = FALSE;

	while (!transfer_done && !g_shutdown_requested)
	{
		uint8_t event_type = 0;
		uint32_t event_op_id = 0;
		uint8_t event_buf[512];
		uint32_t event_size = 0;

		if (IpcClient_ReadEvent(client, &event_type, &event_op_id,
			event_buf, sizeof(event_buf), &event_size, 1000))
		{
			if (event_type == IPC_EVENT_TRANSFER && event_size >= 7)
			{
				uint32_t eid = ReadUint32LE(event_buf);
				if (eid == transfer_id)
				{
					uint8_t state = event_buf[5];

					if (state == 5) {
						transfer_done = TRUE;
						printf("\nTransfer completed successfully.\n");
					} else if (state == 6) {
						transfer_done = TRUE;
						printf("\nTransfer failed.\n");
					} else if (state == 7) {
						transfer_done = TRUE;
						printf("\nTransfer cancelled.\n");
					}
				}
			}
			else if (event_type == IPC_EVENT_PROGRESS && event_op_id == transfer_id)
			{
				if (event_size >= IPC_PROGRESS_PAYLOAD_SIZE)
				{
					uint64_t bytes_done = ReadUint64LE(event_buf + 4);
					uint64_t bytes_total = ReadUint64LE(event_buf + 12);
					uint32_t chunks_done = ReadUint32LE(event_buf + 20);
					uint32_t chunks_total = ReadUint32LE(event_buf + 24);

					if (bytes_total > 0)
					{
						double pct = 100.0 * (double)bytes_done / (double)bytes_total;
						printf("\rReceiving: %u/%u chunks (%.1f%%)     ",
							chunks_done, chunks_total, pct);
						fflush(stdout);
						if (chunks_done == chunks_total)
							printf("\n");
					}
				}
			}
		}
	}

	IpcClient_Disconnect(client);
	return transfer_done ? 0 : 1;
}

static int cmd_store(const char *filepath)
{
	if (!FileExists(filepath))
	{
		LOG_ERROR("Error: File not found: %s\n", filepath);
		return 1;
	}

	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	// Build payload: [2B LE path_len][filepath]
	uint32_t path_len = (uint32_t)strlen(filepath);
	uint32_t payload_size = 2 + path_len;
	uint8_t *payload = (uint8_t *)malloc(payload_size);
	if (!payload)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	WriteUint16LE(payload, (uint16_t)path_len);
	memcpy(payload + 2, filepath, path_len);

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		free(payload);
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_STORE,
		payload, payload_size,
		response, IPC_MAX_MESSAGE_SIZE, &response_size);
	free(payload);

	if (!ok || response_size < 1 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Store command failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Parse response: [1B status][32B manifest_hash][2B filename_len][filename]
	if (response_size >= 1 + WH_HASH_SIZE + 2)
	{
		uint16_t fn_len = ReadUint16LE(response + 1 + WH_HASH_SIZE);
		char filename[260] = {0};
		if (fn_len > 0 && fn_len < 260 &&
			(uint32_t)(1 + WH_HASH_SIZE + 2 + fn_len) <= response_size)
		{
			memcpy(filename, response + 1 + WH_HASH_SIZE + 2, fn_len);
			filename[fn_len] = '\0';
		}
		else
		{
			snprintf(filename, sizeof(filename), "%s", filepath);
		}

		// Get file size for display
		uint64_t fsize = 0;
		GetWormholeFileSize(filepath, &fsize);
		char size_str[32];
		FormatSize(fsize, size_str, sizeof(size_str));

		printf("Stored %s (%s)\n", filename, size_str);
		printf("  File ID: ");
		for (int hi = 0; hi < 4; hi++)
			printf("%02x", response[1 + hi]);
		printf("\n");
		printf("  Replicating to network...\n");
	}
	else
	{
		printf("File stored successfully\n");
	}

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_get(int argc, char *argv[])
{
	const char *file_id = NULL;
	const char *output_file = NULL;

	// Parse args: get <file_id> [-o <path>]
	for (int i = 0; i < argc; i++)
	{
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
		{
			output_file = argv[++i];
		}
		else if (!file_id)
		{
			file_id = argv[i];
		}
	}

	if (!file_id)
	{
		LOG_ERROR("Error: Missing file ID\n");
		LOG_ERROR("Usage: wormhole get <file_id> [-o <path>]\n");
		return 1;
	}

	size_t id_len = strlen(file_id);
	if (id_len < 8)
	{
		LOG_ERROR("Error: File ID must be at least 8 hex characters. Use 'wormhole files' to see stored file IDs.\n");
		return 1;
	}

	// Validate hex characters
	for (size_t i = 0; i < id_len; i++)
	{
		char c = file_id[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
		{
			LOG_ERROR("Error: Invalid hex character in file ID\n");
			return 1;
		}
	}

	// Use IPC_CMD_LIST_FILES to find the file by prefix, then IPC_CMD_FILE_GET
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	// First: list files to find the matching manifest hash
	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to list files\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t file_count = ReadUint32LE(response + 1);
	uint32_t off = 5;

	// Find matching file by prefix
	uint8_t matched_hash[WH_HASH_SIZE] = {0};
	char matched_filename[260] = {0};
	uint64_t matched_size = 0;
	uint32_t matches = 0;

	for (uint32_t i = 0; i < file_count; i++)
	{
		if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > response_size) break;

		uint8_t *hash = response + off; off += WH_HASH_SIZE;
		off += 1;  // status
		uint64_t fsize = ReadUint64LE(response + off); off += 8;
		off += 4;  // chunk_count
		off += 4;  // replicated_count
		off += 8;  // store_time
		uint16_t name_len = ReadUint16LE(response + off); off += 2;

		char fname[260] = {0};
		if (name_len > 0 && name_len < 260 && off + name_len <= response_size)
		{
			memcpy(fname, response + off, name_len);
			fname[name_len] = '\0';
		}
		off += name_len;

		// Convert hash to hex and compare prefix
		char hex[65];
		for (int hi = 0; hi < WH_HASH_SIZE; hi++)
			snprintf(hex + hi * 2, 3, "%02x", hash[hi]);
		hex[64] = '\0';

		#ifdef _WIN32
		if (_strnicmp(hex, file_id, id_len) == 0)
		#else
		if (strncasecmp(hex, file_id, id_len) == 0)
		#endif
		{
			memcpy(matched_hash, hash, WH_HASH_SIZE);
			strncpy(matched_filename, fname, sizeof(matched_filename) - 1);
			matched_size = fsize;
			matches++;
		}
	}

	if (matches == 0)
	{
		LOG_ERROR("Error: No file found matching '%s'. Use 'wormhole files' to list stored files.\n", file_id);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}
	if (matches > 1)
	{
		LOG_ERROR("Error: Ambiguous file ID '%s' matches %u files. Use a longer prefix.\n", file_id, matches);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Determine output path
	char final_output[MAX_PATH];
	if (output_file)
	{
		strncpy(final_output, output_file, sizeof(final_output) - 1);
		final_output[sizeof(final_output) - 1] = '\0';
	}
	else
	{
		// Default to ~/Downloads/<original_filename>
		const char *home = getenv("USERPROFILE");
		if (!home) home = getenv("HOME");
		if (!home)
		{
			LOG_ERROR("Error: Cannot determine home directory\n");
			free(response);
			IpcClient_Disconnect(client);
			return 1;
		}
#ifdef _WIN32
		snprintf(final_output, sizeof(final_output), "%s\\Downloads\\%s", home, matched_filename);
#else
		snprintf(final_output, sizeof(final_output), "%s/Downloads/%s", home, matched_filename);
#endif
	}

	char size_str[32];
	FormatSize(matched_size, size_str, sizeof(size_str));
	printf("Retrieving %s (%s)...\n", matched_filename, size_str);

	// Build FILE_GET payload: [32B manifest_hash][2B path_len][output_path]
	uint16_t out_path_len = (uint16_t)strlen(final_output);
	uint32_t fg_payload_size = WH_HASH_SIZE + 2 + out_path_len;
	uint8_t *fg_payload = (uint8_t *)malloc(fg_payload_size);
	if (!fg_payload)
	{
		LOG_ERROR("Error: Out of memory\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}
	memcpy(fg_payload, matched_hash, WH_HASH_SIZE);
	WriteUint16LE(fg_payload + WH_HASH_SIZE, out_path_len);
	memcpy(fg_payload + WH_HASH_SIZE + 2, final_output, out_path_len);

	response_size = 0;
	ok = IpcClient_SendCommand(client, IPC_CMD_FILE_GET,
		fg_payload, fg_payload_size,
		response, IPC_MAX_MESSAGE_SIZE, &response_size);
	free(fg_payload);

	if (!ok || response_size < 1 || response[0] != IPC_STATUS_OK)
	{
		if (response_size >= 1 && response[0] == IPC_STATUS_NOT_FOUND)
			LOG_ERROR("Error: File not found in registry\n");
		else
			LOG_ERROR("Error: Get command failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Response: [1B status][8B bytes_written]
	if (response_size >= 9)
	{
		uint64_t bytes_written = ReadUint64LE(response + 1);
		char dl_size_str[32];
		FormatSize(bytes_written, dl_size_str, sizeof(dl_size_str));
		printf("Downloaded %s\n", dl_size_str);
		printf("Saved to %s\n", final_output);
	}

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_delete(const char *file_id, BOOLEAN force)
{
	size_t id_len = strlen(file_id);
	if (id_len < 8)
	{
		LOG_ERROR("Error: File ID must be at least 8 hex characters. Use 'wormhole files' to see stored file IDs.\n");
		return 1;
	}

	for (size_t i = 0; i < id_len; i++)
	{
		char c = file_id[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
		{
			LOG_ERROR("Error: Invalid hex character in file ID\n");
			return 1;
		}
	}

	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	// List files to find matching manifest hash
	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to list files\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t file_count = ReadUint32LE(response + 1);
	uint32_t off = 5;

	uint8_t matched_hash[WH_HASH_SIZE] = {0};
	char matched_filename[260] = {0};
	uint64_t matched_size = 0;
	uint32_t matches = 0;

	for (uint32_t i = 0; i < file_count; i++)
	{
		if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > response_size) break;

		uint8_t *hash = response + off; off += WH_HASH_SIZE;
		off += 1;  // status
		uint64_t fsize = ReadUint64LE(response + off); off += 8;
		off += 4;  // chunk_count
		off += 4;  // replicated_count
		off += 8;  // store_time
		uint16_t name_len = ReadUint16LE(response + off); off += 2;

		char fname[260] = {0};
		if (name_len > 0 && name_len < 260 && off + name_len <= response_size)
		{
			memcpy(fname, response + off, name_len);
			fname[name_len] = '\0';
		}
		off += name_len;

		char hex[65];
		for (int hi = 0; hi < WH_HASH_SIZE; hi++)
			snprintf(hex + hi * 2, 3, "%02x", hash[hi]);
		hex[64] = '\0';

		#ifdef _WIN32
		if (_strnicmp(hex, file_id, id_len) == 0)
		#else
		if (strncasecmp(hex, file_id, id_len) == 0)
		#endif
		{
			memcpy(matched_hash, hash, WH_HASH_SIZE);
			strncpy(matched_filename, fname, sizeof(matched_filename) - 1);
			matched_size = fsize;
			matches++;
		}
	}

	if (matches == 0)
	{
		LOG_ERROR("Error: No file found matching '%s'. Use 'wormhole files' to list stored files.\n", file_id);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}
	if (matches > 1)
	{
		LOG_ERROR("Error: Ambiguous file ID '%s' matches %u files. Use a longer prefix.\n", file_id, matches);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Confirm deletion
	char size_str[32];
	FormatSize(matched_size, size_str, sizeof(size_str));
	if (!force)
	{
		printf("Delete %s (%s)? [y/N] ", matched_filename, size_str);
		fflush(stdout);
		char confirm[8];
		if (!fgets(confirm, sizeof(confirm), stdin) || (confirm[0] != 'y' && confirm[0] != 'Y'))
		{
			printf("Cancelled.\n");
			free(response);
			IpcClient_Disconnect(client);
			return 0;
		}
	}

	// Send FILE_DELETE with the full manifest hash
	response_size = 0;
	ok = IpcClient_SendCommand(client, IPC_CMD_FILE_DELETE,
		matched_hash, WH_HASH_SIZE,
		response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 1 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Delete command failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	printf("Deleted %s (%s)\n", matched_filename, size_str);

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_export_key(const char *file_id)
{
	size_t id_len = strlen(file_id);
	if (id_len < 8)
	{
		LOG_ERROR("Error: File ID must be at least 8 hex characters. Use 'wormhole files' to see stored file IDs.\n");
		return 1;
	}

	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	// List files to find matching manifest hash
	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to list files\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t file_count = ReadUint32LE(response + 1);
	uint32_t off = 5;

	uint8_t matched_hash[WH_HASH_SIZE] = {0};
	char matched_filename[260] = {0};
	uint32_t matches = 0;

	for (uint32_t i = 0; i < file_count; i++)
	{
		if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > response_size) break;

		uint8_t *hash = response + off; off += WH_HASH_SIZE;
		off += 1;  // status
		off += 8;  // file_size
		off += 4;  // chunk_count
		off += 4;  // replicated_count
		off += 8;  // store_time
		uint16_t name_len = ReadUint16LE(response + off); off += 2;

		char fname[260] = {0};
		if (name_len > 0 && name_len < 260 && off + name_len <= response_size)
		{
			memcpy(fname, response + off, name_len);
			fname[name_len] = '\0';
		}
		off += name_len;

		char hex[65];
		for (int hi = 0; hi < WH_HASH_SIZE; hi++)
			snprintf(hex + hi * 2, 3, "%02x", hash[hi]);
		hex[64] = '\0';

		#ifdef _WIN32
		if (_strnicmp(hex, file_id, id_len) == 0)
		#else
		if (strncasecmp(hex, file_id, id_len) == 0)
		#endif
		{
			memcpy(matched_hash, hash, WH_HASH_SIZE);
			strncpy(matched_filename, fname, sizeof(matched_filename) - 1);
			matches++;
		}
	}

	if (matches == 0)
	{
		LOG_ERROR("Error: No file found matching '%s'. Use 'wormhole files' to list stored files.\n", file_id);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}
	if (matches > 1)
	{
		LOG_ERROR("Error: Ambiguous file ID '%s' matches %u files. Use a longer prefix.\n", file_id, matches);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Send EXPORT_KEY
	response_size = 0;
	ok = IpcClient_SendCommand(client, IPC_CMD_EXPORT_KEY,
		matched_hash, WH_HASH_SIZE,
		response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 1 || response[0] != IPC_STATUS_OK)
	{
		if (response_size >= 1 && response[0] == IPC_STATUS_NOT_FOUND)
			LOG_ERROR("Error: File not found\n");
		else
			LOG_ERROR("Error: File is not encrypted or export failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	if (response_size < 33)
	{
		LOG_ERROR("Error: Invalid response from daemon\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Print key as hex
	printf("Encryption key for %s:\n  ", matched_filename);
	for (int i = 0; i < 32; i++)
		printf("%02x", response[1 + i]);
	printf("\n\nStore this key safely. Without it, your file cannot be decrypted.\n");

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_import_key(const char *file_id, const char *hex_key)
{
	size_t id_len = strlen(file_id);
	if (id_len < 8)
	{
		LOG_ERROR("Error: File ID must be at least 8 hex characters. Use 'wormhole files' to see stored file IDs.\n");
		return 1;
	}

	// Parse hex key (must be exactly 64 chars = 32 bytes)
	size_t key_len = strlen(hex_key);
	if (key_len != 64)
	{
		LOG_ERROR("Error: Encryption key must be exactly 64 hex characters (32 bytes)\n");
		return 1;
	}

	uint8_t key_bytes[32];
	for (int i = 0; i < 32; i++)
	{
		unsigned int byte_val;
		if (sscanf(hex_key + i * 2, "%2x", &byte_val) != 1)
		{
			LOG_ERROR("Error: Invalid hex character in key at position %d\n", i * 2);
			return 1;
		}
		key_bytes[i] = (uint8_t)byte_val;
	}

	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	// List files to find matching manifest hash
	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to list files\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t file_count = ReadUint32LE(response + 1);
	uint32_t off = 5;

	uint8_t matched_hash[WH_HASH_SIZE] = {0};
	char matched_filename[260] = {0};
	uint32_t matches = 0;

	for (uint32_t i = 0; i < file_count; i++)
	{
		if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > response_size) break;

		uint8_t *hash = response + off; off += WH_HASH_SIZE;
		off += 1;  // status
		off += 8;  // file_size
		off += 4;  // chunk_count
		off += 4;  // replicated_count
		off += 8;  // store_time
		uint16_t name_len = ReadUint16LE(response + off); off += 2;

		char fname[260] = {0};
		if (name_len > 0 && name_len < 260 && off + name_len <= response_size)
		{
			memcpy(fname, response + off, name_len);
			fname[name_len] = '\0';
		}
		off += name_len;

		char hex[65];
		for (int hi = 0; hi < WH_HASH_SIZE; hi++)
			snprintf(hex + hi * 2, 3, "%02x", hash[hi]);
		hex[64] = '\0';

		#ifdef _WIN32
		if (_strnicmp(hex, file_id, id_len) == 0)
		#else
		if (strncasecmp(hex, file_id, id_len) == 0)
		#endif
		{
			memcpy(matched_hash, hash, WH_HASH_SIZE);
			strncpy(matched_filename, fname, sizeof(matched_filename) - 1);
			matches++;
		}
	}

	if (matches == 0)
	{
		LOG_ERROR("Error: No file found matching '%s'. Use 'wormhole files' to list stored files.\n", file_id);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}
	if (matches > 1)
	{
		LOG_ERROR("Error: Ambiguous file ID '%s' matches %u files. Use a longer prefix.\n", file_id, matches);
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	// Send IMPORT_KEY: [32B manifest_hash][32B key]
	uint8_t ik_payload[WH_HASH_SIZE + 32];
	memcpy(ik_payload, matched_hash, WH_HASH_SIZE);
	memcpy(ik_payload + WH_HASH_SIZE, key_bytes, 32);

	response_size = 0;
	ok = IpcClient_SendCommand(client, IPC_CMD_IMPORT_KEY,
		ik_payload, sizeof(ik_payload),
		response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 1 || response[0] != IPC_STATUS_OK)
	{
		if (response_size >= 1 && response[0] == IPC_STATUS_NOT_FOUND)
			LOG_ERROR("Error: File not found in registry\n");
		else
			LOG_ERROR("Error: Import key failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	printf("Encryption key imported for %s\n", matched_filename);

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static const char *StatusString(uint8_t status)
{
	switch (status)
	{
	case 0x01: return "STORING";
	case 0x02: return "REPLICATING";
	case 0x03: return "SAFE";
	case 0x04: return "OFFLOADED";
	default:   return "UNKNOWN";
	}
}

static int cmd_files(int argc, char *argv[])
{
	BOOLEAN verbose = FALSE;
	for (int i = 0; i < argc; i++)
	{
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
			verbose = TRUE;
	}

	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to list files\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t file_count = ReadUint32LE(response + 1);

	if (file_count == 0)
	{
		printf("\nNo stored files.\n\n");
		free(response);
		IpcClient_Disconnect(client);
		return 0;
	}

	printf("\n%-10s %-24s %-10s %s\n", "ID", "NAME", "SIZE", "STATUS");
	printf("%-10s %-24s %-10s %s\n", "--------", "------------------------", "--------", "----------");

	uint32_t off = 5;
	for (uint32_t i = 0; i < file_count; i++)
	{
		if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > response_size) break;

		uint8_t *hash = response + off; off += WH_HASH_SIZE;
		uint8_t status = response[off]; off += 1;
		uint64_t fsize = ReadUint64LE(response + off); off += 8;
		uint32_t chunk_count = ReadUint32LE(response + off); off += 4;
		uint32_t repl_count = ReadUint32LE(response + off); off += 4;
		off += 8;  // store_time
		uint16_t name_len = ReadUint16LE(response + off); off += 2;

		char fname[260] = {0};
		if (name_len > 0 && name_len < 260 && off + name_len <= response_size)
		{
			memcpy(fname, response + off, name_len);
			fname[name_len] = '\0';
		}
		off += name_len;

		// Truncate filename for display
		char display_name[25];
		if (strlen(fname) > 24)
		{
			memcpy(display_name, fname, 21);
			display_name[21] = '.';
			display_name[22] = '.';
			display_name[23] = '.';
			display_name[24] = '\0';
		}
		else
		{
			strncpy(display_name, fname, sizeof(display_name) - 1);
			display_name[sizeof(display_name) - 1] = '\0';
		}

		char id_str[9];
		for (int hi = 0; hi < 4; hi++)
			snprintf(id_str + hi * 2, 3, "%02x", hash[hi]);
		id_str[8] = '\0';

		char size_str[32];
		FormatSize(fsize, size_str, sizeof(size_str));

		char status_detail[64];
		if (status == 0x02)  // REPLICATING
		{
			if (chunk_count > 0)
				snprintf(status_detail, sizeof(status_detail), "REPLICATING (%u%%)",
					(repl_count * 100) / chunk_count);
			else
				snprintf(status_detail, sizeof(status_detail), "REPLICATING");
		}
		else if (status == 0x03 && verbose)  // SAFE with replica info
		{
			snprintf(status_detail, sizeof(status_detail), "SAFE (%u/%u replicas)",
				repl_count, chunk_count);
		}
		else
		{
			snprintf(status_detail, sizeof(status_detail), "%s", StatusString(status));
		}

		printf("%-10s %-24s %-10s %s\n", id_str, display_name, size_str, status_detail);

		if (verbose)
		{
			printf("  > Chunks: %u\n", chunk_count);
		}
	}
	printf("\n");

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_status(void)
{
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_STATUS,
		NULL, 0,
		response, IPC_MAX_MESSAGE_SIZE, &response_size);

	// Daemon sends: [1B status][4B peers][4B chunks][8B storage][1B relay][1B listener] = 19 bytes
	if (!ok || response_size < 19 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Status command failed\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t peers    = ReadUint32LE(response + 1);
	uint32_t chunks   = ReadUint32LE(response + 5);
	uint64_t storage  = ReadUint64LE(response + 9);
	BOOLEAN  relay    = response[17];
	BOOLEAN  listener = response[18];

	LOG("\nWormhole Daemon Status\n");
	LOG("══════════════════════\n");
	LOG("  Peers:         %u\n", peers);
	LOG("  Chunks:        %u\n", chunks);
	LOG("  Storage Used:  %llu bytes\n", (unsigned long long)storage);
	LOG("  Relay:         %s\n", relay ? "connected" : "disconnected");
	LOG("  Listener:      %s\n", listener ? "active" : "inactive");

	// Fetch DHT status
	uint32_t dht_response_size = 0;
	BOOLEAN dht_ok = IpcClient_SendCommand(client, IPC_CMD_DHT_STATUS,
		NULL, 0,
		response, IPC_MAX_MESSAGE_SIZE, &dht_response_size);

	if (dht_ok && dht_response_size >= 25 && response[0] == IPC_STATUS_OK)
	{
		uint32_t dht_nodes  = ReadUint32LE(response + 1);
		uint32_t dht_values = ReadUint32LE(response + 5);
		uint64_t dht_sent   = ReadUint64LE(response + 9);
		uint64_t dht_recv   = ReadUint64LE(response + 17);

		LOG("  DHT Nodes:     %u\n", dht_nodes);
		LOG("  DHT Values:    %u\n", dht_values);
		LOG("  DHT Sent:      %llu msgs\n", (unsigned long long)dht_sent);
		LOG("  DHT Received:  %llu msgs\n", (unsigned long long)dht_recv);
	}
	else
	{
		LOG("  DHT:           not available\n");
	}

	// Fetch file summary via LIST_FILES
	{
		uint32_t files_response_size = 0;
		BOOLEAN files_ok = IpcClient_SendCommand(client, IPC_CMD_LIST_FILES,
			NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &files_response_size);

		if (files_ok && files_response_size >= 5 && response[0] == IPC_STATUS_OK)
		{
			uint32_t file_count = ReadUint32LE(response + 1);
			uint32_t safe = 0, replicating = 0, offloaded = 0;
			uint32_t foff = 5;

			for (uint32_t i = 0; i < file_count; i++)
			{
				if (foff + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > files_response_size) break;
				foff += WH_HASH_SIZE;  // hash
				uint8_t fstatus = response[foff]; foff += 1;
				foff += 8;  // size
				foff += 4;  // chunk_count
				foff += 4;  // repl_count
				foff += 8;  // store_time
				uint16_t name_len = ReadUint16LE(response + foff); foff += 2;
				foff += name_len;

				if (fstatus == 0x03) safe++;
				else if (fstatus == 0x02) replicating++;
				else if (fstatus == 0x04) offloaded++;
			}

			if (file_count > 0)
			{
				LOG("  Stored Files:  %u", file_count);
				if (safe > 0 || replicating > 0 || offloaded > 0)
				{
					printf(" (");
					BOOLEAN first = TRUE;
					if (safe > 0) { printf("%u safe", safe); first = FALSE; }
					if (offloaded > 0) { if (!first) printf(", "); printf("%u offloaded", offloaded); first = FALSE; }
					if (replicating > 0) { if (!first) printf(", "); printf("%u replicating", replicating); }
					printf(")");
				}
				printf("\n");
			}
		}
	}

	// Check heartbeat freshness and query uptime
	{
		// Query uptime via heartbeat IPC
		uint32_t hb_response_size = 0;
		BOOLEAN hb_ok = IpcClient_SendCommand(client, IPC_CMD_HEARTBEAT,
			NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &hb_response_size);
		if (hb_ok && hb_response_size >= 9 && response[0] == IPC_STATUS_OK)
		{
			uint64_t uptime_sec = ReadUint64LE(response + 1);
			uint64_t days = uptime_sec / 86400;
			uint64_t hours = (uptime_sec % 86400) / 3600;
			uint64_t mins = (uptime_sec % 3600) / 60;
			if (days > 0)
				LOG("  Uptime:        %llud %lluh %llum\n",
					(unsigned long long)days, (unsigned long long)hours, (unsigned long long)mins);
			else if (hours > 0)
				LOG("  Uptime:        %lluh %llum\n",
					(unsigned long long)hours, (unsigned long long)mins);
			else
				LOG("  Uptime:        %llum %llus\n",
					(unsigned long long)mins, (unsigned long long)(uptime_sec % 60));
		}

		// Check heartbeat file freshness
		char hb_path[MAX_PATH];
		BOOLEAN hb_stale = FALSE;
#ifdef _WIN32
		const char *home_hb = getenv("USERPROFILE");
		if (home_hb) snprintf(hb_path, sizeof(hb_path), "%s\\.wormhole\\wormholed.heartbeat", home_hb);
#else
		const char *home_hb = getenv("HOME");
		if (home_hb) snprintf(hb_path, sizeof(hb_path), "%s/.wormhole/wormholed.heartbeat", home_hb);
#endif
		if (home_hb) {
			FILE *hf = fopen(hb_path, "r");
			if (hf) {
				long long hb_time = 0;
				if (fscanf(hf, "%lld", &hb_time) == 1) {
					long long age = (long long)time(NULL) - hb_time;
					if (age > 30) hb_stale = TRUE;
				}
				fclose(hf);
			}
		}
		if (hb_stale)
			LOG("  WARNING:       Daemon heartbeat is stale (>30s), may be unresponsive\n");
	}

	LOG("\n");

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_config(int argc, char *argv[])
{
	// argv[0] = "config", argv[1] = subcommand, argv[2..] = args
	if (argc < 2)
	{
		LOG_ERROR("Usage: wormhole config <list|get|set> [key] [value]\n");
		return 1;
	}

	// Try IPC to daemon first — if connected, use daemon's live config
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);

	if (client && strcmp(argv[1], "list") == 0)
	{
		uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
		if (!response)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Error: Out of memory\n");
			return 1;
		}

		uint32_t resp_size = 0;
		if (!IpcClient_SendCommand(client, IPC_CMD_CONFIG_LIST, NULL, 0,
				response, IPC_MAX_MESSAGE_SIZE, &resp_size) ||
			resp_size < 5 || response[0] != IPC_STATUS_OK)
		{
			free(response);
			IpcClient_Disconnect(client);
			LOG_ERROR("Error: Failed to list config from daemon\n");
			return 1;
		}

		uint32_t count = ReadUint32LE(response + 1);
		LOG("\nWormhole Configuration (live from daemon)\n");
		LOG("════════════════════════════════════════════\n");

		uint32_t off = 5;
		for (uint32_t i = 0; i < count && off < resp_size; i++)
		{
			uint8_t key_len = response[off++];
			if (off + key_len > resp_size) break;
			char key[CONFIG_MAX_KEY_LEN];
			uint8_t kc = key_len < CONFIG_MAX_KEY_LEN - 1 ? key_len : CONFIG_MAX_KEY_LEN - 1;
			memcpy(key, response + off, kc); key[kc] = '\0';
			off += key_len;

			if (off >= resp_size) break;
			uint8_t val_len = response[off++];
			if (off + val_len > resp_size) break;
			char val[CONFIG_MAX_VALUE_LEN];
			uint8_t vc = val_len < CONFIG_MAX_VALUE_LEN - 1 ? val_len : CONFIG_MAX_VALUE_LEN - 1;
			memcpy(val, response + off, vc); val[vc] = '\0';
			off += val_len;

			uint8_t hot = (off < resp_size) ? response[off++] : 0;
			LOG("  %-25s = %s%s\n", key, val, hot ? "" : "  (restart required)");
		}
		LOG("\n");

		free(response);
		IpcClient_Disconnect(client);
		return 0;
	}

	if (client && strcmp(argv[1], "get") == 0)
	{
		if (argc < 3)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Usage: wormhole config get <key>\n");
			return 1;
		}

		uint8_t payload[256];
		uint8_t key_len = (uint8_t)strlen(argv[2]);
		payload[0] = key_len;
		memcpy(payload + 1, argv[2], key_len);

		uint8_t response[512];
		uint32_t resp_size = 0;
		if (!IpcClient_SendCommand(client, IPC_CMD_CONFIG_GET, payload, 1 + key_len,
				response, sizeof(response), &resp_size) || resp_size < 1)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Error: Failed to get config from daemon\n");
			return 1;
		}

		if (response[0] == IPC_STATUS_NOT_FOUND)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Key not found: %s\n", argv[2]);
			return 1;
		}
		if (response[0] != IPC_STATUS_OK || resp_size < 2)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Error: Daemon returned error for key '%s'\n", argv[2]);
			return 1;
		}

		uint8_t vl = response[1];
		if ((uint32_t)(2 + vl) > resp_size) vl = (uint8_t)(resp_size - 2);
		char val[CONFIG_MAX_VALUE_LEN];
		uint8_t vc = vl < CONFIG_MAX_VALUE_LEN - 1 ? vl : CONFIG_MAX_VALUE_LEN - 1;
		memcpy(val, response + 2, vc); val[vc] = '\0';
		printf("%s\n", val);

		IpcClient_Disconnect(client);
		return 0;
	}

	if (client && strcmp(argv[1], "set") == 0)
	{
		if (argc < 4)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Usage: wormhole config set <key> <value>\n");
			return 1;
		}

		uint8_t payload[512];
		uint8_t key_len = (uint8_t)strlen(argv[2]);
		uint8_t val_len = (uint8_t)strlen(argv[3]);
		payload[0] = key_len;
		memcpy(payload + 1, argv[2], key_len);
		payload[1 + key_len] = val_len;
		memcpy(payload + 1 + key_len + 1, argv[3], val_len);
		uint32_t pay_size = 1 + key_len + 1 + val_len;

		uint8_t response[512];
		uint32_t resp_size = 0;
		if (!IpcClient_SendCommand(client, IPC_CMD_CONFIG_SET, payload, pay_size,
				response, sizeof(response), &resp_size) || resp_size < 1)
		{
			IpcClient_Disconnect(client);
			LOG_ERROR("Error: Failed to set config via daemon\n");
			return 1;
		}

		if (response[0] != IPC_STATUS_OK)
		{
			// Try to read structured error message
			char errmsg[256] = {0};
			if (resp_size >= 3)
			{
				uint16_t msg_len = (uint16_t)response[1] | ((uint16_t)response[2] << 8);
				if (msg_len > 0 && (uint32_t)(3 + msg_len) <= resp_size)
				{
					uint16_t copy = msg_len < sizeof(errmsg) - 1 ? msg_len : (uint16_t)(sizeof(errmsg) - 1);
					memcpy(errmsg, response + 3, copy);
					errmsg[copy] = '\0';
				}
			}
			IpcClient_Disconnect(client);
			if (errmsg[0])
				LOG_ERROR("Error: %s\n", errmsg);
			else
				LOG_ERROR("Error: Failed to set config value\n");
			return 1;
		}

		LOG("Config updated: %s = %s\n", argv[2], argv[3]);

		// Check restart_required flag
		if (resp_size >= 2 && response[1] == 1)
		{
			LOG("Note: Restart daemon for '%s' to take effect (wormhole daemon restart)\n", argv[2]);
		}

		IpcClient_Disconnect(client);
		return 0;
	}

	// Daemon not running (or unknown subcommand) — fall back to direct file access
	if (client) IpcClient_Disconnect(client);

	WORMHOLE_CONFIG *config = Config_LoadDefault();
	if (!config)
	{
		LOG_ERROR("Error: Failed to load config\n");
		return 1;
	}

	int result = 0;

	if (strcmp(argv[1], "list") == 0)
	{
		LOG("\nWormhole Configuration (~/.wormhole/config)\n");
		LOG("════════════════════════════════════════════\n");
		LOG("  max_storage_gb     = %llu\n",
			(unsigned long long)Config_GetUint64(config, "max_storage_gb",
				CONFIG_DEFAULT_MAX_STORAGE_GB));
		LOG("  replication_target = %llu\n",
			(unsigned long long)Config_GetUint64(config, "replication_target",
				CONFIG_DEFAULT_REPLICATION_TARGET));
		LOG("  relay_host         = %s\n",
			Config_GetString(config, "relay_host", CONFIG_DEFAULT_RELAY_HOST));
		LOG("  relay_port         = %llu\n",
			(unsigned long long)Config_GetUint64(config, "relay_port",
				CONFIG_DEFAULT_RELAY_PORT));
		LOG("  dht_port           = %llu\n",
			(unsigned long long)Config_GetUint64(config, "dht_port",
				CONFIG_DEFAULT_DHT_PORT));
		LOG("  dht_enabled        = %llu\n",
			(unsigned long long)Config_GetUint64(config, "dht_enabled",
				CONFIG_DEFAULT_DHT_ENABLED));
		LOG("  ec_enabled         = %llu\n",
			(unsigned long long)Config_GetUint64(config, "ec_enabled",
				CONFIG_DEFAULT_EC_ENABLED));
		LOG("  ec_data_shards     = %llu\n",
			(unsigned long long)Config_GetUint64(config, "ec_data_shards",
				CONFIG_DEFAULT_EC_DATA_SHARDS));
		LOG("  ec_parity_shards   = %llu\n\n",
			(unsigned long long)Config_GetUint64(config, "ec_parity_shards",
				CONFIG_DEFAULT_EC_PARITY_SHARDS));
	}
	else if (strcmp(argv[1], "get") == 0)
	{
		if (argc < 3)
		{
			LOG_ERROR("Usage: wormhole config get <key>\n");
			result = 1;
		}
		else
		{
			const char *val = Config_GetString(config, argv[2], NULL);
			if (val)
				printf("%s\n", val);
			else
				LOG_ERROR("Key not found: %s\n", argv[2]);
		}
	}
	else if (strcmp(argv[1], "set") == 0)
	{
		if (argc < 4)
		{
			LOG_ERROR("Usage: wormhole config set <key> <value>\n");
			result = 1;
		}
		else
		{
			if (!Config_Set(config, argv[2], argv[3]))
			{
				LOG_ERROR("Error: Invalid value '%s' for key '%s'\n", argv[3], argv[2]);
				result = 1;
			}
			else if (Config_Save(config))
				LOG("Config updated: %s = %s\n", argv[2], argv[3]);
			else
			{
				LOG_ERROR("Error: Failed to save config\n");
				result = 1;
			}
		}
	}
	else
	{
		LOG_ERROR("Unknown config subcommand: %s\n", argv[1]);
		result = 1;
	}

	Config_Destroy(config);
	return result;
}

static int cmd_peers(void)
{
	IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
	if (!client)
	{
		LOG_ERROR("Error: Cannot connect to wormhole daemon. Start it with: wormhole daemon start\n");
		return 1;
	}

	uint8_t *response = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
	if (!response)
	{
		LOG_ERROR("Error: Out of memory\n");
		IpcClient_Disconnect(client);
		return 1;
	}
	uint32_t response_size = 0;

	BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_PEER_LIST,
		NULL, 0, response, IPC_MAX_MESSAGE_SIZE, &response_size);

	if (!ok || response_size < 5 || response[0] != IPC_STATUS_OK)
	{
		LOG_ERROR("Error: Failed to get peer list\n");
		free(response);
		IpcClient_Disconnect(client);
		return 1;
	}

	uint32_t peer_count = ReadUint32LE(response + 1);

	if (peer_count == 0)
	{
		printf("\nNo known peers.\n\n");
		free(response);
		IpcClient_Disconnect(client);
		return 0;
	}

	printf("\n%-10s %-20s %-7s %s\n", "NODE ID", "ADDRESS", "PORT", "LAST SEEN");
	printf("%-10s %-20s %-7s %s\n", "--------", "-------------------", "------", "----------");

	uint32_t off = 5;
	time_t now = time(NULL);
	for (uint32_t i = 0; i < peer_count; i++)
	{
		uint32_t per_peer_size = 32 + 1 + 16 + 2 + 8;
		if (off + per_peer_size > response_size) break;

		uint8_t *node_id = response + off; off += 32;
		uint8_t addr_type = response[off]; off += 1;
		uint8_t *addr = response + off; off += 16;
		uint16_t port = ReadUint16LE(response + off); off += 2;
		uint64_t last_seen = ReadUint64LE(response + off); off += 8;

		// Format node ID (8-char hex prefix)
		char id_str[9];
		for (int hi = 0; hi < 4; hi++)
			snprintf(id_str + hi * 2, 3, "%02x", node_id[hi]);
		id_str[8] = '\0';

		// Format address
		char addr_str[INET6_ADDRSTRLEN];
		if (addr_type == 0x04)  // IPv4
		{
			snprintf(addr_str, sizeof(addr_str), "%u.%u.%u.%u",
				addr[0], addr[1], addr[2], addr[3]);
		}
		else  // IPv6
		{
			inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str));
		}

		// Format last seen as relative time
		char seen_str[32];
		if (last_seen == 0)
		{
			snprintf(seen_str, sizeof(seen_str), "never");
		}
		else
		{
			int64_t elapsed = (int64_t)(now - (time_t)last_seen);
			if (elapsed < 0) elapsed = 0;
			if (elapsed < 60)
				snprintf(seen_str, sizeof(seen_str), "%ds ago", (int)elapsed);
			else if (elapsed < 3600)
				snprintf(seen_str, sizeof(seen_str), "%dm ago", (int)(elapsed / 60));
			else if (elapsed < 86400)
				snprintf(seen_str, sizeof(seen_str), "%dh ago", (int)(elapsed / 3600));
			else
				snprintf(seen_str, sizeof(seen_str), "%dd ago", (int)(elapsed / 86400));
		}

		printf("%-10s %-20s %-7u %s\n", id_str, addr_str, port, seen_str);
	}
	printf("\n%u peer(s) known\n\n", peer_count);

	free(response);
	IpcClient_Disconnect(client);
	return 0;
}

static int cmd_daemon(int argc, char *argv[])
{
	if (argc < 2)
	{
		LOG_ERROR("Usage: wormhole daemon <start|stop|restart|status>\n");
		return 1;
	}

	const char *subcmd = argv[1];

	if (strcmp(subcmd, "status") == 0)
	{
		return cmd_status();
	}
	else if (strcmp(subcmd, "stop") == 0)
	{
		IPC_CLIENT *client = IpcClient_ConnectTo(g_ipc_pipe);
		if (!client)
		{
			printf("Daemon is not running.\n");
			return 0;
		}

		uint8_t response[64];
		uint32_t response_size = 0;
		IpcClient_SendCommand(client, IPC_CMD_SHUTDOWN,
			NULL, 0, response, sizeof(response), &response_size);
		IpcClient_Disconnect(client);

		// Wait for daemon to exit (poll IPC up to 10s)
		for (int i = 0; i < 20; i++)
		{
			WH_SLEEP_MS(500);
			IPC_CLIENT *check = IpcClient_ConnectTo(g_ipc_pipe);
			if (!check)
			{
				printf("Daemon stopped\n");

				// Clean up PID file and lifecycle files
				char pid_path[MAX_PATH];
#ifdef _WIN32
				const char *home = getenv("USERPROFILE");
				if (home) snprintf(pid_path, sizeof(pid_path), "%s\\.wormhole\\wormholed.pid", home);
#else
				const char *home = getenv("HOME");
				if (home) snprintf(pid_path, sizeof(pid_path), "%s/.wormhole/wormholed.pid", home);
#endif
				if (home) {
					remove(pid_path);
					char ready_path[MAX_PATH], hb_path[MAX_PATH];
#ifdef _WIN32
					snprintf(ready_path, sizeof(ready_path), "%s\\.wormhole\\wormholed.ready", home);
					snprintf(hb_path, sizeof(hb_path), "%s\\.wormhole\\wormholed.heartbeat", home);
#else
					snprintf(ready_path, sizeof(ready_path), "%s/.wormhole/wormholed.ready", home);
					snprintf(hb_path, sizeof(hb_path), "%s/.wormhole/wormholed.heartbeat", home);
#endif
					remove(ready_path);
					remove(hb_path);
				}
				return 0;
			}
			IpcClient_Disconnect(check);
		}

		LOG_ERROR("Warning: Daemon did not stop within 10 seconds\n");
		return 1;
	}
	else if (strcmp(subcmd, "start") == 0)
	{
		// Check if daemon already running via IPC
		IPC_CLIENT *check = IpcClient_ConnectTo(g_ipc_pipe);
		if (check)
		{
			IpcClient_Disconnect(check);
			printf("Daemon is already running.\n");
			return 0;
		}

		// Stale PID detection: if PID file exists but process is dead, clean up
		{
			char stale_pid_path[MAX_PATH];
#ifdef _WIN32
			const char *home_stale = getenv("USERPROFILE");
			if (home_stale) snprintf(stale_pid_path, sizeof(stale_pid_path),
				"%s\\.wormhole\\wormholed.pid", home_stale);
#else
			const char *home_stale = getenv("HOME");
			if (home_stale) snprintf(stale_pid_path, sizeof(stale_pid_path),
				"%s/.wormhole/wormholed.pid", home_stale);
#endif
			if (home_stale) {
				FILE *pf_check = fopen(stale_pid_path, "r");
				if (pf_check) {
					long stale_pid = 0;
					if (fscanf(pf_check, "%ld", &stale_pid) == 1 && stale_pid > 0) {
#ifdef _WIN32
						HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)stale_pid);
						if (hProc) {
							CloseHandle(hProc);
							// Process exists but IPC failed — shouldn't happen, but leave PID file
						} else {
							fclose(pf_check);
							remove(stale_pid_path);
							// Also clean up stale readiness file
							char stale_ready[MAX_PATH];
							snprintf(stale_ready, sizeof(stale_ready),
								"%s\\.wormhole\\wormholed.ready", home_stale);
							remove(stale_ready);
							goto pid_cleaned;
						}
#else
						if (kill((pid_t)stale_pid, 0) != 0) {
							fclose(pf_check);
							remove(stale_pid_path);
							// Also clean up stale readiness file
							char stale_ready[MAX_PATH];
							snprintf(stale_ready, sizeof(stale_ready),
								"%s/.wormhole/wormholed.ready", home_stale);
							remove(stale_ready);
							goto pid_cleaned;
						}
#endif
					}
					fclose(pf_check);
					pid_cleaned: ;
				}
			}
		}

		// Spawn wormholed as a detached background process
		char pid_path[MAX_PATH];
#ifdef _WIN32
		const char *home = getenv("USERPROFILE");
		if (!home) { LOG_ERROR("Error: Cannot determine home directory\n"); return 1; }
		snprintf(pid_path, sizeof(pid_path), "%s\\.wormhole\\wormholed.pid", home);

		// Build command line for wormholed
		char cmd_line[MAX_PATH];
		char exe_path[MAX_PATH];
		// Look for wormholed.exe in same directory as wormhole.exe
		GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
		char *last_slash = strrchr(exe_path, '\\');
		if (last_slash) *(last_slash + 1) = '\0';
		snprintf(cmd_line, sizeof(cmd_line), "%swormholed.exe", exe_path);

		// Redirect stdout/stderr to log file
		char log_path[MAX_PATH];
		snprintf(log_path, sizeof(log_path), "%s\\.wormhole\\wormholed.log", home);

		SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
		HANDLE hLog = CreateFileA(log_path, FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		STARTUPINFOA si = { sizeof(si) };
		if (hLog != INVALID_HANDLE_VALUE)
		{
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = hLog;
			si.hStdError = hLog;
			si.hStdInput = NULL;
		}

		PROCESS_INFORMATION pi;
		if (!CreateProcessA(cmd_line, NULL, NULL, NULL,
			hLog != INVALID_HANDLE_VALUE ? TRUE : FALSE,
			DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
			NULL, NULL, &si, &pi))
		{
			LOG_ERROR("Error: Failed to start daemon\n");
			if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
			return 1;
		}
		if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);

		// Write PID file
		FILE *pf = fopen(pid_path, "w");
		if (pf) { fprintf(pf, "%lu", (unsigned long)pi.dwProcessId); fclose(pf); }

		// Wait up to 5 seconds for readiness file
		{
			char ready_path[MAX_PATH];
			snprintf(ready_path, sizeof(ready_path), "%s\\.wormhole\\wormholed.ready", home);
			BOOLEAN ready = FALSE;
			for (int ri = 0; ri < 50; ri++) {
				WH_SLEEP_MS(100);
				DWORD attr = GetFileAttributesA(ready_path);
				if (attr != INVALID_FILE_ATTRIBUTES) { ready = TRUE; break; }
			}
			if (ready)
				printf("Daemon started (PID: %lu)\n", (unsigned long)pi.dwProcessId);
			else
				printf("Daemon spawned (PID: %lu), may still be starting...\n", (unsigned long)pi.dwProcessId);
		}

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
#else
		const char *home = getenv("HOME");
		if (!home) { LOG_ERROR("Error: Cannot determine home directory\n"); return 1; }
		snprintf(pid_path, sizeof(pid_path), "%s/.wormhole/wormholed.pid", home);

		// Find wormholed in same directory as wormhole
		char exe_dir[MAX_PATH];
		ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
		if (len <= 0)
		{
			// Fallback: try PATH
			strncpy(exe_dir, "wormholed", sizeof(exe_dir));
		}
		else
		{
			exe_dir[len] = '\0';
			char *last_slash = strrchr(exe_dir, '/');
			if (last_slash) *(last_slash + 1) = '\0';
			strncat(exe_dir, "wormholed", sizeof(exe_dir) - strlen(exe_dir) - 1);
		}

		char log_path[MAX_PATH];
		snprintf(log_path, sizeof(log_path), "%s/.wormhole/wormholed.log", home);

		pid_t pid = fork();
		if (pid < 0)
		{
			LOG_ERROR("Error: fork() failed\n");
			return 1;
		}
		else if (pid == 0)
		{
			// Child: become session leader, redirect output to log
			setsid();
			int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
			close(STDIN_FILENO);
			execl(exe_dir, "wormholed", (char *)NULL);
			_exit(127);
		}

		// Parent: write PID file and wait for readiness
		FILE *pf = fopen(pid_path, "w");
		if (pf) { fprintf(pf, "%d", pid); fclose(pf); }

		// Wait up to 5 seconds for readiness file
		{
			char ready_path[MAX_PATH];
			snprintf(ready_path, sizeof(ready_path), "%s/.wormhole/wormholed.ready", home);
			BOOLEAN ready = FALSE;
			for (int ri = 0; ri < 50; ri++) {
				WH_SLEEP_MS(100);
				struct stat st;
				if (stat(ready_path, &st) == 0) { ready = TRUE; break; }
			}
			if (ready)
				printf("Daemon started (PID: %d)\n", pid);
			else
				printf("Daemon spawned (PID: %d), may still be starting...\n", pid);
		}
#endif
		return 0;
	}
	else if (strcmp(subcmd, "restart") == 0)
	{
		// Build fake argv for stop and start
		char *stop_args[] = { argv[0], "stop" };
		int rc = cmd_daemon(2, stop_args);
		if (rc != 0) return rc;

		WH_SLEEP_MS(500);

		char *start_args[] = { argv[0], "start" };
		return cmd_daemon(2, start_args);
	}
	else
	{
		LOG_ERROR("Usage: wormhole daemon <start|stop|restart|status>\n");
		return 1;
	}
}

//=============================================================================
// Command-Line Parsing & Usage
//=============================================================================

static void PrintUsage(void)
{
	LOG("\nWormhole - Secure P2P File Transfer & Storage\n\n");
	LOG("Usage:\n");
	LOG("  wormhole send <file|directory>      Send a file or directory\n");
	LOG("  wormhole receive <ticket>           Receive a file using a ticket\n");
	LOG("  wormhole store <file>               Store file via daemon\n");
	LOG("  wormhole get <id> [-o <file>]       Retrieve a stored file by ID\n");
	LOG("  wormhole delete <id> [-f]           Delete a stored file\n");
	LOG("  wormhole files [-v]                 List stored files\n");
	LOG("  wormhole status                     Show daemon status\n");
	LOG("  wormhole peers                      List known peers\n");
	LOG("  wormhole export-key <id>            Export file encryption key\n");
	LOG("  wormhole import-key <id> <key>      Import file encryption key\n");
	LOG("  wormhole daemon <start|stop|restart> Manage daemon process\n");
	LOG("  wormhole config list                Show all settings\n");
	LOG("  wormhole config get <key>           Get a config value\n");
	LOG("  wormhole config set <key> <val>     Set a config value\n");
	LOG("  wormhole --help                     Show this help message\n");
	LOG("  wormhole --version                  Show version\n\n");
	LOG("Global options:\n");
	LOG("  --daemon <port>   Connect to daemon on specified port (default: %u)\n", WORMHOLE_DEFAULT_PORT);
	LOG("  --direct          Bypass daemon for send/receive (standalone mode)\n\n");
	LOG("Examples:\n");
	LOG("  wormhole send document.pdf\n");
	LOG("  wormhole send ./my-project/\n");
	LOG("  wormhole send --direct largefile.zip\n");
	LOG("  wormhole receive 7-guitar-battery\n");
	LOG("  wormhole store myfile.bin\n");
	LOG("  wormhole get a1b2c3d4 -o recovered.bin\n\n");
}

//=============================================================================
// Main Entry Point
//=============================================================================

int main(int argc, char *argv[])
{
#ifdef _WIN32
	if (!getenv("USERPROFILE"))
	{
		fprintf(stderr, "ERROR: USERPROFILE environment variable is not set\n");
		return 1;
	}
#else
	if (!getenv("HOME"))
	{
		fprintf(stderr, "ERROR: HOME environment variable is not set\n");
		return 1;
	}
#endif

	LOG_ERROR("=== Wormhole - Secure P2P File Transfer ===\n\n");

	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}

	if (strcmp(argv[1], "--version") == 0)
	{
		printf("wormhole version %s\n", WORMHOLE_VERSION);
		return 0;
	}

	// First pass: handle --daemon flag (sets IPC pipe name)
	uint16_t daemon_port = WORMHOLE_DEFAULT_PORT;
	int cmd_start = 1;  // Index where the subcommand begins
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--daemon") == 0 && i + 1 < argc)
		{
			daemon_port = (uint16_t)atoi(argv[i + 1]);
			cmd_start = i + 2;  // Skip --daemon and its arg
			break;
		}
	}
	snprintf(g_ipc_pipe, sizeof(g_ipc_pipe), "%s%u", IPC_PIPE_PREFIX, daemon_port);

	if (cmd_start >= argc)
	{
		PrintUsage();
		return 1;
	}

	const char *cmd = argv[cmd_start];

	if (strcmp(cmd, "send") == 0)
	{
		if (cmd_start + 1 >= argc)
		{
			LOG_ERROR("ERROR: Missing filename or directory\n");
			PrintUsage();
			return 1;
		}
		// Check for --direct flag
		BOOLEAN direct_mode = FALSE;
		const char *send_filepath = NULL;
		for (int i = cmd_start + 1; i < argc; i++) {
			if (strcmp(argv[i], "--direct") == 0)
				direct_mode = TRUE;
			else if (!send_filepath)
				send_filepath = argv[i];
		}
		if (!send_filepath) {
			LOG_ERROR("ERROR: Missing filename or directory\n");
			PrintUsage();
			return 1;
		}
		if (direct_mode)
			return cmd_send(send_filepath);
		// Try daemon first, fall back to direct
		IPC_CLIENT *probe = IpcClient_ConnectTo(g_ipc_pipe);
		if (probe) {
			IpcClient_Disconnect(probe);
			return cmd_send_via_daemon(send_filepath);
		}
		return cmd_send(send_filepath);
	}
	else if (strcmp(cmd, "receive") == 0)
	{
		if (cmd_start + 1 >= argc)
		{
			LOG_ERROR("ERROR: Missing ticket\n");
			PrintUsage();
			return 1;
		}
		// Check for --direct flag
		BOOLEAN direct_mode = FALSE;
		const char *recv_ticket = NULL;
		for (int i = cmd_start + 1; i < argc; i++) {
			if (strcmp(argv[i], "--direct") == 0)
				direct_mode = TRUE;
			else if (!recv_ticket)
				recv_ticket = argv[i];
		}
		if (!recv_ticket) {
			LOG_ERROR("ERROR: Missing ticket\n");
			PrintUsage();
			return 1;
		}
		if (direct_mode)
			return cmd_receive(recv_ticket);
		// Try daemon first, fall back to direct
		IPC_CLIENT *probe = IpcClient_ConnectTo(g_ipc_pipe);
		if (probe) {
			IpcClient_Disconnect(probe);
			return cmd_receive_via_daemon(recv_ticket);
		}
		return cmd_receive(recv_ticket);
	}
	else if (strcmp(cmd, "store") == 0)
	{
		if (cmd_start + 1 >= argc)
		{
			LOG_ERROR("ERROR: Missing filename\n");
			PrintUsage();
			return 1;
		}
		return cmd_store(argv[cmd_start + 1]);
	}
	else if (strcmp(cmd, "get") == 0)
	{
		if (cmd_start + 1 >= argc)
		{
			LOG_ERROR("ERROR: Missing file ID\n");
			PrintUsage();
			return 1;
		}
		// Pass remaining args after "get" to cmd_get for -o parsing
		return cmd_get(argc - cmd_start - 1, argv + cmd_start + 1);
	}
	else if (strcmp(cmd, "delete") == 0)
	{
		BOOLEAN force = FALSE;
		const char *del_id = NULL;
		for (int i = cmd_start + 1; i < argc; i++)
		{
			if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0)
				force = TRUE;
			else if (!del_id)
				del_id = argv[i];
		}
		if (!del_id)
		{
			LOG_ERROR("ERROR: Missing file ID\n");
			PrintUsage();
			return 1;
		}
		return cmd_delete(del_id, force);
	}
	else if (strcmp(cmd, "export-key") == 0)
	{
		if (cmd_start + 1 >= argc)
		{
			LOG_ERROR("ERROR: Missing file ID\n");
			LOG_ERROR("Usage: wormhole export-key <file_id>\n");
			return 1;
		}
		return cmd_export_key(argv[cmd_start + 1]);
	}
	else if (strcmp(cmd, "import-key") == 0)
	{
		if (cmd_start + 2 >= argc)
		{
			LOG_ERROR("ERROR: Missing file ID or key\n");
			LOG_ERROR("Usage: wormhole import-key <file_id> <hex_key>\n");
			return 1;
		}
		return cmd_import_key(argv[cmd_start + 1], argv[cmd_start + 2]);
	}
	else if (strcmp(cmd, "files") == 0)
	{
		return cmd_files(argc - cmd_start - 1, argv + cmd_start + 1);
	}
	else if (strcmp(cmd, "status") == 0)
	{
		return cmd_status();
	}
	else if (strcmp(cmd, "peers") == 0)
	{
		return cmd_peers();
	}
	else if (strcmp(cmd, "daemon") == 0)
	{
		return cmd_daemon(argc - cmd_start, argv + cmd_start);
	}
	else if (strcmp(cmd, "config") == 0)
	{
		return cmd_config(argc - cmd_start, argv + cmd_start);
	}
	else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0)
	{
		PrintUsage();
		return 0;
	}
	else
	{
		LOG_ERROR("ERROR: Unknown command: %s\n\n", cmd);
		PrintUsage();
		return 1;
	}
}
