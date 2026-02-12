//
// discovery.h
// Wormhole - Endpoint Discovery (LAN, Public IP, IPv6)
// by Dylan Kress
//

#pragma once

#include "../../relay-server/relay_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

// Discover all available connection endpoints
// endpoints_out: Buffer to store discovered endpoints (must be at least MAX_ENDPOINTS)
// max_endpoints: Maximum number of endpoints to discover
// Returns: Number of endpoints discovered
uint16_t Discovery_FindEndpoints(ENDPOINT* endpoints_out, uint16_t max_endpoints);

// Discover LAN addresses (192.168.x.x, 10.x.x.x, etc.)
// Returns: Number of LAN endpoints added to endpoints_out
uint16_t Discovery_FindLANAddresses(ENDPOINT* endpoints_out, uint16_t max_endpoints);

// Add relay-reflected public address (from REGISTERED message)
// Returns: true if added successfully
bool Discovery_AddReflectedAddress(ENDPOINT* endpoints_out, uint16_t* endpoint_count,
                                   uint16_t max_endpoints, const uint8_t observed_addr[16],
                                   uint8_t observed_addr_type, uint16_t observed_port);

// Discover IPv6 addresses
// Returns: Number of IPv6 endpoints added
uint16_t Discovery_FindIPv6Addresses(ENDPOINT* endpoints_out, uint16_t max_endpoints);

// Helper: Check if address is a private/LAN address
bool Discovery_IsPrivateAddress(const uint8_t* addr, uint8_t addr_type);

// Helper: Convert sockaddr to ENDPOINT
bool Discovery_SockaddrToEndpoint(const struct sockaddr* sa, uint16_t port, uint8_t priority, ENDPOINT* endpoint);

// Helper: Convert ENDPOINT to IP string and port
// ip_out: Buffer for IP string (must be at least INET6_ADDRSTRLEN bytes, typically 46)
// ip_len: Size of ip_out buffer
// port_out: Pointer to store port number (can be NULL if not needed)
// Returns: true if successful, false on error
bool Endpoint_ToString(const ENDPOINT* endpoint, char* ip_out, size_t ip_len, uint16_t* port_out);
