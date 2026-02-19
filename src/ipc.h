//
// ipc.h
// Wormhole - Named pipe IPC for daemon communication.
// by Dylan Kress
//

#pragma once

#include "common.h"
#include <stdint.h>

// IPC endpoint path (platform-specific)
#ifdef _WIN32
#define IPC_PIPE_NAME "\\\\.\\pipe\\wormhole"
#define IPC_PIPE_PREFIX "\\\\.\\pipe\\wormhole_"
#else
// Unix domain socket path — runtime-resolved from $HOME
#define IPC_SOCKET_NAME "wormhole.sock"
#define IPC_SOCKET_PREFIX "wormhole_"
// Pipe name/prefix aliases for shared code
#define IPC_PIPE_NAME   IPC_SOCKET_NAME
#define IPC_PIPE_PREFIX IPC_SOCKET_PREFIX
#endif

// IPC message framing: [4B little-endian length][1B command][payload...]
// The length field includes the command byte and payload (not the length itself).

// IPC command types
#define IPC_CMD_STORE      0x01  // Client->Daemon: chunk file and store
#define IPC_CMD_GET        0x02  // Client->Daemon: retrieve chunk by hash (internal)
#define IPC_CMD_STATUS     0x03  // Client->Daemon: request daemon stats
#define IPC_CMD_SHUTDOWN   0x04  // Client->Daemon: request clean shutdown
#define IPC_CMD_DHT_STATUS 0x05  // Client->Daemon: request DHT stats
#define IPC_CMD_LIST_FILES 0x06  // Client->Daemon: list stored files
#define IPC_CMD_FILE_GET   0x07  // Client->Daemon: retrieve file by manifest hash

// IPC response status codes
#define IPC_STATUS_OK        0x00
#define IPC_STATUS_ERROR     0x01
#define IPC_STATUS_NOT_FOUND 0x02

// Maximum IPC message payload (1 MB should cover any chunk + framing)
#define IPC_MAX_MESSAGE_SIZE (1 * 1024 * 1024)

// IPC status response structure (returned by STATUS command)
typedef struct {
    uint32_t peer_count;
    uint32_t chunk_count;
    uint64_t storage_used;
    BOOLEAN  relay_connected;
    BOOLEAN  listener_active;
} IPC_STATUS_INFO;

// Callback invoked by the IPC server when a command arrives.
// The handler should process the command and write a response into
// response_out (caller-allocated, IPC_MAX_MESSAGE_SIZE bytes).
// Returns the number of bytes written to response_out, or 0 on error.
typedef uint32_t (*IpcCommandHandler)(
    uint8_t   command,
    const uint8_t *payload,
    uint32_t  payload_size,
    uint8_t  *response_out,
    uint32_t  response_capacity,
    void     *context
);

// --- Server API (used by wormholed) ---

// Start the IPC server on the named pipe.
// handler: callback for incoming commands
// context: opaque pointer passed to handler
// pipe_name: pipe path (NULL = IPC_PIPE_NAME default)
// Returns TRUE on success.
BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name);

// Stop the IPC server and clean up resources.
void IpcServer_Stop(void);

// Check if the IPC server is running.
BOOLEAN IpcServer_IsRunning(void);

// --- Client API (used by wormhole CLI thin client) ---

// Opaque IPC client handle
typedef struct IPC_CLIENT IPC_CLIENT;

// Connect to the daemon's named pipe.
// Returns allocated client handle, or NULL on failure.
IPC_CLIENT *IpcClient_Connect(void);

// Connect to a specific named pipe (for multi-daemon support).
IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name);

// Send a command and receive the response.
// command:      IPC_CMD_* type
// payload:      command-specific data (may be NULL if payload_size is 0)
// payload_size: length of payload
// response_out: buffer for response data (caller-allocated)
// response_capacity: size of response_out buffer
// response_size_out: set to actual response size on success
// Returns TRUE on success.
BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client,
    uint8_t     command,
    const uint8_t *payload,
    uint32_t    payload_size,
    uint8_t    *response_out,
    uint32_t    response_capacity,
    uint32_t   *response_size_out
);

// Disconnect and free the client handle.
void IpcClient_Disconnect(IPC_CLIENT *client);
