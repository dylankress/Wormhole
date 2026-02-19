#!/bin/bash
#
# build.sh
# Build script for Wormhole Relay Client Test
# by Dylan Kress
#

set -e

echo "Building Wormhole Relay Client Test..."

# Create build directory
mkdir -p build

# Compiler and flags
CC=gcc
CFLAGS="-Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -I."
LDFLAGS="-lpthread -lm -lsodium"

# Source files
SOURCES="test_relay_client.c peer_id.c relay_client.c discovery.c ticket.c"

# Output binary
OUTPUT="build/test_relay_client"

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
