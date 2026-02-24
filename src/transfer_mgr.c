//
// transfer_mgr.c
// Wormhole - Daemon-side transfer manager for send/receive operations.
// by Dylan Kress
//

#include "transfer_mgr.h"
#include "stream.h"
#include "chunker.h"
#include "file_io.h"
#include "wire_format.h"
#include "crypto.h"
#include "config.h"
#include "../deps/blake3/blake3.h"
#include <sodium.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

//=============================================================================
// External references (defined in wormholed.c)
//=============================================================================

extern const QUIC_API_TABLE *MsQuic;
extern HQUIC DaemonServerConfig;
extern HQUIC DaemonClientConfig;
extern HQUIC DaemonRegistration;

//=============================================================================
// Forward declarations of QUIC callbacks
//=============================================================================

static QUIC_STATUS QUIC_API Daemon_SendListenerCallback(
    HQUIC Listener, void *Context, QUIC_LISTENER_EVENT *Event);
static QUIC_STATUS QUIC_API Daemon_SendConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event);
static QUIC_STATUS QUIC_API Daemon_ReceiveConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event);

//=============================================================================
// Module State
//=============================================================================

static ACTIVE_TRANSFER g_transfers[TRANSFER_MAX_CONCURRENT];
static WH_MUTEX g_transfer_lock;
static KEYPAIR *g_daemon_keypair;
static WORMHOLE_CONFIG *g_config = NULL;
static volatile int32_t g_next_transfer_id = 1;
static volatile int32_t g_initialized = 0;

//=============================================================================
// Internal Helpers
//=============================================================================

static ACTIVE_TRANSFER *FindTransfer(uint32_t transfer_id)
{
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (g_transfers[i].active && g_transfers[i].transfer_id == transfer_id)
            return &g_transfers[i];
    }
    return NULL;
}

static ACTIVE_TRANSFER *AllocTransfer(void)
{
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (!g_transfers[i].active) {
            memset(&g_transfers[i], 0, sizeof(ACTIVE_TRANSFER));
            g_transfers[i].active = TRUE;
            g_transfers[i].transfer_id = (uint32_t)WH_ATOMIC_ADD(&g_next_transfer_id, 1);
            g_transfers[i].complete_event = WH_EVENT_CREATE();
            return &g_transfers[i];
        }
    }
    return NULL;
}

static void FreeTransfer(ACTIVE_TRANSFER *t)
{
    if (!t) return;

    // Close QUIC handles (safety net after thread join)
    if (t->quic_connection) {
        MsQuic->ConnectionShutdown(t->quic_connection,
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        MsQuic->ConnectionClose(t->quic_connection);
        t->quic_connection = NULL;
    }
    if (t->quic_listener) {
        MsQuic->ListenerClose(t->quic_listener);
        t->quic_listener = NULL;
    }

    if (t->relay_client) {
        RelayClient_SendGoodbye(t->relay_client, 0);
        RelayClient_Destroy(t->relay_client);
        t->relay_client = NULL;
    }
    if (t->relay_forwarder) {
        RelayForwarder_Stop(t->relay_forwarder);
        t->relay_forwarder = NULL;
    }
    if (t->manifest) {
        Manifest_Destroy(t->manifest);
        t->manifest = NULL;
    }
    t->recv_ctx = NULL;  // Owned by stream cleanup, don't free here
    WH_EVENT_DESTROY(t->complete_event);
    t->active = FALSE;
}

// Push a transfer progress event via IPC
static void PushTransferProgress(ACTIVE_TRANSFER *t)
{
    double now = WH_TIMER_NOW();
    // Throttle: max 10 events/sec
    if (now - t->last_event_time < 0.1 &&
        t->chunks_transferred < t->chunks_total)
        return;
    t->last_event_time = now;

    IpcServer_UpdateProgress(t->transfer_id,
        t->bytes_transferred, t->bytes_total,
        t->chunks_transferred, t->chunks_total);
}

// Push a transfer state event (started/completed/failed)
static void PushTransferEvent(ACTIVE_TRANSFER *t, uint8_t ipc_status)
{
    // Transfer event payload: [4B transfer_id][1B direction][1B state][1B status]
    //                         [8B bytes_transferred][8B bytes_total]
    //                         [2B error_msg_len][error_msg]
    //                         [1B ticket_len][ticket_bytes]
    uint8_t buf[512];
    uint32_t off = 0;

    WriteUint32LE(buf + off, t->transfer_id); off += 4;
    buf[off++] = (uint8_t)t->direction;
    buf[off++] = (uint8_t)WH_ATOMIC_LOAD(&t->state);
    buf[off++] = ipc_status;
    WriteUint64LE(buf + off, t->bytes_transferred); off += 8;
    WriteUint64LE(buf + off, t->bytes_total); off += 8;

    uint16_t msg_len = (uint16_t)strlen(t->error_msg);
    if (msg_len > sizeof(buf) - off - 2) msg_len = (uint16_t)(sizeof(buf) - off - 2);
    WriteUint16LE(buf + off, msg_len); off += 2;
    if (msg_len > 0) {
        memcpy(buf + off, t->error_msg, msg_len);
        off += msg_len;
    }

    // Ticket field: [1B ticket_len][ticket_bytes]
    uint8_t ticket_len = (uint8_t)strlen(t->ticket);
    if (ticket_len > sizeof(buf) - off - 1) ticket_len = (uint8_t)(sizeof(buf) - off - 1);
    buf[off++] = ticket_len;
    if (ticket_len > 0) {
        memcpy(buf + off, t->ticket, ticket_len);
        off += ticket_len;
    }

    // Filename field: [1B filename_len][filename_bytes]
    uint8_t fn_len = (uint8_t)strlen(t->filename);
    if (fn_len > sizeof(buf) - off - 1) fn_len = (uint8_t)(sizeof(buf) - off - 1);
    buf[off++] = fn_len;
    if (fn_len > 0) {
        memcpy(buf + off, t->filename, fn_len);
        off += fn_len;
    }

    IpcServer_PushEvent(IPC_EVENT_TRANSFER, t->transfer_id, buf, off);
}

// Stream progress callback bridging to IPC events
static BOOLEAN TransferProgressCallback(
    uint32_t chunks_done, uint32_t total_chunks,
    uint64_t bytes_done, uint64_t bytes_total,
    double speed_bps, double eta_seconds,
    void *user_context)
{
    UNREFERENCED_PARAMETER(speed_bps);
    UNREFERENCED_PARAMETER(eta_seconds);
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)user_context;
    if (!t) return TRUE;

    // Lazy-populate filename for receiver (from manifest, available after first progress)
    if (t->filename[0] == '\0' && t->recv_ctx) {
        CHUNK_RECEIVE_CONTEXT *rc = (CHUNK_RECEIVE_CONTEXT *)t->recv_ctx;
        if (rc->manifest && rc->manifest->filename[0]) {
            strncpy(t->filename, rc->manifest->filename, sizeof(t->filename) - 1);
            t->filename[sizeof(t->filename) - 1] = '\0';
        }
    }

    t->bytes_transferred = bytes_done;
    t->bytes_total = bytes_total;
    t->chunks_transferred = chunks_done;
    t->chunks_total = total_chunks;

    PushTransferProgress(t);

    // Check for cancellation
    if (WH_ATOMIC_LOAD(&t->cancel_requested))
        return FALSE;

    return TRUE;
}

//=============================================================================
// Relay Callbacks (per-transfer)
// Signatures must match typedefs in relay_client.h
//=============================================================================

static void on_transfer_relay_connected(void *ctx, uint64_t session_id,
    const uint8_t observed_addr[16], uint8_t observed_addr_type,
    uint16_t observed_port)
{
    UNREFERENCED_PARAMETER(session_id);
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)ctx;
    // Store reflected address info
    t->reflected_addr_type = observed_addr_type;
    memcpy(t->reflected_addr, observed_addr, 16);
    t->reflected_port = observed_port;
    t->reflected_addr_ready = TRUE;
    LOG("[transfer %u] Relay connected\n", t->transfer_id);
}

static void on_transfer_ticket_created(void *ctx, const char *ticket)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)ctx;
    strncpy(t->ticket, ticket, sizeof(t->ticket) - 1);
    t->ticket[sizeof(t->ticket) - 1] = '\0';
    t->ticket_ready = TRUE;
    LOG("[transfer %u] Ticket created: %s\n", t->transfer_id, ticket);
}

static void on_transfer_peer_info(void *ctx, const uint8_t peer_id[32],
    const ENDPOINT *endpoints, uint16_t endpoint_count)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)ctx;
    memcpy(t->peer_id, peer_id, 32);
    t->peer_endpoint_count = endpoint_count < MAX_ENDPOINTS
        ? endpoint_count : MAX_ENDPOINTS;
    memcpy(t->peer_endpoints, endpoints,
           t->peer_endpoint_count * sizeof(ENDPOINT));
    t->peer_info_ready = TRUE;
    LOG("[transfer %u] Peer info received (%u endpoints)\n",
        t->transfer_id, t->peer_endpoint_count);
}

//=============================================================================
// Hole Punch — send WHPK probes via relay socket to open NAT pinholes
//=============================================================================

static void send_hole_punch(RELAY_CLIENT *relay_client,
    const ENDPOINT *peer_endpoints, uint16_t peer_endpoint_count)
{
    int sock_fd = RelayClient_GetSocket(relay_client);
    if (sock_fd < 0) return;

    uint8_t probe[] = { 'W', 'H', 'P', 'K' };

    for (uint16_t ep = 0; ep < peer_endpoint_count; ep++) {
        if (peer_endpoints[ep].priority >= 200) continue; // Skip relay endpoints

        struct sockaddr_storage addr;
        memset(&addr, 0, sizeof(addr));

        if (peer_endpoints[ep].addr_type == 0x04) { // IPv4
            struct sockaddr_in *a4 = (struct sockaddr_in *)&addr;
            a4->sin_family = AF_INET;
            memcpy(&a4->sin_addr, peer_endpoints[ep].addr, 4);
            a4->sin_port = htons(peer_endpoints[ep].port);
            sendto(sock_fd, (const char *)probe, sizeof(probe), 0,
                   (struct sockaddr *)a4, sizeof(*a4));
        } else if (peer_endpoints[ep].addr_type == 0x06) { // IPv6
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addr;
            a6->sin6_family = AF_INET6;
            memcpy(&a6->sin6_addr, peer_endpoints[ep].addr, 16);
            a6->sin6_port = htons(peer_endpoints[ep].port);
            sendto(sock_fd, (const char *)probe, sizeof(probe), 0,
                   (struct sockaddr *)a6, sizeof(*a6));
        }
    }
}

//=============================================================================
// Send Thread — runs the full send lifecycle asynchronously
//=============================================================================

static WH_THREAD_RETURN SendThreadFunc(WH_THREAD_PARAM param)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)param;

    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_REGISTERING);
    PushTransferEvent(t, IPC_STATUS_OK);

    // 1. Build manifest (directory → v2 multi-file, file → v1 single-file)
    if (IsDirectory(t->filepath)) {
        t->manifest = Chunker_BuildManifestFromDirectory(t->filepath);
    } else {
        t->manifest = Chunker_BuildManifest(t->filepath);
    }
    if (!t->manifest) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to build manifest for %s", t->filepath);
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    t->file_size = t->manifest->file_size;
    t->bytes_total = t->manifest->file_size;
    t->chunks_total = t->manifest->chunk_count;

    // 2. Set up relay client
    const char *cfg_relay_host = g_config ? Config_GetString(g_config, "relay_host", CONFIG_DEFAULT_RELAY_HOST) : CONFIG_DEFAULT_RELAY_HOST;
    uint16_t cfg_relay_port = g_config ? (uint16_t)Config_GetUint64(g_config, "relay_port", CONFIG_DEFAULT_RELAY_PORT) : CONFIG_DEFAULT_RELAY_PORT;

    RELAY_CLIENT_CONFIG relay_cfg = {0};
    relay_cfg.relay_host = cfg_relay_host;
    relay_cfg.relay_port = cfg_relay_port;
    relay_cfg.keypair = &t->keypair;
    relay_cfg.on_connected = on_transfer_relay_connected;
    relay_cfg.on_ticket_created = on_transfer_ticket_created;
    relay_cfg.on_peer_info = on_transfer_peer_info;
    relay_cfg.callback_context = t;

    t->relay_client = RelayClient_Create(&relay_cfg);
    if (!t->relay_client) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to create relay client");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // 3. Start QUIC listener BEFORE relay registration so we know the port
    HQUIC listener = NULL;
    QUIC_STATUS status = MsQuic->ListenerOpen(
        DaemonRegistration, Daemon_SendListenerCallback, t, &listener);
    if (QUIC_FAILED(status)) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to open QUIC listener");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }
    t->quic_listener = listener;

    const QUIC_BUFFER alpn = { sizeof("wormhole") - 1, (uint8_t *)"wormhole" };
    QUIC_ADDR addr = {0};
    QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&addr, 0);  // Ephemeral — avoids conflict with daemon listener on 4567
    status = MsQuic->ListenerStart(listener, &alpn, 1, &addr);
    if (QUIC_FAILED(status)) {
        MsQuic->ListenerClose(listener);
        t->quic_listener = NULL;
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to start QUIC listener");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // Query the actual bound port
    QUIC_ADDR listener_addr = {0};
    uint32_t addr_len = sizeof(listener_addr);
    uint16_t listener_port = 0;
    if (QUIC_SUCCEEDED(MsQuic->GetParam(listener, QUIC_PARAM_LISTENER_LOCAL_ADDRESS,
                                         &addr_len, &listener_addr))) {
        listener_port = QuicAddrGetPort(&listener_addr);
    }
    LOG("[transfer %u] QUIC listener started on port %u\n", t->transfer_id, listener_port);

    // 4. Discover endpoints and set ports to the listener's ephemeral port
    t->our_endpoint_count = Discovery_FindEndpoints(
        t->our_endpoints, MAX_ENDPOINTS);

    // Discovery returns interface addresses with port 0 — fix them to the actual
    // listener port so the receiver can connect (mirrors cmd_send in wormhole.c)
    for (uint16_t i = 0; i < t->our_endpoint_count; i++) {
        t->our_endpoints[i].port = listener_port;
    }

    // 5. Register with relay (now endpoints have the correct port)
    RelayClient_Register(t->relay_client, t->our_endpoints, t->our_endpoint_count);

    // Poll for registration confirmation (5s)
    for (int w = 0; w < 50 && !WH_ATOMIC_LOAD(&t->cancel_requested); w++) {
        RelayClient_Poll(t->relay_client, 100);
        if (RelayClient_IsConnected(t->relay_client)) break;
    }

    if (WH_ATOMIC_LOAD(&t->cancel_requested)) {
        MsQuic->ListenerClose(listener);
        t->quic_listener = NULL;
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CANCELLED);
        PushTransferEvent(t, IPC_STATUS_CANCELLED);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // Create ticket
    const char *display = t->filename[0] ? t->filename : t->filepath;
    RelayClient_CreateTicket(t->relay_client, t->file_size, display);

    // Wait for ticket (5s)
    for (int w = 0; w < 50 && !t->ticket_ready && !WH_ATOMIC_LOAD(&t->cancel_requested); w++)
        RelayClient_Poll(t->relay_client, 100);

    if (!t->ticket_ready) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Timeout waiting for ticket");
        MsQuic->ListenerClose(listener);
        t->quic_listener = NULL;
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // 6. Wait for receiver
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_WAITING_PEER);
    PushTransferEvent(t, IPC_STATUS_OK);

    // Poll for peer info (up to 30 minutes)
    for (int w = 0; w < 18000 && !t->peer_info_ready && !WH_ATOMIC_LOAD(&t->cancel_requested); w++) {
        RelayClient_Poll(t->relay_client, 100);
    }

    if (WH_ATOMIC_LOAD(&t->cancel_requested)) {
        MsQuic->ListenerClose(listener);
        t->quic_listener = NULL;
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CANCELLED);
        PushTransferEvent(t, IPC_STATUS_CANCELLED);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    if (!t->peer_info_ready) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Timeout waiting for receiver");
        MsQuic->ListenerClose(listener);
        t->quic_listener = NULL;
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // 7. Send hole punch probes
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CONNECTING);
    PushTransferEvent(t, IPC_STATUS_OK);

    for (int round = 0; round < 5; round++) {
        send_hole_punch(t->relay_client, t->peer_endpoints, t->peer_endpoint_count);
        WH_SLEEP_MS(200);
    }

    // Start relay forwarder for fallback
    {
        RELAY_FORWARDER_CONFIG fwd_cfg = {0};
        fwd_cfg.relay_host = cfg_relay_host;
        fwd_cfg.relay_port = cfg_relay_port;
        fwd_cfg.keypair = &t->keypair;
        memcpy(fwd_cfg.remote_peer_id, t->peer_id, 32);
        fwd_cfg.our_endpoints = t->our_endpoints;
        fwd_cfg.our_endpoint_count = t->our_endpoint_count;
        fwd_cfg.local_quic_port = listener_port;
        t->relay_forwarder = RelayForwarder_Start(&fwd_cfg);
    }

    // 8. Wait for transfer to complete (up to 60 minutes)
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_TRANSFERRING);
    t->start_time = WH_TIMER_NOW();

    uint32_t wait_result = WH_EVENT_WAIT(t->complete_event, 3600000);

    // 9. Cleanup
    if (t->quic_listener) {
        MsQuic->ListenerClose(t->quic_listener);
        t->quic_listener = NULL;
    }
    if (t->relay_forwarder) {
        RelayForwarder_Stop(t->relay_forwarder);
        t->relay_forwarder = NULL;
    }
    if (t->relay_client) {
        RelayClient_SendGoodbye(t->relay_client, 0);
        RelayClient_Destroy(t->relay_client);
        t->relay_client = NULL;
    }

    if (wait_result == WH_EVENT_WAIT_TIMEOUT) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Transfer timed out");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
    } else if (WH_ATOMIC_LOAD(&t->cancel_requested)) {
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CANCELLED);
        PushTransferEvent(t, IPC_STATUS_CANCELLED);
    } else if ((int32_t)WH_ATOMIC_LOAD(&t->state) != TRANSFER_STATE_COMPLETED) {
        // Event may have been signaled from stream.c TRANSFER_COMPLETE before
        // SHUTDOWN_COMPLETE set the state — check chunk counts directly
        if (t->chunks_transferred >= t->chunks_total && t->chunks_total > 0) {
            WH_ATOMIC_SET(&t->state, TRANSFER_STATE_COMPLETED);
            PushTransferEvent(t, IPC_STATUS_OK);
        } else {
            if (t->error_msg[0] == '\0')
                snprintf(t->error_msg, sizeof(t->error_msg), "Transfer failed");
            WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
            PushTransferEvent(t, IPC_STATUS_ERROR);
        }
    }

    return (WH_THREAD_RETURN)0;
}

//=============================================================================
// Receive Thread — runs the full receive lifecycle asynchronously
//=============================================================================

static WH_THREAD_RETURN ReceiveThreadFunc(WH_THREAD_PARAM param)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)param;

    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_REGISTERING);
    PushTransferEvent(t, IPC_STATUS_OK);

    // 1. Set up relay client and look up ticket
    const char *cfg_relay_host = g_config ? Config_GetString(g_config, "relay_host", CONFIG_DEFAULT_RELAY_HOST) : CONFIG_DEFAULT_RELAY_HOST;
    uint16_t cfg_relay_port = g_config ? (uint16_t)Config_GetUint64(g_config, "relay_port", CONFIG_DEFAULT_RELAY_PORT) : CONFIG_DEFAULT_RELAY_PORT;

    RELAY_CLIENT_CONFIG relay_cfg = {0};
    relay_cfg.relay_host = cfg_relay_host;
    relay_cfg.relay_port = cfg_relay_port;
    relay_cfg.keypair = &t->keypair;
    relay_cfg.on_connected = on_transfer_relay_connected;
    relay_cfg.on_peer_info = on_transfer_peer_info;
    relay_cfg.callback_context = t;

    t->relay_client = RelayClient_Create(&relay_cfg);
    if (!t->relay_client) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to create relay client");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // Discover endpoints
    t->our_endpoint_count = Discovery_FindEndpoints(
        t->our_endpoints, MAX_ENDPOINTS);

    // Register and look up ticket
    RelayClient_Register(t->relay_client, t->our_endpoints, t->our_endpoint_count);

    // Wait for registration
    for (int w = 0; w < 50 && !WH_ATOMIC_LOAD(&t->cancel_requested); w++) {
        RelayClient_Poll(t->relay_client, 100);
        if (RelayClient_IsConnected(t->relay_client)) break;
    }

    RelayClient_LookupTicket(t->relay_client, t->ticket);

    // Wait for peer info (15s)
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_WAITING_PEER);
    for (int w = 0; w < 150 && !t->peer_info_ready && !WH_ATOMIC_LOAD(&t->cancel_requested); w++)
        RelayClient_Poll(t->relay_client, 100);

    if (WH_ATOMIC_LOAD(&t->cancel_requested)) {
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CANCELLED);
        PushTransferEvent(t, IPC_STATUS_CANCELLED);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    if (!t->peer_info_ready) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Ticket not found or sender not available");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // 2. Hole punch
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CONNECTING);
    PushTransferEvent(t, IPC_STATUS_OK);

    for (int round = 0; round < 5; round++) {
        send_hole_punch(t->relay_client, t->peer_endpoints, t->peer_endpoint_count);
        WH_SLEEP_MS(200);
    }

    // 3. Parallel connection race
    BOOLEAN connected = FALSE;
    HQUIC connection = NULL;

    for (int attempt = 0; attempt < TRANSFER_RACE_ATTEMPTS && !connected &&
         !WH_ATOMIC_LOAD(&t->cancel_requested); attempt++)
    {
        // Try each direct endpoint
        for (uint16_t ep = 0; ep < t->peer_endpoint_count && !connected; ep++) {
            if (t->peer_endpoints[ep].priority >= 200) continue;

            char addr_str[64];
            uint16_t port;
            if (!Endpoint_ToString(&t->peer_endpoints[ep], addr_str, sizeof(addr_str), &port))
                continue;

            QUIC_ADDRESS_FAMILY family = (t->peer_endpoints[ep].addr_type == 0x06)
                ? QUIC_ADDRESS_FAMILY_INET6 : QUIC_ADDRESS_FAMILY_INET;

            QUIC_STATUS qstatus = MsQuic->ConnectionOpen(
                DaemonRegistration, Daemon_ReceiveConnectionCallback, t, &connection);
            if (QUIC_FAILED(qstatus)) continue;

            qstatus = MsQuic->ConnectionStart(connection, DaemonClientConfig,
                family, addr_str, port);
            if (QUIC_FAILED(qstatus)) {
                MsQuic->ConnectionClose(connection);
                connection = NULL;
                continue;
            }

            // Wait for connection (5s)
            uint32_t wait = WH_EVENT_WAIT(t->complete_event, TRANSFER_RACE_TIMEOUT_MS);
            if (wait == WH_EVENT_WAIT_OK && t->quic_connection != NULL) {
                connected = TRUE;
            } else {
                MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
                MsQuic->ConnectionClose(connection);
                connection = NULL;
            }
        }

        if (!connected)
            WH_SLEEP_MS(3000); // Wait between attempts
    }

    // 4. Relay fallback if direct connections failed
    if (!connected && !WH_ATOMIC_LOAD(&t->cancel_requested)) {
        RELAY_FORWARDER_CONFIG fwd_cfg = {0};
        fwd_cfg.relay_host = cfg_relay_host;
        fwd_cfg.relay_port = cfg_relay_port;
        fwd_cfg.keypair = &t->keypair;
        memcpy(fwd_cfg.remote_peer_id, t->peer_id, 32);
        fwd_cfg.our_endpoints = t->our_endpoints;
        fwd_cfg.our_endpoint_count = t->our_endpoint_count;
        fwd_cfg.local_quic_port = 0; // Receiver mode
        t->relay_forwarder = RelayForwarder_Start(&fwd_cfg);

        if (t->relay_forwarder) {
            // Wait for forwarder ready
            for (int w = 0; w < 30 && !RelayForwarder_IsReady(t->relay_forwarder); w++)
                WH_SLEEP_MS(100);

            uint16_t proxy_port = RelayForwarder_GetProxyPort(t->relay_forwarder);
            if (proxy_port != 0) {
                QUIC_STATUS qstatus = MsQuic->ConnectionOpen(
                    DaemonRegistration, Daemon_ReceiveConnectionCallback, t, &connection);
                if (QUIC_SUCCEEDED(qstatus)) {
                    qstatus = MsQuic->ConnectionStart(connection, DaemonClientConfig,
                        QUIC_ADDRESS_FAMILY_INET, "127.0.0.1", proxy_port);
                    if (QUIC_SUCCEEDED(qstatus)) {
                        uint32_t wait = WH_EVENT_WAIT(t->complete_event, TRANSFER_RELAY_TIMEOUT_MS);
                        if (wait == WH_EVENT_WAIT_OK && t->quic_connection != NULL) {
                            connected = TRUE;
                        }
                    }
                    if (!connected && connection) {
                        MsQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
                        MsQuic->ConnectionClose(connection);
                        connection = NULL;
                    }
                }
            }
        }
    }

    if (!connected) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Failed to connect to sender");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
        WH_EVENT_SET(t->complete_event);
        return (WH_THREAD_RETURN)0;
    }

    // 5. Transfer in progress — wait for completion
    WH_EVENT_RESET(t->complete_event);    // Clear connection signal before transfer wait
    WH_ATOMIC_SET(&t->state, TRANSFER_STATE_TRANSFERRING);
    t->start_time = WH_TIMER_NOW();
    PushTransferEvent(t, IPC_STATUS_OK);

    // The QUIC callbacks will signal complete_event when done
    uint32_t wait_result = WH_EVENT_WAIT(t->complete_event, 3600000);

    // 6. Cleanup
    if (t->quic_connection) {
        MsQuic->ConnectionShutdown(t->quic_connection,
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        MsQuic->ConnectionClose(t->quic_connection);
        t->quic_connection = NULL;
    }
    if (t->relay_forwarder) {
        RelayForwarder_Stop(t->relay_forwarder);
        t->relay_forwarder = NULL;
    }
    if (t->relay_client) {
        RelayClient_SendGoodbye(t->relay_client, 0);
        RelayClient_Destroy(t->relay_client);
        t->relay_client = NULL;
    }

    if (wait_result == WH_EVENT_WAIT_TIMEOUT) {
        snprintf(t->error_msg, sizeof(t->error_msg), "Transfer timed out");
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
        PushTransferEvent(t, IPC_STATUS_ERROR);
    } else if (WH_ATOMIC_LOAD(&t->cancel_requested)) {
        WH_ATOMIC_SET(&t->state, TRANSFER_STATE_CANCELLED);
        PushTransferEvent(t, IPC_STATUS_CANCELLED);
    } else if ((int32_t)WH_ATOMIC_LOAD(&t->state) != TRANSFER_STATE_COMPLETED) {
        // Event may have been signaled from stream.c TRANSFER_COMPLETE before
        // SHUTDOWN_COMPLETE set the state — check chunk counts directly
        if (t->chunks_transferred >= t->chunks_total && t->chunks_total > 0) {
            WH_ATOMIC_SET(&t->state, TRANSFER_STATE_COMPLETED);
            PushTransferEvent(t, IPC_STATUS_OK);
        } else {
            if (t->error_msg[0] == '\0')
                snprintf(t->error_msg, sizeof(t->error_msg), "Transfer failed");
            WH_ATOMIC_SET(&t->state, TRANSFER_STATE_FAILED);
            PushTransferEvent(t, IPC_STATUS_ERROR);
        }
    }

    return (WH_THREAD_RETURN)0;
}

//=============================================================================
// QUIC Callbacks for daemon-managed transfers
//=============================================================================

static QUIC_STATUS QUIC_API Daemon_SendListenerCallback(
    HQUIC Listener, void *Context, QUIC_LISTENER_EVENT *Event)
{
    UNREFERENCED_PARAMETER(Listener);
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)Context;

    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
    {
        HQUIC conn = Event->NEW_CONNECTION.Connection;
        MsQuic->SetCallbackHandler(conn,
            (void *)Daemon_SendConnectionCallback, t);

        QUIC_STATUS status = MsQuic->ConnectionSetConfiguration(
            conn, DaemonServerConfig);
        if (QUIC_FAILED(status)) {
            return QUIC_STATUS_INTERNAL_ERROR;
        }

        t->quic_connection = conn;
        return QUIC_STATUS_SUCCESS;
    }
    default:
        return QUIC_STATUS_SUCCESS;
    }
}

static QUIC_STATUS QUIC_API Daemon_SendConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)Context;

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[transfer %u] Receiver connected\n", t->transfer_id);
        // ChunkSendFile creates its own CHUNK_SEND_CONTEXT internally.
        ChunkSendFile(Connection, t->filepath, t->manifest,
                       t->complete_event, NULL,
                       TransferProgressCallback, t);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        LOG("[transfer %u] Connection shutdown\n", t->transfer_id);
        if ((int32_t)WH_ATOMIC_LOAD(&t->state) == TRANSFER_STATE_TRANSFERRING) {
            if (t->chunks_transferred >= t->chunks_total && t->chunks_total > 0) {
                WH_ATOMIC_SET(&t->state, TRANSFER_STATE_COMPLETED);
                PushTransferEvent(t, IPC_STATUS_OK);
            }
            WH_EVENT_SET(t->complete_event);
        }
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API Daemon_ReceiveConnectionCallback(
    HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event)
{
    ACTIVE_TRANSFER *t = (ACTIVE_TRANSFER *)Context;

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        LOG("[transfer %u] Connected to sender\n", t->transfer_id);
        t->quic_connection = Connection;
        WH_EVENT_SET(t->complete_event);
        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    {
        HQUIC stream = Event->PEER_STREAM_STARTED.Stream;
        BOOLEAN is_bidirectional = !(Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL);

        if (is_bidirectional) {
            // Control stream — allocate receive context
            CHUNK_RECEIVE_CONTEXT *recv_ctx = (CHUNK_RECEIVE_CONTEXT *)calloc(1, sizeof(CHUNK_RECEIVE_CONTEXT));
            if (recv_ctx) {
                recv_ctx->control_stream = stream;
                recv_ctx->transfer_complete_event = t->complete_event;
                recv_ctx->progress_cb = TransferProgressCallback;
                recv_ctx->progress_cb_ctx = t;
                recv_ctx->user_context = t;
                t->recv_ctx = recv_ctx;  // Store for data stream callback

                // Set Downloads path
                const char *home = getenv("HOME");
                if (!home) home = getenv("USERPROFILE");
                if (home) {
#ifdef _WIN32
                    snprintf(recv_ctx->downloads_path, sizeof(recv_ctx->downloads_path),
                             "%s\\Downloads", home);
#else
                    snprintf(recv_ctx->downloads_path, sizeof(recv_ctx->downloads_path),
                             "%s/Downloads", home);
#endif
                }
                if (t->filepath[0])
                    strncpy(recv_ctx->downloads_path, t->filepath,
                            sizeof(recv_ctx->downloads_path) - 1);

                MsQuic->SetCallbackHandler(stream,
                    (void *)ReceiverControlStreamCallback, recv_ctx);

                // Send MANIFEST_REQUEST (heap-allocated for zero-copy StreamSend)
                uint8_t *req_buf = (uint8_t *)malloc(CTRL_HEADER_SIZE);
                if (req_buf) {
                    req_buf[0] = CTRL_MSG_MANIFEST_REQUEST;
                    WriteUint32LE(req_buf + 1, 0); // payload_len = 0
                    QUIC_BUFFER *quic_buf = (QUIC_BUFFER *)malloc(sizeof(QUIC_BUFFER));
                    if (quic_buf) {
                        quic_buf->Buffer = req_buf;
                        quic_buf->Length = CTRL_HEADER_SIZE;
                        MsQuic->StreamSend(stream, quic_buf, 1,
                            QUIC_SEND_FLAG_NONE, quic_buf);
                    } else {
                        free(req_buf);
                    }
                }
            }
        } else {
            // Data stream — pass the receive context (set during control stream setup)
            if (t->recv_ctx) {
                MsQuic->SetCallbackHandler(stream,
                    (void *)ReceiverDataStreamCallback, t->recv_ctx);
            } else {
                LOG_ERROR("[transfer %u] Data stream arrived but recv_ctx is NULL\n", t->transfer_id);
                MsQuic->StreamClose(stream);
            }
        }
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        LOG("[transfer %u] Receive connection shutdown\n", t->transfer_id);
        if ((int32_t)WH_ATOMIC_LOAD(&t->state) == TRANSFER_STATE_TRANSFERRING) {
            if (t->chunks_transferred >= t->chunks_total && t->chunks_total > 0) {
                WH_ATOMIC_SET(&t->state, TRANSFER_STATE_COMPLETED);
                PushTransferEvent(t, IPC_STATUS_OK);
            }
            WH_EVENT_SET(t->complete_event);
        }
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

//=============================================================================
// Public API Implementation
//=============================================================================

void TransferMgr_Init(KEYPAIR *daemon_keypair, WORMHOLE_CONFIG *config)
{
    if (WH_ATOMIC_LOAD(&g_initialized)) return;
    WH_MUTEX_INIT(g_transfer_lock);
    g_daemon_keypair = daemon_keypair;
    g_config = config;
    memset(g_transfers, 0, sizeof(g_transfers));
    WH_ATOMIC_SET(&g_initialized, 1);
}

void TransferMgr_Shutdown(void)
{
    if (!WH_ATOMIC_LOAD(&g_initialized)) return;

    // Phase 1: Signal cancel + set complete_event on all active transfers
    WH_MUTEX_LOCK(g_transfer_lock);
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (g_transfers[i].active) {
            WH_ATOMIC_SET(&g_transfers[i].cancel_requested, 1);
            WH_EVENT_SET(g_transfers[i].complete_event);
        }
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);

    // Phase 2: Force-shutdown QUIC connections + stop listeners (unblocks threads)
    WH_MUTEX_LOCK(g_transfer_lock);
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (!g_transfers[i].active) continue;
        if (g_transfers[i].quic_connection) {
            MsQuic->ConnectionShutdown(g_transfers[i].quic_connection,
                QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        }
        if (g_transfers[i].quic_listener) {
            MsQuic->ListenerStop(g_transfers[i].quic_listener);
        }
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);

    // Phase 3: Join all threads with timeout (5s)
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (g_transfers[i].active && g_transfers[i].thread_valid) {
            WH_THREAD_JOIN(g_transfers[i].thread, 5000);
            g_transfers[i].thread_valid = FALSE;
        }
    }

    // Phase 4: FreeTransfer on remaining active transfers
    WH_MUTEX_LOCK(g_transfer_lock);
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT; i++) {
        if (g_transfers[i].active)
            FreeTransfer(&g_transfers[i]);
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);

    WH_MUTEX_DESTROY(g_transfer_lock);
    WH_ATOMIC_SET(&g_initialized, 0);
}

uint32_t TransferMgr_Send(uint32_t op_id, const char *filepath)
{
    if (!WH_ATOMIC_LOAD(&g_initialized) || !filepath) return 0;

    WH_MUTEX_LOCK(g_transfer_lock);
    ACTIVE_TRANSFER *t = AllocTransfer();
    if (!t) {
        WH_MUTEX_UNLOCK(g_transfer_lock);
        return 0;
    }

    t->direction = TRANSFER_DIR_SEND;
    if (op_id != 0) t->transfer_id = op_id; // Use IPC op_id for correlation
    strncpy(t->filepath, filepath, sizeof(t->filepath) - 1);

    // Extract filename for display
    char *fname = NULL;
    uint32_t fname_len = 0;
    ExtractFilename(filepath, &fname, &fname_len);
    if (fname && fname_len > 0 && fname_len < sizeof(t->filename))
        memcpy(t->filename, fname, fname_len);

    // Copy daemon keypair
    if (g_daemon_keypair)
        memcpy(&t->keypair, g_daemon_keypair, sizeof(KEYPAIR));

    uint32_t id = t->transfer_id;
    WH_MUTEX_UNLOCK(g_transfer_lock);

    // Start send thread
    if (WH_THREAD_CREATE(t->thread, SendThreadFunc, t)) {
        t->thread_valid = TRUE;
    } else {
        LOG_ERROR("[transfer %u] Failed to create send thread\n", id);
    }

    return id;
}

uint32_t TransferMgr_Receive(uint32_t op_id, const char *ticket,
                              const char *output_dir)
{
    if (!WH_ATOMIC_LOAD(&g_initialized) || !ticket) return 0;

    WH_MUTEX_LOCK(g_transfer_lock);
    ACTIVE_TRANSFER *t = AllocTransfer();
    if (!t) {
        WH_MUTEX_UNLOCK(g_transfer_lock);
        return 0;
    }

    t->direction = TRANSFER_DIR_RECEIVE;
    if (op_id != 0) t->transfer_id = op_id;
    strncpy(t->ticket, ticket, sizeof(t->ticket) - 1);

    if (output_dir)
        strncpy(t->filepath, output_dir, sizeof(t->filepath) - 1);

    if (g_daemon_keypair)
        memcpy(&t->keypair, g_daemon_keypair, sizeof(KEYPAIR));

    uint32_t id = t->transfer_id;
    WH_MUTEX_UNLOCK(g_transfer_lock);

    // Start receive thread
    if (WH_THREAD_CREATE(t->thread, ReceiveThreadFunc, t)) {
        t->thread_valid = TRUE;
    } else {
        LOG_ERROR("[transfer %u] Failed to create receive thread\n", id);
    }

    return id;
}

BOOLEAN TransferMgr_Cancel(uint32_t transfer_id)
{
    WH_MUTEX_LOCK(g_transfer_lock);
    ACTIVE_TRANSFER *t = FindTransfer(transfer_id);
    if (t) {
        WH_ATOMIC_SET(&t->cancel_requested, 1);
        WH_EVENT_SET(t->complete_event);
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);
    return t != NULL;
}

BOOLEAN TransferMgr_GetStatus(uint32_t transfer_id, ACTIVE_TRANSFER *out)
{
    WH_MUTEX_LOCK(g_transfer_lock);
    ACTIVE_TRANSFER *t = FindTransfer(transfer_id);
    if (t && out) {
        // Copy snapshot (don't copy pointers to managed resources)
        out->transfer_id = t->transfer_id;
        out->direction = t->direction;
        WH_ATOMIC_SET(&out->state, WH_ATOMIC_LOAD(&t->state));
        out->active = t->active;
        memcpy(out->filepath, t->filepath, sizeof(out->filepath));
        memcpy(out->filename, t->filename, sizeof(out->filename));
        memcpy(out->ticket, t->ticket, sizeof(out->ticket));
        out->ticket_ready = t->ticket_ready;
        out->file_size = t->file_size;
        out->bytes_transferred = t->bytes_transferred;
        out->bytes_total = t->bytes_total;
        out->chunks_transferred = t->chunks_transferred;
        out->chunks_total = t->chunks_total;
        out->start_time = t->start_time;
        memcpy(out->error_msg, t->error_msg, sizeof(out->error_msg));
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);
    return t != NULL;
}

uint32_t TransferMgr_List(ACTIVE_TRANSFER *out, uint32_t max_count)
{
    uint32_t count = 0;
    WH_MUTEX_LOCK(g_transfer_lock);
    for (int i = 0; i < TRANSFER_MAX_CONCURRENT && count < max_count; i++) {
        if (g_transfers[i].active) {
            TransferMgr_GetStatus(g_transfers[i].transfer_id, &out[count]);
            count++;
        }
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);
    return count;
}

BOOLEAN TransferMgr_GetTicket(uint32_t transfer_id, char *ticket_out,
                               uint32_t ticket_capacity)
{
    WH_MUTEX_LOCK(g_transfer_lock);
    ACTIVE_TRANSFER *t = FindTransfer(transfer_id);
    BOOLEAN result = FALSE;
    if (t && t->ticket_ready && ticket_out && ticket_capacity > 0) {
        strncpy(ticket_out, t->ticket, ticket_capacity - 1);
        ticket_out[ticket_capacity - 1] = '\0';
        result = TRUE;
    }
    WH_MUTEX_UNLOCK(g_transfer_lock);
    return result;
}
