//
// chunker.c
// File chunking with Blake3 hashing to build a FILE_MANIFEST.
// by Dylan Kress
//

#include "chunker.h"
#include "chunk_store.h"
#include "file_io.h"
#include <blake3.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

//=============================================================================
// Directory enumeration helpers
//=============================================================================

// Dynamic array for collecting file paths and sizes during enumeration
typedef struct {
    char     **paths;
    uint64_t  *sizes;
    uint32_t   count;
    uint32_t   capacity;
} FILE_LIST;

static BOOLEAN FileList_Init(FILE_LIST *fl)
{
    fl->count = 0;
    fl->capacity = 64;
    fl->paths = (char **)calloc(fl->capacity, sizeof(char *));
    fl->sizes = (uint64_t *)calloc(fl->capacity, sizeof(uint64_t));
    return (fl->paths && fl->sizes);
}

static BOOLEAN FileList_Add(FILE_LIST *fl, const char *path, uint64_t size)
{
    if (fl->count >= fl->capacity)
    {
        uint32_t new_cap = fl->capacity * 2;
        char **new_paths = (char **)realloc(fl->paths, new_cap * sizeof(char *));
        if (!new_paths) return FALSE;
        fl->paths = new_paths;

        uint64_t *new_sizes = (uint64_t *)realloc(fl->sizes, new_cap * sizeof(uint64_t));
        if (!new_sizes) return FALSE;
        fl->sizes = new_sizes;

        fl->capacity = new_cap;
    }

    fl->paths[fl->count] = strdup(path);
    if (!fl->paths[fl->count]) return FALSE;
    fl->sizes[fl->count] = size;
    fl->count++;
    return TRUE;
}

static void FileList_Destroy(FILE_LIST *fl)
{
    if (fl->paths)
    {
        for (uint32_t i = 0; i < fl->count; i++)
            if (fl->paths[i]) free(fl->paths[i]);
        free(fl->paths);
    }
    if (fl->sizes) free(fl->sizes);
    fl->paths = NULL;
    fl->sizes = NULL;
    fl->count = 0;
}

// Recursively enumerate files in a directory.
// relative_prefix: the path prefix relative to the top-level directory (using '/')
// abs_dir: the absolute path on disk to enumerate
#ifdef _WIN32
static BOOLEAN EnumerateDirectory(const char *abs_dir, const char *relative_prefix,
                                  FILE_LIST *fl)
{
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", abs_dir);

    WIN32_FIND_DATAA fdata;
    HANDLE hFind = FindFirstFileA(search_path, &fdata);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do
    {
        // Skip . and ..
        if (strcmp(fdata.cFileName, ".") == 0 || strcmp(fdata.cFileName, "..") == 0)
            continue;

        char abs_path[MAX_PATH];
        snprintf(abs_path, sizeof(abs_path), "%s\\%s", abs_dir, fdata.cFileName);

        // Build relative path with '/' separator for wire format
        char rel_path[MAX_PATH];
        if (relative_prefix[0])
            snprintf(rel_path, sizeof(rel_path), "%s/%s", relative_prefix, fdata.cFileName);
        else
            snprintf(rel_path, sizeof(rel_path), "%s", fdata.cFileName);

        if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Recurse into subdirectory
            if (!EnumerateDirectory(abs_path, rel_path, fl))
            {
                FindClose(hFind);
                return FALSE;
            }
        }
        else
        {
            // Regular file — get size and add to list
            uint64_t fsize = ((uint64_t)fdata.nFileSizeHigh << 32) | fdata.nFileSizeLow;
            if (!FileList_Add(fl, rel_path, fsize))
            {
                FindClose(hFind);
                return FALSE;
            }
        }
    } while (FindNextFileA(hFind, &fdata));

    FindClose(hFind);
    return TRUE;
}
#else
static BOOLEAN EnumerateDirectory(const char *abs_dir, const char *relative_prefix,
                                  FILE_LIST *fl)
{
    DIR *dir = opendir(abs_dir);
    if (!dir) return FALSE;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char abs_path[4096];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", abs_dir, entry->d_name);

        char rel_path[4096];
        if (relative_prefix[0])
            snprintf(rel_path, sizeof(rel_path), "%s/%s", relative_prefix, entry->d_name);
        else
            snprintf(rel_path, sizeof(rel_path), "%s", entry->d_name);

        struct stat st;
        if (stat(abs_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode))
        {
            if (!EnumerateDirectory(abs_path, rel_path, fl))
            {
                closedir(dir);
                return FALSE;
            }
        }
        else if (S_ISREG(st.st_mode))
        {
            if (!FileList_Add(fl, rel_path, (uint64_t)st.st_size))
            {
                closedir(dir);
                return FALSE;
            }
        }
    }

    closedir(dir);
    return TRUE;
}
#endif

// Extract the last component of a path as the directory name
static void ExtractDirName(const char *path, char *dirname_out, size_t dirname_len)
{
    size_t len = strlen(path);

    // Strip trailing separators
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        len--;

    // Find the last separator
    size_t start = len;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\')
        start--;

    size_t name_len = len - start;
    if (name_len >= dirname_len) name_len = dirname_len - 1;
    memcpy(dirname_out, path + start, name_len);
    dirname_out[name_len] = '\0';
}

//=============================================================================
// Public API
//=============================================================================

FILE_MANIFEST *Chunker_BuildManifest(const char *file_path)
{
    if (!file_path) return NULL;

    // Get file size
    uint64_t file_size = 0;
    if (!GetWormholeFileSize(file_path, &file_size))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot get file size: %s\n", file_path);
        return NULL;
    }

    // Extract filename
    char *filename = NULL;
    uint32_t filename_length = 0;
    ExtractFilename(file_path, &filename, &filename_length);
    if (!filename)
    {
        LOG_ERROR("[Chunker] ERROR: Cannot extract filename: %s\n", file_path);
        return NULL;
    }

    // Create manifest
    FILE_MANIFEST *manifest = Manifest_Create(filename, file_size);
    free(filename);
    if (!manifest)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to create manifest\n");
        return NULL;
    }

    // Handle empty file (no chunks to process)
    if (file_size == 0)
    {
        Manifest_ComputeHash(manifest);
        return manifest;
    }

    // Open file
    FILE *fh = NULL;
    if (!OpenFileForRead(file_path, &fh))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot open file: %s\n", file_path);
        Manifest_Destroy(manifest);
        return NULL;
    }

    // Read and hash each chunk
    uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    if (!chunk_buf)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to allocate chunk buffer\n");
        CloseFile(fh);
        Manifest_Destroy(manifest);
        return NULL;
    }

    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        // Calculate how much to read for this chunk
        uint64_t offset = (uint64_t)i * WH_CHUNK_SIZE;
        uint64_t remaining = file_size - offset;
        size_t to_read = (remaining > WH_CHUNK_SIZE) ? WH_CHUNK_SIZE : (size_t)remaining;

        size_t bytes_read = 0;
        if (!ReadFileChunk(fh, chunk_buf, to_read, &bytes_read) || bytes_read != to_read)
        {
            LOG_ERROR("[Chunker] ERROR: Failed to read chunk %u\n", i);
            free(chunk_buf);
            CloseFile(fh);
            Manifest_Destroy(manifest);
            return NULL;
        }

        // Blake3 hash the chunk
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, chunk_buf, bytes_read);
        uint8_t hash[WH_HASH_SIZE];
        blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

        Manifest_AddChunk(manifest, i, hash, (uint32_t)bytes_read);
    }

    free(chunk_buf);
    CloseFile(fh);

    // Compute overall manifest hash
    Manifest_ComputeHash(manifest);

    return manifest;
}

FILE_MANIFEST *Chunker_BuildManifestFromDirectory(const char *dir_path)
{
    if (!dir_path) return NULL;

    LOG("[Chunker] Enumerating directory: %s\n", dir_path);

    // Enumerate all files recursively
    FILE_LIST fl;
    if (!FileList_Init(&fl))
    {
        LOG_ERROR("[Chunker] ERROR: Failed to initialize file list\n");
        return NULL;
    }

    if (!EnumerateDirectory(dir_path, "", &fl) || fl.count == 0)
    {
        LOG_ERROR("[Chunker] ERROR: No files found in directory: %s\n", dir_path);
        FileList_Destroy(&fl);
        return NULL;
    }

    LOG("[Chunker] Found %u files in directory\n", fl.count);

    // Extract directory name
    char dirname[260];
    ExtractDirName(dir_path, dirname, sizeof(dirname));

    // Create multi-file manifest
    FILE_MANIFEST *manifest = Manifest_CreateMultiFile(
        dirname, (const char **)fl.paths, fl.sizes, fl.count);

    if (!manifest)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to create multi-file manifest\n");
        FileList_Destroy(&fl);
        return NULL;
    }

    // Hash each file's chunks
    uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    if (!chunk_buf)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to allocate chunk buffer\n");
        FileList_Destroy(&fl);
        Manifest_Destroy(manifest);
        return NULL;
    }

    for (uint32_t fi = 0; fi < manifest->file_count; fi++)
    {
        FILE_ENTRY *fe = &manifest->files[fi];
        if (fe->chunk_count == 0) continue;  // skip empty files

        // Build absolute path for this file
        char abs_path[MAX_PATH];
#ifdef _WIN32
        // Convert '/' in relative path to '\' for Windows
        snprintf(abs_path, sizeof(abs_path), "%s\\", dir_path);
        size_t base_len = strlen(abs_path);
        size_t rem = sizeof(abs_path) - base_len;
        if (fe->path_length < rem)
        {
            memcpy(abs_path + base_len, fe->relative_path, fe->path_length);
            abs_path[base_len + fe->path_length] = '\0';
            // Replace '/' with '\'
            for (size_t k = base_len; abs_path[k]; k++)
            {
                if (abs_path[k] == '/') abs_path[k] = '\\';
            }
        }
        else
        {
            LOG_ERROR("[Chunker] ERROR: Path too long: %s\n", fe->relative_path);
            free(chunk_buf);
            FileList_Destroy(&fl);
            Manifest_Destroy(manifest);
            return NULL;
        }
#else
        snprintf(abs_path, sizeof(abs_path), "%s/%s", dir_path, fe->relative_path);
#endif

        FILE *fh = NULL;
        if (!OpenFileForRead(abs_path, &fh))
        {
            LOG_ERROR("[Chunker] ERROR: Cannot open file: %s\n", abs_path);
            free(chunk_buf);
            FileList_Destroy(&fl);
            Manifest_Destroy(manifest);
            return NULL;
        }

        for (uint32_t ci = 0; ci < fe->chunk_count; ci++)
        {
            uint32_t global_idx = fe->chunk_start + ci;
            uint64_t offset_in_file = (uint64_t)ci * WH_CHUNK_SIZE;
            uint64_t remaining = fe->file_size - offset_in_file;
            size_t to_read = (remaining > WH_CHUNK_SIZE) ? WH_CHUNK_SIZE : (size_t)remaining;

            size_t bytes_read = 0;
            if (!ReadFileChunk(fh, chunk_buf, to_read, &bytes_read) || bytes_read != to_read)
            {
                LOG_ERROR("[Chunker] ERROR: Failed to read chunk %u of file %s\n",
                          ci, fe->relative_path);
                CloseFile(fh);
                free(chunk_buf);
                FileList_Destroy(&fl);
                Manifest_Destroy(manifest);
                return NULL;
            }

            blake3_hasher hasher;
            blake3_hasher_init(&hasher);
            blake3_hasher_update(&hasher, chunk_buf, bytes_read);
            uint8_t hash[WH_HASH_SIZE];
            blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

            Manifest_AddChunk(manifest, global_idx, hash, (uint32_t)bytes_read);
        }

        CloseFile(fh);
    }

    free(chunk_buf);
    FileList_Destroy(&fl);

    Manifest_ComputeHash(manifest);

    LOG("[Chunker] Directory manifest: %u files, %u chunks, %llu bytes total\n",
        manifest->file_count, manifest->chunk_count,
        (unsigned long long)manifest->file_size);

    return manifest;
}

//=============================================================================
// Single-pass: chunk + hash + store
//=============================================================================

FILE_MANIFEST *Chunker_BuildManifestAndStore(const char *file_path,
                                              uint32_t *stored_count)
{
    if (!file_path) return NULL;
    if (stored_count) *stored_count = 0;

    // Get file size
    uint64_t file_size = 0;
    if (!GetWormholeFileSize(file_path, &file_size))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot get file size: %s\n", file_path);
        return NULL;
    }

    // Extract filename
    char *filename = NULL;
    uint32_t filename_length = 0;
    ExtractFilename(file_path, &filename, &filename_length);
    if (!filename)
    {
        LOG_ERROR("[Chunker] ERROR: Cannot extract filename: %s\n", file_path);
        return NULL;
    }

    // Create manifest
    FILE_MANIFEST *manifest = Manifest_Create(filename, file_size);
    free(filename);
    if (!manifest)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to create manifest\n");
        return NULL;
    }

    // Handle empty file (no chunks to process)
    if (file_size == 0)
    {
        Manifest_ComputeHash(manifest);
        return manifest;
    }

    // Open file
    FILE *fh = NULL;
    if (!OpenFileForRead(file_path, &fh))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot open file: %s\n", file_path);
        Manifest_Destroy(manifest);
        return NULL;
    }

    // Allocate chunk buffer
    uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    if (!chunk_buf)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to allocate chunk buffer\n");
        CloseFile(fh);
        Manifest_Destroy(manifest);
        return NULL;
    }

    uint32_t stored = 0;
    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        // Calculate how much to read for this chunk
        uint64_t offset = (uint64_t)i * WH_CHUNK_SIZE;
        uint64_t remaining = file_size - offset;
        size_t to_read = (remaining > WH_CHUNK_SIZE) ? WH_CHUNK_SIZE : (size_t)remaining;

        size_t bytes_read = 0;
        if (!ReadFileChunk(fh, chunk_buf, to_read, &bytes_read) || bytes_read != to_read)
        {
            LOG_ERROR("[Chunker] ERROR: Failed to read chunk %u\n", i);
            free(chunk_buf);
            CloseFile(fh);
            Manifest_Destroy(manifest);
            return NULL;
        }

        // Blake3 hash the chunk
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, chunk_buf, bytes_read);
        uint8_t hash[WH_HASH_SIZE];
        blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

        Manifest_AddChunk(manifest, i, hash, (uint32_t)bytes_read);

        // Store immediately — single pass, no double-read
        if (ChunkStore_Put(hash, chunk_buf, (uint32_t)bytes_read))
        {
            stored++;
        }
    }

    free(chunk_buf);
    CloseFile(fh);

    // Compute overall manifest hash
    Manifest_ComputeHash(manifest);

    if (stored_count) *stored_count = stored;

    LOG("[Chunker] Built manifest and stored %u/%u chunks for %s\n",
        stored, manifest->chunk_count, file_path);

    return manifest;
}
