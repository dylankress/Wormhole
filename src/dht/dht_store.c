//
// dht_store.c
// Wormhole DHT - Local value store for chunk location metadata.
// by Dylan Kress
//

#include "dht_store.h"
#include "../wire_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DHT_STORE_MAGIC   0x53564844  // "DHVS" little-endian
#define DHT_STORE_VERSION 1

// ============================================================================
// Internal helpers
// ============================================================================

// Find an entry by key. Returns index or -1.
static int FindEntry(const DHT_VALUE_STORE *store, const uint8_t key[32])
{
    for (uint32_t i = 0; i < store->count; i++)
    {
        if (memcmp(store->entries[i].key, key, 32) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

// Check if a location already exists in an entry (by node_id)
static BOOLEAN HasLocation(const DHT_VALUE_ENTRY *entry, const uint8_t node_id[32])
{
    for (uint32_t i = 0; i < entry->location_count; i++)
    {
        if (memcmp(entry->locations[i].node_id, node_id, 32) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// Public API
// ============================================================================

void DhtStore_Init(DHT_VALUE_STORE *store)
{
    memset(store, 0, sizeof(DHT_VALUE_STORE));
#ifdef _WIN32
    InitializeCriticalSection(&store->lock);
#else
    pthread_mutex_init(&store->lock, NULL);
#endif
}

void DhtStore_Destroy(DHT_VALUE_STORE *store)
{
    if (!store) return;
#ifdef _WIN32
    DeleteCriticalSection(&store->lock);
#else
    pthread_mutex_destroy(&store->lock);
#endif
}

BOOLEAN DhtStore_Put(DHT_VALUE_STORE *store, const uint8_t key[32],
                     const DHT_LOCATION *locations, uint32_t location_count)
{
    if (!store || !key || !locations || location_count == 0) return FALSE;

#ifdef _WIN32
    EnterCriticalSection(&store->lock);
#else
    pthread_mutex_lock(&store->lock);
#endif

    int idx = FindEntry(store, key);
    DHT_VALUE_ENTRY *entry;

    if (idx >= 0)
    {
        // Existing entry — merge locations
        entry = &store->entries[idx];
    }
    else
    {
        // New entry
        if (store->count >= DHT_STORE_CAPACITY)
        {
#ifdef _WIN32
            LeaveCriticalSection(&store->lock);
#else
            pthread_mutex_unlock(&store->lock);
#endif
            return FALSE;  // Store full
        }
        entry = &store->entries[store->count];
        memcpy(entry->key, key, 32);
        entry->location_count = 0;
        entry->stored_at = time(NULL);
        store->count++;
    }

    // Merge new locations (dedup by node_id)
    for (uint32_t i = 0; i < location_count; i++)
    {
        if (entry->location_count >= DHT_STORE_MAX_LOCATIONS) break;
        if (HasLocation(entry, locations[i].node_id)) continue;

        entry->locations[entry->location_count] = locations[i];
        entry->location_count++;
    }

    entry->last_refreshed = time(NULL);

#ifdef _WIN32
    LeaveCriticalSection(&store->lock);
#else
    pthread_mutex_unlock(&store->lock);
#endif
    return TRUE;
}

uint32_t DhtStore_Get(const DHT_VALUE_STORE *store, const uint8_t key[32],
                      DHT_LOCATION *out, uint32_t max)
{
    if (!store || !key || !out || max == 0) return 0;

    // Cast away const for lock — lock is mutable state, data is read-only
    DHT_VALUE_STORE *mutable_store = (DHT_VALUE_STORE *)store;
#ifdef _WIN32
    EnterCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_lock(&mutable_store->lock);
#endif

    int idx = FindEntry(store, key);
    uint32_t count = 0;
    if (idx >= 0)
    {
        const DHT_VALUE_ENTRY *entry = &store->entries[idx];
        count = entry->location_count < max ? entry->location_count : max;
        memcpy(out, entry->locations, count * sizeof(DHT_LOCATION));
    }

#ifdef _WIN32
    LeaveCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_unlock(&mutable_store->lock);
#endif
    return count;
}

BOOLEAN DhtStore_Has(const DHT_VALUE_STORE *store, const uint8_t key[32])
{
    DHT_VALUE_STORE *mutable_store = (DHT_VALUE_STORE *)store;
#ifdef _WIN32
    EnterCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_lock(&mutable_store->lock);
#endif

    BOOLEAN result = (FindEntry(store, key) >= 0) ? TRUE : FALSE;

#ifdef _WIN32
    LeaveCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_unlock(&mutable_store->lock);
#endif
    return result;
}

void DhtStore_ExpireOld(DHT_VALUE_STORE *store)
{
    if (!store) return;

#ifdef _WIN32
    EnterCriticalSection(&store->lock);
#else
    pthread_mutex_lock(&store->lock);
#endif

    time_t now = time(NULL);
    uint32_t i = 0;
    while (i < store->count)
    {
        double age = difftime(now, store->entries[i].last_refreshed);
        if (age > DHT_STORE_EXPIRY_SEC)
        {
            // Swap with last entry and decrement count
            if (i < store->count - 1)
            {
                store->entries[i] = store->entries[store->count - 1];
            }
            store->count--;
        }
        else
        {
            i++;
        }
    }

#ifdef _WIN32
    LeaveCriticalSection(&store->lock);
#else
    pthread_mutex_unlock(&store->lock);
#endif
}

uint32_t DhtStore_GetCount(const DHT_VALUE_STORE *store)
{
    if (!store) return 0;
    DHT_VALUE_STORE *mutable_store = (DHT_VALUE_STORE *)store;
#ifdef _WIN32
    EnterCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_lock(&mutable_store->lock);
#endif
    uint32_t c = store->count;
#ifdef _WIN32
    LeaveCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_unlock(&mutable_store->lock);
#endif
    return c;
}

BOOLEAN DhtStore_Save(const DHT_VALUE_STORE *store, const char *path)
{
    if (!store || !path) return FALSE;

    // Write to temp file first, then atomic rename
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fh = fopen(tmp_path, "wb");
    if (!fh) return FALSE;

    // Acquire lock — cast away const since lock is mutable state
    DHT_VALUE_STORE *mutable_store = (DHT_VALUE_STORE *)store;
#ifdef _WIN32
    EnterCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_lock(&mutable_store->lock);
#endif

    BOOLEAN success = TRUE;

    // Header: [4B magic][4B version][4B count]
    uint8_t header[12];
    WriteUint32LE(header, DHT_STORE_MAGIC);
    WriteUint32LE(header + 4, DHT_STORE_VERSION);
    WriteUint32LE(header + 8, store->count);
    if (fwrite(header, 1, sizeof(header), fh) != sizeof(header)) { success = FALSE; goto done; }

    // Entries
    for (uint32_t i = 0; i < store->count; i++)
    {
        const DHT_VALUE_ENTRY *e = &store->entries[i];

        // key[32] + stored_at[8] + last_refreshed[8] + location_count[4]
        if (fwrite(e->key, 1, 32, fh) != 32) { success = FALSE; goto done; }

        uint8_t ts[8];
        WriteUint64LE(ts, (uint64_t)e->stored_at);
        if (fwrite(ts, 1, 8, fh) != 8) { success = FALSE; goto done; }
        WriteUint64LE(ts, (uint64_t)e->last_refreshed);
        if (fwrite(ts, 1, 8, fh) != 8) { success = FALSE; goto done; }

        uint8_t lc[4];
        WriteUint32LE(lc, e->location_count);
        if (fwrite(lc, 1, 4, fh) != 4) { success = FALSE; goto done; }

        // Per location: node_id[32] + addr_type[1] + addr[16] + port[2]
        for (uint32_t j = 0; j < e->location_count; j++)
        {
            const DHT_LOCATION *loc = &e->locations[j];
            if (fwrite(loc->node_id, 1, 32, fh) != 32) { success = FALSE; goto done; }
            if (fwrite(&loc->addr_type, 1, 1, fh) != 1) { success = FALSE; goto done; }
            if (fwrite(loc->addr, 1, 16, fh) != 16) { success = FALSE; goto done; }
            uint8_t port_buf[2];
            WriteUint16LE(port_buf, loc->port);
            if (fwrite(port_buf, 1, 2, fh) != 2) { success = FALSE; goto done; }
        }
    }

done:
#ifdef _WIN32
    LeaveCriticalSection(&mutable_store->lock);
#else
    pthread_mutex_unlock(&mutable_store->lock);
#endif

    fclose(fh);

    if (!success)
    {
        remove(tmp_path);
        return FALSE;
    }

    // Atomic rename: replace original with temp file
#ifdef _WIN32
    MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING);
#else
    rename(tmp_path, path);
#endif
    return TRUE;
}

BOOLEAN DhtStore_Load(DHT_VALUE_STORE *store, const char *path)
{
    if (!store || !path) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Read header
    uint8_t header[12];
    if (fread(header, 1, sizeof(header), fh) != sizeof(header))
    {
        fclose(fh);
        return FALSE;
    }

    uint32_t magic = ReadUint32LE(header);
    uint32_t version = ReadUint32LE(header + 4);
    uint32_t count = ReadUint32LE(header + 8);

    if (magic != DHT_STORE_MAGIC || version != DHT_STORE_VERSION)
    {
        fclose(fh);
        return FALSE;
    }

    if (count > DHT_STORE_CAPACITY) count = DHT_STORE_CAPACITY;

    // Reset fields without destroying the mutex (DhtStore_Init would memset
    // the entire struct, corrupting an already-initialized critical section)
    store->count = 0;
    memset(store->entries, 0, sizeof(store->entries));

    for (uint32_t i = 0; i < count; i++)
    {
        // Read into a temporary entry so partial reads don't corrupt the store
        DHT_VALUE_ENTRY tmp;
        memset(&tmp, 0, sizeof(tmp));
        BOOLEAN entry_ok = TRUE;

        // key[32] + stored_at[8] + last_refreshed[8] + location_count[4]
        if (fread(tmp.key, 1, 32, fh) != 32) break;

        uint8_t ts[8];
        if (fread(ts, 1, 8, fh) != 8) break;
        tmp.stored_at = (time_t)ReadUint64LE(ts);
        if (fread(ts, 1, 8, fh) != 8) break;
        tmp.last_refreshed = (time_t)ReadUint64LE(ts);

        uint8_t lc[4];
        if (fread(lc, 1, 4, fh) != 4) break;
        tmp.location_count = ReadUint32LE(lc);
        if (tmp.location_count > DHT_STORE_MAX_LOCATIONS)
            tmp.location_count = DHT_STORE_MAX_LOCATIONS;

        for (uint32_t j = 0; j < tmp.location_count; j++)
        {
            DHT_LOCATION *loc = &tmp.locations[j];
            if (fread(loc->node_id, 1, 32, fh) != 32) { entry_ok = FALSE; break; }
            if (fread(&loc->addr_type, 1, 1, fh) != 1) { entry_ok = FALSE; break; }
            if (fread(loc->addr, 1, 16, fh) != 16) { entry_ok = FALSE; break; }
            uint8_t port_buf[2];
            if (fread(port_buf, 1, 2, fh) != 2) { entry_ok = FALSE; break; }
            loc->port = ReadUint16LE(port_buf);
        }

        if (!entry_ok) break;

        // Entire entry read successfully — commit to store
        store->entries[store->count] = tmp;
        store->count++;
    }

    fclose(fh);
    return TRUE;
}
