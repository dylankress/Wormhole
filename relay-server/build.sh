#!/bin/bash
#
# build.sh
# Build script for Wormhole Relay Server (Linux)
# by Dylan Kress
#

set -e  # Exit on error

echo "Building Wormhole Relay Server (Linux)..."

# Create build directory
mkdir -p build

# Compiler and flags
CC=gcc
CFLAGS="-Wall -Wextra -O2 -std=c11 -I../deps/libsodium/include"
LDFLAGS="-lpthread -lm -lsodium"

# Note: For libsodium on Linux, install via: sudo apt-get install libsodium-dev
# Or build from source and adjust LDFLAGS to include -L path

# Source files
SOURCES="main.c server.c peer_registry.c ticket_manager.c rate_limiter.c crypto.c"

# Output binary
OUTPUT="build/relay-server"

# Compile
echo "Compiling..."
$CC $CFLAGS $SOURCES $LDFLAGS -o $OUTPUT

if [ $? -eq 0 ]; then
    echo "Build successful: $OUTPUT"
    ls -lh $OUTPUT
else
    echo "Build failed!"
    exit 1
fi
