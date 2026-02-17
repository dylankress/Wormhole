//
// wormholed.c
// Wormhole persistent node daemon — manages QUIC listener, chunk store, and IPC.
// by Dylan Kress
//

#include "common.h"
#include "protocol.h"
#include "ipc.h"
#include "chunk_store.h"
#include "chunker.h"
#include "file_io.h"
#include "manifest.h"
#include "wire_format.h"
#include "crypto.h"
#include "config.h"

// Relay includes (optional relay connectivity)
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"

//=============================================================================
// Daemon Configuration
//=============================================================================

#define DAEMON_DEFAULT_PORT     WORMHOLE_DEFAULT_PORT   // 4567
#define DAEMON_RELAY_HOST       "wormholerelay.com"
#define DAEMON_RELAY_PORT       443
#define DAEMON_KEEPALIVE_SEC    30    // Relay keepalive interval
#define DAEMON_DISCOVERY_SEC   60    // Peer discovery interval

//=============================================================================
// Global MsQuic State
//=============================================================================

const QUIC_API_TABLE *MsQuic = NULL;
static HQUIC DaemonRegistration = NULL;
static HQUIC DaemonServerConfig = NULL;
static HQUIC DaemonClientConfig = NULL;
static HQUIC DaemonListener = NULL;

//=============================================================================
// Daemon Runtime State
//=============================================================================

typedef struct {
    // Shutdown flag
    volatile LONG       shutdown_requested;

    // QUIC
    HQUIC               listener;
    uint16_t            listen_port;

    // Relay
    RELAY_CLIENT       *relay_client;
    KEYPAIR             keypair;
    BOOLEAN             relay_enabled;

    // Discovered peers (refreshed periodically via relay)
    DISCOVERED_PEER     discovered_peers[MAX_FIND_PEERS];
    volatile LONG       discovered_peer_count;

    // Config
    WORMHOLE_CONFIG    *config;
    uint64_t            max_storage_bytes;

    // Stats
    volatile LONG       peer_count;
    volatile LONG       chunk_count;
    volatile LONGLONG   storage_used;
} DAEMON_STATE;

static DAEMON_STATE g_daemon = { 0 };

//=============================================================================
// Forward Declarations
//=============================================================================

static BOOLEAN Daemon_InitMsQuic(void);
static void    Daemon_CleanupMsQuic(void);
static BOOLEAN Daemon_LoadServerConfig(void);
static BOOLEAN Daemon_StartListener(void);
static void    Daemon_StopListener(void);
static BOOLEAN Daemon_ConnectRelay(void);

static uint32_t Daemon_HandleIpcCommand(
    uint8_t command, const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity, void *context);

static QUIC_STATUS QUIC_API Daemon_ListenerCallback(
    HQUIC Listener, void *Context, QUIC_LISTENER_EVENT *Event);
static QUIC_STATUS QUIC_API Daemon_ConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event);
static QUIC_STATUS QUIC_API Daemon_StreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

// Replication
static BOOLEAN Daemon_LoadClientConfig(void);
static void Daemon_ReplicateChunk(const uint8_t hash[WH_HASH_SIZE],
                                   const uint8_t *data, uint32_t size);

//=============================================================================
// Ctrl+C Shutdown Handler
//=============================================================================

#ifdef _WIN32
static BOOL WINAPI DaemonCtrlHandler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT)
    {
        if (InterlockedCompareExchange(&g_daemon.shutdown_requested, 1, 0) == 0)
        {
            LOG("\n[daemon] Shutdown requested, cleaning up...\n");
        }
        return TRUE;
    }
    return FALSE;
}
#endif

//=============================================================================
// MsQuic Initialization (duplicated — will be shared with wormhole.c later)
//=============================================================================

static BOOLEAN Daemon_InitMsQuic(void)
{
    QUIC_STATUS status;

    LOG("[daemon] Opening MsQuic library...\n");

    status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] MsQuicOpen2 failed: 0x%x\n", status);
        return FALSE;
    }

    const QUIC_REGISTRATION_CONFIG reg_config = {
        "wormholed",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY
    };

    status = MsQuic->RegistrationOpen(&reg_config, &DaemonRegistration);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] RegistrationOpen failed: 0x%x\n", status);
        MsQuicClose(MsQuic);
        MsQuic = NULL;
        return FALSE;
    }

    LOG("[daemon] MsQuic initialized\n");
    return TRUE;
}

static void Daemon_CleanupMsQuic(void)
{
    if (DaemonServerConfig)
    {
        MsQuic->ConfigurationClose(DaemonServerConfig);
        DaemonServerConfig = NULL;
    }
    if (DaemonClientConfig)
    {
        MsQuic->ConfigurationClose(DaemonClientConfig);
        DaemonClientConfig = NULL;
    }
    if (DaemonRegistration)
    {
        MsQuic->RegistrationClose(DaemonRegistration);
        DaemonRegistration = NULL;
    }
    if (MsQuic)
    {
        MsQuicClose(MsQuic);
        MsQuic = NULL;
    }
}

//=============================================================================
// Server Configuration (duplicated — will be shared later)
//=============================================================================

static BOOLEAN Daemon_LoadServerConfig(void)
{
    QUIC_STATUS status;

    LOG("[daemon] Loading server configuration...\n");

    // Generate or retrieve self-signed certificate
    char thumbprint[41];
    if (!GenerateSelfSignedCert(thumbprint, sizeof(thumbprint)))
    {
        LOG_ERROR("[daemon] Failed to generate/retrieve certificate\n");
        return FALSE;
    }

    LOG("[daemon] Certificate thumbprint: %s\n", thumbprint);

    // Convert thumbprint hex to binary
    QUIC_CERTIFICATE_HASH cert_hash;
    memset(&cert_hash, 0, sizeof(cert_hash));
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

    // QUIC settings optimized for persistent daemon
    QUIC_SETTINGS settings = { 0 };
    settings.IdleTimeoutMs = 300000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.DisconnectTimeoutMs = 300000;
    settings.IsSet.DisconnectTimeoutMs = TRUE;
    settings.KeepAliveIntervalMs = 10000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.ServerResumptionLevel = QUIC_SERVER_RESUME_ONLY;
    settings.IsSet.ServerResumptionLevel = TRUE;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;

    // Flow control
    settings.StreamRecvWindowDefault = 16777216;   // 16 MB
    settings.IsSet.StreamRecvWindowDefault = TRUE;
    settings.SendBufferingEnabled = TRUE;
    settings.IsSet.SendBufferingEnabled = TRUE;
    settings.ConnFlowControlWindow = 67108864;     // 64 MB
    settings.IsSet.ConnFlowControlWindow = TRUE;
    settings.InitialWindowPackets = 10;
    settings.IsSet.InitialWindowPackets = TRUE;

    // MTU
    settings.MinimumMtu = 1200;
    settings.IsSet.MinimumMtu = TRUE;
    settings.MaximumMtu = 1500;
    settings.IsSet.MaximumMtu = TRUE;

    // Create configuration
    QUIC_BUFFER alpn_buffer;
    alpn_buffer.Buffer = (uint8_t *)WORMHOLE_ALPN;
    alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

    status = MsQuic->ConfigurationOpen(
        DaemonRegistration, &alpn_buffer, 1,
        &settings, sizeof(settings),
        NULL, &DaemonServerConfig
    );
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] ConfigurationOpen failed: 0x%x\n", status);
        return FALSE;
    }

    status = MsQuic->ConfigurationLoadCredential(DaemonServerConfig, &cred_config);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] ConfigurationLoadCredential failed: 0x%x\n", status);
        MsQuic->ConfigurationClose(DaemonServerConfig);
        DaemonServerConfig = NULL;
        return FALSE;
    }

    LOG("[daemon] Server configuration loaded\n");
    return TRUE;
}

//=============================================================================
// QUIC Listener
//=============================================================================

static BOOLEAN Daemon_StartListener(void)
{
    QUIC_STATUS status;
    QUIC_ADDR addr = { 0 };
    QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&addr, g_daemon.listen_port);

    status = MsQuic->ListenerOpen(
        DaemonRegistration,
        Daemon_ListenerCallback,
        &g_daemon,
        &DaemonListener
    );
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] ListenerOpen failed: 0x%x\n", status);
        return FALSE;
    }

    QUIC_BUFFER alpn_buffer;
    alpn_buffer.Buffer = (uint8_t *)WORMHOLE_ALPN;
    alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

    status = MsQuic->ListenerStart(DaemonListener, &alpn_buffer, 1, &addr);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] ListenerStart failed: 0x%x\n", status);
        MsQuic->ListenerClose(DaemonListener);
        DaemonListener = NULL;
        return FALSE;
    }

    g_daemon.listener = DaemonListener;
    LOG("[daemon] QUIC listener started on port %u\n", g_daemon.listen_port);
    return TRUE;
}

static void Daemon_StopListener(void)
{
    if (DaemonListener && MsQuic)
    {
        MsQuic->ListenerStop(DaemonListener);
        MsQuic->ListenerClose(DaemonListener);
        DaemonListener = NULL;
        g_daemon.listener = NULL;
        LOG("[daemon] QUIC listener stopped\n");
    }
}

//=============================================================================
// QUIC Callbacks (minimal — handles incoming P2P connections)
//=============================================================================

static QUIC_STATUS QUIC_API Daemon_ListenerCallback(
    HQUIC Listener, void *Context, QUIC_LISTENER_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Listener);
    DAEMON_STATE *state = (DAEMON_STATE *)Context;

    switch (Event->Type)
    {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
        LOG("[daemon] Incoming QUIC connection\n");
        MsQuic->SetCallbackHandler(
            Event->NEW_CONNECTION.Connection,
            (void *)Daemon_ConnectionCallback,
            state
        );
        InterlockedIncrement(&state->peer_count);
        return MsQuic->ConnectionSetConfiguration(
            Event->NEW_CONNECTION.Connection,
            DaemonServerConfig
        );

    case QUIC_LISTENER_EVENT_STOP_COMPLETE:
        LOG("[daemon] Listener stop complete\n");
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API Daemon_ConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    DAEMON_STATE *state = (DAEMON_STATE *)Context;

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[daemon] Peer connected\n");
        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        LOG("[daemon] Peer opened stream\n");
        MsQuic->SetCallbackHandler(
            Event->PEER_STREAM_STARTED.Stream,
            (void *)Daemon_StreamCallback,
            state
        );
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        LOG("[daemon] Peer initiated shutdown (error: 0x%llx)\n",
            (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        InterlockedDecrement(&state->peer_count);
        MsQuic->ConnectionClose(Connection);
        LOG("[daemon] Peer disconnected (active peers: %ld)\n",
            InterlockedCompareExchange(&state->peer_count, 0, 0));
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Stream Callback — handles incoming chunk replication messages
//=============================================================================

// Buffer for accumulating stream receive data
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    HQUIC    stream;
} STREAM_RECV_CONTEXT;

static STREAM_RECV_CONTEXT *StreamRecvContext_Create(HQUIC stream)
{
    STREAM_RECV_CONTEXT *ctx = (STREAM_RECV_CONTEXT *)calloc(1, sizeof(STREAM_RECV_CONTEXT));
    if (!ctx) return NULL;

    ctx->capacity = CTRL_HEADER_SIZE + WH_CHUNK_SIZE + 64;  // Max possible message
    ctx->data = (uint8_t *)malloc(ctx->capacity);
    if (!ctx->data)
    {
        free(ctx);
        return NULL;
    }
    ctx->stream = stream;
    return ctx;
}

static void StreamRecvContext_Destroy(STREAM_RECV_CONTEXT *ctx)
{
    if (ctx)
    {
        free(ctx->data);
        free(ctx);
    }
}

// Process a complete control message from a peer
static void Daemon_HandlePeerMessage(HQUIC stream, uint8_t msg_type,
                                      const uint8_t *payload, uint32_t payload_len)
{
    switch (msg_type)
    {
    case CTRL_MSG_CHUNK_STORE_REQUEST:
    {
        // Payload: [32B hash][4B data_size][data]
        if (payload_len < WH_HASH_SIZE + 4)
        {
            LOG_ERROR("[daemon] CHUNK_STORE_REQUEST too short\n");
            return;
        }

        const uint8_t *hash = payload;
        uint32_t data_size = ReadUint32LE(payload + WH_HASH_SIZE);
        const uint8_t *chunk_data = payload + WH_HASH_SIZE + 4;

        if (payload_len < WH_HASH_SIZE + 4 + data_size)
        {
            LOG_ERROR("[daemon] CHUNK_STORE_REQUEST payload truncated\n");
            return;
        }

        LOG("[daemon] CHUNK_STORE_REQUEST: %u bytes\n", data_size);

        // Store the chunk
        BOOLEAN stored = ChunkStore_Put(hash, chunk_data, data_size);
        if (stored)
        {
            InterlockedIncrement(&g_daemon.chunk_count);
            InterlockedExchangeAdd64(&g_daemon.storage_used, (LONGLONG)data_size);
        }

        // Send ACK: [1B type][4B payload_len][32B hash][1B status]
        uint8_t ack[CTRL_HEADER_SIZE + WH_HASH_SIZE + 1];
        ack[0] = CTRL_MSG_CHUNK_STORE_ACK;
        WriteUint32LE(ack + 1, WH_HASH_SIZE + 1);
        memcpy(ack + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
        ack[CTRL_HEADER_SIZE + WH_HASH_SIZE] = stored ? 0x00 : 0x01;

        QUIC_BUFFER send_buf;
        send_buf.Buffer = ack;
        send_buf.Length = sizeof(ack);
        MsQuic->StreamSend(stream, &send_buf, 1, QUIC_SEND_FLAG_NONE, NULL);
        break;
    }

    case CTRL_MSG_CHUNK_QUERY:
    {
        // Payload: [32B hash]
        if (payload_len < WH_HASH_SIZE)
        {
            LOG_ERROR("[daemon] CHUNK_QUERY too short\n");
            return;
        }

        const uint8_t *hash = payload;
        BOOLEAN has = ChunkStore_Has(hash);

        if (has)
        {
            // Read chunk and send response with data
            uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
            uint32_t chunk_size = 0;

            if (chunk_buf && ChunkStore_Get(hash, chunk_buf, &chunk_size))
            {
                // Response: [1B type][4B payload_len][32B hash][1B status=0x00][4B size][data]
                uint32_t resp_payload = WH_HASH_SIZE + 1 + 4 + chunk_size;
                uint8_t *resp = (uint8_t *)malloc(CTRL_HEADER_SIZE + resp_payload);

                if (resp)
                {
                    resp[0] = CTRL_MSG_CHUNK_QUERY_RESPONSE;
                    WriteUint32LE(resp + 1, resp_payload);
                    memcpy(resp + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
                    resp[CTRL_HEADER_SIZE + WH_HASH_SIZE] = 0x00;  // has=yes
                    WriteUint32LE(resp + CTRL_HEADER_SIZE + WH_HASH_SIZE + 1, chunk_size);
                    memcpy(resp + CTRL_HEADER_SIZE + WH_HASH_SIZE + 5, chunk_buf, chunk_size);

                    QUIC_BUFFER send_buf;
                    send_buf.Buffer = resp;
                    send_buf.Length = CTRL_HEADER_SIZE + resp_payload;
                    MsQuic->StreamSend(stream, &send_buf, 1, QUIC_SEND_FLAG_NONE, resp);
                    // resp freed in SEND_COMPLETE
                }
            }
            free(chunk_buf);
        }
        else
        {
            // Response: [1B type][4B payload_len][32B hash][1B status=0x01]
            uint8_t resp[CTRL_HEADER_SIZE + WH_HASH_SIZE + 1];
            resp[0] = CTRL_MSG_CHUNK_QUERY_RESPONSE;
            WriteUint32LE(resp + 1, WH_HASH_SIZE + 1);
            memcpy(resp + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
            resp[CTRL_HEADER_SIZE + WH_HASH_SIZE] = 0x01;  // has=no

            QUIC_BUFFER send_buf;
            send_buf.Buffer = resp;
            send_buf.Length = sizeof(resp);
            MsQuic->StreamSend(stream, &send_buf, 1, QUIC_SEND_FLAG_NONE, NULL);
        }
        break;
    }

    case CTRL_MSG_CHUNK_STORE_ACK:
    {
        // Payload: [32B hash][1B status]
        if (payload_len < WH_HASH_SIZE + 1) return;

        const uint8_t *hash = payload;
        uint8_t status = payload[WH_HASH_SIZE];

        if (status == 0x00)
        {
            LOG("[daemon] Chunk stored by peer (ACK)\n");
            // TODO: Extract peer_id from connection context and record
            // ChunkStore_SetReplicaLocation(hash, peer_id);
        }
        else
        {
            LOG("[daemon] Peer rejected chunk store (status: 0x%02x)\n", status);
        }
        break;
    }

    default:
        LOG("[daemon] Unknown peer message type: 0x%02x\n", msg_type);
        break;
    }
}

static QUIC_STATUS QUIC_API Daemon_StreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    DAEMON_STATE *state = (DAEMON_STATE *)Context;
    UNREFERENCED_PARAMETER(state);

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // Accumulate data and parse control messages
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            const uint8_t *data = buf->Buffer;
            uint32_t remaining = buf->Length;

            while (remaining >= CTRL_HEADER_SIZE)
            {
                uint8_t msg_type = data[0];
                uint32_t payload_len = ReadUint32LE(data + 1);

                if (remaining < CTRL_HEADER_SIZE + payload_len)
                {
                    break;  // Incomplete message, wait for more data
                }

                Daemon_HandlePeerMessage(Stream, msg_type,
                    data + CTRL_HEADER_SIZE, payload_len);

                uint32_t consumed = CTRL_HEADER_SIZE + payload_len;
                data += consumed;
                remaining -= consumed;
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        // Free dynamically allocated send buffers
        if (Event->SEND_COMPLETE.ClientContext)
        {
            free(Event->SEND_COMPLETE.ClientContext);
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        LOG("[daemon] Peer stream send shutdown\n");
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Client Configuration (for outbound QUIC connections to peers)
//=============================================================================

static BOOLEAN Daemon_LoadClientConfig(void)
{
    QUIC_STATUS status;

    QUIC_CREDENTIAL_CONFIG cred_config;
    memset(&cred_config, 0, sizeof(cred_config));
    cred_config.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred_config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    QUIC_SETTINGS settings = { 0 };
    settings.IdleTimeoutMs = 30000;   // 30s idle timeout for replication connections
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.KeepAliveIntervalMs = 10000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.StreamRecvWindowDefault = 16777216;
    settings.IsSet.StreamRecvWindowDefault = TRUE;
    settings.SendBufferingEnabled = TRUE;
    settings.IsSet.SendBufferingEnabled = TRUE;

    QUIC_BUFFER alpn_buffer;
    alpn_buffer.Buffer = (uint8_t *)WORMHOLE_ALPN;
    alpn_buffer.Length = (uint32_t)strlen(WORMHOLE_ALPN);

    status = MsQuic->ConfigurationOpen(
        DaemonRegistration, &alpn_buffer, 1,
        &settings, sizeof(settings),
        NULL, &DaemonClientConfig
    );
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] Client ConfigurationOpen failed: 0x%x\n", status);
        return FALSE;
    }

    status = MsQuic->ConfigurationLoadCredential(DaemonClientConfig, &cred_config);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[daemon] Client ConfigurationLoadCredential failed: 0x%x\n", status);
        MsQuic->ConfigurationClose(DaemonClientConfig);
        DaemonClientConfig = NULL;
        return FALSE;
    }

    LOG("[daemon] Client configuration loaded\n");
    return TRUE;
}

//=============================================================================
// Chunk Replication — replicate to discovered peers
//=============================================================================

// Outbound replication connection callback
static QUIC_STATUS QUIC_API Daemon_ReplicaConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[replicate] Connected to peer for replication\n");
        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        MsQuic->SetCallbackHandler(
            Event->PEER_STREAM_STARTED.Stream,
            (void *)Daemon_StreamCallback,
            &g_daemon
        );
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->ConnectionClose(Connection);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Outbound replication stream callback (for ACK responses)
static QUIC_STATUS QUIC_API Daemon_ReplicaStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // Parse CHUNK_STORE_ACK responses
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (buf->Length >= CTRL_HEADER_SIZE)
            {
                uint8_t msg_type = buf->Buffer[0];
                uint32_t payload_len = ReadUint32LE(buf->Buffer + 1);
                if (buf->Length >= CTRL_HEADER_SIZE + payload_len)
                {
                    Daemon_HandlePeerMessage(Stream, msg_type,
                        buf->Buffer + CTRL_HEADER_SIZE, payload_len);
                }
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        if (Event->SEND_COMPLETE.ClientContext)
        {
            free(Event->SEND_COMPLETE.ClientContext);
        }
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static void Daemon_ReplicateChunk(const uint8_t hash[WH_HASH_SIZE],
                                   const uint8_t *data, uint32_t size)
{
    if (!DaemonClientConfig || !MsQuic)
    {
        return;
    }

    // Check current replica count
    uint32_t replicas = ChunkStore_GetReplicaCount(hash);
    if (replicas + 1 >= REPLICATION_TARGET)  // +1 for our local copy
    {
        return;  // Already at target
    }

    uint32_t needed = REPLICATION_TARGET - 1 - replicas;
    LONG peer_count = InterlockedCompareExchange(&g_daemon.discovered_peer_count, 0, 0);
    if (peer_count == 0)
    {
        return;  // No peers available
    }

    LOG("[replicate] Replicating chunk to %u peers (current replicas: %u)\n",
        needed, replicas);

    // Build the store request message
    // [1B type][4B payload_len][32B hash][4B data_size][data]
    uint32_t payload_len = WH_HASH_SIZE + 4 + size;
    uint32_t msg_size = CTRL_HEADER_SIZE + payload_len;
    uint8_t *msg = (uint8_t *)malloc(msg_size);
    if (!msg) return;

    msg[0] = CTRL_MSG_CHUNK_STORE_REQUEST;
    WriteUint32LE(msg + 1, payload_len);
    memcpy(msg + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
    WriteUint32LE(msg + CTRL_HEADER_SIZE + WH_HASH_SIZE, size);
    memcpy(msg + CTRL_HEADER_SIZE + WH_HASH_SIZE + 4, data, size);

    uint32_t sent = 0;
    for (LONG i = 0; i < peer_count && sent < needed; i++)
    {
        DISCOVERED_PEER *peer = &g_daemon.discovered_peers[i];
        if (peer->endpoint_count == 0) continue;

        // Use first endpoint with IPv4
        const ENDPOINT *ep = NULL;
        for (uint16_t j = 0; j < peer->endpoint_count; j++)
        {
            if (peer->endpoints[j].addr_type == 0x04)
            {
                ep = &peer->endpoints[j];
                break;
            }
        }
        if (!ep) continue;

        // Build target address string
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "%u.%u.%u.%u",
                 ep->addr[0], ep->addr[1], ep->addr[2], ep->addr[3]);

        LOG("[replicate] Connecting to %s:%u\n", addr_str, ep->port);

        // Open a QUIC connection to the peer
        HQUIC connection = NULL;
        QUIC_STATUS status = MsQuic->ConnectionOpen(
            DaemonRegistration,
            Daemon_ReplicaConnectionCallback,
            NULL,
            &connection
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] ConnectionOpen failed: 0x%x\n", status);
            continue;
        }

        status = MsQuic->ConnectionStart(
            connection,
            DaemonClientConfig,
            QUIC_ADDRESS_FAMILY_INET,
            addr_str,
            ep->port
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] ConnectionStart failed: 0x%x\n", status);
            MsQuic->ConnectionClose(connection);
            continue;
        }

        // Open a bidirectional stream
        HQUIC stream = NULL;
        status = MsQuic->StreamOpen(
            connection,
            QUIC_STREAM_OPEN_FLAG_NONE,
            Daemon_ReplicaStreamCallback,
            NULL,
            &stream
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] StreamOpen failed: 0x%x\n", status);
            MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            continue;
        }

        status = MsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] StreamStart failed: 0x%x\n", status);
            MsQuic->StreamClose(stream);
            MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            continue;
        }

        // Send the CHUNK_STORE_REQUEST (copy msg since it may be freed before send completes)
        uint8_t *send_copy = (uint8_t *)malloc(msg_size);
        if (send_copy)
        {
            memcpy(send_copy, msg, msg_size);

            QUIC_BUFFER send_buf;
            send_buf.Buffer = send_copy;
            send_buf.Length = msg_size;
            MsQuic->StreamSend(stream, &send_buf, 1,
                QUIC_SEND_FLAG_FIN, send_copy);  // FIN after sending
        }

        sent++;
    }

    free(msg);

    if (sent > 0)
    {
        LOG("[replicate] Sent chunk to %u peers\n", sent);
    }
}

//=============================================================================
// Relay Connection (optional)
//=============================================================================

static void Daemon_OnRelayConnected(void *context, uint64_t session_id,
    const uint8_t observed_addr[16], uint8_t observed_addr_type, uint16_t observed_port)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(observed_addr);
    UNREFERENCED_PARAMETER(observed_addr_type);
    UNREFERENCED_PARAMETER(observed_port);
    LOG("[daemon] Relay connected (session %llu)\n", (unsigned long long)session_id);
}

static void Daemon_OnRelayDisconnected(void *context)
{
    UNREFERENCED_PARAMETER(context);
    LOG("[daemon] Relay disconnected\n");
}

static void Daemon_OnPeersFound(void *context, const DISCOVERED_PEER *peers, uint16_t peer_count)
{
    UNREFERENCED_PARAMETER(context);

    if (peer_count == 0)
    {
        LOG("[daemon] Peer discovery: no active peers found\n");
        InterlockedExchange(&g_daemon.discovered_peer_count, 0);
        return;
    }

    // Update local peer table
    uint16_t count = peer_count < MAX_FIND_PEERS ? peer_count : MAX_FIND_PEERS;
    memcpy(g_daemon.discovered_peers, peers, count * sizeof(DISCOVERED_PEER));
    InterlockedExchange(&g_daemon.discovered_peer_count, (LONG)count);

    LOG("[daemon] Peer discovery: found %u active peers\n", count);
    for (uint16_t i = 0; i < count; i++)
    {
        char hex[9];
        for (int j = 0; j < 4; j++)
        {
            sprintf(hex + j * 2, "%02x", peers[i].peer_id[j]);
        }
        LOG("[daemon]   Peer %u: %s... (%u endpoints)\n",
            i, hex, peers[i].endpoint_count);
    }
}

static BOOLEAN Daemon_ConnectRelay(void)
{
    // Load or generate identity keypair
    char* identity_path = PeerID_GetDefaultPath();
    if (!PeerID_LoadOrGenerate(&g_daemon.keypair, identity_path))
    {
        LOG_ERROR("[daemon] Failed to load/generate keypair\n");
        free(identity_path);
        return FALSE;
    }
    free(identity_path);

    char peer_hex[65];
    PeerID_ToHex(g_daemon.keypair.public_key, peer_hex);
    LOG("[daemon] PeerID: %s\n", peer_hex);

    // Discover our endpoints
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);

    LOG("[daemon] Discovered %u endpoints\n", endpoint_count);

    // Create relay client
    RELAY_CLIENT_CONFIG relay_config = {
        .relay_host       = DAEMON_RELAY_HOST,
        .relay_port       = DAEMON_RELAY_PORT,
        .local_port       = 0,
        .keypair          = &g_daemon.keypair,
        .on_connected     = Daemon_OnRelayConnected,
        .on_disconnected  = Daemon_OnRelayDisconnected,
        .on_ticket_created = NULL,
        .on_peer_info     = NULL,
        .on_peers_found   = Daemon_OnPeersFound,
        .callback_context = &g_daemon,
    };

    g_daemon.relay_client = RelayClient_Create(&relay_config);
    if (!g_daemon.relay_client)
    {
        LOG_ERROR("[daemon] Failed to create relay client\n");
        return FALSE;
    }

    // Register with relay
    if (!RelayClient_Register(g_daemon.relay_client, endpoints, endpoint_count))
    {
        LOG_ERROR("[daemon] Failed to register with relay\n");
        RelayClient_Destroy(g_daemon.relay_client);
        g_daemon.relay_client = NULL;
        return FALSE;
    }

    g_daemon.relay_enabled = TRUE;
    LOG("[daemon] Relay registration sent\n");
    return TRUE;
}

//=============================================================================
// Quota Enforcement
//=============================================================================

static void Daemon_EnforceQuota(uint64_t additional_bytes)
{
    if (g_daemon.max_storage_bytes == 0) return;  // No quota

    uint64_t current = (uint64_t)InterlockedCompareExchange64(&g_daemon.storage_used, 0, 0);
    uint64_t projected = current + additional_bytes;

    if (projected > g_daemon.max_storage_bytes)
    {
        uint64_t overage = projected - g_daemon.max_storage_bytes;
        LOG("[daemon] Storage quota would be exceeded by %llu bytes, evicting...\n",
            (unsigned long long)overage);
        uint64_t freed = ChunkStore_Evict(overage);
        if (freed > 0)
        {
            InterlockedExchangeAdd64(&g_daemon.storage_used, -(LONGLONG)freed);
        }
    }
}

//=============================================================================
// IPC Command Handler
//=============================================================================

static uint32_t Daemon_HandleIpcCommand(
    uint8_t command, const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity, void *context)
{
    UNREFERENCED_PARAMETER(context);

    switch (command)
    {
    //--- STORE: chunk a file and store all chunks ---
    case IPC_CMD_STORE:
    {
        if (payload_size < 2)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Payload: [2B filename_len][filename]
        uint16_t name_len = ReadUint16LE(payload);
        if (name_len == 0 || name_len > payload_size - 2)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Extract null-terminated file path
        char filepath[MAX_PATH];
        uint16_t copy_len = name_len < MAX_PATH - 1 ? name_len : MAX_PATH - 1;
        memcpy(filepath, payload + 2, copy_len);
        filepath[copy_len] = '\0';

        LOG("[daemon] STORE request: %s\n", filepath);

        // Check file exists
        if (!FileExists(filepath))
        {
            LOG_ERROR("[daemon] File not found: %s\n", filepath);
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        // Build manifest (chunks + hashes the file)
        FILE_MANIFEST *manifest = Chunker_BuildManifest(filepath);
        if (!manifest)
        {
            LOG_ERROR("[daemon] Failed to build manifest for: %s\n", filepath);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Enforce quota before storing
        Daemon_EnforceQuota(manifest->file_size);

        // Store each chunk
        FILE *fh = NULL;
        if (!OpenFileForRead(filepath, &fh))
        {
            LOG_ERROR("[daemon] Failed to open file: %s\n", filepath);
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!chunk_buf)
        {
            CloseFile(fh);
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        uint32_t stored = 0;
        for (uint32_t i = 0; i < manifest->chunk_count; i++)
        {
            size_t bytes_read = 0;
            if (!ReadFileChunk(fh, chunk_buf, manifest->chunks[i].chunk_size, &bytes_read))
            {
                LOG_ERROR("[daemon] Failed to read chunk %u\n", i);
                break;
            }

            if (ChunkStore_Put(manifest->chunks[i].hash, chunk_buf, (uint32_t)bytes_read))
            {
                stored++;
                InterlockedIncrement(&g_daemon.chunk_count);
                InterlockedExchangeAdd64(&g_daemon.storage_used, (LONGLONG)bytes_read);
            }
        }

        CloseFile(fh);

        LOG("[daemon] Stored %u/%u chunks for %s\n",
            stored, manifest->chunk_count, filepath);

        // Trigger replication to discovered peers (async, non-blocking)
        for (uint32_t i = 0; i < manifest->chunk_count; i++)
        {
            uint32_t chunk_size = 0;
            if (ChunkStore_Get(manifest->chunks[i].hash, chunk_buf, &chunk_size))
            {
                Daemon_ReplicateChunk(manifest->chunks[i].hash, chunk_buf, chunk_size);
            }
        }

        free(chunk_buf);

        // Response: [status][32B manifest_hash][4B chunk_count]
        if (response_capacity < 1 + WH_HASH_SIZE + 4)
        {
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        response_out[0] = IPC_STATUS_OK;
        memcpy(response_out + 1, manifest->manifest_hash, WH_HASH_SIZE);
        WriteUint32LE(response_out + 1 + WH_HASH_SIZE, manifest->chunk_count);
        Manifest_Destroy(manifest);

        return 1 + WH_HASH_SIZE + 4;  // 37 bytes
    }

    //--- GET: retrieve a single chunk by hash ---
    case IPC_CMD_GET:
    {
        if (payload_size < WH_HASH_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        const uint8_t *hash = payload;

        // Check if chunk exists
        if (!ChunkStore_Has(hash))
        {
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        // Read chunk data
        uint32_t data_size = 0;
        // Response: [status][4B data_size][data]
        if (response_capacity < 1 + 4 + WH_CHUNK_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        if (!ChunkStore_Get(hash, response_out + 1 + 4, &data_size))
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        response_out[0] = IPC_STATUS_OK;
        WriteUint32LE(response_out + 1, data_size);

        return 1 + 4 + data_size;
    }

    //--- STATUS: return daemon stats ---
    case IPC_CMD_STATUS:
    {
        // Response: [status][4B peers][4B chunks][8B storage][1B relay][1B listener]
        if (response_capacity < 1 + 4 + 4 + 8 + 1 + 1)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        response_out[0] = IPC_STATUS_OK;
        WriteUint32LE(response_out + 1,
            (uint32_t)InterlockedCompareExchange(&g_daemon.peer_count, 0, 0));
        WriteUint32LE(response_out + 5,
            (uint32_t)InterlockedCompareExchange(&g_daemon.chunk_count, 0, 0));
        WriteUint64LE(response_out + 9,
            (uint64_t)InterlockedCompareExchange64(&g_daemon.storage_used, 0, 0));
        response_out[17] = g_daemon.relay_client &&
            RelayClient_IsConnected(g_daemon.relay_client) ? 1 : 0;
        response_out[18] = DaemonListener ? 1 : 0;

        return 19;
    }

    //--- SHUTDOWN: clean daemon shutdown ---
    case IPC_CMD_SHUTDOWN:
    {
        LOG("[daemon] Shutdown requested via IPC\n");
        InterlockedExchange(&g_daemon.shutdown_requested, 1);
        response_out[0] = IPC_STATUS_OK;
        return 1;
    }

    default:
        LOG_ERROR("[daemon] Unknown IPC command: 0x%02x\n", command);
        response_out[0] = IPC_STATUS_ERROR;
        return 1;
    }
}

//=============================================================================
// Daemon Startup & Main Loop
//=============================================================================

static void Daemon_PrintBanner(void)
{
    LOG("=== wormholed - Wormhole Persistent Node Daemon ===\n");
    LOG("  Port:  %u\n", g_daemon.listen_port);
    LOG("  Relay: %s\n", g_daemon.relay_enabled ? "enabled" : "disabled");
    LOG("  IPC:   %s\n", IPC_PIPE_NAME);
    LOG("================================================\n");
}

static void Daemon_PrintUsage(void)
{
    printf("Usage: wormholed [options]\n");
    printf("Options:\n");
    printf("  --port <port>     QUIC listener port (default: %u)\n", DAEMON_DEFAULT_PORT);
    printf("  --no-relay        Disable relay connection\n");
    printf("  --help            Show this help\n");
}

int main(int argc, char *argv[])
{
    // Parse command-line arguments
    g_daemon.listen_port = DAEMON_DEFAULT_PORT;
    g_daemon.relay_enabled = TRUE;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            g_daemon.listen_port = (uint16_t)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--no-relay") == 0)
        {
            g_daemon.relay_enabled = FALSE;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            Daemon_PrintUsage();
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            Daemon_PrintUsage();
            return 1;
        }
    }

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        LOG_ERROR("[daemon] WSAStartup failed\n");
        return 1;
    }

    // Register Ctrl+C handler
    SetConsoleCtrlHandler(DaemonCtrlHandler, TRUE);
#endif

    // Step 0: Load configuration
    g_daemon.config = Config_LoadDefault();
    if (g_daemon.config)
    {
        uint64_t max_gb = Config_GetUint64(g_daemon.config, "max_storage_gb",
                                            CONFIG_DEFAULT_MAX_STORAGE_GB);
        g_daemon.max_storage_bytes = max_gb * 1024ULL * 1024ULL * 1024ULL;
        LOG("[daemon] Storage quota: %llu GB\n", (unsigned long long)max_gb);

        // Use config for replication target if needed
        uint64_t repl = Config_GetUint64(g_daemon.config, "replication_target",
                                          CONFIG_DEFAULT_REPLICATION_TARGET);
        LOG("[daemon] Replication target: %llu\n", (unsigned long long)repl);
    }
    else
    {
        g_daemon.max_storage_bytes = CONFIG_DEFAULT_MAX_STORAGE_GB * 1024ULL * 1024ULL * 1024ULL;
        LOG("[daemon] Using default config (quota: %u GB)\n", CONFIG_DEFAULT_MAX_STORAGE_GB);
    }

    Daemon_PrintBanner();

    // Step 1: Initialize chunk store
    LOG("[daemon] Initializing chunk store...\n");
    if (!ChunkStore_Init())
    {
        LOG_ERROR("[daemon] Failed to initialize chunk store\n");
        goto cleanup;
    }
    LOG("[daemon] Chunk store ready\n");

    // Step 2: Initialize MsQuic
    if (!Daemon_InitMsQuic())
    {
        LOG_ERROR("[daemon] Failed to initialize MsQuic\n");
        goto cleanup;
    }

    // Step 3: Load server configuration (TLS cert)
    if (!Daemon_LoadServerConfig())
    {
        LOG_ERROR("[daemon] Failed to load server configuration\n");
        goto cleanup;
    }

    // Step 3b: Load client configuration (for outbound replication)
    if (!Daemon_LoadClientConfig())
    {
        LOG("[daemon] Client config failed (replication disabled)\n");
    }

    // Step 4: Start QUIC listener
    if (!Daemon_StartListener())
    {
        LOG_ERROR("[daemon] Failed to start QUIC listener\n");
        goto cleanup;
    }

    // Step 5: Start IPC server
    if (!IpcServer_Start(Daemon_HandleIpcCommand, &g_daemon))
    {
        LOG_ERROR("[daemon] Failed to start IPC server\n");
        goto cleanup;
    }

    // Step 6: Optionally connect to relay
    if (g_daemon.relay_enabled)
    {
        if (!Daemon_ConnectRelay())
        {
            LOG("[daemon] Relay connection failed (continuing without relay)\n");
            g_daemon.relay_enabled = FALSE;
        }
    }

    LOG("[daemon] Daemon is running. Press Ctrl+C to stop.\n");

    // Main loop: poll relay + check shutdown flag
    time_t last_keepalive = time(NULL);
    time_t last_discovery = 0;  // Trigger immediately on first loop

    while (InterlockedCompareExchange(&g_daemon.shutdown_requested, 0, 0) == 0)
    {
        // Poll relay for incoming messages (non-blocking)
        if (g_daemon.relay_client && RelayClient_IsConnected(g_daemon.relay_client))
        {
            RelayClient_Poll(g_daemon.relay_client, 100);  // 100ms timeout

            time_t now = time(NULL);

            // Send keepalive periodically
            if (now - last_keepalive >= DAEMON_KEEPALIVE_SEC)
            {
                RelayClient_SendKeepalive(g_daemon.relay_client);
                last_keepalive = now;
            }

            // Discover peers periodically
            if (now - last_discovery >= DAEMON_DISCOVERY_SEC)
            {
                RelayClient_FindPeers(g_daemon.relay_client, 20);
                last_discovery = now;
            }
        }
        else
        {
            // No relay — just sleep to avoid busy-loop
            Sleep(100);
        }
    }

    LOG("[daemon] Shutting down...\n");

cleanup:
    // Stop IPC server
    IpcServer_Stop();

    // Disconnect relay
    if (g_daemon.relay_client)
    {
        RelayClient_SendGoodbye(g_daemon.relay_client, 0x01);
        RelayClient_Destroy(g_daemon.relay_client);
        g_daemon.relay_client = NULL;
    }

    // Stop QUIC listener
    Daemon_StopListener();

    // Cleanup MsQuic
    Daemon_CleanupMsQuic();

#ifdef _WIN32
    WSACleanup();
#endif

    // Free config
    if (g_daemon.config)
    {
        Config_Destroy(g_daemon.config);
        g_daemon.config = NULL;
    }

    LOG("[daemon] Shutdown complete\n");
    return 0;
}
