//
// discovery.c
// Wormhole - Endpoint Discovery (LAN, Public IP, IPv6)
// by Dylan Kress
//

#include "discovery.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

bool Discovery_IsPrivateAddress(const uint8_t* addr, uint8_t addr_type) {
    if (addr_type == 0x04) {
        // IPv4 private ranges:
        // 10.0.0.0/8
        if (addr[0] == 10) return true;
        // 172.16.0.0/12
        if (addr[0] == 172 && (addr[1] >= 16 && addr[1] <= 31)) return true;
        // 192.168.0.0/16
        if (addr[0] == 192 && addr[1] == 168) return true;
        // 127.0.0.0/8 (loopback)
        if (addr[0] == 127) return true;
        // 169.254.0.0/16 (link-local)
        if (addr[0] == 169 && addr[1] == 254) return true;
    } else if (addr_type == 0x06) {
        // IPv6: Check for link-local (fe80::/10) or unique local (fc00::/7)
        if (addr[0] == 0xfe && (addr[1] & 0xc0) == 0x80) return true;  // fe80::/10
        if ((addr[0] & 0xfe) == 0xfc) return true;  // fc00::/7
        // Loopback (::1)
        bool is_loopback = true;
        for (int i = 0; i < 15; i++) {
            if (addr[i] != 0) {
                is_loopback = false;
                break;
            }
        }
        if (is_loopback && addr[15] == 1) return true;
    }
    
    return false;
}

bool Discovery_SockaddrToEndpoint(const struct sockaddr* sa, uint16_t port, uint8_t priority, ENDPOINT* endpoint) {
    if (!sa || !endpoint) {
        return false;
    }
    
    memset(endpoint, 0, sizeof(ENDPOINT));
    endpoint->priority = priority;
    endpoint->port = port;
    
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* sa4 = (const struct sockaddr_in*)sa;
        endpoint->addr_type = 0x04;
        memcpy(endpoint->addr, &sa4->sin_addr, 4);
        if (port == 0) {
            endpoint->port = ntohs(sa4->sin_port);
        }
        return true;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* sa6 = (const struct sockaddr_in6*)sa;
        endpoint->addr_type = 0x06;
        memcpy(endpoint->addr, &sa6->sin6_addr, 16);
        if (port == 0) {
            endpoint->port = ntohs(sa6->sin6_port);
        }
        return true;
    }
    
    return false;
}

uint16_t Discovery_FindLANAddresses(ENDPOINT* endpoints_out, uint16_t max_endpoints) {
    if (!endpoints_out || max_endpoints == 0) {
        return 0;
    }
    
    uint16_t count = 0;
    
#ifdef _WIN32
    // Windows: Use GetAdaptersAddresses
    ULONG buf_size = 15000;
    IP_ADAPTER_ADDRESSES* addresses = (IP_ADAPTER_ADDRESSES*)malloc(buf_size);
    if (!addresses) {
        return 0;
    }
    
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, addresses, &buf_size);
    if (result != NO_ERROR) {
        free(addresses);
        return 0;
    }
    
    for (IP_ADAPTER_ADDRESSES* adapter = addresses; adapter && count < max_endpoints; adapter = adapter->Next) {
        // Skip loopback and non-operational adapters
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        
        for (IP_ADAPTER_UNICAST_ADDRESS* addr = adapter->FirstUnicastAddress; addr && count < max_endpoints; addr = addr->Next) {
            ENDPOINT endpoint;
            if (Discovery_SockaddrToEndpoint(addr->Address.lpSockaddr, 0, 0, &endpoint)) {
                // Only add private addresses
                if (Discovery_IsPrivateAddress(endpoint.addr, endpoint.addr_type)) {
                    endpoints_out[count++] = endpoint;
                    printf("[Discovery] Found LAN address (priority 0)\n");
                }
            }
        }
    }
    
    free(addresses);
#else
    // POSIX: Use getifaddrs
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa && count < max_endpoints; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }
        
        // Only check IPv4 and IPv6
        if (ifa->ifa_addr->sa_family != AF_INET && ifa->ifa_addr->sa_family != AF_INET6) {
            continue;
        }
        
        ENDPOINT endpoint;
        if (Discovery_SockaddrToEndpoint(ifa->ifa_addr, 0, 0, &endpoint)) {
            // Only add private addresses
            if (Discovery_IsPrivateAddress(endpoint.addr, endpoint.addr_type)) {
                endpoints_out[count++] = endpoint;
                printf("[Discovery] Found LAN address (priority 0)\n");
            }
        }
    }
    
    freeifaddrs(ifaddr);
#endif
    
    return count;
}

bool Discovery_AddReflectedAddress(ENDPOINT* endpoints_out, uint16_t* endpoint_count,
                                   uint16_t max_endpoints, const uint8_t observed_addr[16],
                                   uint8_t observed_addr_type, uint16_t observed_port) {
    if (!endpoints_out || !endpoint_count || *endpoint_count >= max_endpoints) {
        return false;
    }
    
    ENDPOINT* endpoint = &endpoints_out[*endpoint_count];
    endpoint->addr_type = observed_addr_type;
    memcpy(endpoint->addr, observed_addr, 16);
    endpoint->port = observed_port;
    endpoint->priority = 100;  // Reflected address (medium priority)
    
    (*endpoint_count)++;
    
    printf("[Discovery] Added reflected public address (priority 100)\n");
    return true;
}

uint16_t Discovery_FindIPv6Addresses(ENDPOINT* endpoints_out, uint16_t max_endpoints) {
    if (!endpoints_out || max_endpoints == 0) {
        return 0;
    }
    
    uint16_t count = 0;
    
#ifdef _WIN32
    ULONG buf_size = 15000;
    IP_ADAPTER_ADDRESSES* addresses = (IP_ADAPTER_ADDRESSES*)malloc(buf_size);
    if (!addresses) {
        return 0;
    }
    
    ULONG result = GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, NULL, addresses, &buf_size);
    if (result != NO_ERROR) {
        free(addresses);
        return 0;
    }
    
    for (IP_ADAPTER_ADDRESSES* adapter = addresses; adapter && count < max_endpoints; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        
        for (IP_ADAPTER_UNICAST_ADDRESS* addr = adapter->FirstUnicastAddress; addr && count < max_endpoints; addr = addr->Next) {
            if (addr->Address.lpSockaddr->sa_family == AF_INET6) {
                ENDPOINT endpoint;
                if (Discovery_SockaddrToEndpoint(addr->Address.lpSockaddr, 0, 75, &endpoint)) {
                    // Skip link-local
                    if (!Discovery_IsPrivateAddress(endpoint.addr, endpoint.addr_type)) {
                        endpoints_out[count++] = endpoint;
                        printf("[Discovery] Found IPv6 address (priority 75)\n");
                    }
                }
            }
        }
    }
    
    free(addresses);
#else
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa && count < max_endpoints; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET6) {
            continue;
        }
        
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }
        
        ENDPOINT endpoint;
        if (Discovery_SockaddrToEndpoint(ifa->ifa_addr, 0, 75, &endpoint)) {
            // Skip link-local
            if (!Discovery_IsPrivateAddress(endpoint.addr, endpoint.addr_type)) {
                endpoints_out[count++] = endpoint;
                printf("[Discovery] Found IPv6 address (priority 75)\n");
            }
        }
    }
    
    freeifaddrs(ifaddr);
#endif
    
    return count;
}

uint16_t Discovery_FindEndpoints(ENDPOINT* endpoints_out, uint16_t max_endpoints) {
    if (!endpoints_out || max_endpoints == 0) {
        return 0;
    }
    
    uint16_t count = 0;
    
    // 1. Find LAN addresses (highest priority: 0)
    count += Discovery_FindLANAddresses(endpoints_out + count, max_endpoints - count);
    
    // 2. Find IPv6 addresses (medium priority: 75)
    count += Discovery_FindIPv6Addresses(endpoints_out + count, max_endpoints - count);
    
    printf("[Discovery] Found %u total endpoints\n", count);
    return count;
}

bool Endpoint_ToString(const ENDPOINT* endpoint, char* ip_out, size_t ip_len, uint16_t* port_out) {
    if (!endpoint || !ip_out || ip_len == 0) {
        return false;
    }
    
    if (endpoint->addr_type == 0x04) {
        // IPv4: Convert first 4 bytes to dotted decimal
        snprintf(ip_out, ip_len, "%u.%u.%u.%u",
                 endpoint->addr[0], endpoint->addr[1],
                 endpoint->addr[2], endpoint->addr[3]);
    } else if (endpoint->addr_type == 0x06) {
        // IPv6: Use inet_ntop for proper formatting
#ifdef _WIN32
        struct in6_addr addr6;
        memcpy(&addr6, endpoint->addr, 16);
        if (inet_ntop(AF_INET6, &addr6, ip_out, (socklen_t)ip_len) == NULL) {
            return false;
        }
#else
        if (inet_ntop(AF_INET6, endpoint->addr, ip_out, ip_len) == NULL) {
            return false;
        }
#endif
    } else {
        return false;  // Unknown address type
    }
    
    if (port_out) {
        *port_out = endpoint->port;
    }
    
    return true;
}
