//
// connection.h
// by Dylan Kress
//

#pragma once

#include "common.h"

// Server-side callbacks
QUIC_STATUS QUIC_API ServerListenerCallback(
    HQUIC Listener,
    void *Context,
    QUIC_LISTENER_EVENT *Event
);

QUIC_STATUS QUIC_API ServerConnectionCallback(
    HQUIC Connection,
    void *Context,
    QUIC_CONNECTION_EVENT *Event
);

// Client-side callback
QUIC_STATUS QUIC_API ClientConnectionCallback(
    HQUIC Connection,
    void *Context,
    QUIC_CONNECTION_EVENT *Event
);
