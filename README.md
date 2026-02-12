Wormhole - Secure Peer-to-Peer File Transfer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-0.9.0-blue.svg)](https://github.com/dylankress/Wormhole)

> **Status:** Beta - Actively developed and tested. Ready for local/LAN use. Production relay deployment in progress.
Wormhole is a secure, encrypted peer-to-peer file transfer system built with modern protocols. Send files of any size directly to another machine with just a simple ticket code.

 ✨ Features
- 🔐 **End-to-end encryption** - QUIC with TLS 1.3
- 🚀 **Direct peer connections** - LAN, IPv6, NAT traversal
- 🌐 **Relay fallback** - Works even behind restrictive firewalls
- 🔑 **Ed25519 authentication** - Cryptographic peer identity
- 🎫 **User-friendly tickets** - Share files with codes like "3-guitar-battery"
- 📦 **Large file support** - Tested up to 5GB with 100% integrity
- 💻 **Cross-platform** - Windows and Linux

 🚀 Quick Start
 Prerequisites
**Windows:**
- Visual Studio 2019+ or Build Tools for Visual Studio
- Git (for cloning and submodules)
**Linux:**
- GCC and build-essential
- libsodium-dev: `sudo apt-get install libsodium-dev`

 Build from Source
# Clone repository
git clone --recursive https://github.com/dylankress/Wormhole.git
cd Wormhole
# Windows
cd src
build_with_env.bat
# Linux (relay server)
cd relay-server
./build.sh
Usage
Send a file:
cd src\build
wormhole.exe send myfile.pdf

Output:
╔═══════════════════════════════════════════════════════╗
║  Your ticket:                                         ║
║      3-reoccupy-carload                              ║
╚═══════════════════════════════════════════════════════╝

Receive a file:
wormhole.exe receive 3-reoccupy-carload
Files are automatically saved to your Downloads folder.

📊 Performance
Tested Configurations:
- ✅ Direct LAN: 40-100 Mbps
- ✅ Cross-network (relay): 20-50 Mbps
- ✅ File sizes: 89 bytes to 5GB (100% integrity verified)
- ✅ Connection time: 10-50ms (LAN), 100-500ms (relay)

🏗️ Architecture
Wormhole uses a hybrid architecture:
1. Relay Server - Coordinates peers and provides fallback routing
   - Ed25519 signature verification
   - Ticket-based peer discovery
   - NAT reflection (tells clients their public IP)
   - Packet forwarding when direct connection fails
2. QUIC Transport - Direct peer-to-peer file streaming
   - TLS 1.3 encryption
   - Multiplexed streams
   - Congestion control
   - 64KB chunked transfer
3. Multi-path Connection - Tries multiple routes in parallel
   - LAN (priority 0 - fastest)
   - IPv6 (priority 75)
   - Public IP with hole punching (priority 100)
   - Relay forwarding (priority 200 - fallback)

📁 Project Structure
Wormhole/
├── src/                    # Main wormhole client
│   ├── wormhole.c         # CLI and main logic
│   ├── connection.c       # QUIC connection management
│   ├── stream.c           # File streaming
│   ├── file_io.c          # File operations (64-bit support)
│   └── relay/             # Relay client components
│       ├── peer_id.c      # Ed25519 identity
│       ├── relay_client.c # Relay protocol
│       ├── discovery.c    # Endpoint discovery
│       └── ticket.c       # Ticket display
│
├── relay-server/          # Relay server (UDP)
│   ├── server.c           # Main server loop
│   ├── peer_registry.c    # Peer tracking
│   ├── ticket_manager.c   # Ticket generation
│   ├── rate_limiter.c     # DoS protection
│   └── crypto.c           # Ed25519 verification
│
└── deps/
    ├── eff_large_wordlist.txt  # 7776 words for tickets
    └── libsodium/              # Cryptography library

🔧 Building for Production
Deploy Relay Server
The relay server enables cross-network transfers. Deploy to a public VPS:
# On Ubuntu 22.04 droplet
sudo apt-get update
sudo apt-get install -y build-essential libsodium-dev
# Clone and build
git clone --recursive https://github.com/dylankress/Wormhole.git
cd Wormhole/relay-server
./build.sh
# Run on port 443 (requires root or CAP_NET_BIND_SERVICE)
sudo ./build/relay-server -p 443
Firewall configuration:
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 443/udp  # Wormhole relay
sudo ufw enable
Configure Client for Production Relay
Edit src/wormhole.c before building:
#define DEFAULT_RELAY_HOST "your-server.com"  // Your relay IP/domain
#define DEFAULT_RELAY_PORT 443
Rebuild and distribute the binary.

📖 Documentation
- AGENTS.md (AGENTS.md) - Comprehensive developer guide
- CHANGELOG.md (CHANGELOG.md) - Version history
- relay-server/README.md (relay-server/README.md) - Relay server details
- src/relay/README.md (src/relay/README.md) - Relay client components

🧪 Testing
Generate test files:
# Create various test file sizes
dd if=/dev/urandom of=test_10mb.bin bs=1M count=10
dd if=/dev/urandom of=test_100mb.bin bs=1M count=100
dd if=/dev/urandom of=test_1gb.bin bs=1M count=1024
Verify integrity:
md5sum test_*.bin  # Before transfer
md5sum ~/Downloads/test_*.bin  # After transfer

🐛 Troubleshooting
"Failed to connect to relay (timeout)"
- Check relay server is running
- Verify firewall allows UDP 443
- Confirm DEFAULT_RELAY_HOST is correct
"Ticket not found or expired"
- Tickets expire after 1 hour
- Verify ticket format: N-word-word
- Check relay server logs
"Could not connect to sender"
- Both machines registered with relay?
- Try ping/traceroute to verify connectivity
- Should fall back to relay forwarding automatically

🔐 Security
- Encryption: All file data encrypted with TLS 1.3 via QUIC
- Authentication: Ed25519 signatures prevent peer spoofing
- Privacy: Relay server only sees encrypted QUIC packets
- Rate limiting: 1000 packets/sec per IP (DoS protection)
Future security enhancements:
- Blake3 file verification (chunk-level integrity)
- Certificate pinning with Ed25519 keys
- Onion routing for enhanced privacy

🤝 Contributing
Contributions welcome! This project is in active development.
Areas of focus:
- Cross-platform testing (macOS, Linux desktop)
- Performance optimization
- User experience improvements
- Documentation

📄 License
MIT License - see LICENSE (LICENSE) file for details.

🙏 Acknowledgments
- MsQuic - Microsoft's QUIC implementation
- libsodium - Modern cryptography library
- EFF - Electronic Frontier Foundation (wordlist)

🗺️ Roadmap
v1.0 (Next):
- [ ] Production relay deployment
- [ ] Progress bars for large files
- [ ] Resume interrupted transfers
- [ ] Binary releases (Windows/Linux)
v1.1 (Future):
- [ ] Blake3 integrity verification
- [ ] Multiple file transfers
- [ ] Directory transfers
- [ ] Compression support
- [ ] DHT integration for decentralized relay discovery

---

Made with ❤️ by Dylan Kress
