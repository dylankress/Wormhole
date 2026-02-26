//
// ipc_client.h
// Standalone IPC client for Wormhole GUI — no MsQuic/common.h dependency.
// Extracted from src/ipc.h + src/wire_format.h (client-only API).
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Platform types (standalone — no common.h dependency)
//=============================================================================

#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

//=============================================================================
// IPC Endpoint Path
//=============================================================================

#ifdef _WIN32
#define IPC_PIPE_NAME "\\\\.\\pipe\\wormhole"
#define IPC_PIPE_PREFIX "\\\\.\\pipe\\wormhole_"
#else
#define IPC_SOCKET_NAME "wormhole.sock"
#define IPC_SOCKET_PREFIX "wormhole_"
#define IPC_PIPE_NAME   IPC_SOCKET_NAME
#define IPC_PIPE_PREFIX IPC_SOCKET_PREFIX
#endif

//=============================================================================
// IPC Command Types
//=============================================================================

// Phase 1-7 commands (v1-compatible)
#define IPC_CMD_STORE           0x01
#define IPC_CMD_GET             0x02
#define IPC_CMD_STATUS          0x03
#define IPC_CMD_SHUTDOWN        0x04
#define IPC_CMD_DHT_STATUS      0x05
#define IPC_CMD_LIST_FILES      0x06
#define IPC_CMD_FILE_GET        0x07
#define IPC_CMD_FILE_DELETE     0x08
#define IPC_CMD_EXPORT_KEY      0x09
#define IPC_CMD_IMPORT_KEY      0x0A
#define IPC_CMD_PEER_LIST       0x0B

// Phase 8A: IPC v2 commands
#define IPC_CMD_SUBSCRIBE       0x0C
#define IPC_CMD_CANCEL          0x0D

// Phase 8C: Transfer commands
#define IPC_CMD_SEND            0x0E
#define IPC_CMD_RECEIVE         0x0F
#define IPC_CMD_TRANSFER_STATUS 0x10
#define IPC_CMD_TRANSFER_LIST   0x11

// Phase 8D: Config commands
#define IPC_CMD_CONFIG_LIST     0x12
#define IPC_CMD_CONFIG_GET      0x13
#define IPC_CMD_CONFIG_SET      0x14

// Phase 8E: Lifecycle commands
#define IPC_CMD_HEARTBEAT       0x15

//=============================================================================
// IPC Response Status Codes
//=============================================================================

#define IPC_STATUS_OK           0x00
#define IPC_STATUS_ERROR        0x01
#define IPC_STATUS_NOT_FOUND    0x02
#define IPC_STATUS_CANCELLED    0x03
#define IPC_STATUS_BUSY         0x04

//=============================================================================
// IPC Limits
//=============================================================================

#define IPC_MAX_MESSAGE_SIZE (1 * 1024 * 1024)

//=============================================================================
// IPC v2: Event Types
//=============================================================================

#define IPC_EVENT_PROGRESS      0x01
#define IPC_EVENT_OP_COMPLETE   0x02
#define IPC_EVENT_PEER_CHANGE   0x03
#define IPC_EVENT_FILE_STATUS   0x04
#define IPC_EVENT_HEALTH        0x05
#define IPC_EVENT_TRANSFER      0x06

#define IPC_CMD_EVENT           0xE0

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
//
// Event: PEER_CHANGE (0x03) — 4 bytes
//   [4B peer_count]
//
// Event: FILE_STATUS (0x04) — 36 bytes
//   [32B manifest_hash][1B file_status][2B replica_count_LE][1B padding]
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
//
#define IPC_PROGRESS_PAYLOAD_SIZE  (4 + 8 + 8 + 4 + 4 + 4 + 4)

//=============================================================================
// Hash size (Blake3)
//=============================================================================

#define WH_HASH_SIZE 32

//=============================================================================
// Transfer state enum (mirrors src/transfer_mgr.h)
//=============================================================================

#define TRANSFER_STATE_IDLE         0
#define TRANSFER_STATE_REGISTERING  1
#define TRANSFER_STATE_WAITING_PEER 2
#define TRANSFER_STATE_CONNECTING   3
#define TRANSFER_STATE_TRANSFERRING 4
#define TRANSFER_STATE_COMPLETED    5
#define TRANSFER_STATE_FAILED       6
#define TRANSFER_STATE_CANCELLED    7

#define TRANSFER_DIR_SEND    0
#define TRANSFER_DIR_RECEIVE 1

//=============================================================================
// File status enum (mirrors src/file_registry.h)
//=============================================================================

#define FILE_STATUS_STORING     0x01
#define FILE_STATUS_REPLICATING 0x02
#define FILE_STATUS_SAFE        0x03
#define FILE_STATUS_OFFLOADED   0x04

//=============================================================================
// Wire Format Helpers (from src/wire_format.h — verbatim)
//=============================================================================

static inline void WriteUint64LE(uint8_t *buffer, uint64_t value)
{
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
    buffer[4] = (uint8_t)(value >> 32);
    buffer[5] = (uint8_t)(value >> 40);
    buffer[6] = (uint8_t)(value >> 48);
    buffer[7] = (uint8_t)(value >> 56);
}

static inline uint64_t ReadUint64LE(const uint8_t *buffer)
{
    return (uint64_t)buffer[0] |
        ((uint64_t)buffer[1] << 8) |
        ((uint64_t)buffer[2] << 16) |
        ((uint64_t)buffer[3] << 24) |
        ((uint64_t)buffer[4] << 32) |
        ((uint64_t)buffer[5] << 40) |
        ((uint64_t)buffer[6] << 48) |
        ((uint64_t)buffer[7] << 56);
}

static inline void WriteUint32LE(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static inline uint32_t ReadUint32LE(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
        ((uint32_t)buffer[1] << 8) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 24);
}

static inline void WriteUint16LE(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
}

static inline uint16_t ReadUint16LE(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] |
        ((uint16_t)buffer[1] << 8);
}

//=============================================================================
// v2 Error Response Helpers (from src/ipc.h)
//=============================================================================

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

//=============================================================================
// Client API
//=============================================================================

typedef struct IPC_CLIENT IPC_CLIENT;

IPC_CLIENT *IpcClient_Connect(void);
IPC_CLIENT *IpcClient_ConnectTo(const char *pipe_name);

BOOLEAN IpcClient_SendCommand(
    IPC_CLIENT *client,
    uint8_t     command,
    const uint8_t *payload,
    uint32_t    payload_size,
    uint8_t    *response_out,
    uint32_t    response_capacity,
    uint32_t   *response_size_out
);

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

BOOLEAN IpcClient_Subscribe(IPC_CLIENT *client, uint32_t event_mask);

BOOLEAN IpcClient_ReadEvent(
    IPC_CLIENT *client,
    uint8_t    *event_type_out,
    uint32_t   *op_id_out,
    uint8_t    *payload_out,
    uint32_t    payload_capacity,
    uint32_t   *payload_size_out,
    uint32_t    timeout_ms
);

BOOLEAN IpcClient_CancelOp(IPC_CLIENT *client, uint32_t op_id);

void IpcClient_Disconnect(IPC_CLIENT *client);

#ifdef __cplusplus
}
#endif
