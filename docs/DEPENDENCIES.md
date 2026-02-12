Wormhole Dependencies

 Windows

 Libsodium (Cryptography)
**Pre-built binaries included** for Windows x64 in `deps/libsodium/`.
No additional setup required for Windows builds.

 MsQuic (QUIC Protocol)
**Included as git submodule.**
Initialize submodule after cloning:
git submodule update --init --recursive
Build MsQuic (one-time setup):
cd msquic
.\scripts\build.ps1 -Config Debug -Arch x64

Linux

Libsodium
Install via package manager:
sudo apt-get install libsodium-dev  # Ubuntu/Debian
sudo dnf install libsodium-devel    # Fedora/RHEL

MsQuic
Not required for relay server (relay server uses UDP only, not QUIC).
Required for full client (future Linux client support).

Building Relay Server (Linux)
cd relay-server
./build.sh

Dependencies:
- GCC or Clang
- libsodium-dev
- pthread (included in glibc)

Updating Dependencies

Update libsodium
Download from: https://download.libsodium.org/libsodium/releases/
For Windows, extract to deps/libsodium/ maintaining directory structure.

Update MsQuic
git submodule update --remote msquic
