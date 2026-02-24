# Building Wormhole on Linux

## Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libsodium-dev libssl-dev git
```

## Step 1: Build MsQuic from source

MsQuic is a git submodule. Initialize it first, then build:

```bash
cd /path/to/Wormhole

# Init submodule (fetches MsQuic + its OpenSSL dependency)
git submodule update --init --recursive

# Fix CRLF line endings if checked out on Windows (OpenSSL scripts need LF)
cd msquic/submodules/openssl
find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.pl' -o -name '*.pm' \
  -o -name 'config' -o -name 'Configure' -o -name 'Makefile*' -o -name '*.sh' \) \
  -exec sed -i 's/\r$//' {} +
chmod +x config Configure
cd ../../..

# Build MsQuic with OpenSSL TLS backend
cd msquic
mkdir -p build && cd build
cmake -G "Unix Makefiles" -DQUIC_TLS_LIB=openssl -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release -- -j$(nproc)
```

This produces `msquic/build/bin/Release/libmsquic.so`.

## Step 1.5: Fix CRLF line endings (if cloned on Windows)

If the repo was checked out on Windows, the Makefile and test script may have CRLF line endings. Fix them:

```bash
cd /path/to/Wormhole
sed -i 's/\r$//' src/Makefile src/test/test_linux.sh
```

## Step 2: Build Wormhole

```bash
cd /path/to/Wormhole/src
make
```

This builds both binaries into `src/build/`:
- `build/wormhole` -- CLI client
- `build/wormholed` -- daemon

You can also build individually:

```bash
make wormhole     # CLI only
make wormholed    # daemon only
make clean        # remove all build artifacts
```

If MsQuic is in a non-standard location, override the path:

```bash
make MSQUIC_ROOT=/path/to/msquic MSQUIC_BUILD=/path/to/msquic/build
```

## Step 3: Run the unit tests

```bash
cd /path/to/Wormhole/src/test
./test_linux.sh
```

This compiles and runs all 18 test suites. Each test links only what it needs (no MsQuic required for most tests).

## Step 4: Smoke test the daemon

```bash
# Terminal 1: Start the daemon
cd /path/to/Wormhole/src/build
./wormholed

# Terminal 2: Interact via CLI
cd /path/to/Wormhole/src/build
./wormhole status
echo "hello" > /tmp/test.txt
./wormhole store /tmp/test.txt
./wormhole get <hash> -o /tmp/out.txt    # use hash from store output
```

## Step 5: Build the GUI (optional)

The Qt GUI is a separate CMake project. It requires Qt 6:

```bash
sudo apt install qt6-base-dev
```

Build:

```bash
cd /path/to/Wormhole/gui
cmake -B build_linux
cmake --build build_linux
```

Run:

```bash
./build_linux/wormhole-gui
```

The daemon must be running for the GUI to connect. Start it first with `wormhole daemon start` or `wormholed --port 4567`.

---

# Docker

Docker builds are fully self-contained -- no local toolchain needed. There are two images:
- **wormhole-node** -- daemon + CLI (multi-stage: builds MsQuic, then Wormhole)
- **wormhole-relay** -- relay server

## Build the images

```bash
cd /path/to/Wormhole

docker build -t wormhole-relay -f docker/Dockerfile.relay .
docker build -t wormhole-node -f docker/Dockerfile .
```

## Manual setup (without compose)

### Create the network

```bash
docker network create wormhole-net
```

### Start the relay

```bash
docker run -d \
  --name relay \
  --network wormhole-net \
  -p 8443:443/udp \
  wormhole-relay
```

### Start nodes

```bash
# Node 1
docker run -d \
  --name node1 \
  --network wormhole-net \
  -e RELAY_HOST=relay \
  -e RELAY_PORT=443 \
  -v node1-data:/root/.wormhole \
  wormhole-node

# Node 2
docker run -d \
  --name node2 \
  --network wormhole-net \
  -e RELAY_HOST=relay \
  -e RELAY_PORT=443 \
  -v node2-data:/root/.wormhole \
  wormhole-node

# Node 3
docker run -d \
  --name node3 \
  --network wormhole-net \
  -e RELAY_HOST=relay \
  -e RELAY_PORT=443 \
  -v node3-data:/root/.wormhole \
  wormhole-node
```

### Interact with nodes

```bash
docker logs -f node1                          # view daemon logs
docker exec node1 wormhole status             # check status
```

### Store and retrieve files

```bash
# Create a test file inside node1
docker exec node1 sh -c 'dd if=/dev/urandom of=/tmp/test.bin bs=1024 count=100'

# Store it (outputs a hash)
docker exec node1 wormhole store /tmp/test.bin

# Wait ~30s for replication, then retrieve from another node
docker exec node3 wormhole get <hash> -o /tmp/retrieved.bin
```

### Interactive debugging

```bash
docker exec -it node1 bash

# Inside the container:
wormhole status
ls -la /root/.wormhole/store/
cat /root/.wormhole/config
```

### Simulate node failure

```bash
docker stop node2              # kill a node
docker logs --tail 50 node1    # check if others detect it
docker start node2             # bring it back
```

### Cleanup

```bash
docker stop relay node1 node2 node3
docker rm relay node1 node2 node3
docker volume rm node1-data node2-data node3-data
docker network rm wormhole-net
```

## Using docker compose

```bash
cd /path/to/Wormhole/docker

# Build and start everything (relay + 5 nodes)
docker compose up -d

# Interact with nodes
docker compose exec node1 wormhole status
docker compose exec node1 wormhole store /tmp/test.bin
docker compose exec node3 wormhole get <hash> -o /tmp/out.bin
docker compose logs -f node1

# Stop one node
docker compose stop node2

# Tear down everything
docker compose down -v
```


Each container connects to the real relay at wormholerelay.com:443 (the hardcoded default). Usage:

  cd docker
  docker compose build                    # Build image (~5-10 min first time)
  docker compose up -d                    # Start 5 containers
  docker exec -it docker-node1-1 bash     # Shell into any node
  
  To exit a container's shell: type exit or press Ctrl+D

  # Inside container:
  wormholed &                             # Start daemon
  wormhole status                         # Check connectivity
  wormhole store /tmp/test.bin            # Store a file
