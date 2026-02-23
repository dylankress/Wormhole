//
// ipc.c
// Wormhole - Named pipe / Unix domain socket IPC implementation.
// Supports both v1 (original) and v2 (operation IDs, subscriptions, events).
// by Dylan Kress
//

#include "ipc.h"
#include "wire_format.h"

//=============================================================================
// Cross-platform v2 shared state
//=============================================================================

// Operation registry (global, shared across all clients)
static OPERATION_PROGRESS g_operations[IPC_MAX_OPERATIONS];
static WH_MUTEX g_operations_lock;
static BOOLEAN g_operations_initialized = FALSE;

static void IpcOpsInit(void)
{
    if (!g_operations_initialized) {
        WH_MUTEX_INIT(g_operations_lock);
        memset(g_operations, 0, sizeof(g_operations));
        g_operations_initialized = TRUE;
    }
}

BOOLEAN IpcServer_RegisterOp(uint32_t op_id, uint8_t command)
{
    IpcOpsInit();
    WH_MUTEX_LOCK(g_operations_lock);
    for (int i = 0; i < IPC_MAX_OPERATIONS; i++) {
        if (!g_operations[i].active) {
            memset(&g_operations[i], 0, sizeof(OPERATION_PROGRESS));
            g_operations[i].op_id = op_id;
            g_operations[i].command = command;
            g_operations[i].active = TRUE;
            g_operations[i].start_time = WH_TIMER_NOW();
            WH_MUTEX_UNLOCK(g_operations_lock);
            return TRUE;
        }
    }
    WH_MUTEX_UNLOCK(g_operations_lock);
    return FALSE;
}

void IpcServer_UnregisterOp(uint32_t op_id)
{
    IpcOpsInit();
    WH_MUTEX_LOCK(g_operations_lock);
    for (int i = 0; i < IPC_MAX_OPERATIONS; i++) {
        if (g_operations[i].active && g_operations[i].op_id == op_id) {
            g_operations[i].active = FALSE;
            break;
        }
    }
    WH_MUTEX_UNLOCK(g_operations_lock);
}

OPERATION_PROGRESS *IpcServer_GetOp(uint32_t op_id)
{
    IpcOpsInit();
    for (int i = 0; i < IPC_MAX_OPERATIONS; i++) {
        if (g_operations[i].active && g_operations[i].op_id == op_id) {
            return &g_operations[i];
        }
    }
    return NULL;
}

BOOLEAN IpcServer_IsOpCancelled(uint32_t op_id)
{
    OPERATION_PROGRESS *op = IpcServer_GetOp(op_id);
    if (!op) return FALSE;
    return WH_ATOMIC_LOAD(&op->cancelled) != 0;
}

//=============================================================================
// Subscriber management (cross-platform)
//=============================================================================

// Forward declarations for platform-specific write
#ifdef _WIN32
typedef HANDLE IPC_HANDLE;
#define IPC_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
typedef int IPC_HANDLE;
#define IPC_INVALID_HANDLE (-1)
#endif

typedef struct {
    IPC_HANDLE  handle;
    uint32_t    event_mask;
    BOOLEAN     active;
    BOOLEAN     is_v2;
    WH_MUTEX    write_lock;  // Serialize writes to this client
} IPC_SUBSCRIBER_SLOT;

static IPC_SUBSCRIBER_SLOT g_subscribers[IPC_MAX_SUBSCRIBERS];
static WH_MUTEX g_subscribers_lock;
static BOOLEAN g_subscribers_initialized = FALSE;

static void IpcSubInit(void)
{
    if (!g_subscribers_initialized) {
        WH_MUTEX_INIT(g_subscribers_lock);
        memset(g_subscribers, 0, sizeof(g_subscribers));
        for (int i = 0; i < IPC_MAX_SUBSCRIBERS; i++) {
            g_subscribers[i].handle = IPC_INVALID_HANDLE;
            WH_MUTEX_INIT(g_subscribers[i].write_lock);
        }
        g_subscribers_initialized = TRUE;
    }
}

static int IpcSubRegister(IPC_HANDLE handle, uint32_t event_mask)
{
    IpcSubInit();
    WH_MUTEX_LOCK(g_subscribers_lock);

    // Check if already registered — update mask
    for (int i = 0; i < IPC_MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].active && g_subscribers[i].handle == handle) {
            g_subscribers[i].event_mask = event_mask;
            g_subscribers[i].is_v2 = TRUE;
            WH_MUTEX_UNLOCK(g_subscribers_lock);
            return i;
        }
    }

    // Find empty slot
    for (int i = 0; i < IPC_MAX_SUBSCRIBERS; i++) {
        if (!g_subscribers[i].active) {
            g_subscribers[i].handle = handle;
            g_subscribers[i].event_mask = event_mask;
            g_subscribers[i].active = TRUE;
            g_subscribers[i].is_v2 = TRUE;
            WH_MUTEX_UNLOCK(g_subscribers_lock);
            return i;
        }
    }
    WH_MUTEX_UNLOCK(g_subscribers_lock);
    return -1;
}

static void IpcSubUnregister(IPC_HANDLE handle)
{
    IpcSubInit();
    WH_MUTEX_LOCK(g_subscribers_lock);
    for (int i = 0; i < IPC_MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].active && g_subscribers[i].handle == handle) {
            g_subscribers[i].active = FALSE;
            g_subscribers[i].handle = IPC_INVALID_HANDLE;
            g_subscribers[i].event_mask = 0;
            break;
        }
    }
    WH_MUTEX_UNLOCK(g_subscribers_lock);
}

// Forward declarations for platform-specific event write
#ifdef _WIN32
static BOOLEAN IpcWriteMessageHandle(HANDLE pipe, const uint8_t *body, uint32_t body_len);
#else
static BOOLEAN IpcWriteMessageFd(int fd, const uint8_t *body, uint32_t body_len);
#endif

// Build and write an event message to a specific handle
static BOOLEAN IpcWriteEvent(IPC_HANDLE handle, WH_MUTEX *write_lock,
                              uint8_t event_type, uint32_t op_id,
                              const uint8_t *payload, uint32_t payload_size)
{
    // Event body: [1B IPC_CMD_EVENT][4B op_id][1B event_type][payload]
    uint32_t body_len = 1 + 4 + 1 + payload_size;
    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body) return FALSE;

    body[0] = IPC_CMD_EVENT;
    WriteUint32LE(body + 1, op_id);
    body[5] = event_type;
    if (payload && payload_size > 0)
        memcpy(body + 6, payload, payload_size);

    WH_MUTEX_LOCK(*write_lock);
#ifdef _WIN32
    BOOLEAN ok = IpcWriteMessageHandle(handle, body, body_len);
#else
    BOOLEAN ok = IpcWriteMessageFd(handle, body, body_len);
#endif
    WH_MUTEX_UNLOCK(*write_lock);

    free(body);
    return ok;
}

void IpcServer_PushEvent(uint8_t event_type, uint32_t op_id,
                         const uint8_t *payload, uint32_t payload_size)
{
    IpcSubInit();
    uint8_t mask_bit = (1 << (event_type - 1));

    for (int i = 0; i < IPC_MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].active && (g_subscribers[i].event_mask & mask_bit)) {
            IpcWriteEvent(g_subscribers[i].handle, &g_subscribers[i].write_lock,
                         event_type, op_id, payload, payload_size);
        }
    }
}

void IpcServer_PushEventTo(int client_index, uint8_t event_type,
                           uint32_t op_id, const uint8_t *payload,
                           uint32_t payload_size)
{
    IpcSubInit();
    if (client_index < 0 || client_index >= IPC_MAX_SUBSCRIBERS) return;
    if (!g_subscribers[client_index].active) return;

    IpcWriteEvent(g_subscribers[client_index].handle,
                 &g_subscribers[client_index].write_lock,
                 event_type, op_id, payload, payload_size);
}

void IpcServer_UpdateProgress(uint32_t op_id, uint64_t bytes_done,
                              uint64_t bytes_total, uint32_t chunks_done,
                              uint32_t chunks_total)
{
    OPERATION_PROGRESS *op = IpcServer_GetOp(op_id);
    if (!op) return;

    op->bytes_done = bytes_done;
    op->bytes_total = bytes_total;
    op->chunks_done = chunks_done;
    op->chunks_total = chunks_total;

    // Throttle: max IPC_PROGRESS_THROTTLE_HZ events/sec
    double now = WH_TIMER_NOW();
    double min_interval = 1.0 / IPC_PROGRESS_THROTTLE_HZ;
    if (now - op->last_event_time < min_interval)
        return;
    op->last_event_time = now;

    // Build progress event payload
    uint8_t buf[IPC_PROGRESS_PAYLOAD_SIZE];
    double elapsed = now - op->start_time;
    uint32_t speed_bps = elapsed > 0.1 ? (uint32_t)(bytes_done / elapsed) : 0;
    uint32_t eta_sec = (speed_bps > 0 && bytes_total > bytes_done)
        ? (uint32_t)((bytes_total - bytes_done) / speed_bps)
        : 0;

    WriteUint32LE(buf, op_id);
    WriteUint64LE(buf + 4, bytes_done);
    WriteUint64LE(buf + 12, bytes_total);
    WriteUint32LE(buf + 20, chunks_done);
    WriteUint32LE(buf + 24, chunks_total);
    WriteUint32LE(buf + 28, speed_bps);
    WriteUint32LE(buf + 32, eta_sec);

    IpcServer_PushEvent(IPC_EVENT_PROGRESS, op_id, buf, sizeof(buf));
}

//=============================================================================
// Windows implementation
//=============================================================================
#ifdef _WIN32
#include <sddl.h>

// IPC message header size: 4 bytes (little-endian length)
#define IPC_HEADER_SIZE 4

// --- Internal state ---

typedef struct {
    HANDLE              pipe_handle;
    HANDLE              thread_handle;
    volatile LONG       running;
    volatile LONG       stopping;
    IpcCommandHandler   handler;
    IpcCommandHandlerV2 handler_v2;
    void               *handler_context;
    char                pipe_name[256];
} IPC_SERVER;

static IPC_SERVER g_ipc_server = {
    .pipe_handle      = INVALID_HANDLE_VALUE,
    .thread_handle    = NULL,
    .running          = 0,
    .stopping         = 0,
    .handler          = NULL,
    .handler_v2       = NULL,
    .handler_context  = NULL,
    .pipe_name        = {0},
};

// --- Client handle ---

struct IPC_CLIENT {
    HANDLE pipe_handle;
    BOOLEAN is_v2;       // TRUE after IpcClient_Subscribe
};

// --- Helper: read exactly n bytes from a pipe ---

static BOOLEAN PipeReadExact(HANDLE pipe, uint8_t *buffer, uint32_t count)
{
    uint32_t total_read = 0;
    while (total_read < count)
    {
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, buffer + total_read, count - total_read, &bytes_read, NULL))
            return FALSE;
        if (bytes_read == 0)
            return FALSE;
        total_read += bytes_read;
    }
    return TRUE;
}

// --- Helper: write exactly n bytes to a pipe ---

static BOOLEAN PipeWriteExact(HANDLE pipe, const uint8_t *buffer, uint32_t count)
{
    uint32_t total_written = 0;
    while (total_written < count)
    {
        DWORD bytes_written = 0;
        if (!WriteFile(pipe, buffer + total_written, count - total_written, &bytes_written, NULL))
            return FALSE;
        total_written += bytes_written;
    }
    return TRUE;
}

// --- Helper: read a framed IPC message ---

static BOOLEAN IpcReadMessage(HANDLE pipe, uint8_t **out_buf, uint32_t *out_size)
{
    uint8_t header[IPC_HEADER_SIZE];
    if (!PipeReadExact(pipe, header, IPC_HEADER_SIZE))
        return FALSE;

    uint32_t body_len = ReadUint32LE(header);
    if (body_len == 0 || body_len > IPC_MAX_MESSAGE_SIZE)
    {
        LOG_ERROR("[ipc] Invalid message length: %u\n", body_len);
        return FALSE;
    }

    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body)
    {
        LOG_ERROR("[ipc] Out of memory for message (%u bytes)\n", body_len);
        return FALSE;
    }

    if (!PipeReadExact(pipe, body, body_len))
    {
        free(body);
        return FALSE;
    }

    *out_buf = body;
    *out_size = body_len;
    return TRUE;
}

// --- Helper: write a framed IPC message ---

static BOOLEAN IpcWriteMessageHandle(HANDLE pipe, const uint8_t *body, uint32_t body_len)
{
    uint8_t header[IPC_HEADER_SIZE];
    WriteUint32LE(header, body_len);

    if (!PipeWriteExact(pipe, header, IPC_HEADER_SIZE))
        return FALSE;
    if (body_len > 0 && !PipeWriteExact(pipe, body, body_len))
        return FALSE;
    return TRUE;
}

// Keep old name for backward compatibility within this file
static BOOLEAN IpcWriteMessage(HANDLE pipe, const uint8_t *body, uint32_t body_len)
{
    return IpcWriteMessageHandle(pipe, body, body_len);
}

// --- Server: handle a single client connection ---

static void IpcServerHandleClient(HANDLE client_pipe, IPC_SERVER *server)
{
    uint8_t *response_buf = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
    if (!response_buf)
    {
        LOG_ERROR("[ipc] Out of memory for response buffer\n");
        return;
    }

    BOOLEAN client_is_v2 = FALSE;
    int client_sub_index = -1;

    while (InterlockedCompareExchange(&server->running, 1, 1) == 1)
    {
        if (InterlockedCompareExchange(&server->stopping, 0, 0))
            break;

        uint8_t *msg = NULL;
        uint32_t msg_size = 0;

        if (!IpcReadMessage(client_pipe, &msg, &msg_size))
            break;

        if (InterlockedCompareExchange(&server->stopping, 0, 0))
        {
            free(msg);
            break;
        }

        if (msg_size < 1)
        {
            free(msg);
            continue;
        }

        uint8_t command = msg[0];
        uint32_t op_id = 0;
        const uint8_t *payload;
        uint32_t payload_size;

        if (client_is_v2 && msg_size >= 5) {
            // v2 format: [1B command][4B op_id][payload...]
            op_id = ReadUint32LE(msg + 1);
            payload = msg + 5;
            payload_size = msg_size - 5;
        } else {
            // v1 format: [1B command][payload...]
            payload = msg + 1;
            payload_size = msg_size - 1;
        }

        // Handle SUBSCRIBE specially — switches connection to v2 mode
        if (command == IPC_CMD_SUBSCRIBE) {
            uint32_t event_mask = IPC_EVENT_MASK_ALL;
            if (payload_size >= 4)
                event_mask = ReadUint32LE(payload);
            client_sub_index = IpcSubRegister(client_pipe, event_mask);
            client_is_v2 = TRUE;
            // Respond OK
            if (client_is_v2 && op_id != 0) {
                response_buf[0] = IPC_STATUS_OK;
                WriteUint32LE(response_buf + 1, op_id);
                IpcWriteMessage(client_pipe, response_buf, 5);
            } else {
                response_buf[0] = IPC_STATUS_OK;
                IpcWriteMessage(client_pipe, response_buf, 1);
            }
            FlushFileBuffers(client_pipe);
            free(msg);
            continue;
        }

        // Handle CANCEL specially
        if (command == IPC_CMD_CANCEL) {
            uint32_t cancel_op_id = 0;
            if (payload_size >= 4)
                cancel_op_id = ReadUint32LE(payload);
            OPERATION_PROGRESS *op = IpcServer_GetOp(cancel_op_id);
            if (op) {
                WH_ATOMIC_SET(&op->cancelled, 1);
                response_buf[0] = IPC_STATUS_OK;
            } else {
                response_buf[0] = IPC_STATUS_NOT_FOUND;
            }
            IpcWriteMessage(client_pipe, response_buf, 1);
            FlushFileBuffers(client_pipe);
            free(msg);
            continue;
        }

        // Dispatch to handler
        uint32_t response_size = 0;
        if (server->handler_v2) {
            response_size = server->handler_v2(
                command, op_id, payload, payload_size,
                response_buf, IPC_MAX_MESSAGE_SIZE,
                server->handler_context, client_sub_index
            );
        } else if (server->handler) {
            response_size = server->handler(
                command, payload, payload_size,
                response_buf, IPC_MAX_MESSAGE_SIZE,
                server->handler_context
            );
        }

        free(msg);

        if (response_size > 0)
        {
            if (!IpcWriteMessage(client_pipe, response_buf, response_size))
            {
                LOG_ERROR("[ipc] Failed to send response\n");
                break;
            }
        }

        FlushFileBuffers(client_pipe);
    }

    // Unregister subscriber on disconnect
    if (client_sub_index >= 0)
        IpcSubUnregister(client_pipe);

    free(response_buf);
}

// --- Server: create a new named pipe instance ---

static HANDLE IpcServerCreatePipe(void)
{
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR psd = NULL;
    BOOLEAN have_sd = FALSE;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:(A;;GA;;;OW)",
            SDDL_REVISION_1, &psd, NULL))
    {
        sa.lpSecurityDescriptor = psd;
        have_sd = TRUE;
    }
    else
    {
        LOG_ERROR("[ipc] Failed to create security descriptor: %lu (falling back to default)\n",
                  GetLastError());
    }

    HANDLE pipe = CreateNamedPipeA(
        g_ipc_server.pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        IPC_MAX_MESSAGE_SIZE,
        IPC_MAX_MESSAGE_SIZE,
        0,
        have_sd ? &sa : NULL
    );

    if (psd) LocalFree(psd);

    if (pipe == INVALID_HANDLE_VALUE)
        LOG_ERROR("[ipc] CreateNamedPipe failed: %lu\n", GetLastError());

    return pipe;
}

// --- Server: client handler thread ---

typedef struct {
    HANDLE      client_pipe;
    IPC_SERVER *server;
} CLIENT_THREAD_CONTEXT;

static DWORD WINAPI IpcClientThread(LPVOID param)
{
    CLIENT_THREAD_CONTEXT *ctx = (CLIENT_THREAD_CONTEXT *)param;
    IpcServerHandleClient(ctx->client_pipe, ctx->server);
    DisconnectNamedPipe(ctx->client_pipe);
    CloseHandle(ctx->client_pipe);
    free(ctx);
    return 0;
}

// --- Server: accept loop thread ---

static DWORD WINAPI IpcServerThread(LPVOID param)
{
    IPC_SERVER *server = (IPC_SERVER *)param;
    LOG("[ipc] Server listening on %s\n", g_ipc_server.pipe_name);

    while (InterlockedCompareExchange(&server->running, 1, 1) == 1)
    {
        HANDLE client_pipe = IpcServerCreatePipe();
        if (client_pipe == INVALID_HANDLE_VALUE)
        {
            Sleep(100);
            continue;
        }

        BOOL connected = ConnectNamedPipe(client_pipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
        {
            if (InterlockedCompareExchange(&server->running, 1, 1) != 1)
            {
                CloseHandle(client_pipe);
                break;
            }
            LOG_ERROR("[ipc] ConnectNamedPipe failed: %lu\n", GetLastError());
            CloseHandle(client_pipe);
            continue;
        }

        LOG("[ipc] Client connected\n");

        CLIENT_THREAD_CONTEXT *ctx = (CLIENT_THREAD_CONTEXT *)calloc(1, sizeof(CLIENT_THREAD_CONTEXT));
        if (!ctx)
        {
            LOG_ERROR("[ipc] Out of memory for client context\n");
            DisconnectNamedPipe(client_pipe);
            CloseHandle(client_pipe);
            continue;
        }

        ctx->client_pipe = client_pipe;
        ctx->server = server;

        HANDLE thread = CreateThread(NULL, 0, IpcClientThread, ctx, 0, NULL);
        if (!thread)
        {
            LOG_ERROR("[ipc] CreateThread failed: %lu\n", GetLastError());
            DisconnectNamedPipe(client_pipe);
            CloseHandle(client_pipe);
            free(ctx);
            continue;
        }
        CloseHandle(thread);
    }

    LOG("[ipc] Server thread exiting\n");
    return 0;
}

// --- Server public API ---

static BOOLEAN IpcServerStartInternal(IpcCommandHandler handler_v1,
                                       IpcCommandHandlerV2 handler_v2,
                                       void *context, const char *pipe_name)
{
    if (!handler_v1 && !handler_v2)
    {
        LOG_ERROR("[ipc] NULL handler\n");
        return FALSE;
    }

    if (InterlockedCompareExchange(&g_ipc_server.running, 1, 1) == 1)
    {
        LOG_ERROR("[ipc] Server already running\n");
        return FALSE;
    }

    IpcOpsInit();
    IpcSubInit();

    if (pipe_name)
        strncpy(g_ipc_server.pipe_name, pipe_name, sizeof(g_ipc_server.pipe_name) - 1);
    else
        strncpy(g_ipc_server.pipe_name, IPC_PIPE_NAME, sizeof(g_ipc_server.pipe_name) - 1);
    g_ipc_server.pipe_name[sizeof(g_ipc_server.pipe_name) - 1] = '\0';

    g_ipc_server.handler = handler_v1;
    g_ipc_server.handler_v2 = handler_v2;
    g_ipc_server.handler_context = context;
    InterlockedExchange(&g_ipc_server.stopping, 0);
    InterlockedExchange(&g_ipc_server.running, 1);

    g_ipc_server.thread_handle = CreateThread(
        NULL, 0, IpcServerThread, &g_ipc_server, 0, NULL
    );

    if (!g_ipc_server.thread_handle)
    {
        LOG_ERROR("[ipc] Failed to create server thread: %lu\n", GetLastError());
        InterlockedExchange(&g_ipc_server.running, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name)
{
    return IpcServerStartInternal(handler, NULL, context, pipe_name);
}

BOOLEAN IpcServer_StartV2(IpcCommandHandlerV2 handler_v2,
                          IpcCommandHandler handler_v1,
                          void *context, const char *pipe_name)
{
    return IpcServerStartInternal(handler_v1, handler_v2, context, pipe_name);
}

void IpcServer_Stop(void)
{
    if (InterlockedCompareExchange(&g_ipc_server.running, 0, 1) != 1)
        return;

    LOG("[ipc] Stopping server...\n");

    InterlockedExchange(&g_ipc_server.stopping, 1);

    HANDLE dummy = CreateFileA(
        g_ipc_server.pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );
    if (dummy != INVALID_HANDLE_VALUE)
        CloseHandle(dummy);

    if (g_ipc_server.thread_handle)
    {
        WaitForSingleObject(g_ipc_server.thread_handle, 5000);
        CloseHandle(g_ipc_server.thread_handle);
        g_ipc_server.thread_handle = NULL;
    }

    Sleep(100);

    g_ipc_server.handler = NULL;
    g_ipc_server.handler_v2 = NULL;
    g_ipc_server.handler_context = NULL;
    InterlockedExchange(&g_ipc_server.stopping, 0);

    LOG("[ipc] Server stopped\n");
}

BOOLEAN IpcServer_IsRunning(void)
{
    return InterlockedCompareExchange(&g_ipc_server.running, 0, 0) == 1 ? TRUE : FALSE;
}

// --- Client public API ---

IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name)
{
    const char *name = pipe_name ? pipe_name : IPC_PIPE_NAME;

    HANDLE pipe = CreateFileA(
        name, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (pipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY)
            LOG_ERROR("[ipc] Daemon not running (pipe not found: %s)\n", name);
        else
            LOG_ERROR("[ipc] Failed to connect to pipe: %lu\n", err);
        return NULL;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, NULL, NULL);

    IPC_CLIENT *client = (IPC_CLIENT *)calloc(1, sizeof(IPC_CLIENT));
    if (!client)
    {
        CloseHandle(pipe);
        LOG_ERROR("[ipc] Out of memory for client\n");
        return NULL;
    }

    client->pipe_handle = pipe;
    client->is_v2 = FALSE;
    return client;
}

IPC_CLIENT *IpcClient_Connect(void)
{
    return IpcClient_ConnectTo(NULL);
}

BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client, uint8_t command,
    const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity,
    uint32_t *response_size_out)
{
    if (!client || client->pipe_handle == INVALID_HANDLE_VALUE)
        return FALSE;

    uint32_t msg_len = 1 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;

    msg[0] = command;
    if (payload && payload_size > 0)
        memcpy(msg + 1, payload, payload_size);

    BOOLEAN ok = IpcWriteMessage(client->pipe_handle, msg, msg_len);
    free(msg);
    if (!ok) return FALSE;

    FlushFileBuffers(client->pipe_handle);

    uint8_t *resp = NULL;
    uint32_t resp_size = 0;
    if (!IpcReadMessage(client->pipe_handle, &resp, &resp_size))
        return FALSE;

    if (resp_size > response_capacity)
    {
        LOG_ERROR("[ipc] Response too large (%u > %u)\n", resp_size, response_capacity);
        free(resp);
        return FALSE;
    }

    memcpy(response_out, resp, resp_size);
    *response_size_out = resp_size;
    free(resp);
    return TRUE;
}

BOOLEAN IpcClient_SendCommandV2(
    IPC_CLIENT *client, uint8_t command, uint32_t op_id,
    const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity,
    uint32_t *response_size_out)
{
    if (!client || client->pipe_handle == INVALID_HANDLE_VALUE)
        return FALSE;

    // v2 body: [1B command][4B op_id][payload]
    uint32_t msg_len = 1 + 4 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;

    msg[0] = command;
    WriteUint32LE(msg + 1, op_id);
    if (payload && payload_size > 0)
        memcpy(msg + 5, payload, payload_size);

    BOOLEAN ok = IpcWriteMessage(client->pipe_handle, msg, msg_len);
    free(msg);
    if (!ok) return FALSE;

    FlushFileBuffers(client->pipe_handle);

    uint8_t *resp = NULL;
    uint32_t resp_size = 0;
    if (!IpcReadMessage(client->pipe_handle, &resp, &resp_size))
        return FALSE;

    if (resp_size > response_capacity)
    {
        free(resp);
        return FALSE;
    }

    memcpy(response_out, resp, resp_size);
    *response_size_out = resp_size;
    free(resp);
    return TRUE;
}

BOOLEAN IpcClient_Subscribe(IPC_CLIENT *client, uint32_t event_mask)
{
    if (!client) return FALSE;

    uint8_t payload[4];
    WriteUint32LE(payload, event_mask);

    uint8_t response[16];
    uint32_t resp_size = 0;
    BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_SUBSCRIBE,
                                        payload, 4, response, sizeof(response), &resp_size);
    if (ok && resp_size >= 1 && response[0] == IPC_STATUS_OK) {
        client->is_v2 = TRUE;
        return TRUE;
    }
    return FALSE;
}

BOOLEAN IpcClient_ReadEvent(
    IPC_CLIENT *client, uint8_t *event_type_out, uint32_t *op_id_out,
    uint8_t *payload_out, uint32_t payload_capacity,
    uint32_t *payload_size_out, uint32_t timeout_ms)
{
    if (!client || client->pipe_handle == INVALID_HANDLE_VALUE)
        return FALSE;

    // For Windows, we can use a timeout on the pipe read by setting COMMTIMEOUTS
    // For simplicity, use a polling approach with PeekNamedPipe
    DWORD start = GetTickCount();
    while (1) {
        DWORD available = 0;
        if (PeekNamedPipe(client->pipe_handle, NULL, 0, NULL, &available, NULL) && available > 0)
            break;
        DWORD elapsed = GetTickCount() - start;
        if (timeout_ms != 0xFFFFFFFF && elapsed >= timeout_ms)
            return FALSE;
        Sleep(10);
    }

    uint8_t *msg = NULL;
    uint32_t msg_size = 0;
    if (!IpcReadMessage(client->pipe_handle, &msg, &msg_size))
        return FALSE;

    // Event format: [1B IPC_CMD_EVENT][4B op_id][1B event_type][payload]
    if (msg_size < 6 || msg[0] != IPC_CMD_EVENT) {
        free(msg);
        return FALSE;
    }

    *op_id_out = ReadUint32LE(msg + 1);
    *event_type_out = msg[5];
    uint32_t event_payload_size = msg_size - 6;
    if (event_payload_size > payload_capacity)
        event_payload_size = payload_capacity;
    if (event_payload_size > 0)
        memcpy(payload_out, msg + 6, event_payload_size);
    *payload_size_out = event_payload_size;

    free(msg);
    return TRUE;
}

BOOLEAN IpcClient_CancelOp(IPC_CLIENT *client, uint32_t op_id)
{
    uint8_t payload[4];
    WriteUint32LE(payload, op_id);

    uint8_t response[16];
    uint32_t resp_size = 0;
    return IpcClient_SendCommand(client, IPC_CMD_CANCEL,
                                  payload, 4, response, sizeof(response), &resp_size);
}

void IpcClient_Disconnect(IPC_CLIENT *client)
{
    if (!client) return;
    if (client->pipe_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(client->pipe_handle);
        client->pipe_handle = INVALID_HANDLE_VALUE;
    }
    free(client);
}

#else
// ===================================================================
// POSIX implementation — Unix domain sockets
// ===================================================================

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

// IPC message header size: 4 bytes (little-endian length) — same as Windows
#define IPC_HEADER_SIZE 4

// --- Helper: build socket path from pipe_name argument ---
static BOOLEAN IpcBuildSocketPath(const char *pipe_name, char *path_out, size_t path_len)
{
    const char *home = getenv("HOME");
    if (!home) return FALSE;

    if (!pipe_name || pipe_name[0] == '\0')
    {
        snprintf(path_out, path_len, "%s/.wormhole/%s", home, IPC_SOCKET_NAME);
    }
    else
    {
        if (pipe_name[0] == '/')
            snprintf(path_out, path_len, "%s", pipe_name);
        else
            snprintf(path_out, path_len, "%s/.wormhole/%s.sock", home, pipe_name);
    }
    return TRUE;
}

// --- Internal state ---

typedef struct {
    int                 listen_fd;
    pthread_t           thread;
    volatile int32_t    running;
    volatile int32_t    stopping;
    IpcCommandHandler   handler;
    IpcCommandHandlerV2 handler_v2;
    void               *handler_context;
    char                socket_path[256];
} IPC_SERVER;

static IPC_SERVER g_ipc_server = {
    .listen_fd       = -1,
    .running         = 0,
    .stopping        = 0,
    .handler         = NULL,
    .handler_v2      = NULL,
    .handler_context = NULL,
    .socket_path     = {0},
};

// --- Client handle ---

struct IPC_CLIENT {
    int fd;
    BOOLEAN is_v2;
};

// --- Helper: read exactly n bytes from a socket ---

static BOOLEAN SockReadExact(int fd, uint8_t *buffer, uint32_t count)
{
    uint32_t total_read = 0;
    while (total_read < count)
    {
        ssize_t n = read(fd, buffer + total_read, count - total_read);
        if (n <= 0) return FALSE;
        total_read += (uint32_t)n;
    }
    return TRUE;
}

// --- Helper: write exactly n bytes to a socket ---

static BOOLEAN SockWriteExact(int fd, const uint8_t *buffer, uint32_t count)
{
    uint32_t total_written = 0;
    while (total_written < count)
    {
        ssize_t n = write(fd, buffer + total_written, count - total_written);
        if (n <= 0) return FALSE;
        total_written += (uint32_t)n;
    }
    return TRUE;
}

// --- Helper: read a framed IPC message ---

static BOOLEAN IpcReadMessage(int fd, uint8_t **out_buf, uint32_t *out_size)
{
    uint8_t header[IPC_HEADER_SIZE];
    if (!SockReadExact(fd, header, IPC_HEADER_SIZE))
        return FALSE;

    uint32_t body_len = ReadUint32LE(header);
    if (body_len == 0 || body_len > IPC_MAX_MESSAGE_SIZE)
    {
        LOG_ERROR("[ipc] Invalid message length: %u\n", body_len);
        return FALSE;
    }

    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body) return FALSE;

    if (!SockReadExact(fd, body, body_len))
    {
        free(body);
        return FALSE;
    }

    *out_buf = body;
    *out_size = body_len;
    return TRUE;
}

// --- Helper: write a framed IPC message ---

static BOOLEAN IpcWriteMessageFd(int fd, const uint8_t *body, uint32_t body_len)
{
    uint8_t header[IPC_HEADER_SIZE];
    WriteUint32LE(header, body_len);

    if (!SockWriteExact(fd, header, IPC_HEADER_SIZE))
        return FALSE;
    if (body_len > 0 && !SockWriteExact(fd, body, body_len))
        return FALSE;
    return TRUE;
}

// Keep old name for backward compatibility within this file
static BOOLEAN IpcWriteMessage(int fd, const uint8_t *body, uint32_t body_len)
{
    return IpcWriteMessageFd(fd, body, body_len);
}

// --- Server: handle a single client connection ---

static void IpcServerHandleClient(int client_fd, IPC_SERVER *server)
{
    uint8_t *response_buf = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
    if (!response_buf) return;

    BOOLEAN client_is_v2 = FALSE;
    int client_sub_index = -1;

    while (__atomic_load_n(&server->running, __ATOMIC_SEQ_CST) == 1)
    {
        if (__atomic_load_n(&server->stopping, __ATOMIC_SEQ_CST))
            break;

        uint8_t *msg = NULL;
        uint32_t msg_size = 0;

        if (!IpcReadMessage(client_fd, &msg, &msg_size))
            break;

        if (__atomic_load_n(&server->stopping, __ATOMIC_SEQ_CST))
        {
            free(msg);
            break;
        }

        if (msg_size < 1)
        {
            free(msg);
            continue;
        }

        uint8_t command = msg[0];
        uint32_t op_id = 0;
        const uint8_t *payload;
        uint32_t payload_size;

        if (client_is_v2 && msg_size >= 5) {
            op_id = ReadUint32LE(msg + 1);
            payload = msg + 5;
            payload_size = msg_size - 5;
        } else {
            payload = msg + 1;
            payload_size = msg_size - 1;
        }

        // Handle SUBSCRIBE
        if (command == IPC_CMD_SUBSCRIBE) {
            uint32_t event_mask = IPC_EVENT_MASK_ALL;
            if (payload_size >= 4)
                event_mask = ReadUint32LE(payload);
            client_sub_index = IpcSubRegister(client_fd, event_mask);
            client_is_v2 = TRUE;
            if (client_is_v2 && op_id != 0) {
                response_buf[0] = IPC_STATUS_OK;
                WriteUint32LE(response_buf + 1, op_id);
                IpcWriteMessage(client_fd, response_buf, 5);
            } else {
                response_buf[0] = IPC_STATUS_OK;
                IpcWriteMessage(client_fd, response_buf, 1);
            }
            free(msg);
            continue;
        }

        // Handle CANCEL
        if (command == IPC_CMD_CANCEL) {
            uint32_t cancel_op_id = 0;
            if (payload_size >= 4)
                cancel_op_id = ReadUint32LE(payload);
            OPERATION_PROGRESS *op = IpcServer_GetOp(cancel_op_id);
            if (op) {
                WH_ATOMIC_SET(&op->cancelled, 1);
                response_buf[0] = IPC_STATUS_OK;
            } else {
                response_buf[0] = IPC_STATUS_NOT_FOUND;
            }
            IpcWriteMessage(client_fd, response_buf, 1);
            free(msg);
            continue;
        }

        // Dispatch to handler
        uint32_t response_size = 0;
        if (server->handler_v2) {
            response_size = server->handler_v2(
                command, op_id, payload, payload_size,
                response_buf, IPC_MAX_MESSAGE_SIZE,
                server->handler_context, client_sub_index
            );
        } else if (server->handler) {
            response_size = server->handler(
                command, payload, payload_size,
                response_buf, IPC_MAX_MESSAGE_SIZE,
                server->handler_context
            );
        }

        free(msg);

        if (response_size > 0)
        {
            if (!IpcWriteMessage(client_fd, response_buf, response_size))
                break;
        }
    }

    // Unregister subscriber on disconnect
    if (client_sub_index >= 0)
        IpcSubUnregister(client_fd);

    free(response_buf);
}

// --- Server: per-client thread ---

typedef struct {
    int          client_fd;
    IPC_SERVER  *server;
} CLIENT_THREAD_CONTEXT;

static void *IpcClientThread(void *param)
{
    CLIENT_THREAD_CONTEXT *ctx = (CLIENT_THREAD_CONTEXT *)param;
    IpcServerHandleClient(ctx->client_fd, ctx->server);
    close(ctx->client_fd);
    free(ctx);
    return NULL;
}

// --- Server: accept loop thread ---

static void *IpcServerThread(void *param)
{
    IPC_SERVER *server = (IPC_SERVER *)param;
    LOG("[ipc] Server listening on %s\n", server->socket_path);

    while (__atomic_load_n(&server->running, __ATOMIC_SEQ_CST) == 1)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server->listen_fd, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int ready = select(server->listen_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (__atomic_load_n(&server->running, __ATOMIC_SEQ_CST) != 1)
                break;
            continue;
        }

        LOG("[ipc] Client connected\n");

        CLIENT_THREAD_CONTEXT *ctx = (CLIENT_THREAD_CONTEXT *)calloc(1, sizeof(CLIENT_THREAD_CONTEXT));
        if (!ctx) { close(client_fd); continue; }

        ctx->client_fd = client_fd;
        ctx->server = server;

        pthread_t thread;
        if (pthread_create(&thread, NULL, IpcClientThread, ctx) != 0)
        {
            close(client_fd);
            free(ctx);
            continue;
        }
        pthread_detach(thread);
    }

    LOG("[ipc] Server thread exiting\n");
    return NULL;
}

// --- Server public API ---

static BOOLEAN IpcServerStartInternal(IpcCommandHandler handler_v1,
                                       IpcCommandHandlerV2 handler_v2,
                                       void *context, const char *pipe_name)
{
    if (!handler_v1 && !handler_v2)
    {
        LOG_ERROR("[ipc] NULL handler\n");
        return FALSE;
    }

    if (__atomic_load_n(&g_ipc_server.running, __ATOMIC_SEQ_CST) == 1)
    {
        LOG_ERROR("[ipc] Server already running\n");
        return FALSE;
    }

    IpcOpsInit();
    IpcSubInit();

    if (!IpcBuildSocketPath(pipe_name, g_ipc_server.socket_path,
                             sizeof(g_ipc_server.socket_path)))
    {
        LOG_ERROR("[ipc] Failed to build socket path\n");
        return FALSE;
    }

    unlink(g_ipc_server.socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOG_ERROR("[ipc] socket() failed: %s\n", strerror(errno));
        return FALSE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_ipc_server.socket_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("[ipc] bind() failed on %s: %s\n",
                  g_ipc_server.socket_path, strerror(errno));
        close(fd);
        return FALSE;
    }

    chmod(g_ipc_server.socket_path, 0600);

    if (listen(fd, 5) < 0)
    {
        LOG_ERROR("[ipc] listen() failed: %s\n", strerror(errno));
        close(fd);
        unlink(g_ipc_server.socket_path);
        return FALSE;
    }

    g_ipc_server.listen_fd = fd;
    g_ipc_server.handler = handler_v1;
    g_ipc_server.handler_v2 = handler_v2;
    g_ipc_server.handler_context = context;
    __atomic_store_n(&g_ipc_server.stopping, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_ipc_server.running, 1, __ATOMIC_SEQ_CST);

    if (pthread_create(&g_ipc_server.thread, NULL, IpcServerThread, &g_ipc_server) != 0)
    {
        LOG_ERROR("[ipc] Failed to create server thread\n");
        __atomic_store_n(&g_ipc_server.running, 0, __ATOMIC_SEQ_CST);
        close(fd);
        unlink(g_ipc_server.socket_path);
        g_ipc_server.listen_fd = -1;
        return FALSE;
    }

    return TRUE;
}

BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name)
{
    return IpcServerStartInternal(handler, NULL, context, pipe_name);
}

BOOLEAN IpcServer_StartV2(IpcCommandHandlerV2 handler_v2,
                          IpcCommandHandler handler_v1,
                          void *context, const char *pipe_name)
{
    return IpcServerStartInternal(handler_v1, handler_v2, context, pipe_name);
}

void IpcServer_Stop(void)
{
    if (WH_ATOMIC_CAS(&g_ipc_server.running, 1, 0) != 1)
        return;

    LOG("[ipc] Stopping server...\n");

    __atomic_store_n(&g_ipc_server.stopping, 1, __ATOMIC_SEQ_CST);

    if (g_ipc_server.listen_fd >= 0)
    {
        close(g_ipc_server.listen_fd);
        g_ipc_server.listen_fd = -1;
    }

    pthread_join(g_ipc_server.thread, NULL);

    usleep(100000);

    if (g_ipc_server.socket_path[0])
        unlink(g_ipc_server.socket_path);

    g_ipc_server.handler = NULL;
    g_ipc_server.handler_v2 = NULL;
    g_ipc_server.handler_context = NULL;
    __atomic_store_n(&g_ipc_server.stopping, 0, __ATOMIC_SEQ_CST);

    LOG("[ipc] Server stopped\n");
}

BOOLEAN IpcServer_IsRunning(void)
{
    return __atomic_load_n(&g_ipc_server.running, __ATOMIC_SEQ_CST) == 1 ? TRUE : FALSE;
}

// --- Client public API ---

IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name)
{
    char socket_path[256];
    if (!IpcBuildSocketPath(pipe_name, socket_path, sizeof(socket_path)))
    {
        LOG_ERROR("[ipc] Failed to build socket path\n");
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOG_ERROR("[ipc] socket() failed: %s\n", strerror(errno));
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        if (errno == ENOENT || errno == ECONNREFUSED)
            LOG_ERROR("[ipc] Daemon not running (socket not found: %s)\n", socket_path);
        else
            LOG_ERROR("[ipc] connect() failed: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    IPC_CLIENT *client = (IPC_CLIENT *)calloc(1, sizeof(IPC_CLIENT));
    if (!client)
    {
        close(fd);
        return NULL;
    }

    client->fd = fd;
    client->is_v2 = FALSE;
    return client;
}

IPC_CLIENT *IpcClient_Connect(void)
{
    return IpcClient_ConnectTo(NULL);
}

BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client, uint8_t command,
    const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity,
    uint32_t *response_size_out)
{
    if (!client || client->fd < 0)
        return FALSE;

    uint32_t msg_len = 1 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;

    msg[0] = command;
    if (payload && payload_size > 0)
        memcpy(msg + 1, payload, payload_size);

    BOOLEAN ok = IpcWriteMessage(client->fd, msg, msg_len);
    free(msg);
    if (!ok) return FALSE;

    uint8_t *resp = NULL;
    uint32_t resp_size = 0;
    if (!IpcReadMessage(client->fd, &resp, &resp_size))
        return FALSE;

    if (resp_size > response_capacity)
    {
        free(resp);
        return FALSE;
    }

    memcpy(response_out, resp, resp_size);
    *response_size_out = resp_size;
    free(resp);
    return TRUE;
}

BOOLEAN IpcClient_SendCommandV2(
    IPC_CLIENT *client, uint8_t command, uint32_t op_id,
    const uint8_t *payload, uint32_t payload_size,
    uint8_t *response_out, uint32_t response_capacity,
    uint32_t *response_size_out)
{
    if (!client || client->fd < 0)
        return FALSE;

    uint32_t msg_len = 1 + 4 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;

    msg[0] = command;
    WriteUint32LE(msg + 1, op_id);
    if (payload && payload_size > 0)
        memcpy(msg + 5, payload, payload_size);

    BOOLEAN ok = IpcWriteMessage(client->fd, msg, msg_len);
    free(msg);
    if (!ok) return FALSE;

    uint8_t *resp = NULL;
    uint32_t resp_size = 0;
    if (!IpcReadMessage(client->fd, &resp, &resp_size))
        return FALSE;

    if (resp_size > response_capacity)
    {
        free(resp);
        return FALSE;
    }

    memcpy(response_out, resp, resp_size);
    *response_size_out = resp_size;
    free(resp);
    return TRUE;
}

BOOLEAN IpcClient_Subscribe(IPC_CLIENT *client, uint32_t event_mask)
{
    if (!client) return FALSE;

    uint8_t payload[4];
    WriteUint32LE(payload, event_mask);

    uint8_t response[16];
    uint32_t resp_size = 0;
    BOOLEAN ok = IpcClient_SendCommand(client, IPC_CMD_SUBSCRIBE,
                                        payload, 4, response, sizeof(response), &resp_size);
    if (ok && resp_size >= 1 && response[0] == IPC_STATUS_OK) {
        client->is_v2 = TRUE;
        return TRUE;
    }
    return FALSE;
}

BOOLEAN IpcClient_ReadEvent(
    IPC_CLIENT *client, uint8_t *event_type_out, uint32_t *op_id_out,
    uint8_t *payload_out, uint32_t payload_capacity,
    uint32_t *payload_size_out, uint32_t timeout_ms)
{
    if (!client || client->fd < 0)
        return FALSE;

    // Use poll() for timeout
    struct pollfd pfd = { .fd = client->fd, .events = POLLIN };
    int timeout = (timeout_ms == 0xFFFFFFFF) ? -1 : (int)timeout_ms;
    int ret = poll(&pfd, 1, timeout);
    if (ret <= 0)
        return FALSE;

    uint8_t *msg = NULL;
    uint32_t msg_size = 0;
    if (!IpcReadMessage(client->fd, &msg, &msg_size))
        return FALSE;

    // Event format: [1B IPC_CMD_EVENT][4B op_id][1B event_type][payload]
    if (msg_size < 6 || msg[0] != IPC_CMD_EVENT) {
        free(msg);
        return FALSE;
    }

    *op_id_out = ReadUint32LE(msg + 1);
    *event_type_out = msg[5];
    uint32_t event_payload_size = msg_size - 6;
    if (event_payload_size > payload_capacity)
        event_payload_size = payload_capacity;
    if (event_payload_size > 0)
        memcpy(payload_out, msg + 6, event_payload_size);
    *payload_size_out = event_payload_size;

    free(msg);
    return TRUE;
}

BOOLEAN IpcClient_CancelOp(IPC_CLIENT *client, uint32_t op_id)
{
    uint8_t payload[4];
    WriteUint32LE(payload, op_id);

    uint8_t response[16];
    uint32_t resp_size = 0;
    return IpcClient_SendCommand(client, IPC_CMD_CANCEL,
                                  payload, 4, response, sizeof(response), &resp_size);
}

void IpcClient_Disconnect(IPC_CLIENT *client)
{
    if (!client) return;
    if (client->fd >= 0)
        close(client->fd);
    free(client);
}

#endif // _WIN32
