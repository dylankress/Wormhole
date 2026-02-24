//
// ipc_client.c
// Standalone IPC client for Wormhole GUI — client-only, no server code.
// Extracted from src/ipc.c (client functions only).
// by Dylan Kress
//

#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// IPC message header size: 4 bytes (little-endian length)
#define IPC_HEADER_SIZE 4

//=============================================================================
// Windows implementation
//=============================================================================
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct IPC_CLIENT {
    HANDLE pipe_handle;
    BOOLEAN is_v2;
    uint8_t *buffered_events[8];
    uint32_t buffered_event_sizes[8];
    int buffered_event_count;
};

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

static BOOLEAN IpcReadMessage(HANDLE pipe, uint8_t **out_buf, uint32_t *out_size)
{
    uint8_t header[IPC_HEADER_SIZE];
    if (!PipeReadExact(pipe, header, IPC_HEADER_SIZE))
        return FALSE;

    uint32_t body_len = ReadUint32LE(header);
    if (body_len == 0 || body_len > IPC_MAX_MESSAGE_SIZE)
        return FALSE;

    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body)
        return FALSE;

    if (!PipeReadExact(pipe, body, body_len))
    {
        free(body);
        return FALSE;
    }

    *out_buf = body;
    *out_size = body_len;
    return TRUE;
}

static BOOLEAN IpcWriteMessage(HANDLE pipe, const uint8_t *body, uint32_t body_len)
{
    uint8_t header[IPC_HEADER_SIZE];
    WriteUint32LE(header, body_len);

    if (!PipeWriteExact(pipe, header, IPC_HEADER_SIZE))
        return FALSE;
    if (body_len > 0 && !PipeWriteExact(pipe, body, body_len))
        return FALSE;
    return TRUE;
}

IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name)
{
    const char *name = pipe_name ? pipe_name : IPC_PIPE_NAME;

    HANDLE pipe = CreateFileA(
        name, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (pipe == INVALID_HANDLE_VALUE)
        return NULL;

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, NULL, NULL);

    IPC_CLIENT *client = (IPC_CLIENT *)calloc(1, sizeof(IPC_CLIENT));
    if (!client)
    {
        CloseHandle(pipe);
        return NULL;
    }

    client->pipe_handle = pipe;
    client->is_v2 = FALSE;
    return client;
}

IPC_CLIENT *IpcClient_Connect(void)
{
    return IpcClient_ConnectTo(IPC_PIPE_PREFIX "4567");
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
    while (1) {
        if (!IpcReadMessage(client->pipe_handle, &resp, &resp_size))
            return FALSE;
        if (resp_size >= 1 && resp[0] == IPC_CMD_EVENT) {
            if (client->buffered_event_count < 8) {
                client->buffered_events[client->buffered_event_count] = resp;
                client->buffered_event_sizes[client->buffered_event_count] = resp_size;
                client->buffered_event_count++;
            } else {
                free(resp);
            }
            resp = NULL;
            resp_size = 0;
            continue;
        }
        break;
    }

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
    if (!client || client->pipe_handle == INVALID_HANDLE_VALUE)
        return FALSE;

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
    while (1) {
        if (!IpcReadMessage(client->pipe_handle, &resp, &resp_size))
            return FALSE;
        if (resp_size >= 1 && resp[0] == IPC_CMD_EVENT) {
            if (client->buffered_event_count < 8) {
                client->buffered_events[client->buffered_event_count] = resp;
                client->buffered_event_sizes[client->buffered_event_count] = resp_size;
                client->buffered_event_count++;
            } else {
                free(resp);
            }
            resp = NULL;
            resp_size = 0;
            continue;
        }
        break;
    }

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

    // Drain buffered events first
    if (client->buffered_event_count > 0) {
        uint8_t *msg = client->buffered_events[0];
        uint32_t msg_size = client->buffered_event_sizes[0];
        for (int i = 1; i < client->buffered_event_count; i++) {
            client->buffered_events[i - 1] = client->buffered_events[i];
            client->buffered_event_sizes[i - 1] = client->buffered_event_sizes[i];
        }
        client->buffered_event_count--;
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
    for (int i = 0; i < client->buffered_event_count; i++)
        free(client->buffered_events[i]);
    if (client->pipe_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(client->pipe_handle);
        client->pipe_handle = INVALID_HANDLE_VALUE;
    }
    free(client);
}

#else
//=============================================================================
// POSIX implementation — Unix domain sockets
//=============================================================================

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

struct IPC_CLIENT {
    int fd;
    BOOLEAN is_v2;
    uint8_t *buffered_events[8];
    uint32_t buffered_event_sizes[8];
    int buffered_event_count;
};

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

static BOOLEAN IpcReadMessage(int fd, uint8_t **out_buf, uint32_t *out_size)
{
    uint8_t header[IPC_HEADER_SIZE];
    if (!SockReadExact(fd, header, IPC_HEADER_SIZE))
        return FALSE;

    uint32_t body_len = ReadUint32LE(header);
    if (body_len == 0 || body_len > IPC_MAX_MESSAGE_SIZE)
        return FALSE;

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

IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name)
{
    char socket_path[256];
    if (!IpcBuildSocketPath(pipe_name, socket_path, sizeof(socket_path)))
        return NULL;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
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
    return IpcClient_ConnectTo(IPC_PIPE_PREFIX "4567");
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
    while (1) {
        if (!IpcReadMessage(client->fd, &resp, &resp_size))
            return FALSE;
        if (resp_size >= 1 && resp[0] == IPC_CMD_EVENT) {
            if (client->buffered_event_count < 8) {
                client->buffered_events[client->buffered_event_count] = resp;
                client->buffered_event_sizes[client->buffered_event_count] = resp_size;
                client->buffered_event_count++;
            } else {
                free(resp);
            }
            resp = NULL;
            resp_size = 0;
            continue;
        }
        break;
    }

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
    while (1) {
        if (!IpcReadMessage(client->fd, &resp, &resp_size))
            return FALSE;
        if (resp_size >= 1 && resp[0] == IPC_CMD_EVENT) {
            if (client->buffered_event_count < 8) {
                client->buffered_events[client->buffered_event_count] = resp;
                client->buffered_event_sizes[client->buffered_event_count] = resp_size;
                client->buffered_event_count++;
            } else {
                free(resp);
            }
            resp = NULL;
            resp_size = 0;
            continue;
        }
        break;
    }

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

    // Drain buffered events first
    if (client->buffered_event_count > 0) {
        uint8_t *msg = client->buffered_events[0];
        uint32_t msg_size = client->buffered_event_sizes[0];
        for (int i = 1; i < client->buffered_event_count; i++) {
            client->buffered_events[i - 1] = client->buffered_events[i];
            client->buffered_event_sizes[i - 1] = client->buffered_event_sizes[i];
        }
        client->buffered_event_count--;
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

    struct pollfd pfd = { .fd = client->fd, .events = POLLIN };
    int timeout = (timeout_ms == 0xFFFFFFFF) ? -1 : (int)timeout_ms;
    int ret = poll(&pfd, 1, timeout);
    if (ret <= 0)
        return FALSE;

    uint8_t *msg = NULL;
    uint32_t msg_size = 0;
    if (!IpcReadMessage(client->fd, &msg, &msg_size))
        return FALSE;

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
    for (int i = 0; i < client->buffered_event_count; i++)
        free(client->buffered_events[i]);
    if (client->fd >= 0)
        close(client->fd);
    free(client);
}

#endif // _WIN32
