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
#include <sodium.h>
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"

// DHT includes (Phase 4: decentralized network)
#include "dht/dht_node.h"
#include "dht/dht_store.h"

// Phase 4 includes
#include "proof.h"
#include "incentives.h"
#include "health.h"
#include "erasure.h"
#include "file_registry.h"
#include "file_crypto.h"
#include "../deps/blake3/blake3.h"

// POSIX includes for Linux EC recovery directory scan
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

//=============================================================================
// Daemon Configuration
//=============================================================================

#define DAEMON_DEFAULT_PORT     WORMHOLE_DEFAULT_PORT   // 4567
#define DAEMON_RELAY_HOST       "wormholerelay.com"
#define DAEMON_RELAY_PORT       443
#define DAEMON_KEEPALIVE_SEC    30    // Relay keepalive interval
#define DAEMON_DISCOVERY_SEC   30    // Peer discovery interval

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
    volatile int32_t    shutdown_requested;

    // QUIC
    HQUIC               listener;
    uint16_t            listen_port;

    // Relay
    RELAY_CLIENT       *relay_client;
    KEYPAIR             keypair;
    BOOLEAN             relay_enabled;

    // Discovered peers (refreshed periodically via relay)
    DISCOVERED_PEER     discovered_peers[MAX_FIND_PEERS];
    volatile int32_t    discovered_peer_count;

    // Config
    WORMHOLE_CONFIG    *config;
    uint64_t            max_storage_bytes;

    // DHT (Phase 4)
    DHT_NODE            dht_node;
    BOOLEAN             dht_enabled;
    uint16_t            dht_port;

    // Incentives (Phase 4)
    STORAGE_LEDGER      ledger;

    // Our own endpoints (saved for hole-punch)
    ENDPOINT            our_endpoints[MAX_ENDPOINTS];
    uint16_t            our_endpoint_count;

    // Stats
    volatile int32_t    peer_count;
    volatile int32_t    chunk_count;
    volatile int64_t    storage_used;

    // Pending DHT announcements (deferred when routing table empty)
    uint8_t             pending_announce_hashes[16][WH_HASH_SIZE];
    uint32_t            pending_announce_count;

    // Fix 5: Replication retry queue
    struct {
        uint8_t   hash[WH_HASH_SIZE];
        time_t    retry_after;
        uint32_t  attempt_count;
    }                   retry_queue[32];
    uint32_t            retry_count;
    WH_MUTEX            retry_lock;

    // Synchronization
    WH_MUTEX            ledger_lock;
    WH_MUTEX            dht_lock;
    WH_MUTEX            peers_lock;
} DAEMON_STATE;

static DAEMON_STATE g_daemon = { 0 };

//=============================================================================
// Background Work Queue
//=============================================================================

typedef enum {
    WORK_ERASURE_ENCODE,
    WORK_REPLICATE_CHUNK,
    WORK_REPLICATE_BATCH,
    WORK_PRECOMPUTE_PROOFS,
    WORK_DHT_ANNOUNCE,
    WORK_PERSIST_EC_META,
    WORK_CHECK_REPLICATION,
} WORK_TYPE;

#define MAX_BATCH_CHUNKS 64  // ~16MB max per batch (64 * 256KB)

typedef struct WORK_ITEM {
    WORK_TYPE type;
    struct WORK_ITEM *next;
    union {
        // WORK_ERASURE_ENCODE
        struct {
            uint8_t  *manifest_data;     // Serialized manifest (owned)
            size_t    manifest_size;
            uint8_t   manifest_hash[WH_HASH_SIZE];
        } erasure;
        // WORK_REPLICATE_CHUNK
        struct {
            uint8_t   hash[WH_HASH_SIZE];
            uint8_t  *data;              // Chunk data (owned)
            uint32_t  size;
        } replicate;
        // WORK_REPLICATE_BATCH
        struct {
            uint32_t  chunk_count;
            uint8_t   hashes[MAX_BATCH_CHUNKS][WH_HASH_SIZE];
            uint8_t  *data[MAX_BATCH_CHUNKS];    // Chunk data pointers (owned)
            uint32_t  sizes[MAX_BATCH_CHUNKS];
        } batch;
        // WORK_PRECOMPUTE_PROOFS
        struct {
            uint8_t   hash[WH_HASH_SIZE];
            uint8_t  *data;              // Chunk data (owned)
            uint32_t  size;
        } proofs;
        // WORK_DHT_ANNOUNCE
        struct {
            uint8_t   hashes[64][WH_HASH_SIZE];
            uint32_t  hash_count;
        } dht_announce;
        // WORK_PERSIST_EC_META
        struct {
            EC_GROUP *ec_group;           // Owned
            uint8_t  *manifest_data;     // Serialized manifest (owned)
            size_t    manifest_size;
            uint8_t   manifest_hash[WH_HASH_SIZE];
        } ec_meta;
        // WORK_CHECK_REPLICATION
        struct {
            uint8_t   manifest_hash[WH_HASH_SIZE];
        } check_repl;
    };
} WORK_ITEM;

typedef struct {
    WORK_ITEM       *head;
    WORK_ITEM       *tail;
    WH_MUTEX         lock;
    WH_SEMAPHORE     semaphore;
    WH_THREAD        thread;
    volatile int32_t shutdown;
} WORK_QUEUE;

static WORK_QUEUE g_work_queue = { 0 };

// Forward declarations for work queue
static WH_THREAD_RETURN WorkQueue_ThreadProc(WH_THREAD_PARAM param);
static void WorkQueue_FreeItem(WORK_ITEM *item);

static BOOLEAN WorkQueue_Init(void)
{
    WH_MUTEX_INIT(g_work_queue.lock);
    WH_SEM_CREATE(g_work_queue.semaphore, 0x7FFFFFFF);
    g_work_queue.shutdown = 0;
    g_work_queue.head = NULL;
    g_work_queue.tail = NULL;
    WH_THREAD_CREATE(g_work_queue.thread, WorkQueue_ThreadProc, NULL);
    return TRUE;
}

static void WorkQueue_Push(WORK_ITEM *item)
{
    item->next = NULL;
    WH_MUTEX_LOCK(g_work_queue.lock);
    if (g_work_queue.tail)
    {
        g_work_queue.tail->next = item;
        g_work_queue.tail = item;
    }
    else
    {
        g_work_queue.head = item;
        g_work_queue.tail = item;
    }
    WH_MUTEX_UNLOCK(g_work_queue.lock);
    WH_SEM_POST(g_work_queue.semaphore);
}

static WORK_ITEM *WorkQueue_Pop(void)
{
    int result = WH_SEM_WAIT(g_work_queue.semaphore, 1000);
    if (result != 0) return NULL;

    WH_MUTEX_LOCK(g_work_queue.lock);
    WORK_ITEM *item = g_work_queue.head;
    if (item)
    {
        g_work_queue.head = item->next;
        if (!g_work_queue.head) g_work_queue.tail = NULL;
        item->next = NULL;
    }
    WH_MUTEX_UNLOCK(g_work_queue.lock);
    return item;
}

static void WorkQueue_Shutdown(void)
{
    WH_ATOMIC_SET(&g_work_queue.shutdown, 1);
    WH_SEM_POST(g_work_queue.semaphore);  // Wake thread
    WH_THREAD_JOIN(g_work_queue.thread, 5000);
    // Free remaining items
    WH_MUTEX_LOCK(g_work_queue.lock);
    WORK_ITEM *item = g_work_queue.head;
    while (item)
    {
        WORK_ITEM *next = item->next;
        WorkQueue_FreeItem(item);
        item = next;
    }
    g_work_queue.head = NULL;
    g_work_queue.tail = NULL;
    WH_MUTEX_UNLOCK(g_work_queue.lock);
    WH_SEM_DESTROY(g_work_queue.semaphore);
    WH_MUTEX_DESTROY(g_work_queue.lock);
}

static void WorkQueue_FreeItem(WORK_ITEM *item)
{
    if (!item) return;
    switch (item->type)
    {
    case WORK_ERASURE_ENCODE:
        free(item->erasure.manifest_data);
        break;
    case WORK_REPLICATE_CHUNK:
        free(item->replicate.data);
        break;
    case WORK_REPLICATE_BATCH:
        for (uint32_t i = 0; i < item->batch.chunk_count; i++)
            free(item->batch.data[i]);
        break;
    case WORK_PRECOMPUTE_PROOFS:
        free(item->proofs.data);
        break;
    case WORK_DHT_ANNOUNCE:
        break;  // No heap data
    case WORK_PERSIST_EC_META:
        if (item->ec_meta.ec_group)
            ErasureCoding_DestroyGroup(item->ec_meta.ec_group);
        free(item->ec_meta.manifest_data);
        break;
    case WORK_CHECK_REPLICATION:
        break;  // No heap data
    }
    free(item);
}

// Per-connection context for inbound peers (enables per-peer ledger tracking)
typedef struct {
    DAEMON_STATE *daemon;
    HQUIC         connection;
    uint8_t       peer_id[32];
    BOOLEAN       peer_id_known;
} PEER_CONNECTION_CONTEXT;

// Context for synchronous remote chunk fetch (used by IPC_CMD_GET)
typedef struct {
    WH_EVENT       completion_event;     // Signaled when fetch completes
    volatile int32_t status;             // 0=pending, 1=success, 2=failed, 3=not_found
    uint8_t       *data_buf;             // Caller-provided output buffer
    uint32_t       data_size;            // Filled on success
    uint32_t       data_capacity;        // Size of data_buf
    uint8_t        target_hash[WH_HASH_SIZE];

    // Receive accumulation (CHUNK_QUERY_RESPONSE arrives in fragments)
    uint8_t       *recv_buf;
    uint32_t       recv_size;
    uint32_t       recv_capacity;
} CHUNK_FETCH_CONTEXT;

// Per-stream context: accumulates received data + borrows peer context
typedef struct {
    PEER_CONNECTION_CONTEXT *peer_ctx;    // borrowed pointer (owned by connection)
    uint8_t  *recv_buf;
    size_t    recv_used;
    size_t    recv_capacity;
} DAEMON_STREAM_CONTEXT;

// Per-connection context for outbound replication: carries target peer identity
typedef struct {
    uint8_t peer_id[32];
} REPLICA_CONNECTION_CONTEXT;

// Per-stream context for outbound replication streams: accumulates fragmented data
typedef struct {
    uint8_t  *recv_buf;
    size_t    recv_used;
    size_t    recv_capacity;
    uint8_t   peer_id[32];   // Target peer (for replica tracking on ACK)
    HQUIC     connection;    // Owning connection (for active shutdown)
} REPLICA_STREAM_CONTEXT;

//=============================================================================
// Buffer accumulation helpers (same pattern as stream.c)
//=============================================================================

static BOOLEAN DaemonAccumulateBuffer(uint8_t **buf, size_t *used, size_t *capacity,
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

static void DaemonConsumeBuffer(uint8_t *buf, size_t *used, size_t consumed)
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
// 0-RTT Session Ticket Helpers (per-peer tickets for daemon)
//=============================================================================

static BOOLEAN Daemon_SaveSessionTicket(const uint8_t *ticket, uint32_t len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home) return FALSE;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s" PATH_SEP_STR ".wormhole" PATH_SEP_STR "session_ticket_daemon", home);
    FILE *f = fopen(path, "wb");
    if (!f) return FALSE;
    fwrite(&len, sizeof(len), 1, f);
    fwrite(ticket, 1, len, f);
    fclose(f);
    return TRUE;
}

static uint8_t *Daemon_LoadSessionTicket(uint32_t *out_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home) return NULL;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s" PATH_SEP_STR ".wormhole" PATH_SEP_STR "session_ticket_daemon", home);
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
static QUIC_STATUS QUIC_API Daemon_ReplicaStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

// Remote chunk fetch (used by IPC_CMD_GET for distributed retrieval)
static BOOLEAN Daemon_FetchChunkFromPeer(
    const char *addr_str, uint16_t port,
    QUIC_ADDRESS_FAMILY addr_family,
    const uint8_t hash[WH_HASH_SIZE],
    uint8_t *data_out, uint32_t *size_out);
static QUIC_STATUS QUIC_API Daemon_FetchConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event);
static QUIC_STATUS QUIC_API Daemon_FetchStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event);

// Fix 3: DHT-primary peer list builder
static uint32_t Daemon_BuildPeerList(const uint8_t target_hash[WH_HASH_SIZE],
                                      DISCOVERED_PEER *out_peers, uint32_t max_peers);

// Fix 5: Retry queue helpers
static void Daemon_RetryQueue_Add(const uint8_t hash[WH_HASH_SIZE]);

// Fix 6: Proof challenge helpers
static BOOLEAN Daemon_ResolvePeerAddress(const uint8_t peer_id[32],
                                          char *addr_out, size_t addr_len, uint16_t *port_out,
                                          QUIC_ADDRESS_FAMILY *family_out);
static BOOLEAN Daemon_SendProofChallenge(const char *addr_str, uint16_t port,
                                           QUIC_ADDRESS_FAMILY addr_family,
                                           const uint8_t hash[WH_HASH_SIZE],
                                           const uint8_t *chunk_data, uint32_t size);

// Fix 7: Remote EC recovery
static BOOLEAN Daemon_RecoverChunkWithRemoteFetch(const uint8_t hash[WH_HASH_SIZE],
                                                    const EC_GROUP *ec_group,
                                                    const FILE_MANIFEST *manifest);

//=============================================================================
// Ctrl+C Shutdown Handler
//=============================================================================

#ifdef _WIN32
static BOOL WINAPI DaemonCtrlHandler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT)
    {
        if (WH_ATOMIC_CAS(&g_daemon.shutdown_requested, 0, 1) == 0)
        {
            LOG("\n[daemon] Shutdown requested, cleaning up...\n");
        }
        return TRUE;
    }
    return FALSE;
}
#else
static void DaemonSignalHandler(int sig)
{
    (void)sig;
    WH_ATOMIC_SET(&g_daemon.shutdown_requested, 1);
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
        QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT  // Optimize for bulk file transfer
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
        MsQuic->RegistrationShutdown(DaemonRegistration,
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
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

#ifdef _WIN32
    // Generate or retrieve self-signed certificate (with node ID in CN for peer verification)
    char thumbprint[41];
    if (!GenerateSelfSignedCertWithNodeId(thumbprint, sizeof(thumbprint), g_daemon.keypair.public_key))
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
#else
    char cert_path_buf[MAX_PATH];
    if (!GenerateSelfSignedCertWithNodeId(cert_path_buf, sizeof(cert_path_buf), g_daemon.keypair.public_key))
    {
        LOG_ERROR("[daemon] Failed to generate/retrieve certificate\n");
        return FALSE;
    }

    char cert_path[MAX_PATH], key_path[MAX_PATH];
    if (!Crypto_GetCertPaths(cert_path, sizeof(cert_path), key_path, sizeof(key_path)))
    {
        LOG_ERROR("[daemon] Failed to determine cert paths\n");
        return FALSE;
    }

    LOG("[daemon] Using PEM certificate: %s\n", cert_path);

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

    // QUIC settings optimized for persistent daemon
    QUIC_SETTINGS settings = { 0 };
    settings.IdleTimeoutMs = 300000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.DisconnectTimeoutMs = 300000;
    settings.IsSet.DisconnectTimeoutMs = TRUE;
    settings.KeepAliveIntervalMs = 10000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.ServerResumptionLevel = QUIC_SERVER_RESUME_AND_ZERORTT;
    settings.IsSet.ServerResumptionLevel = TRUE;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;

    // Flow control
    settings.StreamRecvWindowDefault = 16777216;   // 16 MB
    settings.IsSet.StreamRecvWindowDefault = TRUE;
    settings.SendBufferingEnabled = FALSE;  // Zero-copy: MsQuic uses our buffers directly
    settings.IsSet.SendBufferingEnabled = TRUE;
    settings.ConnFlowControlWindow = 67108864;     // 64 MB
    settings.IsSet.ConnFlowControlWindow = TRUE;
    settings.InitialWindowPackets = 20;  // 2x default for faster start
    settings.IsSet.InitialWindowPackets = TRUE;

    // MTU
    settings.MinimumMtu = 1200;
    settings.IsSet.MinimumMtu = TRUE;
    settings.MaximumMtu = 1500;
    settings.IsSet.MaximumMtu = TRUE;

    // Congestion control
    settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
    settings.IsSet.CongestionControlAlgorithm = TRUE;
    settings.EcnEnabled = TRUE;
    settings.IsSet.EcnEnabled = TRUE;

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
    {
        LOG("[daemon] Incoming QUIC connection\n");
        PEER_CONNECTION_CONTEXT *peer_ctx =
            (PEER_CONNECTION_CONTEXT *)calloc(1, sizeof(PEER_CONNECTION_CONTEXT));
        if (!peer_ctx) return QUIC_STATUS_OUT_OF_MEMORY;
        peer_ctx->daemon = state;
        peer_ctx->connection = Event->NEW_CONNECTION.Connection;
        peer_ctx->peer_id_known = FALSE;
        MsQuic->SetCallbackHandler(
            Event->NEW_CONNECTION.Connection,
            (void *)Daemon_ConnectionCallback,
            peer_ctx
        );
        WH_ATOMIC_INC(&state->peer_count);
        return MsQuic->ConnectionSetConfiguration(
            Event->NEW_CONNECTION.Connection,
            DaemonServerConfig
        );
    }

    case QUIC_LISTENER_EVENT_STOP_COMPLETE:
        LOG("[daemon] Listener stop complete\n");
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API Daemon_ConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    PEER_CONNECTION_CONTEXT *peer_ctx = (PEER_CONNECTION_CONTEXT *)Context;
    DAEMON_STATE *state = peer_ctx->daemon;

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
    {
        LOG("[daemon] Peer connected\n");
        // Attempt peer identification by matching remote address against discovered peers
        QUIC_ADDR remote_addr = {0};
        uint32_t addr_len = sizeof(remote_addr);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_REMOTE_ADDRESS,
                                              &addr_len, &remote_addr)))
        {
            int32_t peer_count = WH_ATOMIC_LOAD(&state->discovered_peer_count);
            for (int32_t i = 0; i < peer_count; i++)
            {
                DISCOVERED_PEER *dp = &state->discovered_peers[i];
                for (uint16_t j = 0; j < dp->endpoint_count; j++)
                {
                    if (dp->endpoints[j].port == QuicAddrGetPort(&remote_addr) &&
                        dp->endpoints[j].addr_type == 0x04)
                    {
                        struct sockaddr_in *sin = (struct sockaddr_in *)&remote_addr;
                        if (memcmp(dp->endpoints[j].addr, &sin->sin_addr, 4) == 0)
                        {
                            memcpy(peer_ctx->peer_id, dp->peer_id, 32);
                            peer_ctx->peer_id_known = TRUE;
                            LOG("[daemon] Identified peer from discovered peers\n");
                            goto peer_found;
                        }
                    }
                }
            }
        }
        peer_found:
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    {
        LOG("[daemon] Peer opened stream\n");
        DAEMON_STREAM_CONTEXT *stream_ctx =
            (DAEMON_STREAM_CONTEXT *)calloc(1, sizeof(DAEMON_STREAM_CONTEXT));
        if (!stream_ctx)
        {
            LOG_ERROR("[daemon] Failed to allocate stream context, closing stream\n");
            MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
            break;
        }
        stream_ctx->peer_ctx = peer_ctx;
        // recv_buf starts NULL; DaemonAccumulateBuffer allocates on first use
        MsQuic->SetCallbackHandler(
            Event->PEER_STREAM_STARTED.Stream,
            (void *)Daemon_StreamCallback,
            stream_ctx
        );
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        LOG("[daemon] Peer initiated shutdown (error: 0x%llx)\n",
            (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        WH_ATOMIC_DEC(&state->peer_count);
        MsQuic->ConnectionClose(Connection);
        LOG("[daemon] Peer disconnected (active peers: %d)\n",
            WH_ATOMIC_LOAD(&state->peer_count));
        free(peer_ctx);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Stream Callback — handles incoming chunk replication messages
//=============================================================================

// Process a complete control message from a peer
// peer_id may be NULL if the peer is unidentified
static void Daemon_HandlePeerMessage(HQUIC stream, uint8_t msg_type,
                                      const uint8_t *payload, uint32_t payload_len,
                                      const uint8_t *peer_id)
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

        // Verify Blake3 hash of received data matches claimed hash
        {
            blake3_hasher hasher;
            blake3_hasher_init(&hasher);
            blake3_hasher_update(&hasher, chunk_data, data_size);
            uint8_t computed_hash[32];
            blake3_hasher_finalize(&hasher, computed_hash, 32);
            if (memcmp(computed_hash, hash, 32) != 0)
            {
                LOG("[daemon] Rejecting chunk: hash mismatch\n");
                // Send NACK
                uint32_t ack_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + 1;
                uint8_t *ack = (uint8_t *)malloc(ack_size);
                if (ack)
                {
                    ack[0] = CTRL_MSG_CHUNK_STORE_ACK;
                    WriteUint32LE(ack + 1, WH_HASH_SIZE + 1);
                    memcpy(ack + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
                    ack[CTRL_HEADER_SIZE + WH_HASH_SIZE] = 0x01;  // failure

                    QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
                    if (!send_buf) { free(ack); return; }
                    send_buf->Buffer = ack;
                    send_buf->Length = ack_size;
                    QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_NONE, send_buf);
                    if (QUIC_FAILED(send_status)) { free(ack); free(send_buf); }
                }
                return;
            }
        }

        // Ledger enforcement — reject storage from unbalanced peers
        BOOLEAN accepted = TRUE;
        if (peer_id)
        {
            WH_MUTEX_LOCK(g_daemon.ledger_lock);
            BOOLEAN should_accept = Ledger_ShouldAcceptStorage(&g_daemon.ledger, peer_id, (uint64_t)data_size);
            WH_MUTEX_UNLOCK(g_daemon.ledger_lock);
            if (!should_accept)
            {
                LOG("[daemon] Rejecting storage from peer (ledger ratio too unbalanced)\n");
                accepted = FALSE;
            }
        }
        else
        {
            uint8_t zero_id[32] = {0};
            WH_MUTEX_LOCK(g_daemon.ledger_lock);
            BOOLEAN should_accept = Ledger_ShouldAcceptStorage(&g_daemon.ledger, zero_id, (uint64_t)data_size);
            WH_MUTEX_UNLOCK(g_daemon.ledger_lock);
            if (!should_accept)
            {
                LOG("[daemon] Rejecting storage from unidentified peer\n");
                accepted = FALSE;
            }
        }

        // Store the chunk (if accepted by ledger)
        BOOLEAN stored = FALSE;
        if (accepted)
        {
            stored = ChunkStore_Put(hash, chunk_data, data_size);
            if (stored)
            {
                WH_ATOMIC_INC(&g_daemon.chunk_count);
                WH_ATOMIC_ADD64(&g_daemon.storage_used, (LONGLONG)data_size);
                if (peer_id)
                {
                    WH_MUTEX_LOCK(g_daemon.ledger_lock);
                    Ledger_RecordWeStored(&g_daemon.ledger, peer_id, (uint64_t)data_size);
                    WH_MUTEX_UNLOCK(g_daemon.ledger_lock);
                }

                // Fix 1: Announce received replica to DHT
                if (g_daemon.dht_enabled)
                {
                    WORK_ITEM *dht_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                    if (dht_item)
                    {
                        dht_item->type = WORK_DHT_ANNOUNCE;
                        memcpy(dht_item->dht_announce.hashes[0], hash, WH_HASH_SIZE);
                        dht_item->dht_announce.hash_count = 1;
                        WorkQueue_Push(dht_item);
                    }
                }
            }
        }

        // Send ACK: [1B type][4B payload_len][32B hash][1B status]
        // Note: use QUIC_SEND_FLAG_NONE (not FIN) so multiple ACKs can be sent
        // on the same stream when receiving batch replication messages.
        uint32_t ack_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + 1;
        uint8_t *ack = (uint8_t *)malloc(ack_size);
        if (ack)
        {
            ack[0] = CTRL_MSG_CHUNK_STORE_ACK;
            WriteUint32LE(ack + 1, WH_HASH_SIZE + 1);
            memcpy(ack + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
            ack[CTRL_HEADER_SIZE + WH_HASH_SIZE] = stored ? 0x00 : 0x01;

            QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
            if (!send_buf) { free(ack); break; }
            send_buf->Buffer = ack;
            send_buf->Length = ack_size;
            QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_NONE, send_buf);
            if (QUIC_FAILED(send_status)) { free(ack); free(send_buf); }
        }
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
        char hex[17];
        for (int i = 0; i < 8; i++) sprintf(hex + i * 2, "%02x", hash[i]);
        hex[16] = '\0';
        LOG("[daemon] CHUNK_QUERY for %s...\n", hex);

        BOOLEAN has = ChunkStore_Has(hash);
        LOG("[daemon]   ChunkStore_Has = %s\n", has ? "TRUE" : "FALSE");

        if (has)
        {
            // Read chunk and send response with data
            uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
            uint32_t chunk_size = 0;

            if (chunk_buf && ChunkStore_Get(hash, chunk_buf, &chunk_size))
            {
                LOG("[daemon]   ChunkStore_Get OK, size=%u\n", chunk_size);
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

                    QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
                    if (!send_buf) { free(resp); free(chunk_buf); break; }
                    send_buf->Buffer = resp;
                    send_buf->Length = CTRL_HEADER_SIZE + resp_payload;
                    QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_FIN, send_buf);
                    LOG("[daemon]   StreamSend status: 0x%x\n", send_status);
                    if (QUIC_FAILED(send_status)) { free(resp); free(send_buf); }
                }
            }
            else
            {
                LOG_ERROR("[daemon]   ChunkStore_Get FAILED (buf=%p), sending not-found\n", chunk_buf);
                // Fall through to send "not found" instead of leaving client hanging
                has = FALSE;
            }
            free(chunk_buf);
        }

        if (!has)
        {
            // Response: [1B type][4B payload_len][32B hash][1B status=0x01]
            uint32_t resp_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + 1;
            uint8_t *resp = (uint8_t *)malloc(resp_size);
            if (resp)
            {
                resp[0] = CTRL_MSG_CHUNK_QUERY_RESPONSE;
                WriteUint32LE(resp + 1, WH_HASH_SIZE + 1);
                memcpy(resp + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
                resp[CTRL_HEADER_SIZE + WH_HASH_SIZE] = 0x01;  // has=no

                QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
                if (!send_buf) { free(resp); break; }
                send_buf->Buffer = resp;
                send_buf->Length = resp_size;
                QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_FIN, send_buf);
                LOG("[daemon]   StreamSend (not-found) status: 0x%x\n", send_status);
                if (QUIC_FAILED(send_status)) { free(resp); free(send_buf); }
            }
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
            if (peer_id)
            {
                BOOLEAN ok = ChunkStore_SetReplicaLocation(hash, peer_id);
                char h8[17], p8[17];
                for (int x = 0; x < 8; x++) { sprintf(h8 + x*2, "%02x", hash[x]); sprintf(p8 + x*2, "%02x", peer_id[x]); }
                h8[16] = p8[16] = '\0';
                LOG("[daemon] SetReplicaLocation(%s..., peer=%s...): %s\n", h8, p8, ok ? "OK" : "FAILED");
                WH_MUTEX_LOCK(g_daemon.ledger_lock);
                Ledger_RecordStoredForUs(&g_daemon.ledger, peer_id, (uint64_t)WH_CHUNK_SIZE);
                WH_MUTEX_UNLOCK(g_daemon.ledger_lock);
            }
        }
        else
        {
            LOG("[daemon] Peer rejected chunk store (status: 0x%02x)\n", status);
        }
        break;
    }

    case CTRL_MSG_PROOF_CHALLENGE:
    {
        // Payload: [32B hash][32B seed]
        if (payload_len < WH_HASH_SIZE + WH_HASH_SIZE)
        {
            LOG_ERROR("[daemon] PROOF_CHALLENGE too short\n");
            return;
        }

        const uint8_t *hash = payload;
        const uint8_t *seed = payload + WH_HASH_SIZE;

        // Try cached proof first
        uint8_t proof[WH_HASH_SIZE];
        uint8_t status = 0x01;  // not found

        if (Proof_LookupCached(hash, seed, proof))
        {
            status = 0x00;  // proof found in cache
        }
        else if (ChunkStore_Has(hash))
        {
            // Compute proof on the fly
            uint8_t *chunk_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
            uint32_t chunk_size = 0;
            if (chunk_data && ChunkStore_Get(hash, chunk_data, &chunk_size))
            {
                Proof_Compute(chunk_data, chunk_size, seed, proof);
                status = 0x00;
            }
            free(chunk_data);
        }

        // Send response: [1B type][4B len=65][32B hash][32B proof][1B status]
        uint32_t resp_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + WH_HASH_SIZE + 1;
        uint8_t *resp = (uint8_t *)malloc(resp_size);
        if (resp)
        {
            resp[0] = CTRL_MSG_PROOF_RESPONSE;
            WriteUint32LE(resp + 1, WH_HASH_SIZE + WH_HASH_SIZE + 1);
            memcpy(resp + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
            memcpy(resp + CTRL_HEADER_SIZE + WH_HASH_SIZE, proof, WH_HASH_SIZE);
            resp[CTRL_HEADER_SIZE + WH_HASH_SIZE + WH_HASH_SIZE] = status;

            QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
            if (!send_buf) { free(resp); break; }
            send_buf->Buffer = resp;
            send_buf->Length = resp_size;
            QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_FIN, send_buf);
            if (QUIC_FAILED(send_status)) { free(resp); free(send_buf); }
        }
        break;
    }

    case CTRL_MSG_PROOF_RESPONSE:
    {
        // Payload: [32B hash][32B proof][1B status]
        if (payload_len < WH_HASH_SIZE + WH_HASH_SIZE + 1) return;

        uint8_t status = payload[WH_HASH_SIZE + WH_HASH_SIZE];
        if (status == 0x00)
        {
            LOG("[daemon] Peer proof verified successfully\n");
        }
        else
        {
            LOG("[daemon] Peer does not have chunk (proof status: 0x%02x)\n", status);
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
    DAEMON_STREAM_CONTEXT *stream_ctx = (DAEMON_STREAM_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        if (!stream_ctx) break;

        PEER_CONNECTION_CONTEXT *peer_ctx = stream_ctx->peer_ctx;
        const uint8_t *peer_id = (peer_ctx && peer_ctx->peer_id_known)
            ? peer_ctx->peer_id : NULL;

        // Accumulate data using growable buffer
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (!DaemonAccumulateBuffer(&stream_ctx->recv_buf, &stream_ctx->recv_used,
                                         &stream_ctx->recv_capacity, buf->Buffer, buf->Length))
            {
                LOG_ERROR("[daemon] Stream RECEIVE: failed to accumulate %u bytes\n", buf->Length);
                break;
            }
        }

        // Parse complete messages incrementally (supports batch replication)
        while (stream_ctx->recv_used >= CTRL_HEADER_SIZE)
        {
            uint8_t msg_type = stream_ctx->recv_buf[0];
            uint32_t payload_len = ReadUint32LE(stream_ctx->recv_buf + 1);

            if (payload_len > MAX_CTRL_PAYLOAD)
            {
                LOG_ERROR("[daemon] Payload too large: %u\n", payload_len);
                stream_ctx->recv_used = 0;
                break;
            }

            size_t frame_size = (size_t)CTRL_HEADER_SIZE + payload_len;
            if (stream_ctx->recv_used < frame_size)
                break;  // Wait for more data

            Daemon_HandlePeerMessage(Stream, msg_type,
                stream_ctx->recv_buf + CTRL_HEADER_SIZE, payload_len, peer_id);

            DaemonConsumeBuffer(stream_ctx->recv_buf, &stream_ctx->recv_used, frame_size);
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    {
        // Peer sent FIN — gracefully shut down our send direction
        LOG("[daemon] PEER_SEND_SHUTDOWN: closing send direction\n");
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        // Free heap-allocated QUIC_BUFFER and its data buffer
        QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
        if (sent)
        {
            free(sent->Buffer);
            free(sent);
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        // Free per-stream accumulation context
        if (stream_ctx)
        {
            free(stream_ctx->recv_buf);
            free(stream_ctx);
        }
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
    cred_config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION
                      | QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;

    QUIC_SETTINGS settings = { 0 };
    settings.IdleTimeoutMs = 30000;   // 30s idle timeout for replication connections
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.KeepAliveIntervalMs = 10000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.StreamRecvWindowDefault = 16777216;
    settings.IsSet.StreamRecvWindowDefault = TRUE;
    settings.SendBufferingEnabled = FALSE;  // Zero-copy: MsQuic uses our buffers directly
    settings.IsSet.SendBufferingEnabled = TRUE;

    // Connection flow control + congestion settings (match server config)
    settings.ConnFlowControlWindow = 67108864;  // 64 MB
    settings.IsSet.ConnFlowControlWindow = TRUE;
    settings.InitialWindowPackets = 20;  // Faster ramp-up
    settings.IsSet.InitialWindowPackets = TRUE;
    settings.MinimumMtu = 1200;
    settings.IsSet.MinimumMtu = TRUE;
    settings.MaximumMtu = 1500;
    settings.IsSet.MaximumMtu = TRUE;
    settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
    settings.IsSet.CongestionControlAlgorithm = TRUE;
    settings.EcnEnabled = TRUE;
    settings.IsSet.EcnEnabled = TRUE;

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
// Hole-Punch — NAT traversal for cross-network replication
//=============================================================================

// Fire-and-forget connection callback for NAT mapping (punch connections)
static QUIC_STATUS QUIC_API Daemon_PunchConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[punch] NAT mapping connection succeeded\n");
        MsQuic->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        // Expected — the connection is just for NAT mapping
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->ConnectionClose(Connection);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Called when a remote peer sends a PUNCH_REQUEST (0x10) via relay FORWARD
static void Daemon_OnPunchRequest(void *context, const uint8_t requester_id[32])
{
    UNREFERENCED_PARAMETER(context);

    // Look up requester's endpoints from discovered peers
    const ENDPOINT *ep = NULL;
    WH_MUTEX_LOCK(g_daemon.peers_lock);
    LONG count = WH_ATOMIC_LOAD(&g_daemon.discovered_peer_count);
    for (LONG i = 0; i < count; i++)
    {
        if (memcmp(g_daemon.discovered_peers[i].peer_id, requester_id, 32) == 0)
        {
            DISCOVERED_PEER *peer = &g_daemon.discovered_peers[i];
            // Prefer IPv4, fall back to IPv6
            for (uint16_t j = 0; j < peer->endpoint_count; j++)
            {
                if (peer->endpoints[j].addr_type == 0x04) { ep = &peer->endpoints[j]; break; }
            }
            if (!ep)
            {
                for (uint16_t j = 0; j < peer->endpoint_count; j++)
                {
                    if (peer->endpoints[j].addr_type == 0x06) { ep = &peer->endpoints[j]; break; }
                }
            }
            break;
        }
    }

    // Copy endpoint data before releasing lock
    ENDPOINT ep_copy;
    if (ep)
    {
        memcpy(&ep_copy, ep, sizeof(ENDPOINT));
    }
    WH_MUTEX_UNLOCK(g_daemon.peers_lock);

    if (!ep) return;

    char addr_str[INET6_ADDRSTRLEN];
    uint16_t port;
    if (!Endpoint_ToString(&ep_copy, addr_str, sizeof(addr_str), &port)) return;

    QUIC_ADDRESS_FAMILY family = (ep_copy.addr_type == 0x06)
        ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

    // Open QUIC connection from port 4567 to requester (creates NAT mapping)
    HQUIC punch_conn = NULL;
    if (QUIC_SUCCEEDED(MsQuic->ConnectionOpen(DaemonRegistration,
            Daemon_PunchConnectionCallback, NULL, &punch_conn)))
    {
        if (QUIC_SUCCEEDED(MsQuic->ConnectionStart(
                punch_conn, DaemonClientConfig, family, addr_str, port)))
        {
            LOG("[punch] Opened connection to %s:%u (NAT mapping)\n", addr_str, port);
        }
        else
        {
            MsQuic->ConnectionClose(punch_conn);
        }
    }

    // Send PUNCH_ACK back via relay FORWARD
    uint8_t ack[33];
    ack[0] = 0x11;
    memcpy(ack + 1, g_daemon.keypair.public_key, 32);
    RelayClient_ForwardPacket(g_daemon.relay_client, requester_id, ack, sizeof(ack));
}

//=============================================================================
// Chunk Replication — replicate to discovered peers
//=============================================================================

// Outbound replication connection callback
static QUIC_STATUS QUIC_API Daemon_ReplicaConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    REPLICA_CONNECTION_CONTEXT *conn_ctx = (REPLICA_CONNECTION_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[replicate] Connected to peer for replication\n");
        break;

    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
    {
        const uint8_t *ticket = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket;
        uint32_t ticket_len = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength;
        Daemon_SaveSessionTicket(ticket, ticket_len);
        LOG("[replicate] Session ticket saved (%u bytes) for 0-RTT\n", ticket_len);
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
    {
        // Verify the peer's TLS certificate contains the expected node ID
        if (conn_ctx && Event->PEER_CERTIFICATE_RECEIVED.Certificate)
        {
            if (!Crypto_VerifyPeerCert(Event->PEER_CERTIFICATE_RECEIVED.Certificate,
                                        conn_ctx->peer_id))
            {
                LOG_ERROR("[replicate] Peer TLS certificate does not match expected node ID — rejecting\n");
                return QUIC_STATUS_BAD_CERTIFICATE;
            }
            LOG("[replicate] Peer TLS identity verified\n");
        }
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    {
        REPLICA_STREAM_CONTEXT *rctx =
            (REPLICA_STREAM_CONTEXT *)calloc(1, sizeof(REPLICA_STREAM_CONTEXT));
        if (rctx)
        {
            rctx->connection = Connection;
            if (conn_ctx)
                memcpy(rctx->peer_id, conn_ctx->peer_id, 32);
        }
        MsQuic->SetCallbackHandler(
            Event->PEER_STREAM_STARTED.Stream,
            (void *)Daemon_ReplicaStreamCallback,
            rctx
        );
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        free(conn_ctx);
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
    REPLICA_STREAM_CONTEXT *rctx = (REPLICA_STREAM_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // Accumulate incoming data into buffer
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            if (rctx)
            {
                if (!DaemonAccumulateBuffer(&rctx->recv_buf, &rctx->recv_used,
                                             &rctx->recv_capacity, buf->Buffer, buf->Length))
                {
                    LOG_ERROR("[replicate] Failed to accumulate stream data\n");
                    break;
                }
            }
        }

        // Parse complete control messages from accumulated buffer
        if (rctx)
        {
            while (rctx->recv_used >= CTRL_HEADER_SIZE)
            {
                uint8_t msg_type = rctx->recv_buf[0];
                uint32_t payload_len = ReadUint32LE(rctx->recv_buf + 1);
                if (payload_len > MAX_CTRL_PAYLOAD)
                {
                    LOG_ERROR("[replicate] Payload too large: %u\n", payload_len);
                    rctx->recv_used = 0;  // Reset buffer on protocol error
                    break;
                }

                size_t frame_size = (size_t)CTRL_HEADER_SIZE + payload_len;
                if (rctx->recv_used < frame_size)
                {
                    break;  // Wait for more data
                }

                Daemon_HandlePeerMessage(Stream, msg_type,
                    rctx->recv_buf + CTRL_HEADER_SIZE, payload_len, rctx->peer_id);

                DaemonConsumeBuffer(rctx->recv_buf, &rctx->recv_used, frame_size);
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
        if (sent)
        {
            free(sent->Buffer);
            free(sent);
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        if (rctx)
        {
            // Actively shutdown the connection so the receiver doesn't wait for idle timeout
            if (rctx->connection)
            {
                MsQuic->ConnectionShutdown(rctx->connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            }
            free(rctx->recv_buf);
            free(rctx);
        }
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Fix 3: DHT-primary peer list builder
//=============================================================================

static uint32_t Daemon_BuildPeerList(const uint8_t target_hash[WH_HASH_SIZE],
                                      DISCOVERED_PEER *out_peers, uint32_t max_peers)
{
    uint32_t count = 0;

    // Step 1: Query DHT routing table (primary peer source)
    if (g_daemon.dht_enabled)
    {
        ROUTING_NODE closest[20];  // DHT_K = 20
        uint32_t rt_max = max_peers < 20 ? max_peers : 20;

        WH_MUTEX_LOCK(g_daemon.dht_lock);
        uint32_t rt_count = RoutingTable_FindClosest(
            &g_daemon.dht_node.routing_table, target_hash, closest, rt_max);
        WH_MUTEX_UNLOCK(g_daemon.dht_lock);

        for (uint32_t i = 0; i < rt_count && count < max_peers; i++)
        {
            // Skip self
            if (memcmp(closest[i].node_id, g_daemon.keypair.public_key, 32) == 0)
                continue;

            DISCOVERED_PEER *p = &out_peers[count];
            memset(p, 0, sizeof(DISCOVERED_PEER));
            memcpy(p->peer_id, closest[i].node_id, 32);
            p->endpoint_count = 1;
            p->endpoints[0].addr_type = closest[i].addr_type;
            memcpy(p->endpoints[0].addr, closest[i].addr, 16);
            // DHT routing table stores DHT port; use QUIC port for replication
            p->endpoints[0].port = DAEMON_DEFAULT_PORT;
            p->endpoints[0].priority = 100;
            count++;
        }
    }

    // Step 2: Merge relay-discovered peers (supplement)
    DISCOVERED_PEER relay_peers[MAX_FIND_PEERS];
    LONG relay_count;
    WH_MUTEX_LOCK(g_daemon.peers_lock);
    relay_count = WH_ATOMIC_LOAD(&g_daemon.discovered_peer_count);
    if (relay_count > 0)
        memcpy(relay_peers, g_daemon.discovered_peers, relay_count * sizeof(DISCOVERED_PEER));
    WH_MUTEX_UNLOCK(g_daemon.peers_lock);

    for (LONG ri = 0; ri < relay_count && count < max_peers; ri++)
    {
        // Skip self
        if (memcmp(relay_peers[ri].peer_id, g_daemon.keypair.public_key, 32) == 0)
            continue;

        // Deduplicate: check if already in list from DHT
        BOOLEAN dup = FALSE;
        for (uint32_t ei = 0; ei < count; ei++)
        {
            if (memcmp(out_peers[ei].peer_id, relay_peers[ri].peer_id, 32) == 0)
            {
                // Peer exists from DHT — prefer relay endpoints (better NAT info)
                memcpy(out_peers[ei].endpoints, relay_peers[ri].endpoints,
                       relay_peers[ri].endpoint_count * sizeof(ENDPOINT));
                out_peers[ei].endpoint_count = relay_peers[ri].endpoint_count;
                dup = TRUE;
                break;
            }
        }
        if (!dup)
        {
            memcpy(&out_peers[count], &relay_peers[ri], sizeof(DISCOVERED_PEER));
            count++;
        }
    }

    return count;
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

    // Fix 3: Use DHT-primary peer list
    DISCOVERED_PEER local_peers[MAX_FIND_PEERS];
    uint32_t peer_count = Daemon_BuildPeerList(hash, local_peers, MAX_FIND_PEERS);

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
    for (uint32_t i = 0; i < peer_count && sent < needed; i++)
    {
        DISCOVERED_PEER *peer = &local_peers[i];
        if (peer->endpoint_count == 0) continue;

        // Select best endpoint: prefer IPv4, fall back to IPv6
        const ENDPOINT *ep = NULL;
        for (uint16_t j = 0; j < peer->endpoint_count; j++)
        {
            if (peer->endpoints[j].addr_type == 0x04) { ep = &peer->endpoints[j]; break; }
        }
        if (!ep)
        {
            for (uint16_t j = 0; j < peer->endpoint_count; j++)
            {
                if (peer->endpoints[j].addr_type == 0x06) { ep = &peer->endpoints[j]; break; }
            }
        }
        if (!ep) continue;

        // Build target address string (supports both IPv4 and IPv6)
        char addr_str[INET6_ADDRSTRLEN];
        uint16_t port;
        if (!Endpoint_ToString(ep, addr_str, sizeof(addr_str), &port)) continue;

        QUIC_ADDRESS_FAMILY family = (ep->addr_type == 0x06)
            ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

        LOG("[replicate] Connecting to %s:%u\n", addr_str, port);

        // Hole-punch: signal peer via relay to create NAT mappings
        if (g_daemon.relay_client && RelayClient_IsConnected(g_daemon.relay_client))
        {
            uint8_t punch_req[33];
            punch_req[0] = 0x10;
            memcpy(punch_req + 1, g_daemon.keypair.public_key, 32);
            RelayClient_ResetPunchAck(g_daemon.relay_client);
            RelayClient_ForwardPacket(g_daemon.relay_client, peer->peer_id,
                punch_req, sizeof(punch_req));

            // Also open our own connection to peer (creates our NAT mapping)
            HQUIC our_punch = NULL;
            if (QUIC_SUCCEEDED(MsQuic->ConnectionOpen(DaemonRegistration,
                    Daemon_PunchConnectionCallback, NULL, &our_punch)))
            {
                if (QUIC_FAILED(MsQuic->ConnectionStart(our_punch, DaemonClientConfig,
                        family, addr_str, port)))
                {
                    MsQuic->ConnectionClose(our_punch);
                }
            }

            // Wait for PUNCH_ACK (up to 3 seconds)
            DWORD punch_start = GetTickCount();
            while (GetTickCount() - punch_start < 3000)
            {
                WH_SLEEP_MS(50);  // Don't poll relay socket from IPC thread — main thread handles it
                if (RelayClient_GetPunchAckReceived(g_daemon.relay_client))
                    break;
            }

            // Wait for NAT mappings to stabilize
            if (RelayClient_GetPunchAckReceived(g_daemon.relay_client))
            {
                LOG("[replicate] Hole-punch ACK received, waiting for NAT...\n");
                WH_SLEEP_MS(1000);
            }
            else
            {
                LOG("[replicate] No hole-punch ACK (peer may be offline)\n");
            }
        }

        // Allocate connection context with peer_id for replica tracking
        REPLICA_CONNECTION_CONTEXT *conn_ctx =
            (REPLICA_CONNECTION_CONTEXT *)calloc(1, sizeof(REPLICA_CONNECTION_CONTEXT));
        if (!conn_ctx) continue;
        memcpy(conn_ctx->peer_id, peer->peer_id, 32);

        // Open a QUIC connection to the peer
        HQUIC connection = NULL;
        QUIC_STATUS status = MsQuic->ConnectionOpen(
            DaemonRegistration,
            Daemon_ReplicaConnectionCallback,
            conn_ctx,
            &connection
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] ConnectionOpen failed: 0x%x\n", status);
            free(conn_ctx);
            continue;
        }

        // Apply saved session ticket for 0-RTT
        {
            uint32_t ticket_len;
            uint8_t *ticket = Daemon_LoadSessionTicket(&ticket_len);
            if (ticket)
            {
                MsQuic->SetParam(connection, QUIC_PARAM_CONN_RESUMPTION_TICKET,
                                 ticket_len, ticket);
                free(ticket);
            }
        }

        status = MsQuic->ConnectionStart(
            connection,
            DaemonClientConfig,
            family,
            addr_str,
            port
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] ConnectionStart failed: 0x%x\n", status);
            MsQuic->ConnectionClose(connection);
            continue;
        }

        // Open a bidirectional stream
        HQUIC stream = NULL;
        REPLICA_STREAM_CONTEXT *rctx =
            (REPLICA_STREAM_CONTEXT *)calloc(1, sizeof(REPLICA_STREAM_CONTEXT));
        if (rctx)
        {
            memcpy(rctx->peer_id, peer->peer_id, 32);
            rctx->connection = connection;
        }
        status = MsQuic->StreamOpen(
            connection,
            QUIC_STREAM_OPEN_FLAG_NONE,
            Daemon_ReplicaStreamCallback,
            rctx,
            &stream
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate] StreamOpen failed: 0x%x\n", status);
            free(rctx);
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
        QUIC_BUFFER *send_buf = send_copy ? (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER)) : NULL;
        if (send_copy && send_buf)
        {
            memcpy(send_copy, msg, msg_size);
            send_buf->Buffer = send_copy;
            send_buf->Length = msg_size;
            QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1,
                QUIC_SEND_FLAG_FIN, send_buf);  // FIN after sending
            if (QUIC_FAILED(send_status)) { free(send_copy); free(send_buf); }
        }
        else
        {
            free(send_copy);
            free(send_buf);
        }

        sent++;
    }

    free(msg);

    if (sent > 0)
    {
        LOG("[replicate] Sent chunk to %u peers\n", sent);
    }
    else if (needed > 0)
    {
        // Fix 5: Queue for retry if no peers accepted the chunk
        Daemon_RetryQueue_Add(hash);
    }
}

//=============================================================================
// Batch Chunk Replication — send all chunks over one connection per peer
//=============================================================================

static void Daemon_ReplicateBatch(const uint8_t hashes[][WH_HASH_SIZE],
                                   uint8_t *const *data_ptrs,
                                   const uint32_t *sizes,
                                   uint32_t chunk_count)
{
    if (!DaemonClientConfig || !MsQuic || chunk_count == 0)
        return;

    // Filter to chunks that still need replicas
    uint8_t  need_hashes[MAX_BATCH_CHUNKS][WH_HASH_SIZE];
    uint8_t *need_data[MAX_BATCH_CHUNKS];
    uint32_t need_sizes[MAX_BATCH_CHUNKS];
    uint32_t need_count = 0;

    for (uint32_t i = 0; i < chunk_count && need_count < MAX_BATCH_CHUNKS; i++)
    {
        uint32_t replicas = ChunkStore_GetReplicaCount(hashes[i]);
        if (replicas + 1 < REPLICATION_TARGET)  // +1 for local copy
        {
            memcpy(need_hashes[need_count], hashes[i], WH_HASH_SIZE);
            need_data[need_count] = data_ptrs[i];
            need_sizes[need_count] = sizes[i];
            need_count++;
        }
    }

    if (need_count == 0)
    {
        LOG("[replicate-batch] All %u chunks already at replication target\n", chunk_count);
        return;
    }

    // Fix 3: Use DHT-primary peer list (use first chunk hash as target)
    DISCOVERED_PEER local_peers[MAX_FIND_PEERS];
    uint32_t peer_count = Daemon_BuildPeerList(need_hashes[0], local_peers, MAX_FIND_PEERS);

    if (peer_count == 0)
        return;

    uint32_t peers_needed = REPLICATION_TARGET - 1;  // peers to send to (we have 1 local copy)

    for (uint32_t pi = 0; pi < peer_count && pi < peers_needed; pi++)
    {
        DISCOVERED_PEER *peer = &local_peers[pi];
        if (peer->endpoint_count == 0) continue;

        // Select best endpoint: prefer IPv4, fall back to IPv6
        const ENDPOINT *ep = NULL;
        for (uint16_t j = 0; j < peer->endpoint_count; j++)
        {
            if (peer->endpoints[j].addr_type == 0x04) { ep = &peer->endpoints[j]; break; }
        }
        if (!ep)
        {
            for (uint16_t j = 0; j < peer->endpoint_count; j++)
            {
                if (peer->endpoints[j].addr_type == 0x06) { ep = &peer->endpoints[j]; break; }
            }
        }
        if (!ep) continue;

        char addr_str[INET6_ADDRSTRLEN];
        uint16_t port;
        if (!Endpoint_ToString(ep, addr_str, sizeof(addr_str), &port)) continue;

        QUIC_ADDRESS_FAMILY family = (ep->addr_type == 0x06)
            ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

        LOG("[replicate-batch] Sending %u chunks to %s:%u\n", need_count, addr_str, port);

        // Hole-punch ONCE per peer (not per chunk)
        if (g_daemon.relay_client && RelayClient_IsConnected(g_daemon.relay_client))
        {
            uint8_t punch_req[33];
            punch_req[0] = 0x10;
            memcpy(punch_req + 1, g_daemon.keypair.public_key, 32);
            RelayClient_ResetPunchAck(g_daemon.relay_client);
            RelayClient_ForwardPacket(g_daemon.relay_client, peer->peer_id,
                punch_req, sizeof(punch_req));

            HQUIC our_punch = NULL;
            if (QUIC_SUCCEEDED(MsQuic->ConnectionOpen(DaemonRegistration,
                    Daemon_PunchConnectionCallback, NULL, &our_punch)))
            {
                if (QUIC_FAILED(MsQuic->ConnectionStart(our_punch, DaemonClientConfig,
                        family, addr_str, port)))
                {
                    MsQuic->ConnectionClose(our_punch);
                }
            }

            DWORD punch_start = GetTickCount();
            while (GetTickCount() - punch_start < 3000)
            {
                WH_SLEEP_MS(50);
                if (RelayClient_GetPunchAckReceived(g_daemon.relay_client))
                    break;
            }

            if (RelayClient_GetPunchAckReceived(g_daemon.relay_client))
            {
                LOG("[replicate-batch] Hole-punch ACK received, waiting for NAT...\n");
                WH_SLEEP_MS(1000);
            }
            else
            {
                LOG("[replicate-batch] No hole-punch ACK (peer may be offline)\n");
            }
        }

        // Allocate connection context with peer_id for replica tracking
        REPLICA_CONNECTION_CONTEXT *conn_ctx =
            (REPLICA_CONNECTION_CONTEXT *)calloc(1, sizeof(REPLICA_CONNECTION_CONTEXT));
        if (!conn_ctx) continue;
        memcpy(conn_ctx->peer_id, peer->peer_id, 32);

        // Open ONE QUIC connection for all chunks to this peer
        HQUIC connection = NULL;
        QUIC_STATUS status = MsQuic->ConnectionOpen(
            DaemonRegistration,
            Daemon_ReplicaConnectionCallback,
            conn_ctx,
            &connection
        );
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate-batch] ConnectionOpen failed: 0x%x\n", status);
            free(conn_ctx);
            continue;
        }

        // Apply saved session ticket for 0-RTT
        {
            uint32_t ticket_len;
            uint8_t *ticket = Daemon_LoadSessionTicket(&ticket_len);
            if (ticket)
            {
                MsQuic->SetParam(connection, QUIC_PARAM_CONN_RESUMPTION_TICKET,
                                 ticket_len, ticket);
                free(ticket);
            }
        }

        status = MsQuic->ConnectionStart(connection, DaemonClientConfig,
            family, addr_str, port);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate-batch] ConnectionStart failed: 0x%x\n", status);
            MsQuic->ConnectionClose(connection);
            continue;
        }

        // Open ONE bidirectional stream for all chunks
        HQUIC stream = NULL;
        REPLICA_STREAM_CONTEXT *rctx =
            (REPLICA_STREAM_CONTEXT *)calloc(1, sizeof(REPLICA_STREAM_CONTEXT));
        if (rctx)
        {
            memcpy(rctx->peer_id, peer->peer_id, 32);
            rctx->connection = connection;
        }
        status = MsQuic->StreamOpen(connection, QUIC_STREAM_OPEN_FLAG_NONE,
            Daemon_ReplicaStreamCallback, rctx, &stream);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate-batch] StreamOpen failed: 0x%x\n", status);
            free(rctx);
            MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            continue;
        }

        status = MsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[replicate-batch] StreamStart failed: 0x%x\n", status);
            MsQuic->StreamClose(stream);
            MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            continue;
        }

        // Send all CHUNK_STORE_REQUEST messages on this stream
        for (uint32_t ci = 0; ci < need_count; ci++)
        {
            uint32_t payload_len = WH_HASH_SIZE + 4 + need_sizes[ci];
            uint32_t msg_size = CTRL_HEADER_SIZE + payload_len;
            uint8_t *msg = (uint8_t *)malloc(msg_size);
            QUIC_BUFFER *send_buf = msg ? (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER)) : NULL;

            if (!msg || !send_buf)
            {
                free(msg);
                free(send_buf);
                continue;
            }

            msg[0] = CTRL_MSG_CHUNK_STORE_REQUEST;
            WriteUint32LE(msg + 1, payload_len);
            memcpy(msg + CTRL_HEADER_SIZE, need_hashes[ci], WH_HASH_SIZE);
            WriteUint32LE(msg + CTRL_HEADER_SIZE + WH_HASH_SIZE, need_sizes[ci]);
            memcpy(msg + CTRL_HEADER_SIZE + WH_HASH_SIZE + 4, need_data[ci], need_sizes[ci]);

            send_buf->Buffer = msg;
            send_buf->Length = msg_size;

            // FIN only on the last chunk to signal end of batch
            QUIC_SEND_FLAGS flags = (ci == need_count - 1)
                ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;

            QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, flags, send_buf);
            if (QUIC_FAILED(send_status))
            {
                LOG_ERROR("[replicate-batch] StreamSend failed for chunk %u: 0x%x\n", ci, send_status);
                free(msg);
                free(send_buf);
            }
        }

        LOG("[replicate-batch] Queued %u chunks to %s:%u\n", need_count, addr_str, port);
    }
}

//=============================================================================
// Remote Chunk Fetch — synchronous QUIC fetch from a single peer
//=============================================================================

static QUIC_STATUS QUIC_API Daemon_FetchStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    CHUNK_FETCH_CONTEXT *ctx = (CHUNK_FETCH_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        LOG("[fetch] Stream RECEIVE: %llu bytes (total so far: %u)\n",
            (unsigned long long)Event->RECEIVE.TotalBufferLength, ctx->recv_size);
        // Accumulate received data into recv_buf
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            uint32_t needed = ctx->recv_size + buf->Length;
            if (needed <= ctx->recv_capacity)
            {
                memcpy(ctx->recv_buf + ctx->recv_size, buf->Buffer, buf->Length);
                ctx->recv_size += buf->Length;
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    {
        LOG("[fetch] PEER_SEND_SHUTDOWN, recv_size=%u\n", ctx->recv_size);
        // Peer sent FIN — parse the complete CHUNK_QUERY_RESPONSE
        // Format: [1B type=0x08][4B payload_len][32B hash][1B status][4B size][data]
        if (ctx->recv_size >= CTRL_HEADER_SIZE)
        {
            uint8_t msg_type = ctx->recv_buf[0];
            uint32_t payload_len = ReadUint32LE(ctx->recv_buf + 1);

            if (msg_type == CTRL_MSG_CHUNK_QUERY_RESPONSE &&
                ctx->recv_size >= CTRL_HEADER_SIZE + payload_len &&
                payload_len >= WH_HASH_SIZE + 1)
            {
                const uint8_t *payload = ctx->recv_buf + CTRL_HEADER_SIZE;
                uint8_t status = payload[WH_HASH_SIZE];

                if (status == 0x00 && payload_len >= WH_HASH_SIZE + 1 + 4)
                {
                    // Peer has the chunk
                    uint32_t chunk_size = ReadUint32LE(payload + WH_HASH_SIZE + 1);
                    if (chunk_size <= ctx->data_capacity &&
                        payload_len >= WH_HASH_SIZE + 5 + chunk_size)
                    {
                        memcpy(ctx->data_buf, payload + WH_HASH_SIZE + 5, chunk_size);
                        ctx->data_size = chunk_size;
                        WH_ATOMIC_SET(&ctx->status, 1);  // success
                    }
                    else
                    {
                        WH_ATOMIC_SET(&ctx->status, 2);  // failed (too large)
                    }
                }
                else if (status == 0x01)
                {
                    WH_ATOMIC_SET(&ctx->status, 3);  // not_found
                }
                else
                {
                    WH_ATOMIC_SET(&ctx->status, 2);  // failed
                }
            }
            else
            {
                WH_ATOMIC_SET(&ctx->status, 2);  // failed (bad message)
            }
        }
        else
        {
            WH_ATOMIC_SET(&ctx->status, 2);  // failed (no data)
        }
        WH_EVENT_SET(ctx->completion_event);
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
        if (sent)
        {
            free(sent->Buffer);
            free(sent);
        }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        // Signal failure if not already signaled
        if (WH_ATOMIC_CAS(&ctx->status, 0, 2) == 0)
            WH_EVENT_SET(ctx->completion_event);
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API Daemon_FetchConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    CHUNK_FETCH_CONTEXT *ctx = (CHUNK_FETCH_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
    {
        LOG("[fetch] Connected, sending CHUNK_QUERY\n");

        // Open a bidirectional stream and send CHUNK_QUERY
        HQUIC stream = NULL;
        QUIC_STATUS status = MsQuic->StreamOpen(
            Connection, QUIC_STREAM_OPEN_FLAG_NONE,
            Daemon_FetchStreamCallback, ctx, &stream);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[fetch] StreamOpen failed: 0x%x\n", status);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }

        status = MsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
        if (QUIC_FAILED(status))
        {
            LOG_ERROR("[fetch] StreamStart failed: 0x%x\n", status);
            MsQuic->StreamClose(stream);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }

        // Build CHUNK_QUERY message: [1B type][4B payload_len][32B hash]
        uint32_t msg_size = CTRL_HEADER_SIZE + WH_HASH_SIZE;
        uint8_t *msg = (uint8_t *)malloc(msg_size);
        if (!msg)
        {
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }

        msg[0] = CTRL_MSG_CHUNK_QUERY;
        WriteUint32LE(msg + 1, WH_HASH_SIZE);
        memcpy(msg + CTRL_HEADER_SIZE, ctx->target_hash, WH_HASH_SIZE);

        QUIC_BUFFER *send_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
        if (!send_buf)
        {
            free(msg);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }
        send_buf->Buffer = msg;
        send_buf->Length = msg_size;
        QUIC_STATUS send_status = MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_FIN, send_buf);
        if (QUIC_FAILED(send_status))
        {
            free(msg);
            free(send_buf);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
        }
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        // We initiated the stream, so peer shouldn't start one — ignore
        MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        // Signal failure if not already signaled
        if (WH_ATOMIC_CAS(&ctx->status, 0, 2) == 0)
            WH_EVENT_SET(ctx->completion_event);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        // Signal failure if not already signaled (safety net)
        if (WH_ATOMIC_CAS(&ctx->status, 0, 2) == 0)
            WH_EVENT_SET(ctx->completion_event);
        // Do NOT call ConnectionClose here — caller owns the close
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Synchronous blocking function: connect to a peer, send CHUNK_QUERY,
// wait for CHUNK_QUERY_RESPONSE, return chunk data.
// Called from IPC thread. Returns TRUE on success with data in data_out.
static BOOLEAN Daemon_FetchChunkFromPeer(
    const char *addr_str, uint16_t port,
    QUIC_ADDRESS_FAMILY addr_family,
    const uint8_t hash[WH_HASH_SIZE],
    uint8_t *data_out, uint32_t *size_out)
{
    if (!DaemonClientConfig || !MsQuic)
        return FALSE;

    // Allocate fetch context
    CHUNK_FETCH_CONTEXT *ctx = (CHUNK_FETCH_CONTEXT *)calloc(1, sizeof(CHUNK_FETCH_CONTEXT));
    if (!ctx) return FALSE;

    ctx->completion_event = WH_EVENT_CREATE();  // manual reset
    if (!ctx->completion_event)
    {
        free(ctx);
        return FALSE;
    }

    ctx->data_buf = data_out;
    ctx->data_capacity = WH_CHUNK_SIZE;
    ctx->data_size = 0;
    ctx->status = 0;  // pending
    memcpy(ctx->target_hash, hash, WH_HASH_SIZE);

    // Allocate receive buffer
    ctx->recv_capacity = CTRL_HEADER_SIZE + WH_HASH_SIZE + 5 + WH_CHUNK_SIZE;
    ctx->recv_buf = (uint8_t *)malloc(ctx->recv_capacity);
    if (!ctx->recv_buf)
    {
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }
    ctx->recv_size = 0;

    LOG("[fetch] Connecting to %s:%u for chunk fetch\n", addr_str, port);

    // Open QUIC connection
    HQUIC conn = NULL;
    QUIC_STATUS status = MsQuic->ConnectionOpen(
        DaemonRegistration, Daemon_FetchConnectionCallback, ctx, &conn);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[fetch] ConnectionOpen failed: 0x%x\n", status);
        free(ctx->recv_buf);
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }

    // Apply saved session ticket for 0-RTT
    {
        uint32_t ticket_len;
        uint8_t *ticket = Daemon_LoadSessionTicket(&ticket_len);
        if (ticket)
        {
            MsQuic->SetParam(conn, QUIC_PARAM_CONN_RESUMPTION_TICKET,
                             ticket_len, ticket);
            free(ticket);
        }
    }

    status = MsQuic->ConnectionStart(
        conn, DaemonClientConfig, addr_family, addr_str, port);
    if (QUIC_FAILED(status))
    {
        LOG_ERROR("[fetch] ConnectionStart failed: 0x%x\n", status);
        MsQuic->ConnectionClose(conn);
        free(ctx->recv_buf);
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }

    // Wait for completion (5s timeout)
    int wait_result = WH_EVENT_WAIT(ctx->completion_event, 5000);

    // Close connection synchronously — blocks until all callbacks complete
    MsQuic->ConnectionClose(conn);

    BOOLEAN success = FALSE;
    if (wait_result == WH_EVENT_WAIT_OK && ctx->status == 1)
    {
        *size_out = ctx->data_size;
        success = TRUE;
        LOG("[fetch] Successfully fetched %u bytes from %s:%u\n",
            ctx->data_size, addr_str, port);
    }
    else if (wait_result == WH_EVENT_WAIT_TIMEOUT)
    {
        LOG("[fetch] Fetch timed out after 5s from %s:%u\n", addr_str, port);
    }
    else if (ctx->status == 3)
    {
        LOG("[fetch] Peer %s:%u does not have chunk\n", addr_str, port);
    }
    else
    {
        LOG("[fetch] Fetch failed from %s:%u (status=%ld)\n",
            addr_str, port, ctx->status);
    }

    free(ctx->recv_buf);
    WH_EVENT_DESTROY(ctx->completion_event);
    free(ctx);
    return success;
}

//=============================================================================
// Relay Connection (optional)
//=============================================================================

//=============================================================================
// Fix 5: Replication Retry Queue
//=============================================================================

static void Daemon_RetryQueue_Add(const uint8_t hash[WH_HASH_SIZE])
{
    WH_MUTEX_LOCK(g_daemon.retry_lock);

    // Check for duplicate
    for (uint32_t i = 0; i < g_daemon.retry_count; i++)
    {
        if (memcmp(g_daemon.retry_queue[i].hash, hash, WH_HASH_SIZE) == 0)
        {
            WH_MUTEX_UNLOCK(g_daemon.retry_lock);
            return;  // Already queued
        }
    }

    if (g_daemon.retry_count >= 32)
    {
        WH_MUTEX_UNLOCK(g_daemon.retry_lock);
        return;  // Queue full
    }

    uint32_t idx = g_daemon.retry_count;
    memcpy(g_daemon.retry_queue[idx].hash, hash, WH_HASH_SIZE);
    g_daemon.retry_queue[idx].retry_after = time(NULL) + 120;  // 2 min initial backoff
    g_daemon.retry_queue[idx].attempt_count = 1;
    g_daemon.retry_count++;

    WH_MUTEX_UNLOCK(g_daemon.retry_lock);
    LOG("[retry] Queued chunk for retry (attempt 1, retry in 120s)\n");
}

//=============================================================================
// Fix 6: Peer Address Resolution
//=============================================================================

static BOOLEAN Daemon_ResolvePeerAddress(const uint8_t peer_id[32],
                                          char *addr_out, size_t addr_len, uint16_t *port_out,
                                          QUIC_ADDRESS_FAMILY *family_out)
{
    // Check relay-discovered peers first (better NAT-mapped endpoints)
    // Prefer IPv4 but fall back to IPv6
    WH_MUTEX_LOCK(g_daemon.peers_lock);
    LONG peer_count = WH_ATOMIC_LOAD(&g_daemon.discovered_peer_count);
    for (LONG i = 0; i < peer_count; i++)
    {
        if (memcmp(g_daemon.discovered_peers[i].peer_id, peer_id, 32) == 0)
        {
            const ENDPOINT *ipv6_ep = NULL;
            for (uint16_t j = 0; j < g_daemon.discovered_peers[i].endpoint_count; j++)
            {
                const ENDPOINT *ep = &g_daemon.discovered_peers[i].endpoints[j];
                if (ep->addr_type == 0x04)
                {
                    snprintf(addr_out, addr_len, "%u.%u.%u.%u",
                             ep->addr[0], ep->addr[1], ep->addr[2], ep->addr[3]);
                    *port_out = ep->port;
                    *family_out = QUIC_ADDRESS_FAMILY_INET;
                    WH_MUTEX_UNLOCK(g_daemon.peers_lock);
                    return TRUE;
                }
                if (ep->addr_type == 0x06 && !ipv6_ep)
                    ipv6_ep = ep;
            }
            if (ipv6_ep)
            {
                ENDPOINT ep_copy;
                memcpy(&ep_copy, ipv6_ep, sizeof(ENDPOINT));
                uint16_t ep_port;
                if (Endpoint_ToString(&ep_copy, addr_out, addr_len, &ep_port))
                {
                    *port_out = ep_port;
                    *family_out = QUIC_ADDRESS_FAMILY_INET6;
                    WH_MUTEX_UNLOCK(g_daemon.peers_lock);
                    return TRUE;
                }
            }
        }
    }
    WH_MUTEX_UNLOCK(g_daemon.peers_lock);

    // Fallback: search DHT routing table
    if (g_daemon.dht_enabled)
    {
        WH_MUTEX_LOCK(g_daemon.dht_lock);
        for (int b = 0; b < DHT_BUCKET_COUNT; b++)
        {
            K_BUCKET *bucket = &g_daemon.dht_node.routing_table.buckets[b];
            for (uint32_t n = 0; n < bucket->count; n++)
            {
                if (memcmp(bucket->nodes[n].node_id, peer_id, 32) == 0)
                {
                    ROUTING_NODE *node = &bucket->nodes[n];
                    if (node->addr_type == 0x04 || node->addr_type == DHT_ADDR_IPV4)
                    {
                        snprintf(addr_out, addr_len, "%u.%u.%u.%u",
                                 node->addr[0], node->addr[1], node->addr[2], node->addr[3]);
                        *port_out = DAEMON_DEFAULT_PORT;
                        *family_out = QUIC_ADDRESS_FAMILY_INET;
                        WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                        return TRUE;
                    }
                    if (node->addr_type == 0x06 || node->addr_type == DHT_ADDR_IPV6)
                    {
                        struct in6_addr addr6;
                        memcpy(&addr6, node->addr, 16);
                        if (inet_ntop(AF_INET6, &addr6, addr_out, (socklen_t)addr_len))
                        {
                            *port_out = DAEMON_DEFAULT_PORT;
                            *family_out = QUIC_ADDRESS_FAMILY_INET6;
                            WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                            return TRUE;
                        }
                    }
                }
            }
        }
        WH_MUTEX_UNLOCK(g_daemon.dht_lock);
    }

    return FALSE;
}

//=============================================================================
// Fix 6: Proof-of-Storage Challenge Sender
//=============================================================================

// Context for proof challenge — synchronous pattern like Daemon_FetchChunkFromPeer
typedef struct {
    WH_EVENT       completion_event;
    volatile int32_t status;         // 0=pending, 1=verified, 2=failed, 3=no_chunk
    uint8_t        expected_proof[WH_HASH_SIZE];
    uint8_t       *recv_buf;
    uint32_t       recv_size;
    uint32_t       recv_capacity;
} PROOF_CHALLENGE_CONTEXT;

static QUIC_STATUS QUIC_API Daemon_ProofStreamCallback(
    HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event)
{
    PROOF_CHALLENGE_CONTEXT *ctx = (PROOF_CHALLENGE_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_RECEIVE:
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++)
        {
            const QUIC_BUFFER *buf = &Event->RECEIVE.Buffers[i];
            uint32_t needed = ctx->recv_size + buf->Length;
            if (needed <= ctx->recv_capacity)
            {
                memcpy(ctx->recv_buf + ctx->recv_size, buf->Buffer, buf->Length);
                ctx->recv_size += buf->Length;
            }
        }
        break;

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    {
        // Parse PROOF_RESPONSE: [1B type=0x0A][4B len=65][32B hash][32B proof][1B status]
        if (ctx->recv_size >= CTRL_HEADER_SIZE)
        {
            uint8_t msg_type = ctx->recv_buf[0];
            uint32_t payload_len = ReadUint32LE(ctx->recv_buf + 1);

            if (msg_type == CTRL_MSG_PROOF_RESPONSE &&
                ctx->recv_size >= CTRL_HEADER_SIZE + payload_len &&
                payload_len >= WH_HASH_SIZE + WH_HASH_SIZE + 1)
            {
                const uint8_t *payload = ctx->recv_buf + CTRL_HEADER_SIZE;
                uint8_t status = payload[WH_HASH_SIZE + WH_HASH_SIZE];

                if (status == 0x00)
                {
                    const uint8_t *proof = payload + WH_HASH_SIZE;
                    if (memcmp(proof, ctx->expected_proof, WH_HASH_SIZE) == 0)
                        WH_ATOMIC_SET(&ctx->status, 1);  // verified
                    else
                        WH_ATOMIC_SET(&ctx->status, 2);  // wrong proof
                }
                else
                {
                    WH_ATOMIC_SET(&ctx->status, 3);  // peer doesn't have chunk
                }
            }
            else
            {
                WH_ATOMIC_SET(&ctx->status, 2);
            }
        }
        else
        {
            WH_ATOMIC_SET(&ctx->status, 2);
        }
        WH_EVENT_SET(ctx->completion_event);
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        QUIC_BUFFER *sent = (QUIC_BUFFER *)Event->SEND_COMPLETE.ClientContext;
        if (sent) { free(sent->Buffer); free(sent); }
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API Daemon_ProofConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    PROOF_CHALLENGE_CONTEXT *ctx = (PROOF_CHALLENGE_CONTEXT *)Context;

    switch (Event->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
    {
        // Open a stream and send the proof challenge
        HQUIC stream = NULL;
        QUIC_STATUS status = MsQuic->StreamOpen(Connection, QUIC_STREAM_OPEN_FLAG_NONE,
            Daemon_ProofStreamCallback, ctx, &stream);
        if (QUIC_FAILED(status))
        {
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }
        status = MsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
        if (QUIC_FAILED(status))
        {
            MsQuic->StreamClose(stream);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
            break;
        }

        // Build and send PROOF_CHALLENGE message from recv_buf (reused for outbound)
        // The challenge data is stored in ctx->recv_buf by the caller before connecting
        uint32_t challenge_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + WH_HASH_SIZE;
        uint8_t *msg = (uint8_t *)malloc(challenge_size);
        QUIC_BUFFER *send_buf = msg ? (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER)) : NULL;
        if (msg && send_buf)
        {
            // Copy the pre-built challenge from the start of recv_buf
            memcpy(msg, ctx->recv_buf, challenge_size);
            send_buf->Buffer = msg;
            send_buf->Length = challenge_size;
            // Reset recv_buf for receiving the response
            ctx->recv_size = 0;
            MsQuic->StreamSend(stream, send_buf, 1, QUIC_SEND_FLAG_FIN, send_buf);
        }
        else
        {
            free(msg); free(send_buf);
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
        }
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
        return QUIC_STATUS_SUCCESS;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        if (ctx->status == 0)
        {
            WH_ATOMIC_SET(&ctx->status, 2);
            WH_EVENT_SET(ctx->completion_event);
        }
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static BOOLEAN Daemon_SendProofChallenge(const char *addr_str, uint16_t port,
                                           QUIC_ADDRESS_FAMILY addr_family,
                                           const uint8_t hash[WH_HASH_SIZE],
                                           const uint8_t *chunk_data, uint32_t size)
{
    if (!DaemonClientConfig || !MsQuic) return FALSE;

    PROOF_CHALLENGE_CONTEXT *ctx = (PROOF_CHALLENGE_CONTEXT *)calloc(1, sizeof(PROOF_CHALLENGE_CONTEXT));
    if (!ctx) return FALSE;

    ctx->completion_event = WH_EVENT_CREATE();
    if (!ctx->completion_event) { free(ctx); return FALSE; }

    // Generate random seed and compute expected proof
    uint8_t seed[WH_HASH_SIZE];
    randombytes_buf(seed, WH_HASH_SIZE);
    Proof_Compute(chunk_data, size, seed, ctx->expected_proof);

    // Pre-build the challenge message in recv_buf so the callback can send it
    uint32_t challenge_size = CTRL_HEADER_SIZE + WH_HASH_SIZE + WH_HASH_SIZE;
    ctx->recv_capacity = challenge_size + CTRL_HEADER_SIZE + WH_HASH_SIZE * 2 + 1;
    ctx->recv_buf = (uint8_t *)malloc(ctx->recv_capacity);
    if (!ctx->recv_buf)
    {
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }
    ctx->recv_size = challenge_size;  // Will be reset to 0 before receiving response

    // Build challenge: [1B type=0x09][4B len=64][32B hash][32B seed]
    ctx->recv_buf[0] = CTRL_MSG_PROOF_CHALLENGE;
    WriteUint32LE(ctx->recv_buf + 1, WH_HASH_SIZE + WH_HASH_SIZE);
    memcpy(ctx->recv_buf + CTRL_HEADER_SIZE, hash, WH_HASH_SIZE);
    memcpy(ctx->recv_buf + CTRL_HEADER_SIZE + WH_HASH_SIZE, seed, WH_HASH_SIZE);

    HQUIC conn = NULL;
    QUIC_STATUS status = MsQuic->ConnectionOpen(
        DaemonRegistration, Daemon_ProofConnectionCallback, ctx, &conn);
    if (QUIC_FAILED(status))
    {
        free(ctx->recv_buf);
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }

    status = MsQuic->ConnectionStart(conn, DaemonClientConfig,
        addr_family, addr_str, port);
    if (QUIC_FAILED(status))
    {
        MsQuic->ConnectionClose(conn);
        free(ctx->recv_buf);
        WH_EVENT_DESTROY(ctx->completion_event);
        free(ctx);
        return FALSE;
    }

    // Wait for response (5s timeout)
    WH_EVENT_WAIT(ctx->completion_event, 5000);
    MsQuic->ConnectionClose(conn);

    BOOLEAN verified = (ctx->status == 1);
    free(ctx->recv_buf);
    WH_EVENT_DESTROY(ctx->completion_event);
    free(ctx);

    return verified;
}

//=============================================================================
// Helper: Format DHT location as address string + address family
//=============================================================================

static BOOLEAN FormatDhtLocation(const DHT_LOCATION *loc,
    char *addr_str, size_t addr_len,
    QUIC_ADDRESS_FAMILY *family_out)
{
    if (!loc || !addr_str || addr_len == 0 || !family_out)
        return FALSE;

    // Reject zero addresses
    uint8_t zero_addr[16] = {0};
    if (memcmp(loc->addr, zero_addr, 16) == 0)
        return FALSE;

    if (loc->addr_type == 0x04)
    {
        snprintf(addr_str, addr_len, "%u.%u.%u.%u",
                 loc->addr[0], loc->addr[1], loc->addr[2], loc->addr[3]);
        *family_out = QUIC_ADDRESS_FAMILY_INET;
        return TRUE;
    }
    else if (loc->addr_type == 0x06)
    {
        struct in6_addr addr6;
        memcpy(&addr6, loc->addr, 16);
        if (inet_ntop(AF_INET6, &addr6, addr_str, (socklen_t)addr_len) == NULL)
            return FALSE;
        *family_out = QUIC_ADDRESS_FAMILY_INET6;
        return TRUE;
    }

    return FALSE;  // Unsupported address type
}

//=============================================================================
// Fix 7: EC Recovery with Remote Parity Fetch
//=============================================================================

static BOOLEAN Daemon_RecoverChunkWithRemoteFetch(const uint8_t hash[WH_HASH_SIZE],
                                                    const EC_GROUP *ec_group,
                                                    const FILE_MANIFEST *manifest)
{
    if (!hash || !ec_group || !manifest || manifest->chunk_count == 0)
        return FALSE;

    // Already have it?
    if (ChunkStore_Has(hash)) return TRUE;

    // Try local-only recovery first
    if (Health_RecoverChunk(hash, ec_group, manifest))
        return TRUE;

    // Find which stripe this chunk belongs to
    uint32_t chunk_index = UINT32_MAX;
    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        if (memcmp(manifest->chunks[i].hash, hash, WH_HASH_SIZE) == 0)
        {
            chunk_index = i;
            break;
        }
    }
    if (chunk_index == UINT32_MAX)
        return FALSE;

    // Find the stripe containing this chunk
    uint32_t stripe_idx = UINT32_MAX;
    for (uint32_t s = 0; s < ec_group->stripe_count; s++)
    {
        uint32_t start = ec_group->stripes[s].data_chunk_start;
        uint32_t end = start + ec_group->stripes[s].data_chunk_count;
        if (chunk_index >= start && chunk_index < end)
        {
            stripe_idx = s;
            break;
        }
    }
    if (stripe_idx == UINT32_MAX)
        return FALSE;

    const EC_STRIPE *stripe = &ec_group->stripes[stripe_idx];
    uint8_t k = ec_group->k;

    // Count locally available shards (data + parity)
    uint32_t available = 0;
    for (uint32_t i = 0; i < stripe->data_chunk_count; i++)
    {
        uint32_t ci = stripe->data_chunk_start + i;
        if (ChunkStore_Has(manifest->chunks[ci].hash))
            available++;
    }
    for (uint8_t p = 0; p < ec_group->m; p++)
    {
        if (ChunkStore_Has(stripe->parity_hashes[p]))
            available++;
    }

    // Already have enough shards locally?
    if (available >= k)
    {
        return Health_RecoverChunk(hash, ec_group, manifest);
    }

    // Need to fetch remote shards — try parity first, then data
    uint32_t fetched = 0;
    uint32_t needed = k - available;
    uint8_t *fetch_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    if (!fetch_buf) return FALSE;

    // Fetch missing parity shards
    for (uint8_t p = 0; p < ec_group->m && fetched < needed; p++)
    {
        if (ChunkStore_Has(stripe->parity_hashes[p]))
            continue;

        // Look up parity shard locations via DHT
        if (g_daemon.dht_enabled)
        {
            DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
            WH_MUTEX_LOCK(g_daemon.dht_lock);
            uint32_t loc_count = DhtNode_FindChunkLocations(
                &g_daemon.dht_node, stripe->parity_hashes[p],
                locations, DHT_STORE_MAX_LOCATIONS);
            WH_MUTEX_UNLOCK(g_daemon.dht_lock);

            // Poll if needed
            if (loc_count == 0)
            {
                for (int poll = 0; poll < 10 && loc_count == 0; poll++)
                {
                    WH_SLEEP_MS(100);
                    WH_MUTEX_LOCK(g_daemon.dht_lock);
                    loc_count = DhtStore_Get(&g_daemon.dht_node.value_store,
                        stripe->parity_hashes[p], locations, DHT_STORE_MAX_LOCATIONS);
                    WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                }
            }

            for (uint32_t li = 0; li < loc_count; li++)
            {
                char addr_str[INET6_ADDRSTRLEN];
                QUIC_ADDRESS_FAMILY family;
                if (!FormatDhtLocation(&locations[li], addr_str, sizeof(addr_str), &family))
                    continue;

                uint32_t fetch_size = 0;
                if (Daemon_FetchChunkFromPeer(addr_str, locations[li].port,
                        family, stripe->parity_hashes[p], fetch_buf, &fetch_size))
                {
                    ChunkStore_Put(stripe->parity_hashes[p], fetch_buf, fetch_size);
                    fetched++;
                    LOG("[ec-remote] Fetched parity shard %u from %s:%u\n",
                        p, addr_str, locations[li].port);
                    break;
                }
            }
        }
    }

    // Fetch missing data shards (other than our target)
    for (uint32_t i = 0; i < stripe->data_chunk_count && fetched < needed; i++)
    {
        uint32_t ci = stripe->data_chunk_start + i;
        if (ci == chunk_index) continue;  // Skip target chunk
        if (ChunkStore_Has(manifest->chunks[ci].hash)) continue;

        if (g_daemon.dht_enabled)
        {
            DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
            WH_MUTEX_LOCK(g_daemon.dht_lock);
            uint32_t loc_count = DhtNode_FindChunkLocations(
                &g_daemon.dht_node, manifest->chunks[ci].hash,
                locations, DHT_STORE_MAX_LOCATIONS);
            WH_MUTEX_UNLOCK(g_daemon.dht_lock);

            if (loc_count == 0)
            {
                for (int poll = 0; poll < 10 && loc_count == 0; poll++)
                {
                    WH_SLEEP_MS(100);
                    WH_MUTEX_LOCK(g_daemon.dht_lock);
                    loc_count = DhtStore_Get(&g_daemon.dht_node.value_store,
                        manifest->chunks[ci].hash, locations, DHT_STORE_MAX_LOCATIONS);
                    WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                }
            }

            for (uint32_t li = 0; li < loc_count; li++)
            {
                char addr_str[INET6_ADDRSTRLEN];
                QUIC_ADDRESS_FAMILY family;
                if (!FormatDhtLocation(&locations[li], addr_str, sizeof(addr_str), &family))
                    continue;

                uint32_t fetch_size = 0;
                if (Daemon_FetchChunkFromPeer(addr_str, locations[li].port,
                        family, manifest->chunks[ci].hash, fetch_buf, &fetch_size))
                {
                    ChunkStore_Put(manifest->chunks[ci].hash, fetch_buf, fetch_size);
                    fetched++;
                    LOG("[ec-remote] Fetched data shard %u from %s:%u\n",
                        ci, addr_str, locations[li].port);
                    break;
                }
            }
        }
    }

    free(fetch_buf);

    // Retry local EC recovery with fetched shards
    return Health_RecoverChunk(hash, ec_group, manifest);
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
        WH_MUTEX_LOCK(g_daemon.peers_lock);
        WH_ATOMIC_SET(&g_daemon.discovered_peer_count, 0);
        WH_MUTEX_UNLOCK(g_daemon.peers_lock);
        return;
    }

    // Update local peer table (protected by peers_lock)
    uint16_t count = peer_count < MAX_FIND_PEERS ? peer_count : MAX_FIND_PEERS;
    WH_MUTEX_LOCK(g_daemon.peers_lock);
    memcpy(g_daemon.discovered_peers, peers, count * sizeof(DISCOVERED_PEER));
    WH_ATOMIC_SET(&g_daemon.discovered_peer_count, (int32_t)count);
    WH_MUTEX_UNLOCK(g_daemon.peers_lock);

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

    // Seed DHT routing table with discovered peers
    if (g_daemon.dht_enabled)
    {
        WH_MUTEX_LOCK(g_daemon.dht_lock);
        uint16_t seeded = 0;
        for (uint16_t i = 0; i < count; i++)
        {
            if (memcmp(peers[i].peer_id, g_daemon.keypair.public_key, 32) == 0)
                continue;  // Skip self

            for (uint16_t e = 0; e < peers[i].endpoint_count; e++)
            {
                if (peers[i].endpoints[e].priority >= 200)
                    continue;  // Skip relay-forwarded endpoints

                RoutingTable_AddNode(&g_daemon.dht_node.routing_table,
                                      peers[i].peer_id,
                                      peers[i].endpoints[e].addr_type,
                                      peers[i].endpoints[e].addr,
                                      g_daemon.dht_port);

                DhtNode_SendFindNode(&g_daemon.dht_node,
                                      peers[i].endpoints[e].addr,
                                      peers[i].endpoints[e].addr_type,
                                      g_daemon.dht_port,
                                      g_daemon.dht_node.keypair->public_key);
                seeded++;
                break;  // One endpoint per peer
            }
        }
        WH_MUTEX_UNLOCK(g_daemon.dht_lock);
        if (seeded > 0)
            LOG("[daemon] Seeded DHT routing table with %u peers from relay discovery\n", seeded);
    }
}

static BOOLEAN Daemon_ConnectRelay(void)
{
    // Keypair already loaded in main() init sequence

    // Discover our endpoints
    ENDPOINT endpoints[MAX_ENDPOINTS];
    uint16_t endpoint_count = Discovery_FindEndpoints(endpoints, MAX_ENDPOINTS);

    // Discovery returns port=0; fill in our actual QUIC listening port
    for (uint16_t i = 0; i < endpoint_count; i++) {
        endpoints[i].port = g_daemon.listen_port;
    }

    // Save our endpoints for hole-punch use
    memcpy(g_daemon.our_endpoints, endpoints, endpoint_count * sizeof(ENDPOINT));
    g_daemon.our_endpoint_count = endpoint_count;

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
        .on_punch_request = Daemon_OnPunchRequest,
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
// Background Work Queue — Worker Thread
//=============================================================================

static WH_THREAD_RETURN WorkQueue_ThreadProc(WH_THREAD_PARAM param)
{
    UNREFERENCED_PARAMETER(param);
    LOG("[worker] Background worker thread started\n");

    while (!WH_ATOMIC_LOAD(&g_work_queue.shutdown))
    {
        WORK_ITEM *item = WorkQueue_Pop();
        if (!item) continue;

        switch (item->type)
        {
        case WORK_ERASURE_ENCODE:
        {
            // Deserialize manifest
            FILE_MANIFEST *manifest = Manifest_Deserialize(
                item->erasure.manifest_data, item->erasure.manifest_size);
            if (!manifest)
            {
                LOG("[worker] EC: failed to deserialize manifest\n");
                break;
            }

            uint8_t ec_k = (uint8_t)Config_GetUint64(g_daemon.config, "ec_data_shards",
                                                        CONFIG_DEFAULT_EC_DATA_SHARDS);
            uint8_t ec_m = (uint8_t)Config_GetUint64(g_daemon.config, "ec_parity_shards",
                                                        CONFIG_DEFAULT_EC_PARITY_SHARDS);
            EC_GROUP *ec_group = ErasureCoding_Encode(manifest, ec_k, ec_m);
            if (ec_group)
            {
                LOG("[worker] EC: %u stripes with RS(%u,%u)\n",
                    ec_group->stripe_count, ec_k, ec_m);

                // Account for parity chunks in daemon stats
                {
                    uint32_t parity_count = ec_group->stripe_count * ec_group->m;
                    WH_ATOMIC_ADD(&g_daemon.chunk_count, (LONG)parity_count);
                    WH_ATOMIC_ADD64(&g_daemon.storage_used,
                        (LONGLONG)parity_count * WH_CHUNK_SIZE);
                }

                // Clear stale parity replica metadata so replication starts fresh
                for (uint32_t s = 0; s < ec_group->stripe_count; s++)
                    for (uint8_t p = 0; p < ec_group->m; p++)
                        ChunkStore_ClearReplicas(ec_group->stripes[s].parity_hashes[p]);

                // Batch replication for parity chunks (same pattern as data chunk batching)
                {
                    uint32_t parity_idx = 0;
                    uint32_t total_parity = ec_group->stripe_count * ec_group->m;

                    // Collect parity chunks into batches
                    while (parity_idx < total_parity)
                    {
                        WORK_ITEM *batch_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                        if (!batch_item) break;
                        batch_item->type = WORK_REPLICATE_BATCH;
                        uint32_t count = 0;

                        while (parity_idx < total_parity && count < MAX_BATCH_CHUNKS)
                        {
                            uint32_t s = parity_idx / ec_group->m;
                            uint8_t  p = (uint8_t)(parity_idx % ec_group->m);

                            uint8_t *parity_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                            if (!parity_data) { parity_idx++; continue; }
                            uint32_t parity_size = 0;
                            if (!ChunkStore_Get(ec_group->stripes[s].parity_hashes[p],
                                                parity_data, &parity_size))
                            {
                                free(parity_data);
                                parity_idx++;
                                continue;
                            }

                            memcpy(batch_item->batch.hashes[count],
                                   ec_group->stripes[s].parity_hashes[p], WH_HASH_SIZE);
                            batch_item->batch.data[count] = parity_data;
                            batch_item->batch.sizes[count] = parity_size;
                            count++;
                            parity_idx++;
                        }

                        if (count > 0)
                        {
                            batch_item->batch.chunk_count = count;
                            WorkQueue_Push(batch_item);
                        }
                        else
                        {
                            free(batch_item);
                        }
                    }

                    // Proof precomputation for parity chunks (enqueued after batch items)
                    for (uint32_t s = 0; s < ec_group->stripe_count; s++)
                    {
                        for (uint8_t p = 0; p < ec_group->m; p++)
                        {
                            uint8_t *parity_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                            if (!parity_data) continue;
                            uint32_t parity_size = 0;
                            if (ChunkStore_Get(ec_group->stripes[s].parity_hashes[p],
                                                parity_data, &parity_size))
                            {
                                WORK_ITEM *proof = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                                if (proof)
                                {
                                    proof->type = WORK_PRECOMPUTE_PROOFS;
                                    memcpy(proof->proofs.hash, ec_group->stripes[s].parity_hashes[p], WH_HASH_SIZE);
                                    proof->proofs.data = parity_data;
                                    proof->proofs.size = parity_size;
                                    WorkQueue_Push(proof);
                                    parity_data = NULL;  // Ownership transferred
                                }
                            }
                            free(parity_data);
                        }
                    }
                }

                // Enqueue parity DHT announcements
                if (g_daemon.dht_enabled)
                {
                    WORK_ITEM *dht_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                    if (dht_item)
                    {
                        dht_item->type = WORK_DHT_ANNOUNCE;
                        uint32_t count = 0;
                        for (uint32_t s = 0; s < ec_group->stripe_count && count < 64; s++)
                        {
                            for (uint8_t p = 0; p < ec_group->m && count < 64; p++)
                            {
                                memcpy(dht_item->dht_announce.hashes[count],
                                       ec_group->stripes[s].parity_hashes[p], WH_HASH_SIZE);
                                count++;
                            }
                        }
                        dht_item->dht_announce.hash_count = count;
                        WorkQueue_Push(dht_item);
                    }
                }

                // Enqueue EC metadata persistence
                WORK_ITEM *meta = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                if (meta)
                {
                    meta->type = WORK_PERSIST_EC_META;
                    meta->ec_meta.ec_group = ec_group;
                    ec_group = NULL;  // Ownership transferred
                    meta->ec_meta.manifest_data = (uint8_t *)malloc(item->erasure.manifest_size);
                    if (meta->ec_meta.manifest_data)
                    {
                        memcpy(meta->ec_meta.manifest_data, item->erasure.manifest_data,
                               item->erasure.manifest_size);
                        meta->ec_meta.manifest_size = item->erasure.manifest_size;
                        memcpy(meta->ec_meta.manifest_hash, item->erasure.manifest_hash, WH_HASH_SIZE);
                        WorkQueue_Push(meta);
                    }
                    else
                    {
                        WorkQueue_FreeItem(meta);
                    }
                }

                if (ec_group)
                    ErasureCoding_DestroyGroup(ec_group);
            }
            else
            {
                LOG("[worker] EC encoding failed (continuing without parity)\n");
            }
            Manifest_Destroy(manifest);
            break;
        }

        case WORK_REPLICATE_CHUNK:
        {
            Daemon_ReplicateChunk(item->replicate.hash, item->replicate.data, item->replicate.size);
            break;
        }

        case WORK_REPLICATE_BATCH:
        {
            Daemon_ReplicateBatch(
                (const uint8_t (*)[WH_HASH_SIZE])item->batch.hashes,
                item->batch.data,
                item->batch.sizes,
                item->batch.chunk_count
            );
            break;
        }

        case WORK_PRECOMPUTE_PROOFS:
        {
            Proof_PrecomputeAndCache(item->proofs.hash, item->proofs.data, item->proofs.size);
            break;
        }

        case WORK_DHT_ANNOUNCE:
        {
            if (g_daemon.dht_enabled)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                for (uint32_t i = 0; i < item->dht_announce.hash_count; i++)
                {
                    ROUTING_NODE closest_check[1];
                    uint32_t rt_count = RoutingTable_FindClosest(
                        &g_daemon.dht_node.routing_table,
                        item->dht_announce.hashes[i], closest_check, 1);
                    if (rt_count > 0)
                    {
                        DhtNode_AnnounceChunk(&g_daemon.dht_node,
                            item->dht_announce.hashes[i], g_daemon.listen_port);
                    }
                    else if (g_daemon.pending_announce_count < 16)
                    {
                        memcpy(g_daemon.pending_announce_hashes[g_daemon.pending_announce_count],
                               item->dht_announce.hashes[i], WH_HASH_SIZE);
                        g_daemon.pending_announce_count++;
                    }
                }
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
            }
            break;
        }

        case WORK_PERSIST_EC_META:
        {
            // Deserialize manifest for saving
            FILE_MANIFEST *manifest = Manifest_Deserialize(
                item->ec_meta.manifest_data, item->ec_meta.manifest_size);
            if (!manifest) break;

            char ec_dir[MAX_PATH];
            char ec_path[MAX_PATH];
#ifdef _WIN32
            const char *home_ec = getenv("USERPROFILE");
            if (home_ec)
            {
                snprintf(ec_dir, sizeof(ec_dir), "%s\\.wormhole\\ec", home_ec);
                CreateDirectoryA(ec_dir, NULL);
                char hex[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex + hi * 2, "%02x", item->ec_meta.manifest_hash[hi]);
                hex[64] = '\0';
                snprintf(ec_path, sizeof(ec_path), "%s\\%s.ec", ec_dir, hex);
            }
#else
            const char *home_ec = getenv("HOME");
            if (home_ec)
            {
                snprintf(ec_dir, sizeof(ec_dir), "%s/.wormhole/ec", home_ec);
                mkdir(ec_dir, 0700);
                char hex[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex + hi * 2, "%02x", item->ec_meta.manifest_hash[hi]);
                hex[64] = '\0';
                snprintf(ec_path, sizeof(ec_path), "%s/%s.ec", ec_dir, hex);
            }
#endif
            if (home_ec)
            {
                if (ErasureCoding_SaveMetadata(ec_path, item->ec_meta.ec_group, manifest))
                    LOG("[worker] EC metadata saved to %s\n", ec_path);
                else
                    LOG("[worker] Failed to save EC metadata\n");
            }
            Manifest_Destroy(manifest);
            break;
        }

        case WORK_CHECK_REPLICATION:
        {
            FILE_REG_ENTRY entry;
            FILE_MANIFEST *manifest = NULL;
            if (!FileRegistry_Load(item->check_repl.manifest_hash, &entry, &manifest))
                break;

            if (entry.status == FILE_STATUS_OFFLOADED || entry.status == FILE_STATUS_SAFE)
            {
                Manifest_Destroy(manifest);
                break;  // Already handled
            }

            uint32_t repl_target = (uint32_t)Config_GetUint64(g_daemon.config,
                "replication_target", CONFIG_DEFAULT_REPLICATION_TARGET);
            uint32_t replicated = 0;

            for (uint32_t i = 0; i < manifest->chunk_count; i++)
            {
                uint32_t remote_copies = ChunkStore_GetReplicaCount(manifest->chunks[i].hash);
                if (i == 0)
                {
                    char h8[17];
                    for (int x = 0; x < 8; x++) sprintf(h8 + x*2, "%02x", manifest->chunks[i].hash[x]);
                    h8[16] = '\0';
                    LOG("[worker] Checking replicas: first chunk=%s... count=%u\n", h8, remote_copies);
                }
                if (remote_copies >= repl_target - 1)  // -1 for our local copy
                    replicated++;
            }

            FileRegistry_UpdateStatus(item->check_repl.manifest_hash,
                replicated >= manifest->chunk_count ? FILE_STATUS_SAFE : FILE_STATUS_REPLICATING,
                replicated);

            if (replicated >= manifest->chunk_count)
            {
                LOG("[worker] File %s safely replicated to network\n", entry.filename);

                // Only auto-evict if enabled in config (default: disabled)
                if (Config_GetUint64(g_daemon.config, "auto_evict_enabled",
                        CONFIG_DEFAULT_AUTO_EVICT))
                {
                    uint64_t freed = 0;
                    for (uint32_t i = 0; i < manifest->chunk_count; i++)
                    {
                        if (ChunkStore_Has(manifest->chunks[i].hash))
                        {
                            freed += manifest->chunks[i].chunk_size;
                            ChunkStore_Delete(manifest->chunks[i].hash);
                            WH_ATOMIC_DEC(&g_daemon.chunk_count);
                        }
                    }

                    FileRegistry_UpdateStatus(item->check_repl.manifest_hash,
                        FILE_STATUS_OFFLOADED, replicated);

                    if (freed > 0)
                    {
                        WH_ATOMIC_ADD64(&g_daemon.storage_used, -(LONGLONG)freed);
                        LOG("[worker] Local chunks cleaned up for %s (freed %llu bytes)\n",
                            entry.filename, (unsigned long long)freed);
                    }
                }
                else
                {
                    // Keep local copies, just mark as safe
                    FileRegistry_UpdateStatus(item->check_repl.manifest_hash,
                        FILE_STATUS_SAFE, replicated);
                    LOG("[worker] File %s safe (local copies preserved)\n", entry.filename);
                }
            }
            else
            {
                LOG("[worker] File %s: %u/%u chunks replicated\n",
                    entry.filename, replicated, manifest->chunk_count);
            }

            Manifest_Destroy(manifest);
            break;
        }
        }

        WorkQueue_FreeItem(item);
    }

    LOG("[worker] Background worker thread exiting\n");
    return 0;
}

//=============================================================================
// Quota Enforcement
//=============================================================================

static void Daemon_EnforceQuota(uint64_t additional_bytes)
{
    if (g_daemon.max_storage_bytes == 0) return;  // No quota

    uint64_t current = (uint64_t)WH_ATOMIC_LOAD64(&g_daemon.storage_used);
    uint64_t projected = current + additional_bytes;

    if (projected > g_daemon.max_storage_bytes)
    {
        uint64_t overage = projected - g_daemon.max_storage_bytes;
        LOG("[daemon] Storage quota would be exceeded by %llu bytes, evicting...\n",
            (unsigned long long)overage);
        uint64_t freed = ChunkStore_Evict(overage);
        if (freed > 0)
        {
            WH_ATOMIC_ADD64(&g_daemon.storage_used, -(LONGLONG)freed);
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
    //--- STORE: chunk a file, save to registry, enqueue background work ---
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

        // Enforce quota before storing
        {
            uint64_t fsize = 0;
            GetWormholeFileSize(filepath, &fsize);
            Daemon_EnforceQuota(fsize);
        }

        // Client-side encryption: encrypt file to temp, then chunk the ciphertext
        uint8_t encryption_key[FILE_CRYPTO_KEY_SIZE];
        char encrypted_path[MAX_PATH];
        BOOLEAN file_encrypted = FALSE;
        const char *chunk_source = filepath;

        {
            FileCrypto_GenerateKey(encryption_key);
#ifdef _WIN32
            const char *home_enc = getenv("USERPROFILE");
            if (home_enc)
                snprintf(encrypted_path, sizeof(encrypted_path),
                    "%s\\.wormhole\\.enc_temp_%lu", home_enc, (unsigned long)GetCurrentProcessId());
#else
            const char *home_enc = getenv("HOME");
            if (home_enc)
                snprintf(encrypted_path, sizeof(encrypted_path),
                    "%s/.wormhole/.enc_temp_%d", home_enc, (int)getpid());
#endif
            if (home_enc && FileCrypto_EncryptFile(filepath, encrypted_path, encryption_key))
            {
                chunk_source = encrypted_path;
                file_encrypted = TRUE;
                LOG("[daemon] File encrypted for storage\n");
            }
            else
            {
                LOG("[daemon] Encryption failed, storing plaintext\n");
                memset(encryption_key, 0, sizeof(encryption_key));
            }
        }

        // Single-pass: read + hash + store chunks
        uint32_t stored = 0;
        FILE_MANIFEST *manifest = Chunker_BuildManifestAndStore(chunk_source, &stored);

        // Clean up temp encrypted file
        if (file_encrypted)
            remove(encrypted_path);
        if (!manifest)
        {
            LOG_ERROR("[daemon] Failed to chunk and store: %s\n", filepath);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Update daemon stats
        WH_ATOMIC_ADD(&g_daemon.chunk_count, (LONG)stored);
        WH_ATOMIC_ADD64(&g_daemon.storage_used,
            (LONGLONG)manifest->file_size);

        LOG("[daemon] Stored %u/%u chunks for %s\n",
            stored, manifest->chunk_count, filepath);

        // Extract filename from path for registry
        char *reg_filename = NULL;
        uint32_t reg_filename_len = 0;
        ExtractFilename(filepath, &reg_filename, &reg_filename_len);
        const char *display_name = reg_filename ? reg_filename : filepath;

        // Save to file registry (with encryption key if encrypted)
        if (file_encrypted)
            FileRegistry_SaveEncrypted(manifest, display_name,
                FILE_STATUS_REPLICATING, encryption_key);
        else
            FileRegistry_Save(manifest, display_name, FILE_STATUS_REPLICATING);

        // Serialize manifest for background work items
        size_t manifest_data_size = 0;
        uint8_t *manifest_data = Manifest_Serialize(manifest, &manifest_data_size);

        // Clear stale replica metadata so replication check starts from count=0
        for (uint32_t i = 0; i < manifest->chunk_count; i++)
            ChunkStore_ClearReplicas(manifest->chunks[i].hash);

        // --- Enqueue background work ---

        // 1. Erasure coding
        if (manifest_data &&
            Config_GetUint64(g_daemon.config, "ec_enabled", CONFIG_DEFAULT_EC_ENABLED))
        {
            WORK_ITEM *ec_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
            if (ec_item)
            {
                ec_item->type = WORK_ERASURE_ENCODE;
                ec_item->erasure.manifest_data = (uint8_t *)malloc(manifest_data_size);
                if (ec_item->erasure.manifest_data)
                {
                    memcpy(ec_item->erasure.manifest_data, manifest_data, manifest_data_size);
                    ec_item->erasure.manifest_size = manifest_data_size;
                    memcpy(ec_item->erasure.manifest_hash, manifest->manifest_hash, WH_HASH_SIZE);
                    WorkQueue_Push(ec_item);
                }
                else
                {
                    free(ec_item);
                }
            }
        }

        // 2. Batch replication + proof precomputation for data chunks
        //    Enqueue batches first, then all proof items (not interleaved)
        {
            uint32_t idx = 0;
            while (idx < manifest->chunk_count)
            {
                WORK_ITEM *batch_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                if (!batch_item) break;
                batch_item->type = WORK_REPLICATE_BATCH;
                uint32_t count = 0;

                while (idx < manifest->chunk_count && count < MAX_BATCH_CHUNKS)
                {
                    uint8_t *chunk_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                    if (!chunk_data) { idx++; continue; }
                    uint32_t chunk_size = 0;
                    if (!ChunkStore_Get(manifest->chunks[idx].hash, chunk_data, &chunk_size))
                    {
                        free(chunk_data);
                        idx++;
                        continue;
                    }

                    memcpy(batch_item->batch.hashes[count], manifest->chunks[idx].hash, WH_HASH_SIZE);
                    batch_item->batch.data[count] = chunk_data;
                    batch_item->batch.sizes[count] = chunk_size;
                    count++;
                    idx++;
                }

                if (count > 0)
                {
                    batch_item->batch.chunk_count = count;
                    WorkQueue_Push(batch_item);
                }
                else
                {
                    free(batch_item);
                }
            }

            // Proof precomputation (enqueued after batch items)
            for (uint32_t i = 0; i < manifest->chunk_count; i++)
            {
                uint8_t *chunk_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                if (!chunk_data) continue;
                uint32_t chunk_size = 0;
                if (!ChunkStore_Get(manifest->chunks[i].hash, chunk_data, &chunk_size))
                {
                    free(chunk_data);
                    continue;
                }

                WORK_ITEM *proof = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                if (proof)
                {
                    proof->type = WORK_PRECOMPUTE_PROOFS;
                    memcpy(proof->proofs.hash, manifest->chunks[i].hash, WH_HASH_SIZE);
                    proof->proofs.data = chunk_data;
                    proof->proofs.size = chunk_size;
                    WorkQueue_Push(proof);
                }
                else
                {
                    free(chunk_data);
                }
            }
        }

        // 3. DHT announcements (batch all data chunk hashes)
        if (g_daemon.dht_enabled && manifest->chunk_count > 0)
        {
            uint32_t remaining = manifest->chunk_count;
            uint32_t idx = 0;
            while (remaining > 0)
            {
                WORK_ITEM *dht_item = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                if (!dht_item) break;
                dht_item->type = WORK_DHT_ANNOUNCE;
                uint32_t batch = remaining > 64 ? 64 : remaining;
                for (uint32_t j = 0; j < batch; j++)
                {
                    memcpy(dht_item->dht_announce.hashes[j],
                           manifest->chunks[idx + j].hash, WH_HASH_SIZE);
                }
                dht_item->dht_announce.hash_count = batch;
                WorkQueue_Push(dht_item);
                idx += batch;
                remaining -= batch;
            }
        }

        // 4. Replication check (enqueued last, runs after replication items)
        {
            WORK_ITEM *check = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
            if (check)
            {
                check->type = WORK_CHECK_REPLICATION;
                memcpy(check->check_repl.manifest_hash, manifest->manifest_hash, WH_HASH_SIZE);
                WorkQueue_Push(check);
            }
        }

        // --- Build IPC response (fast: no chunk hashes) ---
        // Response: [1B status][32B manifest_hash][2B filename_len][filename]
        uint16_t fn_len = (uint16_t)strlen(display_name);
        uint32_t resp_size = 1 + WH_HASH_SIZE + 2 + fn_len;
        if (response_capacity < resp_size)
        {
            free(reg_filename);
            free(manifest_data);
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        response_out[0] = IPC_STATUS_OK;
        memcpy(response_out + 1, manifest->manifest_hash, WH_HASH_SIZE);
        WriteUint16LE(response_out + 1 + WH_HASH_SIZE, fn_len);
        memcpy(response_out + 1 + WH_HASH_SIZE + 2, display_name, fn_len);

        free(reg_filename);
        free(manifest_data);
        Manifest_Destroy(manifest);

        return resp_size;
    }

    //--- GET: retrieve a single chunk by hash ---
    // Priority: 1) local store  2) discovered peers  3) DHT lookup
    case IPC_CMD_GET:
    {
        if (payload_size < WH_HASH_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        const uint8_t *hash = payload;

        // Response: [status][4B data_size][data]
        if (response_capacity < 1 + 4 + WH_CHUNK_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        //--- Phase 1: Local ChunkStore (fast path) ---
        if (ChunkStore_Has(hash))
        {
            uint32_t data_size = 0;
            if (ChunkStore_Get(hash, response_out + 1 + 4, &data_size))
            {
                response_out[0] = IPC_STATUS_OK;
                WriteUint32LE(response_out + 1, data_size);
                return 1 + 4 + data_size;
            }
        }

        // Allocate temp buffer for remote fetch attempts
        uint8_t *fetch_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!fetch_buf)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        //--- Phase 2: Discovered peers (relay-known endpoints) ---
        DISCOVERED_PEER local_peers_get[MAX_FIND_PEERS];
        LONG peer_count;
        WH_MUTEX_LOCK(g_daemon.peers_lock);
        peer_count = WH_ATOMIC_LOAD(&g_daemon.discovered_peer_count);
        if (peer_count > 0)
            memcpy(local_peers_get, g_daemon.discovered_peers, peer_count * sizeof(DISCOVERED_PEER));
        WH_MUTEX_UNLOCK(g_daemon.peers_lock);

        for (LONG i = 0; i < peer_count; i++)
        {
            DISCOVERED_PEER *peer = &local_peers_get[i];

            // Select best endpoint: prefer IPv4, fall back to IPv6
            const ENDPOINT *ep = NULL;
            for (uint16_t j = 0; j < peer->endpoint_count; j++)
            {
                if (peer->endpoints[j].addr_type == 0x04) { ep = &peer->endpoints[j]; break; }
            }
            if (!ep)
            {
                for (uint16_t j = 0; j < peer->endpoint_count; j++)
                {
                    if (peer->endpoints[j].addr_type == 0x06) { ep = &peer->endpoints[j]; break; }
                }
            }
            if (!ep) continue;

            char addr_str[INET6_ADDRSTRLEN];
            uint16_t ep_port;
            if (!Endpoint_ToString(ep, addr_str, sizeof(addr_str), &ep_port)) continue;

            QUIC_ADDRESS_FAMILY family = (ep->addr_type == 0x06)
                ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

            uint32_t fetched_size = 0;
            if (Daemon_FetchChunkFromPeer(addr_str, ep_port, family, hash,
                                           fetch_buf, &fetched_size))
            {
                // Cache locally
                ChunkStore_Put(hash, fetch_buf, fetched_size);
                WH_ATOMIC_INC(&g_daemon.chunk_count);
                WH_ATOMIC_ADD64(&g_daemon.storage_used, (LONGLONG)fetched_size);

                // Return to client
                response_out[0] = IPC_STATUS_OK;
                WriteUint32LE(response_out + 1, fetched_size);
                memcpy(response_out + 1 + 4, fetch_buf, fetched_size);
                free(fetch_buf);
                return 1 + 4 + fetched_size;
            }
        }

        //--- Phase 3: DHT lookup ---
        if (g_daemon.dht_enabled)
        {
            LOG("[fetch] Chunk not found on discovered peers, trying DHT...\n");

            // Check cached DHT locations first, and kick off async FIND_VALUE if none
            DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
            WH_MUTEX_LOCK(g_daemon.dht_lock);
            uint32_t loc_count = DhtNode_FindChunkLocations(
                &g_daemon.dht_node, hash, locations, DHT_STORE_MAX_LOCATIONS);
            WH_MUTEX_UNLOCK(g_daemon.dht_lock);

            // If no cached results, poll for async FIND_VALUE responses
            if (loc_count == 0)
            {
                for (int poll = 0; poll < 20; poll++)
                {
                    WH_SLEEP_MS(150);
                    WH_MUTEX_LOCK(g_daemon.dht_lock);
                    loc_count = DhtStore_Get(&g_daemon.dht_node.value_store,
                                              hash, locations, DHT_STORE_MAX_LOCATIONS);
                    WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                    if (loc_count > 0) break;
                }
            }

            // Try each DHT location
            for (uint32_t i = 0; i < loc_count; i++)
            {
                DHT_LOCATION *loc = &locations[i];

                char addr_str[INET6_ADDRSTRLEN];
                QUIC_ADDRESS_FAMILY family;
                if (!FormatDhtLocation(loc, addr_str, sizeof(addr_str), &family))
                    continue;

                uint32_t fetched_size = 0;
                if (Daemon_FetchChunkFromPeer(addr_str, loc->port, family, hash,
                                               fetch_buf, &fetched_size))
                {
                    // Cache locally
                    ChunkStore_Put(hash, fetch_buf, fetched_size);
                    WH_ATOMIC_INC(&g_daemon.chunk_count);
                    WH_ATOMIC_ADD64(&g_daemon.storage_used, (LONGLONG)fetched_size);

                    // Return to client
                    response_out[0] = IPC_STATUS_OK;
                    WriteUint32LE(response_out + 1, fetched_size);
                    memcpy(response_out + 1 + 4, fetch_buf, fetched_size);
                    free(fetch_buf);
                    return 1 + 4 + fetched_size;
                }
            }
        }

        free(fetch_buf);
        response_out[0] = IPC_STATUS_NOT_FOUND;
        return 1;
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
            (uint32_t)WH_ATOMIC_LOAD(&g_daemon.peer_count));
        WriteUint32LE(response_out + 5,
            (uint32_t)WH_ATOMIC_LOAD(&g_daemon.chunk_count));
        WriteUint64LE(response_out + 9,
            (uint64_t)WH_ATOMIC_LOAD64(&g_daemon.storage_used));
        response_out[17] = g_daemon.relay_client &&
            RelayClient_IsConnected(g_daemon.relay_client) ? 1 : 0;
        response_out[18] = DaemonListener ? 1 : 0;

        return 19;
    }

    //--- DHT_STATUS: return DHT stats ---
    case IPC_CMD_DHT_STATUS:
    {
        // Response: [status][4B dht_nodes][4B dht_values][8B msgs_sent][8B msgs_received]
        if (response_capacity < 1 + 4 + 4 + 8 + 8)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        response_out[0] = IPC_STATUS_OK;

        if (g_daemon.dht_enabled)
        {
            WH_MUTEX_LOCK(g_daemon.dht_lock);
            WriteUint32LE(response_out + 1, DhtNode_GetNodeCount(&g_daemon.dht_node));
            WriteUint32LE(response_out + 5, DhtStore_GetCount(&g_daemon.dht_node.value_store));
            WriteUint64LE(response_out + 9, g_daemon.dht_node.msgs_sent);
            WriteUint64LE(response_out + 17, g_daemon.dht_node.msgs_received);
            WH_MUTEX_UNLOCK(g_daemon.dht_lock);
        }
        else
        {
            // DHT not initialized — return zeroes
            WriteUint32LE(response_out + 1, 0);
            WriteUint32LE(response_out + 5, 0);
            WriteUint64LE(response_out + 9, 0);
            WriteUint64LE(response_out + 17, 0);
        }

        return 25;
    }

    //--- LIST_FILES: enumerate stored files ---
    case IPC_CMD_LIST_FILES:
    {
        // Response: [1B status][4B file_count][per file: 32B hash + 1B status + 8B size +
        //           4B chunk_count + 4B repl_count + 8B store_time + 2B name_len + name]
        FILE_REG_ENTRY entries[256];
        uint32_t count = FileRegistry_List(entries, 256);

        response_out[0] = IPC_STATUS_OK;
        WriteUint32LE(response_out + 1, count);
        uint32_t off = 5;

        for (uint32_t i = 0; i < count; i++)
        {
            FILE_REG_ENTRY *e = &entries[i];
            uint16_t name_len = (uint16_t)strlen(e->filename);
            uint32_t entry_size = WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 + name_len;

            if (off + entry_size > response_capacity) break;

            memcpy(response_out + off, e->manifest_hash, WH_HASH_SIZE); off += WH_HASH_SIZE;
            response_out[off] = (uint8_t)e->status; off += 1;
            WriteUint64LE(response_out + off, e->file_size); off += 8;
            WriteUint32LE(response_out + off, e->chunk_count); off += 4;
            WriteUint32LE(response_out + off, e->replicated_count); off += 4;
            WriteUint64LE(response_out + off, e->store_time); off += 8;
            WriteUint16LE(response_out + off, name_len); off += 2;
            memcpy(response_out + off, e->filename, name_len); off += name_len;
        }

        return off;
    }

    //--- FILE_GET: retrieve a full file by manifest hash ---
    case IPC_CMD_FILE_GET:
    {
        // Payload: [32B manifest_hash][2B path_len][output_path]
        if (payload_size < WH_HASH_SIZE + 2)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        const uint8_t *manifest_hash = payload;
        uint16_t path_len = ReadUint16LE(payload + WH_HASH_SIZE);
        if (path_len == 0 || (uint32_t)(WH_HASH_SIZE + 2 + path_len) > payload_size)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        char output_path[MAX_PATH];
        uint16_t cp_len = path_len < MAX_PATH - 1 ? path_len : MAX_PATH - 1;
        memcpy(output_path, payload + WH_HASH_SIZE + 2, cp_len);
        output_path[cp_len] = '\0';

        // Load manifest from file registry
        FILE_REG_ENTRY entry;
        FILE_MANIFEST *manifest = NULL;
        if (!FileRegistry_Load(manifest_hash, &entry, &manifest))
        {
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        LOG("[daemon] FILE_GET: retrieving %s (%u chunks) -> %s\n",
            entry.filename, manifest->chunk_count, output_path);

        // Open output file
        FILE *out_fh = fopen(output_path, "wb");
        if (!out_fh)
        {
            LOG_ERROR("[daemon] Cannot open output file: %s\n", output_path);
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!chunk_buf)
        {
            fclose(out_fh);
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Fix 4: Load EC metadata once before the per-chunk loop
        EC_GROUP *ec_group_get = NULL;
        FILE_MANIFEST *ec_manifest_get = NULL;
        if (Config_GetUint64(g_daemon.config, "ec_enabled", CONFIG_DEFAULT_EC_ENABLED))
        {
            char ec_dir_get[MAX_PATH];
            char ec_path_get[MAX_PATH];
#ifdef _WIN32
            const char *home_get = getenv("USERPROFILE");
            if (home_get)
            {
                snprintf(ec_dir_get, sizeof(ec_dir_get), "%s\\.wormhole\\ec", home_get);
                char hex_get[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex_get + hi * 2, "%02x", manifest_hash[hi]);
                hex_get[64] = '\0';
                snprintf(ec_path_get, sizeof(ec_path_get), "%s\\%s.ec", ec_dir_get, hex_get);
                ec_group_get = ErasureCoding_LoadMetadata(ec_path_get, &ec_manifest_get);
            }
#else
            const char *home_get = getenv("HOME");
            if (home_get)
            {
                snprintf(ec_dir_get, sizeof(ec_dir_get), "%s/.wormhole/ec", home_get);
                char hex_get[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex_get + hi * 2, "%02x", manifest_hash[hi]);
                hex_get[64] = '\0';
                snprintf(ec_path_get, sizeof(ec_path_get), "%s/%s.ec", ec_dir_get, hex_get);
                ec_group_get = ErasureCoding_LoadMetadata(ec_path_get, &ec_manifest_get);
            }
#endif
            if (ec_group_get)
                LOG("[daemon] FILE_GET: loaded EC metadata for recovery\n");
        }

        uint64_t bytes_written = 0;
        BOOLEAN success = TRUE;

        for (uint32_t i = 0; i < manifest->chunk_count; i++)
        {
            uint32_t chunk_size = 0;
            BOOLEAN got = FALSE;

            // Try local store first
            if (ChunkStore_Has(manifest->chunks[i].hash))
            {
                got = ChunkStore_Get(manifest->chunks[i].hash, chunk_buf, &chunk_size);
            }

            // Fix 4: Try DHT lookup first (primary peer source)
            if (!got && g_daemon.dht_enabled)
            {
                DHT_LOCATION locations[DHT_STORE_MAX_LOCATIONS];
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                uint32_t loc_count = DhtNode_FindChunkLocations(
                    &g_daemon.dht_node, manifest->chunks[i].hash,
                    locations, DHT_STORE_MAX_LOCATIONS);
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);

                if (loc_count == 0)
                {
                    for (int poll = 0; poll < 20 && loc_count == 0; poll++)
                    {
                        WH_SLEEP_MS(150);
                        WH_MUTEX_LOCK(g_daemon.dht_lock);
                        loc_count = DhtStore_Get(&g_daemon.dht_node.value_store,
                            manifest->chunks[i].hash, locations, DHT_STORE_MAX_LOCATIONS);
                        WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                    }
                }

                for (uint32_t li = 0; li < loc_count && !got; li++)
                {
                    char addr_str[INET6_ADDRSTRLEN];
                    QUIC_ADDRESS_FAMILY family;
                    if (!FormatDhtLocation(&locations[li], addr_str, sizeof(addr_str), &family))
                        continue;

                    got = Daemon_FetchChunkFromPeer(addr_str, locations[li].port,
                        family, manifest->chunks[i].hash, chunk_buf, &chunk_size);
                }
            }

            // Try relay-discovered peers (supplement)
            if (!got)
            {
                DISCOVERED_PEER local_peers_fg[MAX_FIND_PEERS];
                LONG fg_peer_count;
                WH_MUTEX_LOCK(g_daemon.peers_lock);
                fg_peer_count = WH_ATOMIC_LOAD(&g_daemon.discovered_peer_count);
                if (fg_peer_count > 0)
                    memcpy(local_peers_fg, g_daemon.discovered_peers,
                           fg_peer_count * sizeof(DISCOVERED_PEER));
                WH_MUTEX_UNLOCK(g_daemon.peers_lock);

                for (LONG pi = 0; pi < fg_peer_count && !got; pi++)
                {
                    // Select best endpoint: prefer IPv4, fall back to IPv6
                    const ENDPOINT *ep = NULL;
                    for (uint16_t j = 0; j < local_peers_fg[pi].endpoint_count; j++)
                    {
                        if (local_peers_fg[pi].endpoints[j].addr_type == 0x04)
                        { ep = &local_peers_fg[pi].endpoints[j]; break; }
                    }
                    if (!ep)
                    {
                        for (uint16_t j = 0; j < local_peers_fg[pi].endpoint_count; j++)
                        {
                            if (local_peers_fg[pi].endpoints[j].addr_type == 0x06)
                            { ep = &local_peers_fg[pi].endpoints[j]; break; }
                        }
                    }
                    if (!ep) continue;

                    char addr_str[INET6_ADDRSTRLEN];
                    uint16_t ep_port;
                    if (!Endpoint_ToString(ep, addr_str, sizeof(addr_str), &ep_port)) continue;

                    QUIC_ADDRESS_FAMILY family = (ep->addr_type == 0x06)
                        ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

                    got = Daemon_FetchChunkFromPeer(addr_str, ep_port,
                        family, manifest->chunks[i].hash, chunk_buf, &chunk_size);
                }
            }

            // Fix 4: Try EC reconstruction as last resort
            if (!got && ec_group_get && ec_manifest_get)
            {
                LOG("[daemon] FILE_GET: attempting EC recovery for chunk %u\n", i);
                if (Daemon_RecoverChunkWithRemoteFetch(manifest->chunks[i].hash,
                        ec_group_get, ec_manifest_get))
                {
                    got = ChunkStore_Get(manifest->chunks[i].hash, chunk_buf, &chunk_size);
                }
            }

            if (!got)
            {
                LOG_ERROR("[daemon] FILE_GET: failed to retrieve chunk %u\n", i);
                success = FALSE;
                break;
            }

            fwrite(chunk_buf, 1, chunk_size, out_fh);
            bytes_written += chunk_size;
        }

        free(chunk_buf);
        fclose(out_fh);

        // Fix 4: Clean up EC metadata
        if (ec_group_get) ErasureCoding_DestroyGroup(ec_group_get);
        if (ec_manifest_get) Manifest_Destroy(ec_manifest_get);

        if (!success)
        {
            remove(output_path);  // Clean up partial file
            Manifest_Destroy(manifest);
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Decrypt if the file was stored encrypted
        if (entry.encrypted)
        {
            char decrypt_temp[MAX_PATH];
#ifdef _WIN32
            snprintf(decrypt_temp, sizeof(decrypt_temp), "%s.dec_tmp", output_path);
#else
            snprintf(decrypt_temp, sizeof(decrypt_temp), "%s.dec_tmp", output_path);
#endif
            // Reassembled file is ciphertext — decrypt to temp, then replace
            if (FileCrypto_DecryptFile(output_path, decrypt_temp, entry.encryption_key))
            {
                remove(output_path);
                rename(decrypt_temp, output_path);
                LOG("[daemon] FILE_GET: decrypted successfully\n");
            }
            else
            {
                remove(decrypt_temp);
                LOG_ERROR("[daemon] FILE_GET: decryption failed\n");
                remove(output_path);
                Manifest_Destroy(manifest);
                response_out[0] = IPC_STATUS_ERROR;
                return 1;
            }
        }

        LOG("[daemon] FILE_GET: wrote %llu bytes to %s\n",
            (unsigned long long)bytes_written, output_path);

        Manifest_Destroy(manifest);

        // Response: [1B status][8B bytes_written]
        response_out[0] = IPC_STATUS_OK;
        WriteUint64LE(response_out + 1, bytes_written);
        return 9;
    }

    //--- FILE_DELETE: delete a stored file and all its chunks ---
    case IPC_CMD_FILE_DELETE:
    {
        if (payload_size < WH_HASH_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        const uint8_t *manifest_hash = payload;

        // Load file entry and manifest
        FILE_REG_ENTRY del_entry;
        FILE_MANIFEST *del_manifest = NULL;
        if (!FileRegistry_Load(manifest_hash, &del_entry, &del_manifest))
        {
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        LOG("[daemon] FILE_DELETE: deleting %s (%u chunks)\n",
            del_entry.filename, del_manifest->chunk_count);

        // Delete each data chunk from local store
        uint32_t chunks_deleted = 0;
        uint64_t bytes_freed = 0;
        for (uint32_t i = 0; i < del_manifest->chunk_count; i++)
        {
            if (ChunkStore_Has(del_manifest->chunks[i].hash))
            {
                bytes_freed += del_manifest->chunks[i].chunk_size;
                ChunkStore_Delete(del_manifest->chunks[i].hash);
                chunks_deleted++;
            }

            // Remove chunk from local DHT store
            if (g_daemon.dht_enabled)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                DhtStore_Remove(&g_daemon.dht_node.value_store, del_manifest->chunks[i].hash);
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
            }
        }

        // Delete EC parity chunks and metadata if present
        if (Config_GetUint64(g_daemon.config, "ec_enabled", CONFIG_DEFAULT_EC_ENABLED))
        {
            char ec_path[MAX_PATH];
#ifdef _WIN32
            const char *home_del = getenv("USERPROFILE");
            if (home_del)
            {
                char hex_del[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex_del + hi * 2, "%02x", manifest_hash[hi]);
                hex_del[64] = '\0';
                snprintf(ec_path, sizeof(ec_path), "%s\\.wormhole\\ec\\%s.ec", home_del, hex_del);

                // Load EC metadata to find parity chunks
                FILE_MANIFEST *ec_manifest_del = NULL;
                EC_GROUP *ec_group_del = ErasureCoding_LoadMetadata(ec_path, &ec_manifest_del);
                if (ec_group_del && ec_manifest_del)
                {
                    // Delete parity chunks (they are beyond manifest->chunk_count in EC manifest)
                    for (uint32_t i = del_manifest->chunk_count; i < ec_manifest_del->chunk_count; i++)
                    {
                        if (ChunkStore_Has(ec_manifest_del->chunks[i].hash))
                        {
                            bytes_freed += ec_manifest_del->chunks[i].chunk_size;
                            ChunkStore_Delete(ec_manifest_del->chunks[i].hash);
                            chunks_deleted++;
                        }
                        if (g_daemon.dht_enabled)
                        {
                            WH_MUTEX_LOCK(g_daemon.dht_lock);
                            DhtStore_Remove(&g_daemon.dht_node.value_store, ec_manifest_del->chunks[i].hash);
                            WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                        }
                    }
                    Manifest_Destroy(ec_manifest_del);
                    ErasureCoding_DestroyGroup(ec_group_del);
                }
                remove(ec_path);
            }
#else
            const char *home_del = getenv("HOME");
            if (home_del)
            {
                char hex_del[65];
                for (int hi = 0; hi < WH_HASH_SIZE; hi++)
                    sprintf(hex_del + hi * 2, "%02x", manifest_hash[hi]);
                hex_del[64] = '\0';
                snprintf(ec_path, sizeof(ec_path), "%s/.wormhole/ec/%s.ec", home_del, hex_del);

                FILE_MANIFEST *ec_manifest_del = NULL;
                EC_GROUP *ec_group_del = ErasureCoding_LoadMetadata(ec_path, &ec_manifest_del);
                if (ec_group_del && ec_manifest_del)
                {
                    for (uint32_t i = del_manifest->chunk_count; i < ec_manifest_del->chunk_count; i++)
                    {
                        if (ChunkStore_Has(ec_manifest_del->chunks[i].hash))
                        {
                            bytes_freed += ec_manifest_del->chunks[i].chunk_size;
                            ChunkStore_Delete(ec_manifest_del->chunks[i].hash);
                            chunks_deleted++;
                        }
                        if (g_daemon.dht_enabled)
                        {
                            WH_MUTEX_LOCK(g_daemon.dht_lock);
                            DhtStore_Remove(&g_daemon.dht_node.value_store, ec_manifest_del->chunks[i].hash);
                            WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                        }
                    }
                    Manifest_Destroy(ec_manifest_del);
                    ErasureCoding_DestroyGroup(ec_group_del);
                }
                remove(ec_path);
            }
#endif
        }

        Manifest_Destroy(del_manifest);

        // Delete file registry entry
        FileRegistry_Delete(manifest_hash);

        // Update daemon stats
        WH_ATOMIC_ADD(&g_daemon.chunk_count, -(LONG)chunks_deleted);
        if (bytes_freed > 0)
            WH_ATOMIC_ADD64(&g_daemon.storage_used, -(LONGLONG)bytes_freed);

        LOG("[daemon] FILE_DELETE: removed %u chunks, freed %llu bytes\n",
            chunks_deleted, (unsigned long long)bytes_freed);

        response_out[0] = IPC_STATUS_OK;
        return 1;
    }

    //--- EXPORT_KEY: return encryption key for a file ---
    case IPC_CMD_EXPORT_KEY:
    {
        if (payload_size < WH_HASH_SIZE)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        FILE_REG_ENTRY ek_entry;
        if (!FileRegistry_Load(payload, &ek_entry, NULL))
        {
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        if (!ek_entry.encrypted)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        // Response: [1B status][32B encryption_key]
        response_out[0] = IPC_STATUS_OK;
        memcpy(response_out + 1, ek_entry.encryption_key, 32);
        return 33;
    }

    //--- IMPORT_KEY: set encryption key for a file ---
    case IPC_CMD_IMPORT_KEY:
    {
        if (payload_size < WH_HASH_SIZE + 32)
        {
            response_out[0] = IPC_STATUS_ERROR;
            return 1;
        }

        const uint8_t *ik_hash = payload;
        const uint8_t *ik_key = payload + WH_HASH_SIZE;

        // Verify file exists
        FILE_REG_ENTRY ik_entry;
        FILE_MANIFEST *ik_manifest = NULL;
        if (!FileRegistry_Load(ik_hash, &ik_entry, &ik_manifest))
        {
            response_out[0] = IPC_STATUS_NOT_FOUND;
            return 1;
        }

        // Re-save with encryption key
        BOOLEAN ik_ok = FileRegistry_SaveEncrypted(ik_manifest, ik_entry.filename,
                                                     ik_entry.status, ik_key);
        Manifest_Destroy(ik_manifest);

        response_out[0] = ik_ok ? IPC_STATUS_OK : IPC_STATUS_ERROR;
        return 1;
    }

    //--- PEER_LIST: enumerate known DHT peers ---
    case IPC_CMD_PEER_LIST:
    {
        if (!g_daemon.dht_enabled)
        {
            response_out[0] = IPC_STATUS_OK;
            WriteUint32LE(response_out + 1, 0);
            return 5;
        }

        // Query routing table for all known nodes
        ROUTING_NODE peers[256];
        WH_MUTEX_LOCK(g_daemon.dht_lock);
        uint32_t peer_count = RoutingTable_FindClosest(
            &g_daemon.dht_node.routing_table,
            g_daemon.dht_node.routing_table.self_id,
            peers, 256);
        WH_MUTEX_UNLOCK(g_daemon.dht_lock);

        // Response: [1B status][4B count][per peer: 32B node_id + 1B addr_type + 16B addr + 2B port + 8B last_seen]
        response_out[0] = IPC_STATUS_OK;
        WriteUint32LE(response_out + 1, peer_count);
        uint32_t pl_off = 5;
        uint32_t per_peer_size = 32 + 1 + 16 + 2 + 8;  // 59 bytes

        for (uint32_t i = 0; i < peer_count; i++)
        {
            if (pl_off + per_peer_size > response_capacity) break;

            memcpy(response_out + pl_off, peers[i].node_id, 32); pl_off += 32;
            response_out[pl_off] = peers[i].addr_type; pl_off += 1;
            memcpy(response_out + pl_off, peers[i].addr, 16); pl_off += 16;
            WriteUint16LE(response_out + pl_off, peers[i].port); pl_off += 2;
            WriteUint64LE(response_out + pl_off, (uint64_t)peers[i].last_seen); pl_off += 8;
        }

        return pl_off;
    }

    //--- SHUTDOWN: clean daemon shutdown ---
    case IPC_CMD_SHUTDOWN:
    {
        LOG("[daemon] Shutdown requested via IPC\n");
        WH_ATOMIC_SET(&g_daemon.shutdown_requested, 1);
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
    LOG("================================================\n");
}

static void Daemon_PrintUsage(void)
{
    printf("Usage: wormholed [options]\n");
    printf("Options:\n");
    printf("  --port <port>           QUIC listener port (default: %u)\n", DAEMON_DEFAULT_PORT);
    printf("  --data-dir <path>       Data directory (default: ~/.wormhole)\n");
    printf("  --dht-port <port>       DHT UDP port (default: %u)\n", CONFIG_DEFAULT_DHT_PORT);
    printf("  --bootstrap <host:port> DHT bootstrap peer (default: relay server)\n");
    printf("  --no-relay              Disable relay connection\n");
    printf("  --help                  Show this help\n");
}

int main(int argc, char *argv[])
{
    // First pass: handle --data-dir before anything else (affects all path construction)
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc)
        {
            const char *dir = argv[i + 1];
#ifdef _WIN32
            _putenv_s("USERPROFILE", dir);
#else
            setenv("HOME", dir, 1);
#endif
            LOG("[daemon] Data directory: %s\n", dir);
            break;
        }
    }

    // Verify home directory is set (may have been set by --data-dir above)
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

    // Parse command-line arguments
    g_daemon.listen_port = DAEMON_DEFAULT_PORT;
    g_daemon.relay_enabled = TRUE;
    uint16_t cli_dht_port = 0;
    const char *cli_bootstrap = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            g_daemon.listen_port = (uint16_t)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc)
        {
            i++;  // Already handled in first pass
        }
        else if (strcmp(argv[i], "--dht-port") == 0 && i + 1 < argc)
        {
            cli_dht_port = (uint16_t)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--bootstrap") == 0 && i + 1 < argc)
        {
            cli_bootstrap = argv[++i];
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
#else
    // Register SIGINT/SIGTERM handlers
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = DaemonSignalHandler;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
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

    // Initialize synchronization primitives
    WH_MUTEX_INIT(g_daemon.ledger_lock);
    WH_MUTEX_INIT(g_daemon.dht_lock);
    WH_MUTEX_INIT(g_daemon.peers_lock);
    WH_MUTEX_INIT(g_daemon.retry_lock);

    // Step 0b: Initialize libsodium + load/generate identity keypair
    if (sodium_init() < 0)
    {
        LOG_ERROR("[daemon] Failed to initialize libsodium\n");
        return 1;
    }

    // Use USERPROFILE/HOME (redirected by --data-dir) so each daemon gets its own identity
    {
#ifdef _WIN32
        const char *home = getenv("USERPROFILE");
#else
        const char *home = getenv("HOME");
#endif
        if (!home)
        {
            LOG_ERROR("[daemon] Failed to get home directory for identity\n");
            return 1;
        }

        char identity_path[512];
        char wormhole_dir[512];
#ifdef _WIN32
        snprintf(wormhole_dir, sizeof(wormhole_dir), "%s\\.wormhole", home);
        CreateDirectoryA(wormhole_dir, NULL);
        snprintf(identity_path, sizeof(identity_path), "%s\\.wormhole\\identity", home);
#else
        snprintf(wormhole_dir, sizeof(wormhole_dir), "%s/.wormhole", home);
        mkdir(wormhole_dir, 0700);
        chmod(wormhole_dir, 0700);  // Fix perms if dir already existed
        snprintf(identity_path, sizeof(identity_path), "%s/.wormhole/identity", home);
#endif

        if (!PeerID_LoadOrGenerate(&g_daemon.keypair, identity_path))
        {
            LOG_ERROR("[daemon] Failed to load/generate keypair\n");
            return 1;
        }

        char peer_hex[65];
        PeerID_ToHex(g_daemon.keypair.public_key, peer_hex);
        LOG("[daemon] PeerID: %s\n", peer_hex);
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

    // Step 5: Start IPC server (pipe name derived from QUIC port)
    char ipc_pipe_name[256];
    snprintf(ipc_pipe_name, sizeof(ipc_pipe_name), "%s%u",
             IPC_PIPE_PREFIX, g_daemon.listen_port);
    if (!IpcServer_Start(Daemon_HandleIpcCommand, &g_daemon, ipc_pipe_name))
    {
        LOG_ERROR("[daemon] Failed to start IPC server\n");
        goto cleanup;
    }

    // Step 5b: Initialize file registry
    if (!FileRegistry_Init())
    {
        LOG("[daemon] Warning: Failed to initialize file registry\n");
    }

    // Step 5c: Start background work queue
    if (!WorkQueue_Init())
    {
        LOG_ERROR("[daemon] Failed to start work queue\n");
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

    // Step 7: Initialize DHT
    g_daemon.dht_enabled = (BOOLEAN)Config_GetUint64(g_daemon.config, "dht_enabled",
                                                       CONFIG_DEFAULT_DHT_ENABLED);
    g_daemon.dht_port = cli_dht_port ? cli_dht_port
        : (uint16_t)Config_GetUint64(g_daemon.config, "dht_port",
                                       CONFIG_DEFAULT_DHT_PORT);

    if (g_daemon.dht_enabled)
    {
        const char *bootstrap_host;
        uint16_t bootstrap_port;

        if (cli_bootstrap)
        {
            // CLI flag takes precedence — may be comma-separated list
            bootstrap_host = cli_bootstrap;
            bootstrap_port = g_daemon.dht_port;
            LOG("[daemon] DHT bootstrap peer(s): %s\n", bootstrap_host);
        }
        else
        {
            // Check config for multi-bootstrap list
            const char *cfg_bootstrap = Config_GetString(g_daemon.config,
                "dht_bootstrap_nodes", CONFIG_DEFAULT_DHT_BOOTSTRAP);
            if (cfg_bootstrap && cfg_bootstrap[0] != '\0')
            {
                bootstrap_host = cfg_bootstrap;
                bootstrap_port = g_daemon.dht_port;
                LOG("[daemon] DHT bootstrap peer(s) from config: %s\n", bootstrap_host);
            }
            else
            {
                // Fall back to relay server as bootstrap
                bootstrap_host = Config_GetString(g_daemon.config, "relay_host",
                                                    CONFIG_DEFAULT_RELAY_HOST);
                bootstrap_port = (uint16_t)Config_GetUint64(g_daemon.config, "relay_port",
                                                             CONFIG_DEFAULT_RELAY_PORT);
            }
        }

        if (DhtNode_Init(&g_daemon.dht_node, &g_daemon.keypair,
                          g_daemon.dht_port, bootstrap_host, bootstrap_port))
        {
            LOG("[daemon] DHT node initialized on port %u\n", g_daemon.dht_port);

            // Load persisted DHT value store (chunk locations)
            {
                char dht_store_path[MAX_PATH];
#ifdef _WIN32
                const char *home_ds = getenv("USERPROFILE");
                if (home_ds) snprintf(dht_store_path, sizeof(dht_store_path),
                    "%s\\.wormhole\\dht_store.bin", home_ds);
#else
                const char *home_ds = getenv("HOME");
                if (home_ds) snprintf(dht_store_path, sizeof(dht_store_path),
                    "%s/.wormhole/dht_store.bin", home_ds);
#endif
                if (home_ds && DhtStore_Load(&g_daemon.dht_node.value_store, dht_store_path))
                    LOG("[daemon] Loaded DHT value store (%u entries)\n",
                        DhtStore_GetCount(&g_daemon.dht_node.value_store));
            }

            DhtNode_Bootstrap(&g_daemon.dht_node);
        }
        else
        {
            LOG("[daemon] DHT init failed (continuing without DHT)\n");
            g_daemon.dht_enabled = FALSE;
        }
    }

    // Step 8: Initialize incentives ledger
    Ledger_Init(&g_daemon.ledger);
    {
        char ledger_path[MAX_PATH];
        BOOLEAN loaded = FALSE;
#ifdef _WIN32
        const char *home_ldgr = getenv("USERPROFILE");
        if (home_ldgr)
        {
            snprintf(ledger_path, sizeof(ledger_path), "%s\\.wormhole\\storage_ledger.bin", home_ldgr);
            loaded = Ledger_Load(&g_daemon.ledger, ledger_path);
        }
#else
        const char *home_ldgr = getenv("HOME");
        if (home_ldgr)
        {
            snprintf(ledger_path, sizeof(ledger_path), "%s/.wormhole/storage_ledger.bin", home_ldgr);
            loaded = Ledger_Load(&g_daemon.ledger, ledger_path);
        }
#endif
        if (loaded)
            LOG("[daemon] Loaded ledger (%u peers)\n", g_daemon.ledger.peer_count);
        else
            LOG("[daemon] Starting with fresh ledger\n");
    }

    LOG("[daemon] Daemon is running. Press Ctrl+C to stop.\n");

    // Main loop: poll relay + DHT + check shutdown flag
    time_t last_keepalive = time(NULL);
    time_t last_discovery = time(NULL) - DAEMON_DISCOVERY_SEC + 5;  // First FIND_PEERS after ~5s
    time_t last_dht_refresh = time(NULL);
    time_t last_health_check = time(NULL);
    time_t last_dht_expire = time(NULL);
    time_t last_dht_bootstrap_retry = time(NULL);
    time_t last_replication_check = time(NULL);
    time_t last_dht_store_save = time(NULL);
    uint32_t dht_bootstrap_retries = 0;
    uint32_t dht_bootstrap_backoff_sec = 5;
#define DHT_BOOTSTRAP_RETRY_SEC   10
#define DHT_STORE_SAVE_SEC        300  // Save DHT value store every 5 minutes
#define REPLICATION_CHECK_SEC     60

    while (WH_ATOMIC_LOAD(&g_daemon.shutdown_requested) == 0)
    {
        // Poll relay for incoming messages (non-blocking)
        // Poll must run even before connected — it receives the REGISTERED response
        if (g_daemon.relay_client)
        {
            RelayClient_Poll(g_daemon.relay_client, 100);  // 100ms timeout

            if (RelayClient_IsConnected(g_daemon.relay_client))
            {
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
        }
        else
        {
            // No relay client at all — just sleep to avoid busy-loop
            WH_SLEEP_MS(100);
        }

        // Poll DHT for incoming messages (non-blocking)
        if (g_daemon.dht_enabled)
        {
            WH_MUTEX_LOCK(g_daemon.dht_lock);
            DhtNode_Poll(&g_daemon.dht_node, 0);
            WH_MUTEX_UNLOCK(g_daemon.dht_lock);

            time_t now = time(NULL);

            // Refresh DHT buckets periodically
            if (now - last_dht_refresh >= DHT_BUCKET_REFRESH_SEC)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                DhtNode_RefreshBuckets(&g_daemon.dht_node);
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                last_dht_refresh = now;
            }

            // Retry deferred chunk announcements once routing table has nodes
            if (g_daemon.pending_announce_count > 0)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                ROUTING_NODE rt_check[1];
                uint32_t rt_has = RoutingTable_FindClosest(
                    &g_daemon.dht_node.routing_table,
                    g_daemon.pending_announce_hashes[0], rt_check, 1);
                if (rt_has > 0)
                {
                    uint32_t count = g_daemon.pending_announce_count;
                    for (uint32_t pa = 0; pa < count; pa++)
                    {
                        DhtNode_AnnounceChunk(&g_daemon.dht_node,
                            g_daemon.pending_announce_hashes[pa], g_daemon.listen_port);
                    }
                    LOG("[daemon] Retried %u deferred DHT announcements\n", count);
                    g_daemon.pending_announce_count = 0;
                }
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
            }

            // Expire pending RPCs and stale value store entries
            if (now - last_dht_expire >= 5)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                DhtNode_ExpirePendingRPCs(&g_daemon.dht_node);
                DhtStore_ExpireOld(&g_daemon.dht_node.value_store);
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                last_dht_expire = now;
            }

            // Periodically save DHT value store to disk
            if (now - last_dht_store_save >= DHT_STORE_SAVE_SEC)
            {
                char dht_sp[MAX_PATH];
#ifdef _WIN32
                const char *home_sp = getenv("USERPROFILE");
                if (home_sp) snprintf(dht_sp, sizeof(dht_sp),
                    "%s\\.wormhole\\dht_store.bin", home_sp);
#else
                const char *home_sp = getenv("HOME");
                if (home_sp) snprintf(dht_sp, sizeof(dht_sp),
                    "%s/.wormhole/dht_store.bin", home_sp);
#endif
                if (home_sp)
                {
                    WH_MUTEX_LOCK(g_daemon.dht_lock);
                    DhtStore_Save(&g_daemon.dht_node.value_store, dht_sp);
                    WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                }
                last_dht_store_save = now;
            }

            // Retry DHT bootstrap with exponential backoff (never give up)
            if (now - last_dht_bootstrap_retry >= (time_t)dht_bootstrap_backoff_sec)
            {
                WH_MUTEX_LOCK(g_daemon.dht_lock);
                ROUTING_NODE rt_check[1];
                uint32_t rt_has = RoutingTable_FindClosest(
                    &g_daemon.dht_node.routing_table,
                    g_daemon.dht_node.keypair->public_key, rt_check, 1);
                if (rt_has == 0)
                {
                    LOG("[daemon] DHT routing table empty, retrying bootstrap (backoff %us)\n",
                        dht_bootstrap_backoff_sec);
                    if (g_daemon.dht_node.bootstrap_count == 0) {
                        DhtNode_ReResolveBootstrap(&g_daemon.dht_node);
                    }
                    DhtNode_Bootstrap(&g_daemon.dht_node);
                    dht_bootstrap_retries++;
                    // Exponential backoff: 5s → 10s → 20s → ... → 300s cap
                    if (dht_bootstrap_backoff_sec < 300)
                        dht_bootstrap_backoff_sec *= 2;
                    if (dht_bootstrap_backoff_sec > 300)
                        dht_bootstrap_backoff_sec = 300;
                }
                else
                {
                    // Reset backoff on success
                    dht_bootstrap_backoff_sec = 5;
                }
                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                last_dht_bootstrap_retry = now;
            }
        }

        // Periodic replication check for REPLICATING files
        {
            time_t now = time(NULL);
            if (now - last_replication_check >= REPLICATION_CHECK_SEC)
            {
                FILE_REG_ENTRY entries[64];
                uint32_t count = FileRegistry_List(entries, 64);
                for (uint32_t fi = 0; fi < count; fi++)
                {
                    if (entries[fi].status == FILE_STATUS_REPLICATING)
                    {
                        WORK_ITEM *check = (WORK_ITEM *)calloc(1, sizeof(WORK_ITEM));
                        if (check)
                        {
                            check->type = WORK_CHECK_REPLICATION;
                            memcpy(check->check_repl.manifest_hash,
                                   entries[fi].manifest_hash, WH_HASH_SIZE);
                            WorkQueue_Push(check);
                        }
                    }
                }
                last_replication_check = now;
            }
        }

        // Health check periodically (runs independently of DHT)
        {
            time_t now = time(NULL);
            uint64_t health_interval = Config_GetUint64(g_daemon.config,
                "health_check_interval_sec", CONFIG_DEFAULT_HEALTH_CHECK_SEC);
            if (now - last_health_check >= (time_t)health_interval)
            {
                // Fix 2: Scale health check count with store size
                LONG total_chunks = WH_ATOMIC_LOAD(&g_daemon.chunk_count);
                uint32_t check_count = (uint32_t)(total_chunks / 5);
                if (check_count < 100) check_count = 100;
                if (check_count > 1000) check_count = 1000;

                HEALTH_STATS stats = Health_CheckChunks(check_count);
                if (stats.chunks_checked > 0)
                {
                    LOG("[daemon] Health check: %u checked, %u healthy, %u degraded, %u critical\n",
                        stats.chunks_checked, stats.chunks_healthy,
                        stats.chunks_degraded, stats.chunks_critical);
                    fflush(stdout);
                }

                // Replication pass: replicate degraded chunks to peers
                if (stats.chunks_degraded + stats.chunks_critical > 0)
                {
                    // Fix 2: Scale degraded fetch count
                    uint32_t deg_max = stats.chunks_degraded + stats.chunks_critical;
                    if (deg_max > 50) deg_max = 50;
                    if (deg_max < 10) deg_max = 10;

                    uint8_t (*degraded_hashes)[WH_HASH_SIZE] =
                        (uint8_t (*)[WH_HASH_SIZE])malloc(deg_max * WH_HASH_SIZE);
                    if (!degraded_hashes) goto skip_replication;

                    uint32_t deg_count = Health_GetDegradedChunks(degraded_hashes, deg_max);
                    uint32_t replicated = 0;

                    for (uint32_t di = 0; di < deg_count; di++)
                    {
                        uint8_t *chunk_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                        if (!chunk_data) break;

                        uint32_t chunk_size = 0;
                        if (ChunkStore_Get(degraded_hashes[di], chunk_data, &chunk_size))
                        {
                            Daemon_ReplicateChunk(degraded_hashes[di], chunk_data, chunk_size);
                            replicated++;
                        }
                        free(chunk_data);
                    }

                    free(degraded_hashes);

                    if (replicated > 0)
                        LOG("[daemon] Replication pass: attempted %u chunks\n", replicated);
                }
                skip_replication:

                // EC recovery pass: reconstruct missing chunks from parity
                if (Config_GetUint64(g_daemon.config, "ec_enabled", CONFIG_DEFAULT_EC_ENABLED))
                {
                    char ec_dir[MAX_PATH];
                    BOOLEAN ec_dir_ok = FALSE;
#ifdef _WIN32
                    const char *home_hc = getenv("USERPROFILE");
                    if (home_hc)
                    {
                        snprintf(ec_dir, sizeof(ec_dir), "%s\\.wormhole\\ec", home_hc);
                        ec_dir_ok = TRUE;
                    }
#else
                    const char *home_hc = getenv("HOME");
                    if (home_hc)
                    {
                        snprintf(ec_dir, sizeof(ec_dir), "%s/.wormhole/ec", home_hc);
                        ec_dir_ok = TRUE;
                    }
#endif
                    if (ec_dir_ok)
                    {
                        uint32_t ec_recovered = 0;
                        uint32_t ec_attempts = 0;
#ifdef _WIN32
                        char ec_search[MAX_PATH];
                        snprintf(ec_search, sizeof(ec_search), "%s\\*.ec", ec_dir);

                        WIN32_FIND_DATAA ec_find;
                        HANDLE hEcFind = FindFirstFileA(ec_search, &ec_find);
                        if (hEcFind != INVALID_HANDLE_VALUE)
                        {
                            do {
                                if (ec_find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                                if (ec_attempts >= 20) break;

                                char ec_file[MAX_PATH];
                                snprintf(ec_file, sizeof(ec_file), "%s\\%s", ec_dir, ec_find.cFileName);

                                FILE_MANIFEST *ec_manifest = NULL;
                                EC_GROUP *ec_group = ErasureCoding_LoadMetadata(ec_file, &ec_manifest);
                                if (!ec_group) continue;

                                // Check each data chunk in manifest — recover if missing
                                for (uint32_t ci = 0; ci < ec_manifest->chunk_count && ec_attempts < 20; ci++)
                                {
                                    if (!ChunkStore_Has(ec_manifest->chunks[ci].hash))
                                    {
                                        ec_attempts++;
                                        if (Daemon_RecoverChunkWithRemoteFetch(ec_manifest->chunks[ci].hash, ec_group, ec_manifest))
                                        {
                                            ec_recovered++;
                                            LOG("[daemon] EC recovered chunk %u from %s\n", ci, ec_find.cFileName);

                                            // Announce recovered chunk to DHT
                                            if (g_daemon.dht_enabled)
                                            {
                                                WH_MUTEX_LOCK(g_daemon.dht_lock);
                                                DhtNode_AnnounceChunk(&g_daemon.dht_node,
                                                    ec_manifest->chunks[ci].hash, g_daemon.listen_port);
                                                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                                            }
                                        }
                                    }
                                }

                                // Check parity chunks — regenerate if missing
                                for (uint32_t s = 0; s < ec_group->stripe_count && ec_attempts < 20; s++)
                                {
                                    const EC_STRIPE *stripe = &ec_group->stripes[s];
                                    BOOLEAN parity_missing = FALSE;
                                    for (uint8_t p = 0; p < ec_group->m; p++)
                                    {
                                        if (!ChunkStore_Has(stripe->parity_hashes[p]))
                                        {
                                            parity_missing = TRUE;
                                            break;
                                        }
                                    }
                                    if (!parity_missing) continue;

                                    // Verify all data chunks present before regenerating
                                    BOOLEAN all_data = TRUE;
                                    for (uint32_t i = 0; i < stripe->data_chunk_count; i++)
                                    {
                                        uint32_t ci = stripe->data_chunk_start + i;
                                        if (!ChunkStore_Has(ec_manifest->chunks[ci].hash))
                                        {
                                            all_data = FALSE;
                                            break;
                                        }
                                    }
                                    if (all_data)
                                    {
                                        ec_attempts++;
                                        if (ErasureCoding_RegenerateStripeParity(s, ec_group, ec_manifest))
                                        {
                                            ec_recovered++;
                                            LOG("[daemon] EC regenerated parity for stripe %u of %s\n", s, ec_find.cFileName);
                                        }
                                    }
                                }

                                Manifest_Destroy(ec_manifest);
                                ErasureCoding_DestroyGroup(ec_group);
                            } while (FindNextFileA(hEcFind, &ec_find));

                            FindClose(hEcFind);
                        }
#else
                        DIR *ec_dp = opendir(ec_dir);
                        if (ec_dp)
                        {
                            struct dirent *ec_ent;
                            while ((ec_ent = readdir(ec_dp)) != NULL)
                            {
                                if (ec_ent->d_name[0] == '.') continue;
                                if (ec_attempts >= 20) break;

                                // Check .ec extension
                                size_t nlen = strlen(ec_ent->d_name);
                                if (nlen < 4 || strcmp(ec_ent->d_name + nlen - 3, ".ec") != 0) continue;

                                char ec_file[MAX_PATH];
                                snprintf(ec_file, sizeof(ec_file), "%s/%s", ec_dir, ec_ent->d_name);

                                FILE_MANIFEST *ec_manifest = NULL;
                                EC_GROUP *ec_group = ErasureCoding_LoadMetadata(ec_file, &ec_manifest);
                                if (!ec_group) continue;

                                for (uint32_t ci = 0; ci < ec_manifest->chunk_count && ec_attempts < 20; ci++)
                                {
                                    if (!ChunkStore_Has(ec_manifest->chunks[ci].hash))
                                    {
                                        ec_attempts++;
                                        if (Daemon_RecoverChunkWithRemoteFetch(ec_manifest->chunks[ci].hash, ec_group, ec_manifest))
                                        {
                                            ec_recovered++;
                                            LOG("[daemon] EC recovered chunk %u from %s\n", ci, ec_ent->d_name);

                                            if (g_daemon.dht_enabled)
                                            {
                                                WH_MUTEX_LOCK(g_daemon.dht_lock);
                                                DhtNode_AnnounceChunk(&g_daemon.dht_node,
                                                    ec_manifest->chunks[ci].hash, g_daemon.listen_port);
                                                WH_MUTEX_UNLOCK(g_daemon.dht_lock);
                                            }
                                        }
                                    }
                                }

                                // Check parity chunks — regenerate if missing
                                for (uint32_t s = 0; s < ec_group->stripe_count && ec_attempts < 20; s++)
                                {
                                    const EC_STRIPE *stripe = &ec_group->stripes[s];
                                    BOOLEAN parity_missing = FALSE;
                                    for (uint8_t p = 0; p < ec_group->m; p++)
                                    {
                                        if (!ChunkStore_Has(stripe->parity_hashes[p]))
                                        {
                                            parity_missing = TRUE;
                                            break;
                                        }
                                    }
                                    if (!parity_missing) continue;

                                    // Verify all data chunks present before regenerating
                                    BOOLEAN all_data = TRUE;
                                    for (uint32_t i = 0; i < stripe->data_chunk_count; i++)
                                    {
                                        uint32_t ci = stripe->data_chunk_start + i;
                                        if (!ChunkStore_Has(ec_manifest->chunks[ci].hash))
                                        {
                                            all_data = FALSE;
                                            break;
                                        }
                                    }
                                    if (all_data)
                                    {
                                        ec_attempts++;
                                        if (ErasureCoding_RegenerateStripeParity(s, ec_group, ec_manifest))
                                        {
                                            ec_recovered++;
                                            LOG("[daemon] EC regenerated parity for stripe %u of %s\n", s, ec_ent->d_name);
                                        }
                                    }
                                }

                                Manifest_Destroy(ec_manifest);
                                ErasureCoding_DestroyGroup(ec_group);
                            }

                            closedir(ec_dp);
                        }
#endif
                        if (ec_recovered > 0)
                        {
                            LOG("[daemon] EC recovery pass: %u/%u chunks recovered\n",
                                ec_recovered, ec_attempts);
                            fflush(stdout);
                        }
                    }
                }

                // Proof-of-storage challenge initiation (20 chunks, 3 peers each)
                {
                    uint8_t repl_hashes[20][WH_HASH_SIZE];
                    uint32_t repl_count = Health_GetReplicatedChunks(repl_hashes, 20);
                    uint32_t proofs_sent = 0, proofs_ok = 0, proofs_fail = 0;

                    for (uint32_t ri = 0; ri < repl_count; ri++)
                    {
                        uint8_t peer_ids[MAX_REPLICAS][32];
                        uint32_t peer_count_r = ChunkStore_GetReplicaLocations(
                            repl_hashes[ri], peer_ids, MAX_REPLICAS);

                        // Load chunk data for proof computation
                        uint8_t *proof_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                        if (!proof_data) continue;
                        uint32_t proof_size = 0;
                        if (!ChunkStore_Get(repl_hashes[ri], proof_data, &proof_size))
                        {
                            free(proof_data);
                            continue;
                        }

                        for (uint32_t pi = 0; pi < peer_count_r && pi < 3; pi++)
                        {
                            char addr_buf[INET6_ADDRSTRLEN];
                            uint16_t port_buf;
                            QUIC_ADDRESS_FAMILY proof_family;
                            if (!Daemon_ResolvePeerAddress(peer_ids[pi], addr_buf, sizeof(addr_buf), &port_buf, &proof_family))
                                continue;

                            proofs_sent++;
                            if (Daemon_SendProofChallenge(addr_buf, port_buf,
                                    proof_family, repl_hashes[ri], proof_data, proof_size))
                            {
                                proofs_ok++;
                            }
                            else
                            {
                                proofs_fail++;
                                // Remove unverified replica location
                                ChunkStore_RemoveReplicaLocation(repl_hashes[ri], peer_ids[pi]);
                                LOG("[daemon] Removed unverified replica for peer\n");
                            }
                        }
                        free(proof_data);
                    }

                    if (proofs_sent > 0)
                    {
                        LOG("[daemon] Proof challenges: %u sent, %u ok, %u failed\n",
                            proofs_sent, proofs_ok, proofs_fail);
                    }
                }

                last_health_check = now;
            }
        }

        // Fix 5: Process retry queue
        {
            time_t now = time(NULL);
            WH_MUTEX_LOCK(g_daemon.retry_lock);
            for (uint32_t ri = 0; ri < g_daemon.retry_count; )
            {
                if (now >= g_daemon.retry_queue[ri].retry_after)
                {
                    uint8_t hash[WH_HASH_SIZE];
                    memcpy(hash, g_daemon.retry_queue[ri].hash, WH_HASH_SIZE);
                    uint32_t attempt = g_daemon.retry_queue[ri].attempt_count;

                    // Remove from queue (shift down)
                    for (uint32_t j = ri; j < g_daemon.retry_count - 1; j++)
                        g_daemon.retry_queue[j] = g_daemon.retry_queue[j + 1];
                    g_daemon.retry_count--;

                    WH_MUTEX_UNLOCK(g_daemon.retry_lock);

                    // Check if still needs replication
                    uint32_t replicas = ChunkStore_GetReplicaCount(hash);
                    if (replicas + 1 < REPLICATION_TARGET)
                    {
                        uint8_t *chunk_data = (uint8_t *)malloc(WH_CHUNK_SIZE);
                        if (chunk_data)
                        {
                            uint32_t chunk_size = 0;
                            if (ChunkStore_Get(hash, chunk_data, &chunk_size))
                            {
                                LOG("[retry] Retrying replication (attempt %u)\n", attempt + 1);
                                Daemon_ReplicateChunk(hash, chunk_data, chunk_size);
                            }
                            free(chunk_data);
                        }

                        // Re-queue with exponential backoff if still needed
                        replicas = ChunkStore_GetReplicaCount(hash);
                        if (replicas + 1 < REPLICATION_TARGET && attempt < 4)
                        {
                            WH_MUTEX_LOCK(g_daemon.retry_lock);
                            if (g_daemon.retry_count < 32)
                            {
                                uint32_t idx = g_daemon.retry_count;
                                memcpy(g_daemon.retry_queue[idx].hash, hash, WH_HASH_SIZE);
                                // Exponential backoff: 2min, 4min, 8min cap
                                uint32_t delay = 120 << attempt;
                                if (delay > 480) delay = 480;
                                g_daemon.retry_queue[idx].retry_after = time(NULL) + delay;
                                g_daemon.retry_queue[idx].attempt_count = attempt + 1;
                                g_daemon.retry_count++;
                            }
                            WH_MUTEX_UNLOCK(g_daemon.retry_lock);
                        }
                    }

                    WH_MUTEX_LOCK(g_daemon.retry_lock);
                    // Don't increment ri — the array shifted
                }
                else
                {
                    ri++;
                }
            }
            WH_MUTEX_UNLOCK(g_daemon.retry_lock);
        }
    }

    LOG("[daemon] Shutting down...\n");

cleanup:
    // Stop background work queue
    WorkQueue_Shutdown();

    // Stop IPC server
    IpcServer_Stop();

    // Shutdown DHT (save value store before shutdown destroys it)
    if (g_daemon.dht_enabled)
    {
        char dht_store_path[MAX_PATH];
#ifdef _WIN32
        const char *home_dht = getenv("USERPROFILE");
        if (home_dht) snprintf(dht_store_path, sizeof(dht_store_path),
            "%s\\.wormhole\\dht_store.bin", home_dht);
#else
        const char *home_dht = getenv("HOME");
        if (home_dht) snprintf(dht_store_path, sizeof(dht_store_path),
            "%s/.wormhole/dht_store.bin", home_dht);
#endif
        if (home_dht)
        {
            DhtStore_Save(&g_daemon.dht_node.value_store, dht_store_path);
            LOG("[daemon] DHT value store saved\n");
        }

        DhtNode_Shutdown(&g_daemon.dht_node);
        LOG("[daemon] DHT node shut down\n");
    }

    // Save incentives ledger
    {
        char ledger_path[MAX_PATH];
#ifdef _WIN32
        const char *home = getenv("USERPROFILE");
        if (home) snprintf(ledger_path, sizeof(ledger_path), "%s\\.wormhole\\storage_ledger.bin", home);
#else
        const char *home = getenv("HOME");
        if (home) snprintf(ledger_path, sizeof(ledger_path), "%s/.wormhole/storage_ledger.bin", home);
#endif
        if (home) Ledger_Save(&g_daemon.ledger, ledger_path);
    }

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

    // Cleanup synchronization primitives
    WH_MUTEX_DESTROY(g_daemon.ledger_lock);
    WH_MUTEX_DESTROY(g_daemon.dht_lock);
    WH_MUTEX_DESTROY(g_daemon.peers_lock);
    WH_MUTEX_DESTROY(g_daemon.retry_lock);

    LOG("[daemon] Shutdown complete\n");
    return 0;
}
