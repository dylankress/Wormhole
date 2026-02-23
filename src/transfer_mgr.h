//
// transfer_mgr.h
// Wormhole - Daemon-side transfer manager for send/receive operations.
// by Dylan Kress
//
// Manages ACTIVE_TRANSFER structs with per-transfer relay clients, QUIC
// connections, and progress tracking. Integrates with the IPC event system
// to push progress updates to subscribed clients.
//

#pragma once

#include "common.h"
#include "manifest.h"
#include "relay/peer_id.h"
#include "relay/relay_client.h"
#include "relay/discovery.h"
#include "relay_forwarder.h"
#include "ipc.h"

//=============================================================================
// Transfer Configuration
//=============================================================================

#define TRANSFER_MAX_CONCURRENT     8
#define TRANSFER_TICKET_MAX_LEN     64
#define TRANSFER_RACE_ATTEMPTS      4
#define TRANSFER_RACE_TIMEOUT_MS    5000
#define TRANSFER_RELAY_TIMEOUT_MS   15000
#define TRANSFER_CONNECT_TIMEOUT_MS 30000

//=============================================================================
// Transfer State Enum
//=============================================================================

typedef enum {
    TRANSFER_STATE_IDLE = 0,
    TRANSFER_STATE_REGISTERING,     // Connecting to relay
    TRANSFER_STATE_WAITING_PEER,    // Waiting for peer (sender waits for receiver)
    TRANSFER_STATE_CONNECTING,      // Establishing QUIC connection
    TRANSFER_STATE_TRANSFERRING,    // File transfer in progress
    TRANSFER_STATE_COMPLETED,       // Transfer completed successfully
    TRANSFER_STATE_FAILED,          // Transfer failed
    TRANSFER_STATE_CANCELLED        // Transfer cancelled by user
} TRANSFER_STATE;

typedef enum {
    TRANSFER_DIR_SEND,
    TRANSFER_DIR_RECEIVE
} TRANSFER_DIRECTION;

//=============================================================================
// Active Transfer Structure
//=============================================================================

typedef struct {
    // Identity
    uint32_t        transfer_id;        // Unique transfer ID (= IPC op_id)
    TRANSFER_DIRECTION direction;
    volatile int32_t state;             // TRANSFER_STATE
    BOOLEAN         active;

    // File info
    char            filepath[MAX_PATH]; // Source (send) or destination (receive)
    char            filename[260];      // Display name
    FILE_MANIFEST  *manifest;
    uint64_t        file_size;

    // Ticket
    char            ticket[TRANSFER_TICKET_MAX_LEN];
    BOOLEAN         ticket_ready;

    // Relay
    RELAY_CLIENT   *relay_client;
    KEYPAIR         keypair;            // Per-transfer keypair

    // Peer info (received from relay)
    uint8_t         peer_id[32];
    ENDPOINT        peer_endpoints[MAX_ENDPOINTS];
    uint16_t        peer_endpoint_count;
    BOOLEAN         peer_info_ready;

    // Network
    ENDPOINT        our_endpoints[MAX_ENDPOINTS];
    uint16_t        our_endpoint_count;

    // QUIC
    HQUIC           quic_connection;
    HQUIC           quic_listener;      // Send only
    RELAY_FORWARDER *relay_forwarder;

    // Progress
    uint64_t        bytes_transferred;
    uint64_t        bytes_total;
    uint32_t        chunks_transferred;
    uint32_t        chunks_total;
    double          start_time;
    double          last_event_time;    // Throttle IPC progress events

    // Error
    char            error_msg[256];

    // Synchronization
    WH_EVENT        complete_event;     // Signaled when transfer finishes
    volatile int32_t cancel_requested;

    // Reflected address from relay
    uint8_t         reflected_addr_type;
    uint8_t         reflected_addr[16];
    uint16_t        reflected_port;
    BOOLEAN         reflected_addr_ready;
} ACTIVE_TRANSFER;

//=============================================================================
// Transfer Manager API
//=============================================================================

// Initialize the transfer manager. Call once at daemon startup.
void TransferMgr_Init(KEYPAIR *daemon_keypair);

// Shut down the transfer manager. Cancels all active transfers.
void TransferMgr_Shutdown(void);

// Start a new send transfer. Returns transfer ID (> 0) on success, 0 on failure.
// The transfer runs asynchronously; progress is pushed via IPC events.
// op_id: IPC operation ID for progress correlation
// filepath: path to file or directory to send
uint32_t TransferMgr_Send(uint32_t op_id, const char *filepath);

// Start a new receive transfer. Returns transfer ID (> 0) on success, 0 on failure.
// op_id: IPC operation ID for progress correlation
// ticket: ticket code (e.g., "3-guitar-battery")
// output_dir: directory to save received file (NULL = ~/Downloads)
uint32_t TransferMgr_Receive(uint32_t op_id, const char *ticket,
                              const char *output_dir);

// Cancel an active transfer.
BOOLEAN TransferMgr_Cancel(uint32_t transfer_id);

// Get transfer status. Returns FALSE if transfer not found.
BOOLEAN TransferMgr_GetStatus(uint32_t transfer_id, ACTIVE_TRANSFER *out);

// List all active and recent transfers (up to max_count).
// Returns number of entries written.
uint32_t TransferMgr_List(ACTIVE_TRANSFER *out, uint32_t max_count);

// Get the ticket for a pending send transfer.
// Returns FALSE if transfer not found or ticket not yet ready.
BOOLEAN TransferMgr_GetTicket(uint32_t transfer_id, char *ticket_out,
                               uint32_t ticket_capacity);
