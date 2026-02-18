//
// relay_protocol.h
// Wormhole Relay Protocol - Wire Format Definitions
// by Dylan Kress
//

#pragma once

#include <stdint.h>

// Message types
typedef enum {
    RELAY_MSG_REGISTER       = 0x01,  // Client → Relay: Register PeerID
    RELAY_MSG_REGISTERED     = 0x02,  // Relay → Client: Registration confirmed
    RELAY_MSG_LOOKUP         = 0x03,  // Client → Relay: Find peer by ticket
    RELAY_MSG_PEER_INFO      = 0x04,  // Relay → Client: Here's your peer's info
    RELAY_MSG_FORWARD        = 0x05,  // Client → Relay: Forward packet to peer
    RELAY_MSG_KEEPALIVE      = 0x06,  // Client ↔ Relay: Keep connection alive
    RELAY_MSG_GOODBYE        = 0x07,  // Client → Relay: Closing connection
    RELAY_MSG_CREATE_TICKET  = 0x08,  // Client → Relay: Create transfer ticket
    RELAY_MSG_TICKET_CREATED = 0x09,  // Relay → Client: Ticket is "N-word-word"
    RELAY_MSG_FIND_PEERS     = 0x0A,  // Client → Relay: Request active peers
    RELAY_MSG_PEERS_FOUND    = 0x0B,  // Relay → Client: List of active peers
} RELAY_MESSAGE_TYPE;

// Use MSVC-compatible packing
#pragma pack(push, 1)

// Endpoint structure (candidate connection address)
typedef struct {
    uint8_t  addr_type;       // 0x04=IPv4, 0x06=IPv6
    uint8_t  addr[16];        // IP address (IPv4 uses first 4 bytes)
    uint16_t port;            // UDP port (little-endian)
    uint8_t  priority;        // 0=highest priority, 255=lowest
} ENDPOINT;

// 1. REGISTER: Client announces itself to relay
typedef struct {
    uint8_t  message_type;    // 0x01
    uint8_t  peer_id[32];     // Ed25519 public key (this IS the PeerID)
    uint8_t  signature[64];   // Ed25519 signature: Sign(peer_id || timestamp || endpoints_hash)
    uint64_t timestamp;       // Unix timestamp (prevents replay attacks)
    uint16_t endpoint_count;  // Number of ENDPOINT structures following this header
    // Followed by endpoint_count × ENDPOINT structures
} RegisterMsg;

// 2. REGISTERED: Relay confirms registration and reflects client's address
typedef struct {
    uint8_t  message_type;    // 0x02
    uint8_t  status;          // 0x00=success, 0x01=error (invalid signature, etc.)
    uint8_t  observed_addr_type;  // What type of address relay observed
    uint8_t  observed_addr[16];   // Public IP address relay saw (NAT reflection)
    uint16_t observed_port;       // Public port relay saw (NAT reflection)
    uint64_t session_id;          // Unique session ID for this registration
} RegisteredMsg;

// 3. CREATE_TICKET: Request a transfer ticket for sending a file
typedef struct {
    uint8_t  message_type;    // 0x08
    uint64_t session_id;      // From REGISTERED message
    uint64_t file_size;       // Size of file to transfer (for receiver info)
    uint16_t filename_length; // Length of filename string
    // Followed by filename (UTF-8, no null terminator)
} CreateTicketMsg;

// 4. TICKET_CREATED: Relay returns generated ticket
typedef struct {
    uint8_t  message_type;    // 0x09
    uint8_t  ticket_length;   // Length of ticket string (typically ~20 bytes)
    // Followed by ticket string (ASCII, format: "N-word-word", e.g., "7-guitar-battery")
} TicketCreatedMsg;

// 5. LOOKUP: Receiver looks up sender by ticket
typedef struct {
    uint8_t  message_type;    // 0x03
    uint64_t session_id;      // Receiver's session ID from REGISTERED
    uint8_t  ticket_length;   // Length of ticket string
    // Followed by ticket string
} LookupMsg;

// 6. PEER_INFO: Relay provides peer's connection information
typedef struct {
    uint8_t  message_type;    // 0x04
    uint8_t  peer_id[32];     // Other peer's PeerID (Ed25519 public key)
    uint16_t endpoint_count;  // Number of ENDPOINT structures following
    // Followed by endpoint_count × ENDPOINT structures
} PeerInfoMsg;

// 7. FORWARD: Forward encrypted packet to destination peer (fallback only)
typedef struct {
    uint8_t  message_type;    // 0x05
    uint8_t  dest_peer_id[32];    // Destination peer's ID
    uint16_t payload_length;      // Length of encrypted payload
    // Followed by payload (encrypted QUIC packet, opaque to relay)
} ForwardMsg;

// 8. KEEPALIVE: Keep connection alive
typedef struct {
    uint8_t  message_type;    // 0x06
    uint64_t timestamp;       // Current timestamp
} KeepaliveMsg;

// 9. GOODBYE: Gracefully close connection
typedef struct {
    uint8_t  message_type;    // 0x07
    uint8_t  reason;          // 0x00=upgraded to direct, 0x01=error, 0x02=transfer complete
} GoodbyeMsg;

// 10. FIND_PEERS: Request list of active peers for P2P storage
typedef struct {
    uint8_t  message_type;    // 0x0A
    uint64_t session_id;      // Requester's session ID
    uint16_t max_peers;       // Maximum peers to return (capped at MAX_FIND_PEERS)
} FindPeersMsg;

// 11. PEERS_FOUND: Response with list of active peers
// Followed by peer_count × { peer_id[32], endpoint_count (uint16), endpoints[] }
typedef struct {
    uint8_t  message_type;    // 0x0B
    uint16_t peer_count;      // Number of peers in response
    // Followed by variable-length peer entries (see above)
} PeersFoundMsg;

#pragma pack(pop)

// Protocol constants
#define MAX_ENDPOINTS 16              // Maximum endpoints per peer (headroom for relay-added endpoints)
#define MAX_TICKET_LENGTH 32          // Maximum ticket string length
#define MAX_FILENAME_LENGTH 256       // Maximum filename length
#define MAX_PAYLOAD_LENGTH 65535      // Maximum forwarded packet size (fits uint16_t)
#define TICKET_EXPIRY_SECONDS 3600    // Tickets expire after 1 hour
#define MAX_FIND_PEERS 50             // Maximum peers returned by FIND_PEERS

// Helper macros for endianness (little-endian)
#define htole16(x) (x)  // No-op on x86/x64 (already little-endian)
#define htole32(x) (x)
#define htole64(x) (x)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)
