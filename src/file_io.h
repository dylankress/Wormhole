//
// file_io.h
// by Dylan Kress
//

#pragma once

#include "common.h"

// File existence and metadata
BOOLEAN FileExists(const char *path);
BOOLEAN IsDirectory(const char *path);
BOOLEAN GetWormholeFileSize(const char *path, uint64_t *size_out);
void ExtractFilename(const char *path, char **filename_out, uint32_t *length_out);

// Get user's Downloads folder path
// path_out: Buffer to store path (should be at least MAX_PATH bytes)
// path_len: Size of path_out buffer
// Returns: TRUE on success, FALSE on error
BOOLEAN GetDownloadsPath(char *path_out, size_t path_len);

// Generate unique filename if file already exists (appends numbers)
// base_path: Directory path (e.g., "C:\Users\Dylan\Downloads")
// filename: Original filename (e.g., "file.txt")
// unique_path_out: Buffer for full unique path (should be at least MAX_PATH bytes)
// unique_path_len: Size of unique_path_out buffer
// Returns: TRUE on success, FALSE on error
BOOLEAN GetUniqueFilename(const char *base_path, const char *filename, 
                          char *unique_path_out, size_t unique_path_len);

// File operations
BOOLEAN OpenFileForRead(const char *path, FILE **handle_out);
BOOLEAN OpenFileForWrite(const char *path, FILE **handle_out);
BOOLEAN ReadFileChunk(FILE *handle, uint8_t *buffer, size_t size, size_t *bytes_read);
BOOLEAN WriteFileChunkWithRetry(FILE *handle, const uint8_t *data, size_t length);
void CloseFile(FILE *handle);
