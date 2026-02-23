# Dockerfile — Wormhole node (daemon + CLI)
# Multi-stage build: MsQuic → Wormhole → runtime image

# ─── Stage 1: Build MsQuic ─────────────────────────────────────────────
FROM ubuntu:22.04 AS build-msquic

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    libssl-dev && \
    rm -rf /var/lib/apt/lists/*

COPY msquic/ /build/msquic/

# Fix CRLF line endings from Windows checkout (OpenSSL scripts need LF)
RUN find /build/msquic/submodules/openssl -type f \( -name '*.c' -o -name '*.h' \
    -o -name '*.pl' -o -name '*.pm' -o -name 'config' -o -name 'Configure' \
    -o -name 'Makefile*' -o -name '*.sh' \) -exec sed -i 's/\r$//' {} + && \
    chmod +x /build/msquic/submodules/openssl/config \
             /build/msquic/submodules/openssl/Configure

WORKDIR /build/msquic
RUN rm -rf build
RUN mkdir -p build && cd build && \
    cmake -G "Unix Makefiles" \
        -DQUIC_TLS_LIB=openssl \
        -DCMAKE_BUILD_TYPE=Release \
        -DQUIC_BUILD_TOOLS=OFF \
        -DQUIC_BUILD_TEST=OFF \
        -DQUIC_BUILD_PERF=OFF \
        .. && \
    cmake --build . --config Release -- -j$(nproc)

# ─── Stage 2: Build Wormhole ───────────────────────────────────────────
FROM ubuntu:22.04 AS build-wormhole

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential libsodium-dev libssl-dev && \
    rm -rf /var/lib/apt/lists/*

# Copy MsQuic artifacts
COPY --from=build-msquic /build/msquic/src/inc/ /msquic/src/inc/
COPY --from=build-msquic /build/msquic/build/bin/ /msquic/build/bin/

# Copy wormhole source + deps
COPY src/ /build/src/
COPY deps/ /build/deps/
COPY relay-server/ /build/relay-server/

# Fix CRLF line endings in Makefile (may come from Windows checkout)
RUN sed -i 's/\r$//' /build/src/Makefile

WORKDIR /build/src

# Point Makefile at the copied MsQuic
RUN make CC=gcc \
    MSQUIC_ROOT=/msquic \
    MSQUIC_BUILD=/msquic/build

# ─── Stage 3: Runtime ──────────────────────────────────────────────────
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsodium23 openssl ca-certificates procps && \
    rm -rf /var/lib/apt/lists/*

# MsQuic shared library
COPY --from=build-msquic /build/msquic/build/bin/Release/libmsquic.so* /usr/local/lib/
RUN ldconfig

# Wormhole binaries
COPY --from=build-wormhole /build/src/build/wormhole /usr/local/bin/
COPY --from=build-wormhole /build/src/build/wormholed /usr/local/bin/

# EFF wordlist for ticket generation
COPY deps/eff_large_wordlist.txt /usr/local/share/wormhole/

# Create wormhole data directory
RUN mkdir -p /root/.wormhole && chmod 700 /root/.wormhole

EXPOSE 4567/udp 4568/udp

ENTRYPOINT ["wormholed"]
CMD ["--port", "4567", "--dht-port", "4568"]
