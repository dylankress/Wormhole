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

//=============================================================================
// IPC Protocol Versioning
//=============================================================================

// Protocol version 1: original request-response only
// Protocol version 2: adds operation IDs, structured errors, subscriptions, cancellation
#define IPC_PROTOCOL_VERSION_1  1
#define IPC_PROTOCOL_VERSION_2  2
#define IPC_PROTOCOL_VERSION    IPC_PROTOCOL_VERSION_2

// v2 message framing: [2B version][4B length][1B command][4B op_id][payload...]
// v1 message framing: [4B length][1B command][payload...]
// v2 detection: if first 2 bytes == 0x0002 (LE), it's v2. v1 lengths are always > 0x0002
// so there's no ambiguity.
#define IPC_V2_MAGIC            0x0002   // LE uint16: protocol version 2
#define IPC_V2_HEADER_SIZE      10       // 2B version + 4B length + 1B command + 4B op_id (minus: see framing)
// Actual v2 wire: [2B version=0x0002][4B body_len][body...]
// where body = [1B command][4B op_id][payload...]
// so body_len = 1 + 4 + payload_size = 5 + payload_size

// v1 header size (unchanged)
#define IPC_V1_HEADER_SIZE      4

//=============================================================================
// IPC Command Types
//=============================================================================

// Phase 1-7 commands (v1-compatible)
#define IPC_CMD_STORE           0x01  // Client->Daemon: chunk file and store
#define IPC_CMD_GET             0x02  // Client->Daemon: retrieve chunk by hash (internal)
#define IPC_CMD_STATUS          0x03  // Client->Daemon: request daemon stats
#define IPC_CMD_SHUTDOWN        0x04  // Client->Daemon: request clean shutdown
#define IPC_CMD_DHT_STATUS      0x05  // Client->Daemon: request DHT stats
#define IPC_CMD_LIST_FILES      0x06  // Client->Daemon: list stored files
#define IPC_CMD_FILE_GET        0x07  // Client->Daemon: retrieve file by manifest hash
#define IPC_CMD_FILE_DELETE     0x08  // Client->Daemon: delete a stored file
#define IPC_CMD_EXPORT_KEY      0x09  // Client->Daemon: export file encryption key
#define IPC_CMD_IMPORT_KEY      0x0A  // Client->Daemon: import file encryption key
#define IPC_CMD_PEER_LIST       0x0B  // Client->Daemon: list known peers

// Phase 8A: IPC v2 commands
#define IPC_CMD_SUBSCRIBE       0x0C  // Client->Daemon: subscribe to events
#define IPC_CMD_CANCEL          0x0D  // Client->Daemon: cancel operation by ID

// Phase 8C: Transfer commands
#define IPC_CMD_SEND            0x0E  // Client->Daemon: initiate file send
#define IPC_CMD_RECEIVE         0x0F  // Client->Daemon: initiate file receive
#define IPC_CMD_TRANSFER_STATUS 0x10  // Client->Daemon: query active transfer
#define IPC_CMD_TRANSFER_LIST   0x11  // Client->Daemon: list active/recent transfers

// Phase 8D: Config commands
#define IPC_CMD_CONFIG_LIST     0x12  // Client->Daemon: list all config entries
#define IPC_CMD_CONFIG_GET      0x13  // Client->Daemon: get single config value
#define IPC_CMD_CONFIG_SET      0x14  // Client->Daemon: set config value

// Phase 8E: Lifecycle commands
#define IPC_CMD_HEARTBEAT       0x15  // Client->Daemon: lightweight ping, returns uptime

//=============================================================================
// IPC Response Status Codes
//=============================================================================

#define IPC_STATUS_OK           0x00
#define IPC_STATUS_ERROR        0x01
#define IPC_STATUS_NOT_FOUND    0x02
#define IPC_STATUS_CANCELLED    0x03  // Operation was cancelled
#define IPC_STATUS_BUSY         0x04  // Too many concurrent operations

// Maximum IPC message payload (1 MB should cover any chunk + framing)
#define IPC_MAX_MESSAGE_SIZE (1 * 1024 * 1024)

//=============================================================================
// IPC v2: Event Types (daemon pushes to subscribed clients)
//=============================================================================

#define IPC_EVENT_PROGRESS      0x01  // Operation progress update
#define IPC_EVENT_OP_COMPLETE   0x02  // Operation finished (success or failure)
#define IPC_EVENT_PEER_CHANGE   0x03  // Peer connected/disconnected
#define IPC_EVENT_FILE_STATUS   0x04  // File replication status changed
#define IPC_EVENT_HEALTH        0x05  // Health check results
#define IPC_EVENT_TRANSFER      0x06  // Transfer started/completed/failed

// Event message framing (daemon -> client, on subscribed connections):
// [2B version=0x0002][4B body_len][body]
// body = [1B IPC_CMD_EVENT][4B op_id][1B event_type][event_payload...]
#define IPC_CMD_EVENT           0xE0  // Daemon->Client: pushed event notification

// Subscribe payload: [4B event_mask] — bitmask of IPC_EVENT_* types
// Bit 0 = EVENT_PROGRESS, Bit 1 = EVENT_OP_COMPLETE, etc.
#define IPC_EVENT_MASK_PROGRESS     (1 << 0)
#define IPC_EVENT_MASK_OP_COMPLETE  (1 << 1)
#define IPC_EVENT_MASK_PEER_CHANGE  (1 << 2)
#define IPC_EVENT_MASK_FILE_STATUS  (1 << 3)
#define IPC_EVENT_MASK_HEALTH       (1 << 4)
#define IPC_EVENT_MASK_TRANSFER     (1 << 5)
#define IPC_EVENT_MASK_ALL          0x3F

//=============================================================================
// IPC v2: Structured Error Response
//=============================================================================

// v2 error response format: [1B status][2B error_msg_len][error_msg_utf8]
// v1 error response format: [1B status] (unchanged)
// When status == IPC_STATUS_OK, no error message follows.

// Maximum error message length
#define IPC_MAX_ERROR_MSG_LEN   512

//=============================================================================
// IPC v2: Event Payload Binary Formats
//=============================================================================
//
// All events are framed as:
//   [4B msg_len][1B IPC_CMD_EVENT(0xE0)][4B op_id][1B event_type][payload...]
// The payloads below describe the [payload...] portion only.
// All multi-byte integers are little-endian.
//
// Event: PROGRESS (0x01) — 36 bytes
//   [4B op_id][8B bytes_done][8B bytes_total]
//   [4B chunks_done][4B chunks_total][4B speed_bps][4B eta_sec]
//
// Event: OP_COMPLETE (0x02) — 7+ bytes
//   [1B status][4B op_id][2B error_msg_len][error_msg_bytes...]
//   status: IPC_STATUS_OK(0x00) or IPC_STATUS_ERROR(0x01)
//
// Event: PEER_CHANGE (0x03) — 4 bytes
//   [4B peer_count]
//
// Event: FILE_STATUS (0x04) — 36 bytes
//   [32B manifest_hash][1B file_status][2B replica_count_LE][1B padding]
//   file_status: FILE_STATUS_STORING/REPLICATING/SAFE/OFFLOADED
//
// Event: HEALTH (0x05) — 16 bytes
//   [4B chunks_checked][4B chunks_healthy][4B chunks_degraded][4B chunks_critical]
//
// Event: TRANSFER (0x06) — 25+ bytes (variable length)
//   [4B transfer_id][1B direction][1B state][1B status]
//   [8B bytes_transferred][8B bytes_total]
//   [2B error_msg_len][error_msg_bytes...]
//   [1B ticket_len][ticket_bytes...]
//   [1B filename_len][filename_bytes...]
//   direction: 0=SEND, 1=RECEIVE
//   state: TRANSFER_STATE_PENDING/CONNECTING/ACTIVE/COMPLETED/FAILED/CANCELLED
//
#define IPC_PROGRESS_PAYLOAD_SIZE  (4 + 8 + 8 + 4 + 4 + 4 + 4)

// Maximum concurrent operations tracked
#define IPC_MAX_OPERATIONS      16

// Maximum concurrent subscribed clients
#define IPC_MAX_SUBSCRIBERS     8

// Progress event throttle: max events per second per operation
#define IPC_PROGRESS_THROTTLE_HZ  10

//=============================================================================
// IPC v2: Structures
//=============================================================================

// Operation progress tracking (used by daemon)
typedef struct {
    uint32_t    op_id;
    volatile int32_t cancelled;
    uint64_t    bytes_done;
    uint64_t    bytes_total;
    uint32_t    chunks_done;
    uint32_t    chunks_total;
    double      start_time;
    double      last_event_time;   // For throttling
    uint8_t     command;           // Which IPC_CMD started this
    BOOLEAN     active;
} OPERATION_PROGRESS;

// Subscriber tracking (server-side)
typedef struct {
    uint32_t    event_mask;     // Bitmask of IPC_EVENT_MASK_*
    BOOLEAN     active;
    // Platform-specific connection handle stored separately
} IPC_SUBSCRIBER;

// IPC status response structure (returned by STATUS command)
typedef struct {
    uint32_t peer_count;
    uint32_t chunk_count;
    uint64_t storage_used;
    BOOLEAN  relay_connected;
    BOOLEAN  listener_active;
} IPC_STATUS_INFO;

//=============================================================================
// v1 Command Handler (unchanged — backward compatibility)
//=============================================================================

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

//=============================================================================
// v2 Command Handler (adds op_id and subscriber context)
//=============================================================================

// v2 handler receives op_id for operation tracking and client_index for
// identifying which client sent the request (for directed event push).
typedef uint32_t (*IpcCommandHandlerV2)(
    uint8_t   command,
    uint32_t  op_id,
    const uint8_t *payload,
    uint32_t  payload_size,
    uint8_t  *response_out,
    uint32_t  response_capacity,
    void     *context,
    int       client_index     // Which client connection sent this (-1 for v1)
);

// --- Server API (used by wormholed) ---

// Start the IPC server on the named pipe.
// handler: callback for incoming commands (v1 — legacy)
// context: opaque pointer passed to handler
// pipe_name: pipe path (NULL = IPC_PIPE_NAME default)
// Returns TRUE on success.
BOOLEAN IpcServer_Start(IpcCommandHandler handler, void *context,
                        const char *pipe_name);

// Start the IPC server with v2 handler support.
// handler_v2: callback for v2 commands (receives op_id + client_index)
// handler_v1: fallback for v1 commands (NULL = reject v1 clients)
BOOLEAN IpcServer_StartV2(IpcCommandHandlerV2 handler_v2,
                          IpcCommandHandler handler_v1,
                          void *context, const char *pipe_name);

// Stop the IPC server and clean up resources.
void IpcServer_Stop(void);

// Check if the IPC server is running.
BOOLEAN IpcServer_IsRunning(void);

// --- Server: Event Push API ---

// Push an event to all subscribed clients matching the event mask.
// event_type: IPC_EVENT_* constant
// op_id: operation ID (0 for global events)
// payload: event-specific data
// payload_size: size of payload
void IpcServer_PushEvent(uint8_t event_type, uint32_t op_id,
                         const uint8_t *payload, uint32_t payload_size);

// Push an event to a specific client.
void IpcServer_PushEventTo(int client_index, uint8_t event_type,
                           uint32_t op_id, const uint8_t *payload,
                           uint32_t payload_size);

// --- Server: Operation Registry ---

// Register an active operation. Returns TRUE if registered, FALSE if full.
BOOLEAN IpcServer_RegisterOp(uint32_t op_id, uint8_t command);

// Unregister a completed operation.
void IpcServer_UnregisterOp(uint32_t op_id);

// Get operation progress by ID. Returns NULL if not found.
OPERATION_PROGRESS *IpcServer_GetOp(uint32_t op_id);

// Check if operation has been cancelled.
BOOLEAN IpcServer_IsOpCancelled(uint32_t op_id);

// Update operation progress and emit throttled EVENT_PROGRESS.
void IpcServer_UpdateProgress(uint32_t op_id, uint64_t bytes_done,
                              uint64_t bytes_total, uint32_t chunks_done,
                              uint32_t chunks_total);

// --- Client API (used by wormhole CLI thin client) ---

// Opaque IPC client handle
typedef struct IPC_CLIENT IPC_CLIENT;

// Connect to the daemon's named pipe.
// Returns allocated client handle, or NULL on failure.
IPC_CLIENT *IpcClient_Connect(void);

// Connect to a specific named pipe (for multi-daemon support).
IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name);

// Send a command and receive the response (v1 protocol).
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

// Send a v2 command with operation ID.
// op_id: client-assigned operation ID for correlation
BOOLEAN IpcClient_SendCommandV2(
    IPC_CLIENT *client,
    uint8_t     command,
    uint32_t    op_id,
    const uint8_t *payload,
    uint32_t    payload_size,
    uint8_t    *response_out,
    uint32_t    response_capacity,
    uint32_t   *response_size_out
);

// Subscribe to daemon events.
// event_mask: bitmask of IPC_EVENT_MASK_* values
// Returns TRUE on success.
BOOLEAN IpcClient_Subscribe(IPC_CLIENT *client, uint32_t event_mask);

// Read the next event from a subscribed connection (blocking with timeout).
// event_type_out: set to IPC_EVENT_* type
// op_id_out: set to associated operation ID
// payload_out: buffer for event payload
// payload_capacity: size of payload_out
// payload_size_out: actual payload size
// timeout_ms: max wait time (0 = non-blocking, UINT32_MAX = infinite)
// Returns TRUE if event received, FALSE on timeout/error.
BOOLEAN IpcClient_ReadEvent(
    IPC_CLIENT *client,
    uint8_t    *event_type_out,
    uint32_t   *op_id_out,
    uint8_t    *payload_out,
    uint32_t    payload_capacity,
    uint32_t   *payload_size_out,
    uint32_t    timeout_ms
);

// Cancel a remote operation.
BOOLEAN IpcClient_CancelOp(IPC_CLIENT *client, uint32_t op_id);

// Disconnect and free the client handle.
void IpcClient_Disconnect(IPC_CLIENT *client);

//=============================================================================
// Helper: build v2 error response
//=============================================================================

// Write a v2 structured error response: [1B status][2B msg_len][msg]
// Returns bytes written.
static inline uint32_t IpcWriteErrorResponse(uint8_t *buf, uint32_t capacity,
                                              uint8_t status, const char *msg)
{
    if (!msg || !buf) {
        if (buf && capacity >= 1) { buf[0] = status; return 1; }
        return 0;
    }
    uint16_t msg_len = (uint16_t)strlen(msg);
    if (msg_len > IPC_MAX_ERROR_MSG_LEN)
        msg_len = IPC_MAX_ERROR_MSG_LEN;
    uint32_t need = 1 + 2 + msg_len;
    if (need > capacity) {
        buf[0] = status;
        return 1;
    }
    buf[0] = status;
    buf[1] = (uint8_t)(msg_len);
    buf[2] = (uint8_t)(msg_len >> 8);
    memcpy(buf + 3, msg, msg_len);
    return need;
}

// Read a v2 structured error response. Returns error message or NULL.
// status_out: set to status byte
// msg_out: buffer for error message (null-terminated)
// msg_capacity: size of msg_out
static inline BOOLEAN IpcReadErrorResponse(const uint8_t *buf, uint32_t buf_size,
                                            uint8_t *status_out, char *msg_out,
                                            uint32_t msg_capacity)
{
    if (!buf || buf_size < 1) return FALSE;
    *status_out = buf[0];
    if (buf_size >= 3 && buf[0] != IPC_STATUS_OK) {
        uint16_t msg_len = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        if (msg_len > 0 && (uint32_t)(3 + msg_len) <= buf_size && msg_out && msg_capacity > 0) {
            uint16_t copy = msg_len < msg_capacity - 1 ? msg_len : (uint16_t)(msg_capacity - 1);
            memcpy(msg_out, buf + 3, copy);
            msg_out[copy] = '\0';
            return TRUE;
        }
    }
    if (msg_out && msg_capacity > 0) msg_out[0] = '\0';
    return TRUE;
}
