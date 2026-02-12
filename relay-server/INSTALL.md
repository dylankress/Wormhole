# Installation Instructions

## Prerequisites

You need to install GCC in WSL to compile the relay server. Run this command in your WSL terminal:

```bash
sudo apt-get update
sudo apt-get install -y build-essential
```

This will install GCC and other essential build tools.

## Verify Installation

```bash
gcc --version
```

You should see output like:
```
gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
```

## Build the Relay Server

Once GCC is installed, run:

```bash
cd /mnt/c/Dev/Wormhole/relay-server
./build.sh
```

This will create `build/relay-server` executable.

## Test the Server

Run the server on a non-privileged port (no sudo required):

```bash
./build/relay-server -p 8080
```

You should see output like:
```
===============================================
  Wormhole Relay Server v0.1.0
  Port: 8080
  Max Peers: 10000
  Max Tickets: 5000
  Wordlist: ../deps/eff_large_wordlist.txt
===============================================

[Crypto] libsodium initialized (version: 1.0.18)
[PeerRegistry] Initialized with 16384 buckets
[TicketManager] Loaded 7776 words from wordlist
[RateLimiter] Initialized with 1024 buckets (max 1000 packets/sec)
[Server] Initialized on port 8080 (max peers: 10000, max tickets: 5000)
[Server] Running (listening for packets)...
[Main] Press Ctrl+C to stop server
```

Press Ctrl+C to stop the server gracefully.

## Alternative: Windows Build

If you prefer to build on Windows (not in WSL), you need:

1. **Visual Studio 2019 or later** with C++ build tools
2. Run `build.bat` from a **Developer Command Prompt**:

```batch
cd C:\Dev\Wormhole\relay-server
build.bat
```

This will create `build\relay-server.exe` and copy `libsodium.dll` to the build directory.

Run the server:
```batch
cd build
relay-server.exe -p 8080
```
