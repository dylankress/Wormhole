//
// crypto.h
// by Dylan Kress
//

#pragma once

#include "common.h"

// Generate self-signed certificate for TLS.
//
// Windows: Generates/finds cert in Windows Certificate Store.
//   thumbprint_out: Must be at least 41 bytes (40 hex chars + null terminator).
//   Returns TRUE on success, FALSE on failure.
//
// Linux: Generates PEM cert+key files in ~/.wormhole/ via openssl CLI.
//   cert_path_out: Receives the path to the cert.pem file.
//   path_size: Size of the output buffer.
//   Returns TRUE on success, FALSE on failure.
//
// On Linux the key path is always the sibling "key.pem" in the same directory.
BOOLEAN GenerateSelfSignedCert(char *thumbprint_or_path_out, size_t buffer_size);

// Generate self-signed cert with Ed25519 node ID embedded in CN as hex string.
// Forces regeneration to ensure CN matches current identity.
BOOLEAN GenerateSelfSignedCertWithNodeId(char *thumbprint_or_path_out, size_t buffer_size,
                                          const uint8_t node_id[32]);

// Verify that a peer's TLS certificate contains the expected node ID in its CN.
// certificate: platform-specific cert (X509* on Linux, PCCERT_CONTEXT on Windows)
// Returns TRUE if CN matches hex of expected_node_id (64 hex chars)
BOOLEAN Crypto_VerifyPeerCert(void *certificate, const uint8_t expected_node_id[32]);

// Helper: get paths to PEM cert/key (Linux only, returns FALSE on Windows)
#ifndef _WIN32
BOOLEAN Crypto_GetCertPaths(char *cert_path, size_t cert_len,
                             char *key_path, size_t key_len);
#endif
