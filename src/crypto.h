//
// crypto.h
// by Dylan Kress
//

#pragma once

#include "common.h"

// Generate self-signed certificate for TLS
// thumbprint_out: Must be at least 41 bytes (40 hex chars + null terminator)
// Returns TRUE on success, FALSE on failure
BOOLEAN GenerateSelfSignedCert(char *thumbprint_out, size_t thumbprint_size);
