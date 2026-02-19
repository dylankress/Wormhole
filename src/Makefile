# Makefile — Wormhole Linux build
# Builds wormhole (CLI) and wormholed (daemon)
#
# Prerequisites:
#   - MsQuic built from source: cd msquic && mkdir build && cd build && \
#       cmake -G "Unix Makefiles" -DQUIC_TLS_LIB=openssl .. && cmake --build . --config Release
#   - System packages: libsodium-dev openssl
#
# Usage:
#   make              # Build both binaries
#   make wormhole     # Build CLI only
#   make wormholed    # Build daemon only
#   make clean        # Remove build artifacts

CC      ?= gcc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -O2 -std=gnu11 \
          -D_GNU_SOURCE \
          -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 \
          -DQUIC_API_ENABLE_PREVIEW_FEATURES
LDFLAGS = -lmsquic -lsodium -lpthread -lm

# Paths — adjust MSQUIC_BUILD if your build directory differs
MSQUIC_ROOT  = ../msquic
MSQUIC_INC   = $(MSQUIC_ROOT)/src/inc
MSQUIC_BUILD = $(MSQUIC_ROOT)/build

BLAKE3_ROOT  = ../deps/blake3
RS_ROOT      = ../deps/reed_solomon
SODIUM_INC   = /usr/include

INCLUDES = -I$(MSQUIC_INC) -I$(BLAKE3_ROOT) -I$(RS_ROOT) -I$(SODIUM_INC) -I.

# Add MsQuic library path (for linking and runtime)
LDFLAGS += -L$(MSQUIC_BUILD)/bin/Release -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(MSQUIC_BUILD)/bin/Release

BUILDDIR = build

# Shared sources (used by both wormhole and wormholed)
SHARED_SOURCES = \
	stream.c \
	file_io.c \
	crypto.c \
	manifest.c \
	chunker.c \
	chunk_store.c \
	transfer_state.c \
	config.c \
	ipc.c \
	erasure.c \
	proof.c \
	incentives.c \
	health.c \
	file_registry.c \
	relay_forwarder.c \
	relay/peer_id.c \
	relay/relay_client.c \
	relay/discovery.c \
	relay/ticket.c \
	dht/routing_table.c \
	dht/dht_node.c \
	dht/dht_store.c \
	dht/dht_lookup.c

DEP_SOURCES = \
	$(BLAKE3_ROOT)/blake3.c \
	$(BLAKE3_ROOT)/blake3_dispatch.c \
	$(BLAKE3_ROOT)/blake3_portable.c \
	$(RS_ROOT)/rs.c

CLIENT_SOURCES  = wormhole.c $(SHARED_SOURCES) $(DEP_SOURCES)
DAEMON_SOURCES  = wormholed.c $(SHARED_SOURCES) $(DEP_SOURCES)

# Object files go into build/
CLIENT_OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(notdir $(CLIENT_SOURCES)))
DAEMON_OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(notdir $(DAEMON_SOURCES)))

# VPATH for finding source files in subdirectories
VPATH = . relay dht $(BLAKE3_ROOT) $(RS_ROOT)

.PHONY: all clean wormhole wormholed

all: $(BUILDDIR)/wormhole $(BUILDDIR)/wormholed

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/wormhole: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	@cp -a $(MSQUIC_BUILD)/bin/Release/libmsquic.so* $(BUILDDIR)/ 2>/dev/null || true
	@echo "Built: $@"

$(BUILDDIR)/wormholed: $(DAEMON_OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

wormhole: $(BUILDDIR)/wormhole
wormholed: $(BUILDDIR)/wormholed

clean:
	rm -rf $(BUILDDIR)
