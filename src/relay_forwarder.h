//
// relay_forwarder.h
// Wormhole - Relay Forwarder (loopback UDP proxy for QUIC over relay)
// by Dylan Kress
//
// When direct connections fail (symmetric NAT, firewalls), this module proxies
// MsQuic UDP packets through the relay server using FORWARD messages.
//
// Architecture:
//   MsQuic ↔ [proxy socket on 127.0.0.1:proxy_port] RelayForwarder [relay socket] ↔ Relay Server
//
// The forwarder thread:
//   - Receives QUIC packets from MsQuic on the proxy socket, wraps them in
//     FORWARD messages, and sends to the relay server.
//   - Receives forwarded QUIC packets from the relay server (raw payload),
//     and sends them back to MsQuic via the proxy socket.
//

#pragma once

#include "common.h"
#include "relay/peer_id.h"
#include "relay/relay_client.h"

typedef struct RELAY_FORWARDER RELAY_FORWARDER;

typedef struct {
	const char* relay_host;
	uint16_t relay_port;
	KEYPAIR* keypair;
	uint8_t remote_peer_id[32];       // The peer we're forwarding QUIC packets to
	ENDPOINT* our_endpoints;          // Our endpoints for relay re-registration
	uint16_t our_endpoint_count;
	uint16_t local_quic_port;         // Sender mode: MsQuic listener port (e.g., 4567)
	                                  // Receiver mode: 0 (proxy provides the port)
} RELAY_FORWARDER_CONFIG;

// Start the relay forwarder. Creates a relay client, registers with relay,
// binds a proxy socket, and starts a background thread.
// Returns: RELAY_FORWARDER handle, or NULL on failure.
RELAY_FORWARDER* RelayForwarder_Start(const RELAY_FORWARDER_CONFIG* config);

// Get the proxy port MsQuic should connect to (receiver mode).
// For sender mode, this returns the proxy's ephemeral source port (not usually needed).
uint16_t RelayForwarder_GetProxyPort(RELAY_FORWARDER* fwd);

// Check if the forwarder's relay client is registered and ready.
BOOLEAN RelayForwarder_IsReady(RELAY_FORWARDER* fwd);

// Stop the forwarder thread and clean up resources.
void RelayForwarder_Stop(RELAY_FORWARDER* fwd);
