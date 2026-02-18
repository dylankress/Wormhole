//
// file_io.c
// by Dylan Kress
//

#include "file_io.h"

#ifdef _WIN32
#include <shlobj.h>  // For SHGetKnownFolderPath
#include <windows.h>
#pragma comment(lib, "shell32.lib")
#else
#include <unistd.h>  // For usleep()
#endif

BOOLEAN FileExists(const char *path)
{
	// Try to open for reading
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		return FALSE;
	}

	// Close immediately
	fclose(file);
	return TRUE;
}

BOOLEAN GetWormholeFileSize(const char *path, uint64_t *size_out)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		return FALSE;
	}

#ifdef _WIN32
	// Use 64-bit seek/tell on Windows (supports files > 2GB)
	if (_fseeki64(file, 0, SEEK_END) != 0) {
		fclose(file);
		return FALSE;
	}
	
	__int64 size = _ftelli64(file);
	if (size < 0) {
		fclose(file);
		return FALSE;
	}
#else
	// Use fseeko/ftello on POSIX (already 64-bit on modern systems)
	if (fseeko(file, 0, SEEK_END) != 0) {
		fclose(file);
		return FALSE;
	}
	
	off_t size = ftello(file);
	if (size < 0) {
		fclose(file);
		return FALSE;
	}
#endif

	fclose(file);
	*size_out = (uint64_t)size;
	return TRUE;
}

void ExtractFilename(const char *path, char **filename_out, uint32_t *length_out)
{
	// Find the last slash (either \ or /)
	const char *last_slash = NULL;
	const char *p = path;

	while (*p) {
		if (*p == '\\' || *p == '/') {
			last_slash = p;
		}
		p++;
	}

	// If no slash found, entire path is the filename
	const char *filename_start = (last_slash == NULL) ? path : (last_slash + 1);

	// Calculate length and allocate
	size_t len = strlen(filename_start);
	*length_out = (uint32_t)len;

	*filename_out = (char*)malloc(len + 1);  // +1 for null terminator
	if (*filename_out == NULL) {
		*length_out = 0;
		return;
	}

	// Copy the filename
	strcpy(*filename_out, filename_start);
}

BOOLEAN OpenFileForRead(const char *path, FILE **handle_out)
{
	*handle_out = fopen(path, "rb");  // "rb" = read binary
	if (*handle_out == NULL) {
		LOG_ERROR("Failed to open file for reading: %s\n", path);
		return FALSE;
	}
	return TRUE;
}

BOOLEAN OpenFileForWrite(const char *path, FILE **handle_out)
{
	*handle_out = fopen(path, "wb");  // "wb" = write binary
	if (*handle_out == NULL) {
		LOG_ERROR("Failed to open file for writing: %s\n", path);
		return FALSE;
	}
	return TRUE;
}

BOOLEAN ReadFileChunk(FILE *handle, uint8_t *buffer, size_t size, size_t *bytes_read)
{
	*bytes_read = fread(buffer, 1, size, handle);

	// Check for errors (not EOF - EOF is expected at end of file)
	if (*bytes_read == 0 && ferror(handle)) {
		LOG_ERROR("Error reading from file\n");
		return FALSE;
	}

	return TRUE;
}

BOOLEAN WriteFileChunkWithRetry(FILE *handle, const uint8_t *data, size_t length)
{
	const int MAX_RETRIES = 3;
	const int RETRY_DELAY_MS = 100;

	// Save original position so we can seek back on retry
#ifdef _WIN32
	int64_t original_pos = _ftelli64(handle);
#else
	off_t original_pos = ftello(handle);
#endif

	for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
		// Seek back to original position before each retry
		if (attempt > 0) {
#ifdef _WIN32
			_fseeki64(handle, original_pos, SEEK_SET);
#else
			fseeko(handle, original_pos, SEEK_SET);
#endif
		}

		size_t written = fwrite(data, 1, length, handle);

		if (written == length) {
			return TRUE;  // Success!
		}

		// Failed - retry if we have attempts left
		if (attempt < MAX_RETRIES - 1) {
			LOG_ERROR("Write failed (attempt %d/%d), retrying...\n",
					attempt + 1, MAX_RETRIES);
#ifdef _WIN32
			Sleep(RETRY_DELAY_MS);
#else
			usleep(RETRY_DELAY_MS * 1000);
#endif
			clearerr(handle);  // Clear error flag
		}
	}

	// All retries exhausted
	LOG_ERROR("Write failed after %d attempts\n", MAX_RETRIES);
	return FALSE;
}

void CloseFile(FILE *handle)
{
    if (handle != NULL) {
        fclose(handle);
    }
}

BOOLEAN GetDownloadsPath(char *path_out, size_t path_len)
{
    if (!path_out || path_len == 0) {
        return FALSE;
    }
    
#ifdef _WIN32
    // Windows: Use SHGetKnownFolderPath for Downloads folder
    PWSTR downloads_path = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &downloads_path);
    
    if (SUCCEEDED(hr)) {
        // Convert wide string to narrow string
        size_t converted = 0;
        wcstombs_s(&converted, path_out, path_len, downloads_path, path_len - 1);
        CoTaskMemFree(downloads_path);
        
        if (converted > 0) {
            return TRUE;
        }
    }
    
    // Fallback: Use %USERPROFILE%\Downloads
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
        snprintf(path_out, path_len, "%s\\Downloads", userprofile);
        return TRUE;
    }
    
    return FALSE;
#else
    // Linux/Mac: Use ~/Downloads
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_out, path_len, "%s/Downloads", home);
        return TRUE;
    }
    
    return FALSE;
#endif
}

BOOLEAN GetUniqueFilename(const char *base_path, const char *filename, 
                          char *unique_path_out, size_t unique_path_len)
{
    if (!base_path || !filename || !unique_path_out || unique_path_len == 0) {
        return FALSE;
    }
    
    // Try original filename first
    snprintf(unique_path_out, unique_path_len, "%s\\%s", base_path, filename);
    
    if (!FileExists(unique_path_out)) {
        return TRUE;  // Original filename is available
    }
    
    // File exists - need to append number
    // Find extension (e.g., "file.txt" → "file" + ".txt")
    const char *dot = strrchr(filename, '.');
    size_t base_len = dot ? (size_t)(dot - filename) : strlen(filename);
    const char *ext = dot ? dot : "";
    
    // Try file(1), file(2), etc.
    for (int i = 1; i < 1000; i++) {
        char new_filename[MAX_PATH];
        snprintf(new_filename, sizeof(new_filename), "%.*s(%d)%s", 
                 (int)base_len, filename, i, ext);
        
        snprintf(unique_path_out, unique_path_len, "%s\\%s", base_path, new_filename);
        
        if (!FileExists(unique_path_out)) {
            return TRUE;  // Found available filename
        }
    }
    
    // Couldn't find unique filename (tried 1000 variations!)
    LOG_ERROR("Could not generate unique filename for: %s\n", filename);
    return FALSE;
}
