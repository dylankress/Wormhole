//
// relay_forwarder.c
// Wormhole - Relay Forwarder (loopback UDP proxy for QUIC over relay)
// by Dylan Kress
//
// Proxies MsQuic UDP packets through the relay server's FORWARD handler.
// See relay_forwarder.h for architecture overview.
//

#include "relay_forwarder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#endif

#define FORWARDER_MAX_PACKET 65536

// QUIC packets have the fixed bit (bit 6) set, so first byte >= 0x40.
// Relay protocol message types are 0x01-0x09, so they don't collide.
#define IS_QUIC_PACKET(first_byte) ((first_byte) >= 0x40)

struct RELAY_FORWARDER {
	// Relay side
	RELAY_CLIENT* relay_client;
	uint8_t remote_peer_id[32];

	// Proxy side (loopback socket for MsQuic)
	int proxy_sock;
	uint16_t proxy_port;

	// Keepalive tracking (per-instance, not static)
	double last_keepalive;

	// MsQuic's address (learned from first packet in receiver mode)
	struct sockaddr_storage msquic_addr;
	socklen_t msquic_addr_len;
	BOOLEAN msquic_addr_known;

	// For sender mode: send to MsQuic at 127.0.0.1:local_quic_port
	uint16_t local_quic_port;

	// Thread control
	WH_THREAD thread;
	volatile int32_t running;
};

// Relay callbacks (minimal — we only need registration)
static void fwd_on_connected(void* ctx, uint64_t session_id,
                              const uint8_t observed_addr[16],
                              uint8_t observed_addr_type, uint16_t observed_port)
{
	(void)observed_addr; (void)observed_addr_type; (void)observed_port;
	LOG("[RelayFwd] Registered with relay (session %llu)\n", (unsigned long long)session_id);
}

static void fwd_on_disconnected(void* ctx)
{
	LOG("[RelayFwd] Disconnected from relay\n");
}

// The forwarding thread: polls both relay socket and proxy socket.
static WH_THREAD_RETURN ForwarderThread(WH_THREAD_PARAM param)
{
	RELAY_FORWARDER* fwd = (RELAY_FORWARDER*)param;
	uint8_t buf[FORWARDER_MAX_PACKET];
	int relay_sock = RelayClient_GetSocket(fwd->relay_client);

	// If sender mode, pre-fill MsQuic address as 127.0.0.1:local_quic_port
	if (fwd->local_quic_port > 0)
	{
		struct sockaddr_in* addr4 = (struct sockaddr_in*)&fwd->msquic_addr;
		memset(addr4, 0, sizeof(*addr4));
		addr4->sin_family = AF_INET;
		addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr4->sin_port = htons(fwd->local_quic_port);
		fwd->msquic_addr_len = sizeof(struct sockaddr_in);
		fwd->msquic_addr_known = TRUE;
	}

	// Increase relay socket buffers to handle QUIC burst traffic
	{
		int buf_size = 1048576;  // 1MB
		setsockopt(relay_sock, SOL_SOCKET, SO_RCVBUF, (char*)&buf_size, sizeof(buf_size));
		setsockopt(relay_sock, SOL_SOCKET, SO_SNDBUF, (char*)&buf_size, sizeof(buf_size));
	}

	LOG("[RelayFwd] Forwarder thread started (relay_sock=%d, proxy_sock=%d)\n",
		relay_sock, fwd->proxy_sock);

	while (WH_ATOMIC_LOAD(&fwd->running) == 1)
	{
		// Poll both sockets with a short timeout
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(relay_sock, &readfds);
		FD_SET(fwd->proxy_sock, &readfds);

		int max_fd = (relay_sock > fwd->proxy_sock) ? relay_sock : fwd->proxy_sock;

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 10000;  // 10ms — lower latency for relay-forwarded QUIC

		int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
		if (ready <= 0) continue;

		// --- Relay socket: receive packets from relay server ---
		if (FD_ISSET(relay_sock, &readfds))
		{
			struct sockaddr_storage from_addr;
			socklen_t from_len = sizeof(from_addr);
			int recv_len = recvfrom(relay_sock, (char*)buf, sizeof(buf), 0,
			                        (struct sockaddr*)&from_addr, &from_len);

			if (recv_len > 0)
			{
				if (IS_QUIC_PACKET(buf[0]))
				{
					// Forwarded QUIC packet from relay — send to MsQuic via proxy socket
					if (fwd->msquic_addr_known)
					{
						int ret = sendto(fwd->proxy_sock, (const char*)buf, recv_len, 0,
						       (struct sockaddr*)&fwd->msquic_addr, fwd->msquic_addr_len);
						if (ret < 0)
						{
							static double last_error_log_proxy = 0;
							double err_now = WH_TIMER_NOW();
							if (err_now - last_error_log_proxy > 5.0)
							{
								#ifdef _WIN32
								printf("[relay_forwarder] sendto proxy failed: %d\n", WSAGetLastError());
								#else
								printf("[relay_forwarder] sendto proxy failed: %d\n", errno);
								#endif
								last_error_log_proxy = err_now;
							}
						}
					}
				}
				else
				{
					// Relay protocol message (keepalive etc.) — already consumed by
					// recvfrom() above. Don't call RelayClient_Poll() here because it
					// would do a second recvfrom() on the same socket, consuming the
					// next packet (likely a QUIC packet) and misinterpreting it.
					// Keepalives are sent independently (lines below).
				}
			}
		}

		// --- Proxy socket: receive QUIC packets from MsQuic ---
		if (FD_ISSET(fwd->proxy_sock, &readfds))
		{
			struct sockaddr_storage from_addr;
			socklen_t from_len = sizeof(from_addr);
			int recv_len = recvfrom(fwd->proxy_sock, (char*)buf, sizeof(buf), 0,
			                        (struct sockaddr*)&from_addr, &from_len);

			if (recv_len > 0)
			{
				// Remember MsQuic's source address (for receiver mode — learn once only)
				if (!fwd->msquic_addr_known)
				{
					memcpy(&fwd->msquic_addr, &from_addr, from_len);
					fwd->msquic_addr_len = from_len;
					fwd->msquic_addr_known = TRUE;
				}

				// Wrap in FORWARD and send to relay
				if (!RelayClient_ForwardPacket(fwd->relay_client, fwd->remote_peer_id,
				                               buf, (uint16_t)recv_len))
				{
					static double last_error_log_fwd = 0;
					double err_now = WH_TIMER_NOW();
					if (err_now - last_error_log_fwd > 5.0)
					{
						printf("[relay_forwarder] ForwardPacket failed\n");
						last_error_log_fwd = err_now;
					}
				}
			}
		}

		// Send keepalive periodically (relay_client handles timing internally
		// through Poll, but we do an explicit one every ~10s as backup)
		double now = WH_TIMER_NOW();
		if (now - fwd->last_keepalive > 10.0)
		{
			RelayClient_SendKeepalive(fwd->relay_client);
			fwd->last_keepalive = now;
		}
	}

	LOG("[RelayFwd] Forwarder thread exiting\n");
	return 0;
}

RELAY_FORWARDER* RelayForwarder_Start(const RELAY_FORWARDER_CONFIG* config)
{
	if (!config || !config->relay_host || !config->keypair)
	{
		return NULL;
	}

	RELAY_FORWARDER* fwd = (RELAY_FORWARDER*)calloc(1, sizeof(RELAY_FORWARDER));
	if (!fwd) return NULL;

	memcpy(fwd->remote_peer_id, config->remote_peer_id, 32);
	fwd->local_quic_port = config->local_quic_port;

	// Create relay client for the forwarder (separate registration)
	RELAY_CLIENT_CONFIG relay_cfg;
	memset(&relay_cfg, 0, sizeof(relay_cfg));
	relay_cfg.relay_host = config->relay_host;
	relay_cfg.relay_port = config->relay_port;
	relay_cfg.local_port = 0;  // Ephemeral port
	relay_cfg.keypair = config->keypair;
	relay_cfg.on_connected = fwd_on_connected;
	relay_cfg.on_disconnected = fwd_on_disconnected;
	relay_cfg.on_ticket_created = NULL;
	relay_cfg.on_peer_info = NULL;
	relay_cfg.callback_context = fwd;

	fwd->relay_client = RelayClient_Create(&relay_cfg);
	if (!fwd->relay_client)
	{
		LOG_ERROR("[RelayFwd] Failed to create relay client\n");
		free(fwd);
		return NULL;
	}

	// Register with relay
	if (config->our_endpoints && config->our_endpoint_count > 0)
	{
		if (!RelayClient_Register(fwd->relay_client, config->our_endpoints,
		                          config->our_endpoint_count))
		{
			LOG_ERROR("[RelayFwd] Failed to register with relay\n");
			RelayClient_Destroy(fwd->relay_client);
			free(fwd);
			return NULL;
		}
	}

	// Wait for registration (up to 5 seconds)
	for (int i = 0; i < 50; i++)
	{
		if (RelayClient_IsConnected(fwd->relay_client)) break;
		RelayClient_Poll(fwd->relay_client, 100);
	}

	if (!RelayClient_IsConnected(fwd->relay_client))
	{
		LOG_ERROR("[RelayFwd] Relay registration timed out\n");
		RelayClient_Destroy(fwd->relay_client);
		free(fwd);
		return NULL;
	}

	// Create proxy socket on loopback
	fwd->proxy_sock = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fwd->proxy_sock < 0)
	{
		LOG_ERROR("[RelayFwd] Failed to create proxy socket\n");
		RelayClient_Destroy(fwd->relay_client);
		free(fwd);
		return NULL;
	}

	// Increase socket buffers to handle QUIC burst traffic
	{
		int buf_size = 1048576;  // 1MB
		setsockopt(fwd->proxy_sock, SOL_SOCKET, SO_RCVBUF, (char*)&buf_size, sizeof(buf_size));
		setsockopt(fwd->proxy_sock, SOL_SOCKET, SO_SNDBUF, (char*)&buf_size, sizeof(buf_size));
	}

	struct sockaddr_in bind_addr;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind_addr.sin_port = 0;  // Let OS assign port

	if (bind(fwd->proxy_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0)
	{
		LOG_ERROR("[RelayFwd] Failed to bind proxy socket\n");
#ifdef _WIN32
		closesocket(fwd->proxy_sock);
#else
		close(fwd->proxy_sock);
#endif
		RelayClient_Destroy(fwd->relay_client);
		free(fwd);
		return NULL;
	}

	// Get the assigned port
	struct sockaddr_in actual_addr;
	socklen_t actual_len = sizeof(actual_addr);
	getsockname(fwd->proxy_sock, (struct sockaddr*)&actual_addr, &actual_len);
	fwd->proxy_port = ntohs(actual_addr.sin_port);

	LOG("[RelayFwd] Proxy socket bound to 127.0.0.1:%u\n", fwd->proxy_port);

	// Start forwarder thread
	fwd->running = 1;
	if (!WH_THREAD_CREATE(fwd->thread, ForwarderThread, fwd))
	{
		LOG_ERROR("[RelayFwd] Failed to create forwarder thread\n");
#ifdef _WIN32
		closesocket(fwd->proxy_sock);
#else
		close(fwd->proxy_sock);
#endif
		RelayClient_Destroy(fwd->relay_client);
		free(fwd);
		return NULL;
	}

	LOG("[RelayFwd] Started (proxy port %u, forwarding to peer)\n", fwd->proxy_port);
	return fwd;
}

uint16_t RelayForwarder_GetProxyPort(RELAY_FORWARDER* fwd)
{
	return fwd ? fwd->proxy_port : 0;
}

BOOLEAN RelayForwarder_IsReady(RELAY_FORWARDER* fwd)
{
	return fwd && RelayClient_IsConnected(fwd->relay_client);
}

void RelayForwarder_Stop(RELAY_FORWARDER* fwd)
{
	if (!fwd) return;

	LOG("[RelayFwd] Stopping...\n");

	// Signal thread to stop
	WH_ATOMIC_SET(&fwd->running, 0);

	// Close proxy socket to unblock any pending select()/recvfrom() in the thread
	if (fwd->proxy_sock >= 0)
	{
#ifdef _WIN32
		closesocket(fwd->proxy_sock);
#else
		close(fwd->proxy_sock);
#endif
		fwd->proxy_sock = -1;
	}

	// Wait for thread to finish (up to 5 seconds)
	WH_THREAD_JOIN(fwd->thread, 5000);

	// Clean up relay
	if (fwd->relay_client)
	{
		RelayClient_SendGoodbye(fwd->relay_client, 0x02);
		RelayClient_Destroy(fwd->relay_client);
	}

	free(fwd);
	LOG("[RelayFwd] Stopped\n");
}
