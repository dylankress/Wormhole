//
// incentives.c
// Storage ratio tracking to prevent freeloading in the P2P network.
// by Dylan Kress
//

#include "incentives.h"
#include "wire_format.h"
#include <stdio.h>
#include <string.h>

//=============================================================================
// Internal helpers
//=============================================================================

// Find a peer by ID, or return NULL if not found.
static PEER_BALANCE *FindPeer(const STORAGE_LEDGER *ledger, const uint8_t peer_id[32])
{
    for (uint32_t i = 0; i < ledger->peer_count; i++)
    {
        if (memcmp(ledger->peers[i].peer_id, peer_id, 32) == 0)
        {
            return (PEER_BALANCE *)&ledger->peers[i];
        }
    }
    return NULL;
}

// Find a peer by ID, or create a new entry if room.
// Returns NULL if ledger is full and peer not found.
static PEER_BALANCE *FindOrCreatePeer(STORAGE_LEDGER *ledger, const uint8_t peer_id[32])
{
    PEER_BALANCE *existing = FindPeer(ledger, peer_id);
    if (existing) return existing;

    if (ledger->peer_count >= LEDGER_MAX_PEERS) return NULL;

    PEER_BALANCE *entry = &ledger->peers[ledger->peer_count];
    memset(entry, 0, sizeof(PEER_BALANCE));
    memcpy(entry->peer_id, peer_id, 32);
    ledger->peer_count++;
    return entry;
}

//=============================================================================
// Public API
//=============================================================================

void Ledger_Init(STORAGE_LEDGER *ledger)
{
    memset(ledger, 0, sizeof(STORAGE_LEDGER));
}

void Ledger_RecordStoredForUs(STORAGE_LEDGER *ledger, const uint8_t peer_id[32], uint64_t bytes)
{
    if (!ledger || !peer_id || bytes == 0) return;

    PEER_BALANCE *peer = FindOrCreatePeer(ledger, peer_id);
    if (!peer) return;

    peer->bytes_stored_for_us += bytes;
    peer->last_updated = time(NULL);
    ledger->total_stored_for_us += bytes;
}

void Ledger_RecordWeStored(STORAGE_LEDGER *ledger, const uint8_t peer_id[32], uint64_t bytes)
{
    if (!ledger || !peer_id || bytes == 0) return;

    PEER_BALANCE *peer = FindOrCreatePeer(ledger, peer_id);
    if (!peer) return;

    peer->bytes_we_store_for_them += bytes;
    peer->last_updated = time(NULL);
    ledger->total_we_store += bytes;
}

BOOLEAN Ledger_ShouldAcceptStorage(const STORAGE_LEDGER *ledger, const uint8_t peer_id[32],
                                     uint64_t additional_bytes)
{
    if (!ledger || !peer_id) return FALSE;

    const PEER_BALANCE *peer = FindPeer(ledger, peer_id);

    // New peer (no history): accept up to threshold
    if (!peer)
    {
        return (additional_bytes <= LEDGER_NEW_PEER_THRESHOLD) ? TRUE : FALSE;
    }

    uint64_t they_store_for_us = peer->bytes_stored_for_us;
    uint64_t we_would_store = peer->bytes_we_store_for_them + additional_bytes;

    // If they store nothing for us, allow up to the new peer threshold
    if (they_store_for_us == 0)
    {
        return (we_would_store <= LEDGER_NEW_PEER_THRESHOLD) ? TRUE : FALSE;
    }

    // Reject if we'd store more than 2x what they store for us
    // Integer check: reject if they_store_for_us * 2 < we_would_store
    if (they_store_for_us * 2 < we_would_store)
    {
        return FALSE;
    }

    return TRUE;
}

double Ledger_GetPeerRatio(const STORAGE_LEDGER *ledger, const uint8_t peer_id[32])
{
    if (!ledger || !peer_id) return 1.0;

    const PEER_BALANCE *peer = FindPeer(ledger, peer_id);
    if (!peer) return 1.0;

    // No data stored for them — neutral ratio
    if (peer->bytes_we_store_for_them == 0) return 1.0;

    return (double)peer->bytes_stored_for_us / (double)peer->bytes_we_store_for_them;
}

BOOLEAN Ledger_Save(const STORAGE_LEDGER *ledger, const char *path)
{
    if (!ledger || !path) return FALSE;

    FILE *fh = fopen(path, "wb");
    if (!fh) return FALSE;

    // Header: [4B magic][4B version][4B peer_count]
    uint8_t header[12];
    WriteUint32LE(header, LEDGER_MAGIC);
    WriteUint32LE(header + 4, LEDGER_VERSION);
    WriteUint32LE(header + 8, ledger->peer_count);

    if (fwrite(header, 1, 12, fh) != 12)
    {
        fclose(fh);
        return FALSE;
    }

    // Per peer: [32B id][8B for_us][8B we_store][8B last_updated]
    for (uint32_t i = 0; i < ledger->peer_count; i++)
    {
        const PEER_BALANCE *peer = &ledger->peers[i];

        if (fwrite(peer->peer_id, 1, 32, fh) != 32)
        {
            fclose(fh);
            return FALSE;
        }

        uint8_t fields[24];
        WriteUint64LE(fields, peer->bytes_stored_for_us);
        WriteUint64LE(fields + 8, peer->bytes_we_store_for_them);
        WriteUint64LE(fields + 16, (uint64_t)peer->last_updated);

        if (fwrite(fields, 1, 24, fh) != 24)
        {
            fclose(fh);
            return FALSE;
        }
    }

    fclose(fh);
    return TRUE;
}

BOOLEAN Ledger_Load(STORAGE_LEDGER *ledger, const char *path)
{
    if (!ledger || !path) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Read header
    uint8_t header[12];
    if (fread(header, 1, 12, fh) != 12)
    {
        fclose(fh);
        return FALSE;
    }

    uint32_t magic = ReadUint32LE(header);
    uint32_t version = ReadUint32LE(header + 4);
    uint32_t peer_count = ReadUint32LE(header + 8);

    if (magic != LEDGER_MAGIC || version != LEDGER_VERSION)
    {
        fclose(fh);
        return FALSE;
    }

    if (peer_count > LEDGER_MAX_PEERS)
    {
        fclose(fh);
        return FALSE;
    }

    // Initialize ledger
    Ledger_Init(ledger);
    ledger->peer_count = peer_count;

    // Read peers
    for (uint32_t i = 0; i < peer_count; i++)
    {
        PEER_BALANCE *peer = &ledger->peers[i];

        if (fread(peer->peer_id, 1, 32, fh) != 32)
        {
            fclose(fh);
            Ledger_Init(ledger);
            return FALSE;
        }

        uint8_t fields[24];
        if (fread(fields, 1, 24, fh) != 24)
        {
            fclose(fh);
            Ledger_Init(ledger);
            return FALSE;
        }

        peer->bytes_stored_for_us     = ReadUint64LE(fields);
        peer->bytes_we_store_for_them = ReadUint64LE(fields + 8);
        peer->last_updated            = (time_t)ReadUint64LE(fields + 16);

        // Rebuild totals
        ledger->total_stored_for_us += peer->bytes_stored_for_us;
        ledger->total_we_store      += peer->bytes_we_store_for_them;
    }

    fclose(fh);
    return TRUE;
}
