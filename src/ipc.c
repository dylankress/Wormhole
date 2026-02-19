//
// ipc.c
// Wormhole - Named pipe IPC implementation (Windows).
// by Dylan Kress
//

#include "ipc.h"
#include "wire_format.h"

#ifdef _WIN32

// IPC message header size: 4 bytes (little-endian length)
#define IPC_HEADER_SIZE 4

// --- Internal state ---

typedef struct {
    HANDLE              pipe_handle;
    HANDLE              thread_handle;
    volatile LONG       running;
    volatile LONG       stopping;
    IpcCommandHandler   handler;
    void               *handler_context;
    char                pipe_name[256];
} IPC_SERVER;

static IPC_SERVER g_ipc_server = {
    .pipe_handle      = INVALID_HANDLE_VALUE,
    .thread_handle    = NULL,
    .running          = 0,
    .stopping         = 0,
    .handler          = NULL,
    .handler_context  = NULL,
    .pipe_name        = {0},
};

// --- Client handle ---

struct IPC_CLIENT {
    HANDLE pipe_handle;
};

// --- Helper: read exactly n bytes from a pipe ---

static BOOLEAN PipeReadExact(HANDLE pipe, uint8_t *buffer, uint32_t count)
{
    uint32_t total_read = 0;
    while (total_read < count)
    {
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, buffer + total_read, count - total_read, &bytes_read, NULL))
        {
            return FALSE;
        }
        if (bytes_read == 0)
        {
            return FALSE;  // Pipe closed
        }
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
        {
            return FALSE;
        }
        total_written += bytes_written;
    }
    return TRUE;
}

// --- Helper: read a framed IPC message ---
// Returns the message body (command + payload) in *out_buf (caller must free).
// Sets *out_size to the body length. Returns FALSE on error.

static BOOLEAN IpcReadMessage(HANDLE pipe, uint8_t **out_buf, uint32_t *out_size)
{
    // Read 4-byte length header
    uint8_t header[IPC_HEADER_SIZE];
    if (!PipeReadExact(pipe, header, IPC_HEADER_SIZE))
    {
        return FALSE;
    }

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

static BOOLEAN IpcWriteMessage(HANDLE pipe, const uint8_t *body, uint32_t body_len)
{
    uint8_t header[IPC_HEADER_SIZE];
    WriteUint32LE(header, body_len);

    if (!PipeWriteExact(pipe, header, IPC_HEADER_SIZE))
    {
        return FALSE;
    }
    if (body_len > 0 && !PipeWriteExact(pipe, body, body_len))
    {
        return FALSE;
    }
    return TRUE;
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

    // Process commands until the client disconnects or server stops
    while (InterlockedCompareExchange(&server->running, 1, 1) == 1)
    {
        // Check if server is shutting down
        if (InterlockedCompareExchange(&server->stopping, 0, 0))
        {
            break;
        }

        uint8_t *msg = NULL;
        uint32_t msg_size = 0;

        if (!IpcReadMessage(client_pipe, &msg, &msg_size))
        {
            break;  // Client disconnected or read error
        }

        // Re-check stopping flag after blocking read
        if (InterlockedCompareExchange(&server->stopping, 0, 0))
        {
            free(msg);
            break;
        }

        if (msg_size < 1)
        {
            free(msg);
            continue;  // Need at least the command byte
        }

        uint8_t command = msg[0];
        const uint8_t *payload = msg + 1;
        uint32_t payload_size = msg_size - 1;

        // Dispatch to handler
        uint32_t response_size = server->handler(
            command,
            payload,
            payload_size,
            response_buf,
            IPC_MAX_MESSAGE_SIZE,
            server->handler_context
        );

        free(msg);

        // Send response
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

    free(response_buf);
}

// --- Server: create a new named pipe instance ---

static HANDLE IpcServerCreatePipe(void)
{
    HANDLE pipe = CreateNamedPipeA(
        g_ipc_server.pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        IPC_MAX_MESSAGE_SIZE,  // Output buffer size
        IPC_MAX_MESSAGE_SIZE,  // Input buffer size
        0,                     // Default timeout
        NULL                   // Default security
    );

    if (pipe == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("[ipc] CreateNamedPipe failed: %lu\n", GetLastError());
    }

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
        // Create a new pipe instance for the next client
        HANDLE client_pipe = IpcServerCreatePipe();
        if (client_pipe == INVALID_HANDLE_VALUE)
        {
            Sleep(100);  // Brief backoff on error
            continue;
        }

        // Wait for a client to connect
        BOOL connected = ConnectNamedPipe(client_pipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
        {
            // Check if we're shutting down
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

        // Spawn a thread to handle this client
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

        CloseHandle(thread);  // We don't need to wait on client threads
    }

    LOG("[ipc] Server thread exiting\n");
    return 0;
}

// --- Server public API ---

BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name)
{
    if (!handler)
    {
        LOG_ERROR("[ipc] NULL handler\n");
        return FALSE;
    }

    if (InterlockedCompareExchange(&g_ipc_server.running, 1, 1) == 1)
    {
        LOG_ERROR("[ipc] Server already running\n");
        return FALSE;
    }

    // Store pipe name (use default if NULL)
    if (pipe_name)
        strncpy(g_ipc_server.pipe_name, pipe_name, sizeof(g_ipc_server.pipe_name) - 1);
    else
        strncpy(g_ipc_server.pipe_name, IPC_PIPE_NAME, sizeof(g_ipc_server.pipe_name) - 1);
    g_ipc_server.pipe_name[sizeof(g_ipc_server.pipe_name) - 1] = '\0';

    g_ipc_server.handler = handler;
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

void IpcServer_Stop(void)
{
    if (InterlockedCompareExchange(&g_ipc_server.running, 0, 1) != 1)
    {
        return;  // Not running
    }

    LOG("[ipc] Stopping server...\n");

    // Signal all handler threads to stop before closing handles
    InterlockedExchange(&g_ipc_server.stopping, 1);

    // Connect to our own pipe to unblock ConnectNamedPipe
    HANDLE dummy = CreateFileA(
        g_ipc_server.pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        0, NULL
    );
    if (dummy != INVALID_HANDLE_VALUE)
    {
        CloseHandle(dummy);
    }

    // Wait for server thread to exit
    if (g_ipc_server.thread_handle)
    {
        WaitForSingleObject(g_ipc_server.thread_handle, 5000);
        CloseHandle(g_ipc_server.thread_handle);
        g_ipc_server.thread_handle = NULL;
    }

    // Brief wait for in-flight client handler threads to notice stopping flag
    Sleep(100);

    g_ipc_server.handler = NULL;
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
        name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY)
        {
            LOG_ERROR("[ipc] Daemon not running (pipe not found: %s)\n", name);
        }
        else
        {
            LOG_ERROR("[ipc] Failed to connect to pipe: %lu\n", err);
        }
        return NULL;
    }

    // Set pipe to byte-read mode
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
    return client;
}

IPC_CLIENT *IpcClient_Connect(void)
{
    return IpcClient_ConnectTo(NULL);
}

BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client,
    uint8_t     command,
    const uint8_t *payload,
    uint32_t    payload_size,
    uint8_t    *response_out,
    uint32_t    response_capacity,
    uint32_t   *response_size_out)
{
    if (!client || client->pipe_handle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    // Build message: [command][payload]
    uint32_t msg_len = 1 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg)
    {
        LOG_ERROR("[ipc] Out of memory for command message\n");
        return FALSE;
    }

    msg[0] = command;
    if (payload && payload_size > 0)
    {
        memcpy(msg + 1, payload, payload_size);
    }

    // Send framed message
    BOOLEAN ok = IpcWriteMessage(client->pipe_handle, msg, msg_len);
    free(msg);

    if (!ok)
    {
        LOG_ERROR("[ipc] Failed to send command\n");
        return FALSE;
    }

    FlushFileBuffers(client->pipe_handle);

    // Read framed response
    uint8_t *resp = NULL;
    uint32_t resp_size = 0;
    if (!IpcReadMessage(client->pipe_handle, &resp, &resp_size))
    {
        LOG_ERROR("[ipc] Failed to read response\n");
        return FALSE;
    }

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

void IpcClient_Disconnect(IPC_CLIENT *client)
{
    if (!client)
    {
        return;
    }

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
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

// IPC message header size: 4 bytes (little-endian length) — same as Windows
#define IPC_HEADER_SIZE 4

// --- Helper: build socket path from pipe_name argument ---
// pipe_name is either NULL (default), a bare name like "wormhole_4567",
// or the IPC_SOCKET_PREFIX + port string from wormholed.c.
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
        // pipe_name could be "wormhole_4567" (prefix+port) or a full path
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
    void               *handler_context;
    char                socket_path[256];
} IPC_SERVER;

static IPC_SERVER g_ipc_server = {
    .listen_fd       = -1,
    .running         = 0,
    .stopping        = 0,
    .handler         = NULL,
    .handler_context = NULL,
    .socket_path     = {0},
};

// --- Client handle ---

struct IPC_CLIENT {
    int fd;
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

static BOOLEAN IpcWriteMessage(int fd, const uint8_t *body, uint32_t body_len)
{
    uint8_t header[IPC_HEADER_SIZE];
    WriteUint32LE(header, body_len);

    if (!SockWriteExact(fd, header, IPC_HEADER_SIZE))
        return FALSE;
    if (body_len > 0 && !SockWriteExact(fd, body, body_len))
        return FALSE;
    return TRUE;
}

// --- Server: handle a single client connection ---

static void IpcServerHandleClient(int client_fd, IPC_SERVER *server)
{
    uint8_t *response_buf = (uint8_t *)malloc(IPC_MAX_MESSAGE_SIZE);
    if (!response_buf) return;

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
        const uint8_t *payload = msg + 1;
        uint32_t payload_size = msg_size - 1;

        uint32_t response_size = server->handler(
            command, payload, payload_size,
            response_buf, IPC_MAX_MESSAGE_SIZE,
            server->handler_context
        );

        free(msg);

        if (response_size > 0)
        {
            if (!IpcWriteMessage(client_fd, response_buf, response_size))
                break;
        }
    }

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
        // Use a timeout via poll/select to periodically check running flag
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
        if (!ctx)
        {
            close(client_fd);
            continue;
        }

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

BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name)
{
    if (!handler)
    {
        LOG_ERROR("[ipc] NULL handler\n");
        return FALSE;
    }

    if (__atomic_load_n(&g_ipc_server.running, __ATOMIC_SEQ_CST) == 1)
    {
        LOG_ERROR("[ipc] Server already running\n");
        return FALSE;
    }

    // Build socket path
    if (!IpcBuildSocketPath(pipe_name, g_ipc_server.socket_path,
                             sizeof(g_ipc_server.socket_path)))
    {
        LOG_ERROR("[ipc] Failed to build socket path\n");
        return FALSE;
    }

    // Remove stale socket file
    unlink(g_ipc_server.socket_path);

    // Create and bind Unix domain socket
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

    if (listen(fd, 5) < 0)
    {
        LOG_ERROR("[ipc] listen() failed: %s\n", strerror(errno));
        close(fd);
        unlink(g_ipc_server.socket_path);
        return FALSE;
    }

    g_ipc_server.listen_fd = fd;
    g_ipc_server.handler = handler;
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

void IpcServer_Stop(void)
{
    if (WH_ATOMIC_CAS(&g_ipc_server.running, 1, 0) != 1)
        return;

    LOG("[ipc] Stopping server...\n");

    __atomic_store_n(&g_ipc_server.stopping, 1, __ATOMIC_SEQ_CST);

    // Close listen fd to unblock accept()
    if (g_ipc_server.listen_fd >= 0)
    {
        close(g_ipc_server.listen_fd);
        g_ipc_server.listen_fd = -1;
    }

    pthread_join(g_ipc_server.thread, NULL);

    // Brief wait for client handler threads
    usleep(100000);

    // Clean up socket file
    if (g_ipc_server.socket_path[0])
        unlink(g_ipc_server.socket_path);

    g_ipc_server.handler = NULL;
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
    return client;
}

IPC_CLIENT *IpcClient_Connect(void)
{
    return IpcClient_ConnectTo(NULL);
}

BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client,
    uint8_t     command,
    const uint8_t *payload,
    uint32_t    payload_size,
    uint8_t    *response_out,
    uint32_t    response_capacity,
    uint32_t   *response_size_out)
{
    if (!client || client->fd < 0)
        return FALSE;

    // Build message: [command][payload]
    uint32_t msg_len = 1 + payload_size;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    if (!msg) return FALSE;

    msg[0] = command;
    if (payload && payload_size > 0)
        memcpy(msg + 1, payload, payload_size);

    BOOLEAN ok = IpcWriteMessage(client->fd, msg, msg_len);
    free(msg);
    if (!ok) return FALSE;

    // Read framed response
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

void IpcClient_Disconnect(IPC_CLIENT *client)
{
    if (!client) return;
    if (client->fd >= 0)
        close(client->fd);
    free(client);
}

#endif // _WIN32
