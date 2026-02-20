//
// crypto.c
// by Dylan Kress
//

#include "crypto.h"

// Helper: parse 64-hex-char CN and compare to expected node_id
static BOOLEAN VerifyCN(const char *cn, const uint8_t expected_node_id[32])
{
	if (!cn) return FALSE;
	if (strlen(cn) < 64) return FALSE;  // 64 hex chars

	uint8_t parsed[32];
	for (int i = 0; i < 32; i++)
	{
		char hb[3] = { cn[i * 2], cn[i * 2 + 1], 0 };
		char *ep;
		unsigned long val = strtoul(hb, &ep, 16);
		if (*ep != '\0') return FALSE;
		parsed[i] = (uint8_t)val;
	}
	return memcmp(parsed, expected_node_id, 32) == 0;
}

#ifdef _WIN32

#include <wincrypt.h>

// Helper: Validate that a string contains exactly 40 hexadecimal characters
static BOOLEAN IsValidThumbprint(const char *str)
{
	if (str == NULL) {
		return FALSE;
	}

	// Must be exactly 40 characters
	size_t len = strlen(str);
	if (len != 40) {
		return FALSE;
	}

	// Every character must be 0-9, A-F, or a-f
	for (size_t i = 0; i < 40; i++) {
		char c = str[i];
		if (!((c >= '0' && c <= '9') ||
			  (c >= 'A' && c <= 'F') ||
			  (c >= 'a' && c <= 'f'))) {
			return FALSE;
		}
	}

	return TRUE;
}

// Helper: Execute PowerShell command and capture output
static BOOLEAN ExecutePowerShellCommand(const char *command, char *output_buffer, size_t buffer_size)
{
	// Build the full PowerShell command
	// Using -NoProfile for faster startup, -Command to execute
	char full_command[2048];
	int result = snprintf(full_command, sizeof(full_command),
						 "powershell -NoProfile -Command \"%s\"", command);

	if (result < 0 || (size_t)result >= sizeof(full_command)) {
		LOG_ERROR("PowerShell command too long\n");
		return FALSE;
	}

	// Open pipe to execute command
	FILE *pipe = _popen(full_command, "r");
	if (pipe == NULL) {
		LOG_ERROR("Failed to execute PowerShell command\n");
		return FALSE;
	}

	// Read all output into buffer
	size_t total_read = 0;
	while (fgets(output_buffer + total_read,
				 (int)(buffer_size - total_read), pipe) != NULL) {
		total_read = strlen(output_buffer);
		if (total_read >= buffer_size - 1) {
			break;  // Buffer full
		}
	}

	// Close pipe
	int exit_code = _pclose(pipe);

	if (exit_code != 0) {
		LOG_ERROR("PowerShell command failed with exit code: %d\n", exit_code);
		return FALSE;
	}

	return total_read > 0;
}

// Helper: Parse thumbprint from PowerShell output
// Looks for a line containing exactly 40 hex characters
static BOOLEAN ParseThumbprintFromOutput(const char *output, char *thumbprint_out)
{
	if (output == NULL || thumbprint_out == NULL) {
		return FALSE;
	}

	// PowerShell output might have the thumbprint on its own line
	// or mixed with other text. We'll look for 40 consecutive hex chars.

	const char *line = output;
	char candidate[41];

	// Process line by line
	while (*line != '\0') {
		// Skip whitespace at start of line
		while (*line == ' ' || *line == '\t') {
			line++;
		}

		// Try to read 40 characters
		if (sscanf(line, "%40s", candidate) == 1) {
			// Check if it's a valid thumbprint (40 hex chars)
			if (IsValidThumbprint(candidate)) {
				// Found it! Copy to output
				strncpy(thumbprint_out, candidate, 40);
				thumbprint_out[40] = '\0';
				return TRUE;
			}
		}

		// Move to next line
		while (*line != '\0' && *line != '\n') {
			line++;
		}
		if (*line == '\n') {
			line++;
		}
	}

	LOG_ERROR("Could not find valid thumbprint in PowerShell output\n");
	return FALSE;
}

// Main function: Generate or find self-signed certificate (Windows — cert store)
BOOLEAN GenerateSelfSignedCert(char *thumbprint_out, size_t thumbprint_size)
{
	// Validate parameters
	if (thumbprint_out == NULL || thumbprint_size < 41) {
		LOG_ERROR("Invalid parameters for GenerateSelfSignedCert\n");
		return FALSE;
	}

	char output[4096];
	memset(output, 0, sizeof(output));

	//
	// Step 1: Try to find existing "Wormhole-Dev" certificate
	//
	LOG("Checking for existing Wormhole-Dev certificate...\n");

	const char *find_command =
		"Get-ChildItem -Path Cert:\\CurrentUser\\My | "
		"Where-Object {$_.FriendlyName -eq 'Wormhole-Dev'} | "
		"Select-Object -ExpandProperty Thumbprint";

	if (ExecutePowerShellCommand(find_command, output, sizeof(output))) {
		if (ParseThumbprintFromOutput(output, thumbprint_out)) {
			LOG("Found existing certificate: %s\n", thumbprint_out);
			return TRUE;
		}
	}

	//
	// Step 2: No existing cert found, generate a new one
	//
	LOG("No existing certificate found. Generating new certificate...\n");
	LOG("(This may take a few seconds)\n");

	memset(output, 0, sizeof(output));

	const char *generate_command =
		"New-SelfSignedCertificate "
		"-DnsName localhost "
		"-FriendlyName 'Wormhole-Dev' "
		"-KeyUsageProperty Sign "
		"-KeyUsage DigitalSignature "
		"-CertStoreLocation cert:\\CurrentUser\\My "
		"-HashAlgorithm SHA256 "
		"-Provider 'Microsoft Software Key Storage Provider' "
		"-KeyExportPolicy Exportable | "
		"Select-Object -ExpandProperty Thumbprint";

	if (ExecutePowerShellCommand(generate_command, output, sizeof(output))) {
		if (ParseThumbprintFromOutput(output, thumbprint_out)) {
			LOG("Successfully generated certificate: %s\n", thumbprint_out);
			return TRUE;
		}
	}

	//
	// Step 3: Both attempts failed
	//
	LOG_ERROR("ERROR: Failed to generate certificate\n");
	LOG_ERROR("Make sure PowerShell is available and you have permissions to create certificates\n");
	return FALSE;
}

BOOLEAN GenerateSelfSignedCertWithNodeId(char *thumbprint_out, size_t thumbprint_size,
                                          const uint8_t node_id[32])
{
	if (!thumbprint_out || thumbprint_size < 41 || !node_id) return FALSE;

	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", node_id[i]);

	char output[4096];
	memset(output, 0, sizeof(output));

	// Check for existing cert with matching node ID Subject
	char find_cmd[512];
	snprintf(find_cmd, sizeof(find_cmd),
		"Get-ChildItem -Path Cert:\\CurrentUser\\My | "
		"Where-Object {$_.Subject -eq 'CN=%s'} | "
		"Select-Object -ExpandProperty Thumbprint", hex);

	if (ExecutePowerShellCommand(find_cmd, output, sizeof(output)))
	{
		if (ParseThumbprintFromOutput(output, thumbprint_out))
		{
			LOG("Found existing certificate with node ID\n");
			return TRUE;
		}
	}

	// Generate new cert with node ID in Subject
	memset(output, 0, sizeof(output));
	char gen_cmd[512];
	snprintf(gen_cmd, sizeof(gen_cmd),
		"New-SelfSignedCertificate "
		"-Subject 'CN=%s' "
		"-FriendlyName 'Wormhole-Dev' "
		"-KeyUsageProperty Sign "
		"-KeyUsage DigitalSignature "
		"-CertStoreLocation cert:\\CurrentUser\\My "
		"-HashAlgorithm SHA256 "
		"-Provider 'Microsoft Software Key Storage Provider' "
		"-KeyExportPolicy Exportable | "
		"Select-Object -ExpandProperty Thumbprint", hex);

	if (ExecutePowerShellCommand(gen_cmd, output, sizeof(output)))
	{
		if (ParseThumbprintFromOutput(output, thumbprint_out))
		{
			LOG("Generated certificate with node ID\n");
			return TRUE;
		}
	}

	LOG_ERROR("Failed to generate certificate with node ID\n");
	return FALSE;
}

BOOLEAN Crypto_VerifyPeerCert(void *certificate, const uint8_t expected_node_id[32])
{
	if (!certificate || !expected_node_id) return FALSE;

	PCCERT_CONTEXT cert = (PCCERT_CONTEXT)certificate;
	char cn[128] = {0};
	CertGetNameStringA(cert, CERT_NAME_ATTR_TYPE, 0,
	                   (void *)szOID_COMMON_NAME, cn, sizeof(cn));
	return VerifyCN(cn, expected_node_id);
}

#else // Linux / POSIX

// Build the paths for PEM cert/key files in ~/.wormhole/
static BOOLEAN GetCertPaths(char *cert_path, size_t cert_len,
                             char *key_path, size_t key_len)
{
	const char *home = getenv("HOME");
	if (!home) return FALSE;

	snprintf(cert_path, cert_len, "%s/.wormhole/cert.pem", home);
	snprintf(key_path, key_len, "%s/.wormhole/key.pem", home);
	return TRUE;
}

// Public helper for other files to get cert/key paths
BOOLEAN Crypto_GetCertPaths(char *cert_path, size_t cert_len,
                              char *key_path, size_t key_len)
{
	return GetCertPaths(cert_path, cert_len, key_path, key_len);
}

BOOLEAN GenerateSelfSignedCert(char *cert_path_out, size_t path_size)
{
	if (!cert_path_out || path_size < 2) {
		LOG_ERROR("Invalid parameters for GenerateSelfSignedCert\n");
		return FALSE;
	}

	char cert_path[MAX_PATH];
	char key_path[MAX_PATH];
	if (!GetCertPaths(cert_path, sizeof(cert_path), key_path, sizeof(key_path))) {
		LOG_ERROR("Failed to determine cert paths\n");
		return FALSE;
	}

	// Ensure ~/.wormhole/ exists
	const char *home = getenv("HOME");
	if (home) {
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), "%s/.wormhole", home);
		mkdir(dir, 0700);
	}

	// Check if cert and key already exist
	FILE *f = fopen(cert_path, "r");
	if (f) {
		fclose(f);
		f = fopen(key_path, "r");
		if (f) {
			fclose(f);
			LOG("Using existing TLS certificate: %s\n", cert_path);
			snprintf(cert_path_out, path_size, "%s", cert_path);
			return TRUE;
		}
	}

	// Generate self-signed cert via openssl CLI
	LOG("Generating self-signed TLS certificate...\n");

	char cmd[1024];
	// Try -noenc first (OpenSSL 3.0+), fall back to -nodes (OpenSSL 1.x)
	snprintf(cmd, sizeof(cmd),
		"openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
		"-keyout '%s' -out '%s' -days 365 -noenc "
		"-subj '/CN=wormhole' 2>&1",
		key_path, cert_path);

	int rc = system(cmd);
	if (rc != 0) {
		// Fallback for OpenSSL 1.x which doesn't have -noenc
		snprintf(cmd, sizeof(cmd),
			"openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
			"-keyout '%s' -out '%s' -days 365 -nodes "
			"-subj '/CN=wormhole' 2>&1",
			key_path, cert_path);

		rc = system(cmd);
		if (rc != 0) {
			LOG_ERROR("openssl command failed (exit code %d). Is openssl installed?\n", rc);
			return FALSE;
		}
	}

	// Verify files were created
	f = fopen(cert_path, "r");
	if (!f) {
		LOG_ERROR("Certificate file not created: %s\n", cert_path);
		return FALSE;
	}
	fclose(f);

	f = fopen(key_path, "r");
	if (!f) {
		LOG_ERROR("Key file not created: %s\n", key_path);
		return FALSE;
	}
	fclose(f);

	LOG("Certificate generated: %s\n", cert_path);
	snprintf(cert_path_out, path_size, "%s", cert_path);
	return TRUE;
}

BOOLEAN GenerateSelfSignedCertWithNodeId(char *cert_path_out, size_t path_size,
                                          const uint8_t node_id[32])
{
	if (!cert_path_out || path_size < 2 || !node_id) return FALSE;

	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", node_id[i]);

	char cert_path[MAX_PATH], key_path[MAX_PATH];
	if (!GetCertPaths(cert_path, sizeof(cert_path), key_path, sizeof(key_path)))
		return FALSE;

	const char *home = getenv("HOME");
	if (home) {
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), "%s/.wormhole", home);
		mkdir(dir, 0700);
	}

	// Remove existing cert to regenerate with correct node ID CN
	unlink(cert_path);
	unlink(key_path);

	LOG("Generating TLS certificate with node ID...\n");

	char cmd[1024];
	// Try -noenc first (OpenSSL 3.0+), fall back to -nodes (OpenSSL 1.x)
	snprintf(cmd, sizeof(cmd),
		"openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
		"-keyout '%s' -out '%s' -days 365 -noenc "
		"-subj '/CN=%s' 2>&1",
		key_path, cert_path, hex);

	if (system(cmd) != 0) {
		// Fallback for OpenSSL 1.x which doesn't have -noenc
		snprintf(cmd, sizeof(cmd),
			"openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
			"-keyout '%s' -out '%s' -days 365 -nodes "
			"-subj '/CN=%s' 2>&1",
			key_path, cert_path, hex);

		if (system(cmd) != 0) {
			LOG_ERROR("openssl command failed. Is openssl installed?\n");
			return FALSE;
		}
	}

	snprintf(cert_path_out, path_size, "%s", cert_path);
	return TRUE;
}

#include <openssl/x509.h>

BOOLEAN Crypto_VerifyPeerCert(void *certificate, const uint8_t expected_node_id[32])
{
	if (!certificate || !expected_node_id) return FALSE;

	X509 *cert = (X509 *)certificate;
	X509_NAME *subject = X509_get_subject_name(cert);
	if (!subject) return FALSE;

	char cn[128] = {0};
	if (X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn)) < 64)
		return FALSE;

	return VerifyCN(cn, expected_node_id);
}

#endif // _WIN32
