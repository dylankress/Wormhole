//
// file_registry.c
// File-level tracking: ~/.wormhole/files/<hex>.file per stored file.
// by Dylan Kress
//

#include "file_registry.h"
#include "file_io.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif

//=============================================================================
// Internal helpers
//=============================================================================

static void HashToHexStr(const uint8_t hash[WH_HASH_SIZE], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < WH_HASH_SIZE; i++)
    {
        hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
    hex[64] = '\0';
}

static BOOLEAN EnsureDirExists(const char *dir)
{
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL))
        return TRUE;
    return (GetLastError() == ERROR_ALREADY_EXISTS);
#else
    if (mkdir(dir, 0755) == 0)
        return TRUE;
    return (errno == EEXIST);
#endif
}

// Build the files directory path: ~/.wormhole/files
static BOOLEAN GetFilesBasePath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\files", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/files", home);
#endif
    return TRUE;
}

// Build path to a specific .file entry
static BOOLEAN GetFilePath(const uint8_t manifest_hash[WH_HASH_SIZE],
                            char *path, size_t path_len)
{
    char base[MAX_PATH];
    if (!GetFilesBasePath(base, sizeof(base))) return FALSE;

    char hex[65];
    HashToHexStr(manifest_hash, hex);

#ifdef _WIN32
    snprintf(path, path_len, "%s\\%s.file", base, hex);
#else
    snprintf(path, path_len, "%s/%s.file", base, hex);
#endif
    return TRUE;
}

//=============================================================================
// File format:
//   [4B magic 0x46524547 "FREG"]
//   [1B status]
//   [8B store_time LE]
//   [4B replicated_count LE]
//   [2B filename_len LE]
//   [filename bytes]
//   [4B manifest_data_size LE]
//   [manifest_data]
//=============================================================================

#define FILE_REG_MAGIC 0x46524547  // "FREG"

static void WriteLE16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void WriteLE32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void WriteLE64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8)); }

static uint16_t ReadLE16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t ReadLE32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t ReadLE64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (i * 8)); return v; }

//=============================================================================
// Public API
//=============================================================================

BOOLEAN FileRegistry_Init(void)
{
    char base[MAX_PATH];
    if (!GetFilesBasePath(base, sizeof(base))) return FALSE;

    // Ensure ~/.wormhole exists
    char wormhole_dir[MAX_PATH];
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s\\.wormhole", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s/.wormhole", home);
#endif
    EnsureDirExists(wormhole_dir);

    return EnsureDirExists(base);
}

BOOLEAN FileRegistry_Save(const FILE_MANIFEST *manifest, const char *filename,
                           FILE_STATUS status)
{
    if (!manifest || !filename) return FALSE;

    // Ensure directory exists
    FileRegistry_Init();

    char path[MAX_PATH];
    if (!GetFilePath(manifest->manifest_hash, path, sizeof(path))) return FALSE;

    // Serialize the manifest
    size_t manifest_size = 0;
    uint8_t *manifest_data = Manifest_Serialize(manifest, &manifest_size);
    if (!manifest_data) return FALSE;

    uint16_t name_len = (uint16_t)strlen(filename);
    if (name_len > 259) name_len = 259;

    // Header: 4 + 1 + 8 + 4 + 2 = 19 bytes
    uint32_t header_size = 4 + 1 + 8 + 4 + 2 + name_len + 4;
    uint8_t *header = (uint8_t *)malloc(header_size);
    if (!header)
    {
        free(manifest_data);
        return FALSE;
    }

    uint32_t off = 0;
    WriteLE32(header + off, FILE_REG_MAGIC); off += 4;
    header[off] = (uint8_t)status; off += 1;
    WriteLE64(header + off, (uint64_t)time(NULL)); off += 8;
    WriteLE32(header + off, 0); off += 4;  // replicated_count = 0
    WriteLE16(header + off, name_len); off += 2;
    memcpy(header + off, filename, name_len); off += name_len;
    WriteLE32(header + off, (uint32_t)manifest_size); off += 4;

    FILE *fh = fopen(path, "wb");
    if (!fh)
    {
        free(header);
        free(manifest_data);
        return FALSE;
    }

    fwrite(header, 1, header_size, fh);
    fwrite(manifest_data, 1, manifest_size, fh);
    fclose(fh);

    free(header);
    free(manifest_data);
    return TRUE;
}

BOOLEAN FileRegistry_Load(const uint8_t manifest_hash[WH_HASH_SIZE],
                           FILE_REG_ENTRY *entry_out,
                           FILE_MANIFEST **manifest_out)
{
    if (!entry_out) return FALSE;

    char path[MAX_PATH];
    if (!GetFilePath(manifest_hash, path, sizeof(path))) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Read header
    uint8_t hdr[19];  // magic(4) + status(1) + time(8) + repl_count(4) + name_len(2)
    if (fread(hdr, 1, 19, fh) != 19)
    {
        fclose(fh);
        return FALSE;
    }

    uint32_t magic = ReadLE32(hdr);
    if (magic != FILE_REG_MAGIC)
    {
        fclose(fh);
        return FALSE;
    }

    memcpy(entry_out->manifest_hash, manifest_hash, WH_HASH_SIZE);
    entry_out->status = (FILE_STATUS)hdr[4];
    entry_out->store_time = ReadLE64(hdr + 5);
    entry_out->replicated_count = ReadLE32(hdr + 13);
    uint16_t name_len = ReadLE16(hdr + 17);

    // Read filename
    if (name_len > 259) name_len = 259;
    if (fread(entry_out->filename, 1, name_len, fh) != name_len)
    {
        fclose(fh);
        return FALSE;
    }
    entry_out->filename[name_len] = '\0';

    // Read manifest size
    uint8_t ms_buf[4];
    if (fread(ms_buf, 1, 4, fh) != 4)
    {
        fclose(fh);
        return FALSE;
    }
    uint32_t manifest_size = ReadLE32(ms_buf);

    // Read manifest data
    uint8_t *manifest_data = (uint8_t *)malloc(manifest_size);
    if (!manifest_data)
    {
        fclose(fh);
        return FALSE;
    }
    if (fread(manifest_data, 1, manifest_size, fh) != manifest_size)
    {
        free(manifest_data);
        fclose(fh);
        return FALSE;
    }
    fclose(fh);

    // Deserialize manifest to extract file_size and chunk_count
    FILE_MANIFEST *manifest = Manifest_Deserialize(manifest_data, manifest_size);
    if (!manifest)
    {
        free(manifest_data);
        return FALSE;
    }

    entry_out->file_size = manifest->file_size;
    entry_out->chunk_count = manifest->chunk_count;

    if (manifest_out)
    {
        *manifest_out = manifest;
    }
    else
    {
        Manifest_Destroy(manifest);
    }

    free(manifest_data);
    return TRUE;
}

BOOLEAN FileRegistry_UpdateStatus(const uint8_t manifest_hash[WH_HASH_SIZE],
                                    FILE_STATUS status, uint32_t replicated_count)
{
    char path[MAX_PATH];
    if (!GetFilePath(manifest_hash, path, sizeof(path))) return FALSE;

    // Open for read+write
    FILE *fh = fopen(path, "r+b");
    if (!fh) return FALSE;

    // Verify magic
    uint8_t magic_buf[4];
    if (fread(magic_buf, 1, 4, fh) != 4 || ReadLE32(magic_buf) != FILE_REG_MAGIC)
    {
        fclose(fh);
        return FALSE;
    }

    // Write status at offset 4
    fseek(fh, 4, SEEK_SET);
    uint8_t status_byte = (uint8_t)status;
    fwrite(&status_byte, 1, 1, fh);

    // Write replicated_count at offset 13
    fseek(fh, 13, SEEK_SET);
    uint8_t rc_buf[4];
    WriteLE32(rc_buf, replicated_count);
    fwrite(rc_buf, 1, 4, fh);

    fclose(fh);
    return TRUE;
}

uint32_t FileRegistry_List(FILE_REG_ENTRY *entries_out, uint32_t max_entries)
{
    if (!entries_out || max_entries == 0) return 0;

    char base[MAX_PATH];
    if (!GetFilesBasePath(base, sizeof(base))) return 0;

    uint32_t count = 0;

#ifdef _WIN32
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\*.file", base);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= max_entries) break;

        // Extract hash from filename (first 64 hex chars before .file)
        size_t fname_len = strlen(find_data.cFileName);
        if (fname_len < 69) continue;  // 64 hex + ".file"

        uint8_t hash[WH_HASH_SIZE];
        BOOLEAN valid = TRUE;
        for (int i = 0; i < WH_HASH_SIZE && valid; i++)
        {
            unsigned int byte_val;
            if (sscanf(find_data.cFileName + i * 2, "%2x", &byte_val) != 1)
                valid = FALSE;
            else
                hash[i] = (uint8_t)byte_val;
        }
        if (!valid) continue;

        if (FileRegistry_Load(hash, &entries_out[count], NULL))
        {
            count++;
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    DIR *dir = opendir(base);
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries)
    {
        size_t fname_len = strlen(ent->d_name);
        if (fname_len < 69) continue;

        // Check .file extension
        if (strcmp(ent->d_name + fname_len - 5, ".file") != 0) continue;

        uint8_t hash[WH_HASH_SIZE];
        BOOLEAN valid = TRUE;
        for (int i = 0; i < WH_HASH_SIZE && valid; i++)
        {
            unsigned int byte_val;
            if (sscanf(ent->d_name + i * 2, "%2x", &byte_val) != 1)
                valid = FALSE;
            else
                hash[i] = (uint8_t)byte_val;
        }
        if (!valid) continue;

        if (FileRegistry_Load(hash, &entries_out[count], NULL))
        {
            count++;
        }
    }

    closedir(dir);
#endif

    return count;
}

BOOLEAN FileRegistry_FindByPrefix(const char *hex_prefix,
                                    FILE_REG_ENTRY *entry_out,
                                    FILE_MANIFEST **manifest_out)
{
    if (!hex_prefix || !entry_out) return FALSE;

    size_t prefix_len = strlen(hex_prefix);
    if (prefix_len < 8 || prefix_len > 64) return FALSE;

    char base[MAX_PATH];
    if (!GetFilesBasePath(base, sizeof(base))) return FALSE;

    uint32_t matches = 0;
    uint8_t matched_hash[WH_HASH_SIZE] = {0};

#ifdef _WIN32
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\%s*.file", base, hex_prefix);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        size_t fname_len = strlen(find_data.cFileName);
        if (fname_len < 69) continue;

        // Verify prefix match (case-insensitive)
        if (_strnicmp(find_data.cFileName, hex_prefix, prefix_len) != 0) continue;

        // Parse full hash
        uint8_t hash[WH_HASH_SIZE];
        BOOLEAN valid = TRUE;
        for (int i = 0; i < WH_HASH_SIZE && valid; i++)
        {
            unsigned int byte_val;
            if (sscanf(find_data.cFileName + i * 2, "%2x", &byte_val) != 1)
                valid = FALSE;
            else
                hash[i] = (uint8_t)byte_val;
        }
        if (!valid) continue;

        matches++;
        memcpy(matched_hash, hash, WH_HASH_SIZE);

        if (matches > 1)
        {
            FindClose(hFind);
            return FALSE;  // Ambiguous
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    DIR *dir = opendir(base);
    if (!dir) return FALSE;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        size_t fname_len = strlen(ent->d_name);
        if (fname_len < 69) continue;
        if (strcmp(ent->d_name + fname_len - 5, ".file") != 0) continue;
        if (strncasecmp(ent->d_name, hex_prefix, prefix_len) != 0) continue;

        uint8_t hash[WH_HASH_SIZE];
        BOOLEAN valid = TRUE;
        for (int i = 0; i < WH_HASH_SIZE && valid; i++)
        {
            unsigned int byte_val;
            if (sscanf(ent->d_name + i * 2, "%2x", &byte_val) != 1)
                valid = FALSE;
            else
                hash[i] = (uint8_t)byte_val;
        }
        if (!valid) continue;

        matches++;
        memcpy(matched_hash, hash, WH_HASH_SIZE);

        if (matches > 1)
        {
            closedir(dir);
            return FALSE;  // Ambiguous
        }
    }

    closedir(dir);
#endif

    if (matches != 1) return FALSE;

    return FileRegistry_Load(matched_hash, entry_out, manifest_out);
}
