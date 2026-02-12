All notable changes to Wormhole will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
 [0.9.0] - 2026-02-12
 Added
- Relay server for peer coordination and NAT traversal
- Ed25519 peer identity and authentication
- Ticket-based file transfer system (e.g., "3-guitar-battery")
- Cross-network relay fallback when direct connection fails
- Support for files > 2GB (tested up to 5GB)
- 64-bit file size support (Windows: _fseeki64, Linux: fseeko)
- Extended transfer timeout (60 minutes for large files)
- File integrity verification (MD5 checksums)
- Downloads folder integration (automatic file placement)
 Fixed
- File size limitation (was 2GB, now supports 5GB+)
- Transfer timeouts for large files
- Rate limiting false positives (keepalive ping-pong loop)
- File path handling (Downloads folder vs current directory)
- Transfer speed calculation accuracy
 Performance
- Direct LAN: 40-100 Mbps
- Relay forwarding: 20-50 Mbps
- Tested file sizes: 89 bytes to 5GB (100% integrity)
 [0.1.0] - 2026-02-11
 Added
- Initial QUIC-based file transfer implementation
- Local LAN file transfers
- Basic send/receive commands
- MsQuic integration (Windows)
- Certificate generation for TLS
- 64KB chunked streaming
